"""Gizmos and Gadgets client: bridges the recomp's Archipelago mod and the Archipelago server.

The mod (mod/ap_mod.c) and this client share files in <game folder>/ap/ (docs/MOD.md):
  checks.txt    mod appends location ids ("goal" when 15 races are won)  -> LocationChecks / goal
  received.txt  client writes item ids in the server's order            <- ReceivedItems
  config.txt    client writes level choices (/level)
  seed.txt      client: which seed and slot this game folder belongs to

Run: from the Launcher ("Gizmos and Gadgets Client"), or
  python -m worlds.gizmos_and_gadgets.client --gamedir <game folder> [--connect host:port] [--name slot]
"""
import asyncio
import os
from typing import List, Optional

import Utils
from CommonClient import ClientCommandProcessor, CommonContext, get_base_parser, gui_enabled, logger, server_loop
from NetUtils import ClientStatus

from . import BASE, BUILDINGS, GAME

POLL_SECONDS = 0.5
BUILDING_ALIASES = {"auto": 0, "automotive": 0, "car": 0, "air": 1, "aircraft": 1, "plane": 1,
                    "alt": 2, "alternative": 2, "energy": 2, "alt-energy": 2}


class GizmosCommandProcessor(ClientCommandProcessor):
    ctx: "GizmosContext"

    def _cmd_gamedir(self, path: str = "") -> bool:
        """Show or set the game folder (the one with gizmo000.dat and mods/)."""
        if path:
            self.ctx.set_game_dir(path)
        self.output(f"Game folder: {self.ctx.game_dir or '(not set)'}")
        return True

    def _cmd_level(self, building: str = "", level: str = "") -> bool:
        """/level <building> <1-5|auto>: which level the building loads next time you enter it."""
        if not building:
            self.output(self.ctx.levels_text())
            return True
        b = BUILDING_ALIASES.get(building.lower())
        if b is None and building.isdigit() and 1 <= int(building) <= 3:
            b = int(building) - 1
        if b is None:
            self.output("Building: automotive, aircraft or alt (or 1-3).")
            return False
        if level.lower() in ("", "auto"):
            self.ctx.level_choice[b] = -1
        elif level.isdigit() and 1 <= int(level) <= 5:
            lv = int(level) - 1
            if lv > self.ctx.highest_unlocked(b):
                self.output(f"{BUILDINGS[b]} Level {lv + 1} is not unlocked yet.")
                return False
            self.ctx.level_choice[b] = lv
        else:
            self.output("Level: 1-5 or auto.")
            return False
        self.ctx.write_config()
        self.output(self.ctx.levels_text())
        return True


