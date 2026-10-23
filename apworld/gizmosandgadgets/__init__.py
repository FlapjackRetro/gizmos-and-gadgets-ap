"""Super Solvers: Gizmos & Gadgets! -- Archipelago world.

The game side is the Archipelago mod for the native recompilation (mod/ap_mod.c in the
gizmos-and-gadgets-ap repository); the client in client.py bridges it to the server.

Ids match the mod (docs/MOD.md):  base 0x47470000
  race won     base + 0x0000 + v                   v = building * 5 + level (0-based)
  part box     base + 0x1000 + v * 0x40 + part     the box holding that part
  (puzzle doors base + 0x2000 + v * 0x40 + rank are reserved; not locations for now)
  item: part   base + 0x1000 + v * 0x40 + part
  item: +5 bananas  base + 0x0F00
"""
from dataclasses import dataclass
from typing import Any, Dict, List

from BaseClasses import Item, ItemClassification, Location, Region, Tutorial
from Options import Choice, DefaultOnToggle, PerGameCommonOptions, Range
from worlds.AutoWorld import WebWorld, World
from worlds.LauncherComponents import Component, Type, components, launch_subprocess

from .data import BUILDINGS, PARTS, RACE

GAME = "Gizmos and Gadgets"
BASE = 0x47470000
BANANAS = "5 Bananas"


def level_name(v: int) -> str:
    return f"{BUILDINGS[v // 5]} Level {v % 5 + 1}"


def _part_names() -> Dict[int, Dict[int, str]]:
    """Item names per level; a name used twice in one level gets its part number."""
    out: Dict[int, Dict[int, str]] = {}
    for v, parts in PARTS.items():
        seen: Dict[str, int] = {}
        for pid, _g, _t, _c, name in parts:
            seen[name] = seen.get(name, 0) + 1
        out[v] = {pid: f"{level_name(v)} - {name}" + (f" #{pid}" if seen[name] > 1 else "")
                  for pid, _g, _t, _c, name in parts}
    return out


PART_NAMES = _part_names()


def race_location(v: int) -> str:
    return f"{level_name(v)} - Race"


def box_location(v: int, pid: int) -> str:
    """A box is named by its number, not by the part it holds in the vanilla game: in a seed it holds any item.
    Box n = the box of the level's part n (part table order, 1-based), i.e. location id base + 0x1000 + v*0x40 + n-1."""
    return f"{level_name(v)} - Box {pid + 1}"


def thresholds(v: int) -> Dict[int, int]:
    """Minimum tier per required group for a guaranteed race win (tools/race_logic.py, flight_sim.py):
    every non-cosmetic group must be built; stat groups need tier >= min(k, best tier in the group)."""
    k, stat = RACE[v]
    best: Dict[int, int] = {}
    for _pid, g, t, cosmetic, _n in PARTS[v]:
        if not cosmetic:
            best[g] = max(best.get(g, 0), t)
    return {g: (min(k, best[g]) if g in stat else 0) for g in best}


ITEM_IDS: Dict[str, int] = {BANANAS: BASE + 0x0F00}


def access_item(v: int) -> str:
    """level_access: item_progression -- the item that opens level v (levels 2-5 of each building)."""
    return f"{level_name(v)} Access"


ACCESS_ITEMS = [access_item(v) for v in range(15) if v % 5]
for _v in range(15):
    if _v % 5:
        ITEM_IDS[access_item(_v)] = BASE + 0x0E00 + _v
LOCATION_IDS: Dict[str, int] = {}
for _v in range(15):
    LOCATION_IDS[race_location(_v)] = BASE + _v
    for _pid, _name in PART_NAMES[_v].items():
        ITEM_IDS[_name] = BASE + 0x1000 + _v * 0x40 + _pid
        LOCATION_IDS[box_location(_v, _pid)] = BASE + 0x1000 + _v * 0x40 + _pid


class LevelAccess(Choice):
    """Which levels can be entered (the level picker after clicking a building).
    race_progression: a building's next level opens when you win its race, as in the game; won levels stay open.
    open: every level of every building can be entered from the start.
    item_progression: level 1 of each building is open; the other 12 levels open when you find their
    "<Building> Level <n> Access" item, in any order."""
    display_name = "Level Access"
    option_race_progression = 0
    option_open = 1
    option_item_progression = 2
    default = 0


class Cyberchimps(DefaultOnToggle):
    """Play with Cyberchimps in the warehouse (the game's "With Chimps" choice for a new player). Chimps open boxes
    and carry off what they find -- also Archipelago checks, which a chimp drops again when hit with a banana --
    and steal parts from you. Off: no chimps. The game does not ask; it shows this setting instead."""
    display_name = "Cyberchimps"


class RacesRequired(Range):
    """How many different races you must win to finish (goal). Any races count, in any building."""
    display_name = "Races Required"
    range_start = 1
    range_end = 15
    default = 15


@dataclass
class GizmosOptions(PerGameCommonOptions):
    level_access: LevelAccess
    cyberchimps: Cyberchimps
    races_required: RacesRequired


