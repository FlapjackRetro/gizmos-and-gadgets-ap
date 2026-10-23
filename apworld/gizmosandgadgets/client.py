"""Gizmos and Gadgets client: bridges the recomp's Archipelago mod and the Archipelago server.

The mod (mod/ap_mod.c) and this client share files in <game folder>/ap/ (docs/MOD.md):
  checks.txt    mod appends location ids ("goal" once races_required races are won)  -> LocationChecks / goal
  received.txt  client writes item ids in the server's order            <- ReceivedItems
  config.txt    client writes settings and which levels are open
  labels.txt    client writes what each of this slot's locations holds (shown in the game when a box opens)
  game.txt      mod rewrites about once a second: the game is running (player, place, items applied)
  seed.txt      client: which seed, slot and in-game player this game folder belongs to

Run: from the Launcher ("Gizmos and Gadgets Client"), or
  python -m worlds.gizmosandgadgets.client --gamedir <game folder> [--connect host:port] [--name slot]
"""
import asyncio
import os
import re
import time
from typing import Dict, List, Optional

import Utils
from CommonClient import ClientCommandProcessor, CommonContext, get_base_parser, gui_enabled, logger, server_loop
from MultiServer import mark_raw
from NetUtils import ClientStatus

from . import BASE, BUILDINGS, GAME, level_name

POLL_SECONDS = 0.5
LAST_DIR_FILE = "gizmos_and_gadgets_gamedir.txt"   # in Archipelago's user folder: the last game folder used
GAME_TIMEOUT = 5.0          # seconds without a game.txt update before the game counts as gone