class GizmosContext(CommonContext):
    game = GAME
    items_handling = 0b111          # every item comes from the server, our own included (boxes are suppressed)
    command_processor = GizmosCommandProcessor
    want_slot_data = True

    def __init__(self, server_address: Optional[str], password: Optional[str], game_dir: Optional[str]):
        super().__init__(server_address, password)
        self.game_dir: Optional[str] = None
        self.checks_read = 0                 # bytes of checks.txt already forwarded
        self.goal_sent = False
        self.written_items: List[int] = []   # what received.txt holds
        self.level_choice = [-1, -1, -1]
        self.seed_ok = False
        self.slot_data: dict = {}
        if game_dir:
            self.set_game_dir(game_dir)

    # ---- files -------------------------------------------------------------------------------
    def ap_path(self, name: str) -> str:
        return os.path.join(self.game_dir, "ap", name)

    def set_game_dir(self, path: str) -> None:
        path = os.path.abspath(os.path.expanduser(path))
        if not os.path.isdir(path):
            logger.error(f"Not a folder: {path}")
            return
        self.game_dir = path
        os.makedirs(os.path.join(path, "ap"), exist_ok=True)
        if not os.path.isdir(os.path.join(path, "mods")):
            logger.warning("No mods/ folder there: copy ap_mod.dll / ap_mod.so into <game folder>/mods/.")
        self.checks_read = 0
        self.written_items = self._read_received()
        self._read_config()

    def _read_received(self) -> List[int]:
        try:
            with open(self.ap_path("received.txt")) as f:
                return [int(x) for x in f.read().split()]
        except (OSError, ValueError):
            return []

    def _read_config(self) -> None:
        try:
            with open(self.ap_path("config.txt")) as f:
                for line in f:
                    key, _, val = line.partition("=")
                    if key.strip().startswith("level."):
                        b = int(key.strip()[6:])
                        if 0 <= b < 3:
                            self.level_choice[b] = int(val)
        except (OSError, ValueError):
            pass

    def write_config(self) -> None:
        if not self.game_dir:
            return
        door = 1 if (self.slot_data or {}).get("door_checks") else 0
        lines = [f"door_checks={door}", "box_checks=1", "open_world=1", "title_logo=1"]
        lines += [f"level.{b}={self.level_choice[b]}" for b in range(3)]
        tmp = self.ap_path("config.txt.tmp")
        with open(tmp, "w") as f:
            f.write("\n".join(lines) + "\n")
        os.replace(tmp, self.ap_path("config.txt"))

    def write_received(self) -> None:
        """Keep received.txt equal to the server's item list. It only ever grows, so append."""
        if not self.game_dir or not self.seed_ok:
            return
        items = [item.item for item in self.items_received]
        if items[:len(self.written_items)] != self.written_items:
            # a different list (should not happen within one seed): rewrite it; the mod re-reads it
            with open(self.ap_path("received.txt"), "w") as f:
                f.write("".join(f"{i}\n" for i in items))
        elif len(items) > len(self.written_items):
            with open(self.ap_path("received.txt"), "a") as f:
                f.write("".join(f"{i}\n" for i in items[len(self.written_items):]))
        else:
            return
        self.written_items = items

    def bind_seed(self) -> bool:
        """A game folder belongs to one seed and slot: its ap/ files would mix otherwise."""
        tag = f"{self.server_seed_name} {self.team} {self.slot}"
        path = self.ap_path("seed.txt")
        try:
            with open(path) as f:
                old = f.read().strip()
        except OSError:
            old = ""
        if old and old != tag:
            logger.error(f"This game folder belongs to another seed ({old}). Use a separate game folder per "
                         f"seed, or delete its ap/ folder (and the PLAYERS.DAT player) to start over.")
            return False
        if not old:
            for name in ("checks.txt", "received.txt", "state.txt"):
                if os.path.exists(self.ap_path(name)):
                    logger.error(f"ap/{name} exists but no seed is recorded: delete the ap/ folder first.")
                    return False
            with open(path, "w") as f:
                f.write(tag + "\n")
        return True

    # ---- levels ------------------------------------------------------------------------------
    def highest_unlocked(self, b: int) -> int:
        """Vanilla order: level n+1 of a building opens once its level n race is won."""
        lv = 0
        while lv < 4 and (BASE + b * 5 + lv) in self.checked_locations:
            lv += 1
        return lv

    def levels_text(self) -> str:
        out = []
        for b in range(3):
            choice = self.level_choice[b]
            pick = "game's choice" if choice < 0 else f"Level {choice + 1}"
            out.append(f"{BUILDINGS[b]}: levels 1-{self.highest_unlocked(b) + 1} open, next entry loads {pick}")
        return "\n".join(out)

    # ---- server ------------------------------------------------------------------------------
    async def server_auth(self, password_requested: bool = False):
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        if not self.game_dir:
            logger.error("Set the game folder first: /gamedir <path>")
            return
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict):
        if cmd == "Connected":
            self.slot_data = args.get("slot_data") or {}
            self.seed_ok = bool(self.game_dir) and self.bind_seed()
            if self.seed_ok:
                self.write_config()
                self.write_received()
                logger.info(self.levels_text())
        elif cmd == "ReceivedItems":
            self.write_received()

    async def disconnect(self, allow_autoreconnect: bool = False):
        self.seed_ok = False
        await super().disconnect(allow_autoreconnect)

    async def forward_checks(self) -> None:
        if not (self.seed_ok and self.slot is not None):
            return
        try:
            with open(self.ap_path("checks.txt")) as f:
                f.seek(self.checks_read)
                data = f.read()
        except OSError:
            return
        end = data.rfind("\n") + 1          # only whole lines
        if not end:
            return
        self.checks_read += len(data[:end].encode())
        ids, goal = set(), False
        for line in data[:end].split():
            if line == "goal":
                goal = True
            elif line.isdigit():
                ids.add(int(line))
        if ids:
            await self.check_locations(ids)
        if goal and not self.goal_sent:
            self.goal_sent = True
            await self.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            logger.info("Goal sent: all 15 races won!")

    def make_gui(self):
        ui = super().make_gui()
        ui.base_title = "Gizmos and Gadgets Client"
        return ui


async def game_watcher(ctx: GizmosContext) -> None:
    while not ctx.exit_event.is_set():
        try:
            await ctx.forward_checks()
        except Exception as e:  # keep the loop alive; report and carry on
            logger.exception(f"Reading the game's checks failed: {e}")
        await asyncio.sleep(POLL_SECONDS)


def launch(*args: str) -> None:
    async def main(a):
        ctx = GizmosContext(a.connect, a.password, a.gamedir)
        if a.name:
            ctx.auth = a.name
        ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
        watcher = asyncio.create_task(game_watcher(ctx), name="game watcher")
        if gui_enabled:
            ctx.run_gui()
        ctx.run_cli()
        await ctx.exit_event.wait()
        watcher.cancel()
        await ctx.shutdown()

    import colorama
    parser = get_base_parser(description="Gizmos and Gadgets Archipelago client")
    parser.add_argument("--gamedir", default=None, help="game folder (with gizmo000.dat and mods/)")
    parser.add_argument("--name", default=None, help="slot name")
    a = parser.parse_args(args)
    colorama.just_fix_windows_console()
    Utils.init_logging("GizmosClient", exception_logger="Client")
    asyncio.run(main(a))
    colorama.deinit()


if __name__ == "__main__":
    import sys
    launch(*sys.argv[1:])