class GizmosItem(Item):
    game = GAME


class GizmosLocation(Location):
    game = GAME


class GizmosWeb(WebWorld):
    theme = "partyTime"
    tutorials = [Tutorial("Setup Guide", "Playing Gizmos and Gadgets with the recomp mod and the client.",
                          "English", "setup_en.md", "setup/en", ["FlapjackRetro"])]


def launch_client(*args: str) -> None:
    from .client import launch
    launch_subprocess(launch, name="GizmosClient", args=args)


components.append(Component("Gizmos and Gadgets Client", func=launch_client, component_type=Type.CLIENT))


class GizmosWorld(World):
    """Super Solvers: Gizmos & Gadgets! (The Learning Company, 1993). Search Shady Glen's warehouses for
    parts, build vehicles and beat Morty Maxwell in 15 races."""
    game = GAME
    web = GizmosWeb()
    options_dataclass = GizmosOptions
    options: GizmosOptions
    item_name_to_id = ITEM_IDS
    location_name_to_id = LOCATION_IDS
    origin_region_name = "Technology Center"

    def create_item(self, name: str) -> GizmosItem:
        if name in ACCESS_ITEMS:
            return GizmosItem(name, ItemClassification.progression, ITEM_IDS[name], self.player)
        if name == BANANAS:
            cls = ItemClassification.filler
        else:
            cls = self._part_class[name]
        return GizmosItem(name, cls, ITEM_IDS[name], self.player)

    def generate_early(self) -> None:
        self._part_class: Dict[str, ItemClassification] = {}
        for v in range(15):
            thr = thresholds(v)
            for pid, g, t, cosmetic, _n in PARTS[v]:
                name = PART_NAMES[v][pid]
                if cosmetic:
                    c = ItemClassification.filler
                elif t >= thr[g]:
                    c = ItemClassification.progression
                else:
                    c = ItemClassification.useful
                self._part_class[name] = c

    def create_regions(self) -> None:
        center = Region("Technology Center", self.player, self.multiworld)
        self.multiworld.regions.append(center)
        for b in range(3):
            prev = center
            for lv in range(5):
                v = b * 5 + lv
                region = Region(level_name(v), self.player, self.multiworld)
                self.multiworld.regions.append(region)
                for pid in PART_NAMES[v]:
                    name = box_location(v, pid)
                    region.locations.append(GizmosLocation(self.player, name, LOCATION_IDS[name], region))
                race = GizmosLocation(self.player, race_location(v), LOCATION_IDS[race_location(v)], region)
                race.access_rule = self._race_rule(v)
                region.locations.append(race)
                done = GizmosLocation(self.player, f"{level_name(v)} - Won", None, region)
                done.access_rule = self._race_rule(v)
                done.place_locked_item(GizmosItem(f"{level_name(v)} Won", ItemClassification.progression,
                                                  None, self.player))
                region.locations.append(done)
                # race_progression: a building's next level opens when the previous level's race is won
                if prev is center or self.options.level_access == LevelAccess.option_open:
                    center.connect(region, f"Enter {level_name(v)}")
                elif self.options.level_access == LevelAccess.option_item_progression:
                    center.connect(region, f"Enter {level_name(v)}",
                                   lambda state, a=access_item(v): state.has(a, self.player))
                else:
                    prev_won = f"{level_name(v - 1)} Won"
                    prev.connect(region, f"Enter {level_name(v)}",
                                 lambda state, e=prev_won: state.has(e, self.player))
                prev = region

    def _race_rule(self, v: int):
        groups: Dict[int, List[str]] = {}
        thr = thresholds(v)
        for pid, g, t, cosmetic, _n in PARTS[v]:
            if not cosmetic and t >= thr[g]:
                groups.setdefault(g, []).append(PART_NAMES[v][pid])
        needed = list(groups.values())
        return lambda state: all(state.has_any(names, self.player) for names in needed)

    def create_items(self) -> None:
        pool = [self.create_item(name) for level in PART_NAMES.values() for name in level.values()]
        n_locations = len(self.multiworld.get_unfilled_locations(self.player))
        if self.options.level_access == LevelAccess.option_item_progression:
            pool += [self.create_item(name) for name in ACCESS_ITEMS]
        pool += [self.create_item(BANANAS) for _ in range(n_locations - len(pool))]
        self.multiworld.itempool += pool

    def set_rules(self) -> None:
        wins = [f"{level_name(v)} Won" for v in range(15)]
        need = self.options.races_required.value
        self.multiworld.completion_condition[self.player] = \
            lambda state: state.has_from_list(wins, self.player, need)

    def get_filler_item_name(self) -> str:
        return BANANAS

    def fill_slot_data(self) -> Dict[str, Any]:
        return {"id_base": BASE, "level_access": self.options.level_access.current_key,
                "cyberchimps": bool(self.options.cyberchimps.value),
                "races_required": self.options.races_required.value}