class GizmosCommandProcessor(ClientCommandProcessor):
    ctx: "GizmosContext"

    @mark_raw
    def _cmd_gamedir(self, path: str = "") -> bool:
        """Show or set the game folder (the one with gizmo000.dat and mods/)."""
        path = path.strip().strip('"')         # raw text: Windows backslashes and spaces kept
        if path:
            self.ctx.set_game_dir(path)
        self.output(f"Game folder: {self.ctx.game_dir or '(not set)'}")
        return True

    def _cmd_game(self) -> bool:
        """Is the game running with the mod, and is it in sync?"""
        self.output(self.ctx.game_text())
        return True

    def _cmd_levels(self) -> bool:
        """Which levels are open in each building."""
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
        self.seed_ok = False
        self.slot_data: dict = {}
        self.written_config = ""
        self.game_info: Dict[str, str] = {}  # last game.txt
        self.game_alive = False
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
        try:                                    # remembered for the next start of the client
            with open(Utils.user_path(LAST_DIR_FILE), "w") as f:
                f.write(path)
        except OSError:
            pass
        os.makedirs(os.path.join(path, "ap"), exist_ok=True)
        if not os.path.isdir(os.path.join(path, "mods")):
            logger.warning("No mods/ folder there: copy ap_mod.dll / ap_mod.so into <game folder>/mods/.")
        self.checks_read = 0
        self.written_items = self._read_received()
        self.written_config = ""
        if self.slot is not None and self.server_seed_name:  # already connected: set the folder up now
            self.attach_game_folder()

    def _read_received(self) -> List[int]:
        try:
            with open(self.ap_path("received.txt")) as f:
                return [int(x) for x in f.read().split()]
        except (OSError, ValueError):
            return []

    def _replace(self, name: str, text: str) -> None:
        tmp = self.ap_path(name + ".tmp")
        with open(tmp, "w") as f:
            f.write(text)
        os.replace(tmp, self.ap_path(name))

    def write_config(self) -> None:
        if not (self.game_dir and self.seed_ok):
            return
        lines = ["door_checks=0", "box_checks=1", "open_world=1", "title_logo=1"]
        lines.append(f"races_required={int(self.slot_data.get('races_required', 15))}")
        if "cyberchimps" in self.slot_data:
            lines.append(f"chimps={1 if self.slot_data['cyberchimps'] else 0}")
        lines += [f"avail.{b}=" + "".join("1" if self.level_open(b * 5 + lv) else "0" for lv in range(5))
                  for b in range(3)]
        text = "\n".join(lines) + "\n"
        if text != self.written_config:
            self._replace("config.txt", text)
            self.written_config = text

    def write_received(self) -> None:
        """Keep received.txt equal to the server's item list. It only ever grows, so append."""
        if not (self.game_dir and self.seed_ok):
            return
        items = [item.item for item in self.items_received]
        if items[:len(self.written_items)] != self.written_items:
            self._replace("received.txt", "".join(f"{i}\n" for i in items))   # the mod re-reads a shorter file
        elif len(items) > len(self.written_items):
            with open(self.ap_path("received.txt"), "a") as f:
                f.write("".join(f"{i}\n" for i in items[len(self.written_items):]))
        else:
            return
        self.written_items = items

    def label(self, item) -> str:
        """What the game shows when the box of this location opens: short, plain ASCII."""
        name = self.item_names.lookup_in_slot(item.item, item.player)
        if item.player == self.slot:
            text = name.split(" - ", 1)[1] if " - " in name else name     # "<Building> Level n - <part>"
            text = re.sub(r" #\d+$", "", text)                        # "#<part>" only tells same names apart
        else:
            text = f"{name} for {self.player_names.get(item.player, '?')}"
        text = "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in text)
        # the HUD's name line holds 3 lines of about 19 characters and wraps at spaces; cut longer words
        words = [w if len(w) <= 18 else w[:17] + "." for w in text.split()]
        text = " ".join(words)
        return text if len(text) <= 56 else text[:55].rstrip() + "."

    def write_labels(self) -> None:
        if not (self.game_dir and self.seed_ok):
            return
        lines = [f"{loc}={self.label(item)}" for loc, item in sorted(self.locations_info.items())]
        self._replace("labels.txt", "\n".join(lines) + "\n")

    def read_seed_file(self) -> Dict[str, str]:
        out: Dict[str, str] = {}
        try:
            with open(self.ap_path("seed.txt")) as f:
                for line in f:
                    k, _, v = line.strip().partition("=")
                    if v:
                        out[k] = v
        except OSError:
            pass
        return out

    def bind_seed(self) -> bool:
        """A game folder belongs to one seed and slot: its ap/ files would mix otherwise."""
        tag = f"{self.server_seed_name} {self.team} {self.slot}"
        info = self.read_seed_file()
        if info.get("seed") and info["seed"] != tag:
            logger.error(f"This game folder belongs to another seed ({info['seed']}). Use a separate game folder "
                         f"per seed, or delete its ap/ folder (and that player in PLAYERS.DAT) to start over.")
            return False
        if not info.get("seed"):
            # The game may have run before the client ever connected (state.txt, unsent checks.txt: they belong
            # to this seed). Items from a seed nobody recorded would not: refuse those.
            if self._read_received():
                logger.error("ap/received.txt holds items but no seed is recorded: delete the ap/ folder first.")
                return False
            self._replace("seed.txt", f"seed={tag}\n")
            logger.info("This game folder is now bound to this seed.")
        return True

    # ---- the game ------------------------------------------------------------------------------
    def poll_game(self) -> None:
        """game.txt: key=value lines the mod rewrites about once a second while the game runs."""
        try:
            age = time.time() - os.path.getmtime(self.ap_path("game.txt"))
            with open(self.ap_path("game.txt")) as f:
                text = f.read()
        except OSError:
            age, text = GAME_TIMEOUT + 1, ""
        info = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
        player_changed = info.get("player") != self.game_info.get("player")
        if info:
            self.game_info = info
        alive = bool(info) and age < GAME_TIMEOUT          # the mod rewrites it about once a second
        was_alive, self.game_alive = self.game_alive, alive
        if alive and not was_alive:
            logger.info("Game connected. " + self.game_text())
        elif not alive and was_alive:
            logger.warning("Game not responding (closed, or the mod is not loaded).")
        if alive and (player_changed or not was_alive):
            self.check_player()

    def check_player(self) -> None:
        """Remember which in-game player plays this seed, and warn when a different one is signed in."""
        player = self.game_info.get("player", "")
        if not (player and self.seed_ok):
            return
        info = self.read_seed_file()
        if not info.get("player"):
            with open(self.ap_path("seed.txt"), "a") as f:
                f.write(f"player={player}\n")
            logger.info(f"In-game player '{player}' recorded for this seed.")
        elif info["player"] != player:
            logger.warning(f"This seed is played as '{info['player']}', but '{player}' is signed in. Sign in as "
                           f"'{info['player']}' to continue this seed.")

    def game_text(self) -> str:
        if not self.game_dir:
            return "Game folder not set (/gamedir)."
        if not self.game_alive:
            return "Game: not running (or the Archipelago mod is not loaded)."
        g = self.game_info
        place = "Technology Center"
        if g.get("building", "-1") not in ("-1", "") and g.get("in_level") == "1":
            place = level_name(int(g["building"]) * 5 + int(g["level"]))
        applied, total = int(g.get("items", "0")), len(self.written_items)
        sync = "all received items in the game" if applied >= total else f"{applied}/{total} received items in the game"
        waiting = int(g.get("ap_inventory", "0") or 0)
        if waiting:
            sync += f", {waiting} part(s) in the Archipelago inventory"
        return f"Game: running, player '{g.get('player') or '(not signed in)'}', {place}, {sync}."

    # ---- levels ------------------------------------------------------------------------------
    def level_open(self, v: int) -> bool:
        if self.slot_data.get("level_access") == "open":
            return True
        lv = v % 5
        if self.slot_data.get("level_access") == "item_progression":
            return lv == 0 or any(item.item == BASE + 0x0E00 + v for item in self.items_received)
        won = lambda x: (BASE + x) in self.checked_locations
        return lv == 0 or won(v - 1) or won(v)

    def levels_text(self) -> str:
        out = []
        for b in range(3):
            marks = " ".join(f"{lv + 1}{'' if self.level_open(b * 5 + lv) else '(locked)'}" for lv in range(5))
            out.append(f"{BUILDINGS[b]}: {marks}")
        return "\n".join(out)

    # ---- server ------------------------------------------------------------------------------
    async def server_auth(self, password_requested: bool = False):
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        if not self.game_dir:
            logger.warning("No game folder yet: set it with /gamedir <path> (the game gets its settings then).")
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict):
        if cmd == "Connected":
            self.slot_data = args.get("slot_data") or {}
            self.attach_game_folder()
        elif cmd == "LocationInfo":
            self.write_labels()
        elif cmd == "ReceivedItems":
            self.write_received()
            self.write_config()          # an Access item opens a level
        elif cmd == "RoomUpdate" and "checked_locations" in args:
            self.write_config()          # a race won opens the next level

    def attach_game_folder(self) -> None:
        """Connected to a slot, with a game folder: bind it and give the game its settings, items and labels.
        Runs on connecting, and again when the folder is set (or changed) while connected."""
        self.seed_ok = bool(self.game_dir) and self.slot is not None and self.bind_seed()
        if not self.seed_ok:
            return
        self.write_config()
        self.write_received()
        logger.info(self.levels_text())
        if self.game_alive:
            self.check_player()
        asyncio.create_task(self.send_msgs([{"cmd": "LocationScouts", "create_as_hint": 0,
                                              "locations": sorted(self.server_locations)}]))

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
            logger.info("Goal sent: enough races won!")

    def make_gui(self):
        ui = super().make_gui()
        ui.base_title = "Gizmos and Gadgets Client"
        return ui


async def game_watcher(ctx: GizmosContext) -> None:
    while not ctx.exit_event.is_set():
        try:
            if ctx.game_dir:
                ctx.poll_game()
            await ctx.forward_checks()
        except Exception as e:  # keep the loop alive; report and carry on
            logger.exception(f"Talking to the game failed: {e}")
        await asyncio.sleep(POLL_SECONDS)


def launch(*args: str) -> None:
    async def main(a):
        gamedir = a.gamedir
        if not gamedir:
            try:
                with open(Utils.user_path(LAST_DIR_FILE)) as f:
                    gamedir = f.read().strip() or None
            except OSError:
                gamedir = None
        ctx = GizmosContext(a.connect, a.password, gamedir)
        if ctx.game_dir:
            logger.info(f"Game folder: {ctx.game_dir} (change it with /gamedir <path>)")
        else:
            logger.info("Set the game folder (the one with gizmos.exe and gizmo000.dat): /gamedir <path>")
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
