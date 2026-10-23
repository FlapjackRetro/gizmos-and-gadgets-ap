"""Gizmos and Gadgets client: bridges the recomp's Archipelago mod and the Archipelago server.

The mod (mod/ap_mod.c) and this client share files in <game folder>/ap/ (docs/MOD.md):
Each seed and slot gets its own folder, ap/<seed>_<team>_<slot>/, created on the first connection; the client
writes its name to ap/current.txt, which the mod follows (at startup and at the Technology Center):
  current.txt   client: the folder of the connected seed/slot
  game.txt      mod rewrites about once a second: the game is running (player, place, items, folder)
  players.txt   mod: which in-game player plays which seed (one each; the game refuses a mismatch)
  <s>/checks.txt    mod appends location ids ("goal" once races_required races are won) -> LocationChecks / goal
  <s>/received.txt  client writes item ids in the server's order                         <- ReceivedItems
  <s>/config.txt    client writes settings and which levels are open
  <s>/labels.txt    client writes what each of this slot's locations holds (shown when a box opens)
  <s>/seed.txt      client: which seed and slot the folder is for
  <s>/state.txt, <s>/levels/  mod: progress and each level's saved warehouse

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
        self.session: Optional[str] = None   # ap/<session>/ of the connected seed/slot
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
    def root_path(self, name: str) -> str:
        return os.path.join(self.game_dir, "ap", name)

    def ap_path(self, name: str) -> str:
        """A file of the connected seed/slot's folder."""
        return os.path.join(self.game_dir, "ap", self.session or "_none", name)

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
        self.session = None
        self.seed_ok = False
        if self.slot is not None and self.server_seed_name:  # already connected: set the folder up now
            self.attach_game_folder()

    def _read_received(self) -> List[int]:
        try:
            with open(self.ap_path("received.txt")) as f:
                return [int(x) for x in f.read().split()]
        except (OSError, ValueError):
            return []

    def _replace(self, name: str, text: str, root: bool = False) -> None:
        dst = self.root_path(name) if root else self.ap_path(name)
        tmp = dst + ".tmp"
        with open(tmp, "w") as f:
            f.write(text)
        os.replace(tmp, dst)

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

    def session_name(self) -> str:
        safe = lambda s: re.sub(r"[^A-Za-z0-9_-]", "-", str(s))
        return f"{safe(self.server_seed_name)}_{self.team}_{self.slot}"

    def open_session(self) -> bool:
        """The connected seed/slot's own folder, ap/<seed>_<team>_<slot>/: created on the first connection, so
        a new seed never mixes with an earlier one in the same game folder. Returns True for a new seed."""
        self.session = self.session_name()
        folder = self.root_path(self.session)
        new = not os.path.isdir(folder)
        os.makedirs(folder, exist_ok=True)
        tag = f"{self.server_seed_name} {self.team} {self.slot}"
        if new:
            self.migrate_flat_layout(tag)
        if not os.path.exists(self.ap_path("seed.txt")):
            self._replace("seed.txt", f"seed={tag}\nslot_name={self.auth or ''}\n")
        self._replace("current.txt", self.session + "\n", root=True)
        self.checks_read = 0             # re-sending old checks is harmless; the server keeps each once
        self.written_items = self._read_received()
        self.written_config = ""
        return new and not os.path.exists(self.ap_path("state.txt"))

    def migrate_flat_layout(self, tag: str) -> None:
        """Before 0.5.0 a game folder held one seed, directly in ap/: move it into its seed's folder."""
        try:
            with open(self.root_path("seed.txt")) as f:
                old = dict(line.strip().partition("=")[::2] for line in f if "=" in line)
        except OSError:
            return
        if old.get("seed") != tag:
            return
        for name in ("checks.txt", "received.txt", "state.txt", "config.txt", "labels.txt", "seed.txt", "levels"):
            src = self.root_path(name)
            if os.path.exists(src):
                os.replace(src, self.ap_path(name))
        logger.info(f"Moved this seed's files from ap/ into ap/{self.session}/.")

    # ---- the game ------------------------------------------------------------------------------
    def poll_game(self) -> None:
        """game.txt: key=value lines the mod rewrites about once a second while the game runs."""
        try:
            age = time.time() - os.path.getmtime(self.root_path("game.txt"))
            with open(self.root_path("game.txt")) as f:
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
        session_changed = info.get("session") != getattr(self, "_seen_session", None)
        self._seen_session = info.get("session")
        if alive and self.session and session_changed and info.get("session") != self.session:
            logger.warning("The game is still on " + (f"another seed's folder (ap/{info['session']})" if info.get("session")
                           else "no seed") + ": it switches at the Technology Center (click a building).")

    def check_player(self) -> None:
        """The mod binds a seed to the in-game player that first enters a building with it, and refuses others;
        here only a hint in the log."""
        player = self.game_info.get("player", "")
        if not (player and self.seed_ok):
            return
        bound = ""
        try:
            with open(self.ap_path("state.txt")) as f:
                for line in f:
                    if line.startswith("player="):
                        bound = line[7:].strip()
        except OSError:
            pass
        if bound and bound != player:
            logger.warning(f"This seed is played as '{bound}', but '{player}' is signed in. Sign in as '{bound}'.")
        elif not bound:
            logger.info(f"'{player}' is signed in. A seed needs a player of its own: if '{player}' played another "
                        f"seed, create a new player in the game for this one.")

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
        if not (self.game_dir and self.slot is not None):
            self.seed_ok = False
            return
        new = self.open_session()
        self.seed_ok = True
        if new:
            logger.info(f"New seed for this game folder: its files are in ap/{self.session}/. In the game, create a "
                        f"new player (Sign In, New Player) for it.")
        else:
            logger.info(f"Continuing this seed (ap/{self.session}/).")
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
