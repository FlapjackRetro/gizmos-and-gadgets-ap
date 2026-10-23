from test.bases import WorldTestBase

from .. import BASE, GAME, LOCATION_IDS, PARTS, PART_NAMES, level_name, race_location, thresholds


class GizmosTestBase(WorldTestBase):
    game = GAME


class TestDefault(GizmosTestBase):
    def test_counts(self) -> None:
        locs = self.multiworld.get_locations(self.player)
        real = [loc for loc in locs if loc.address is not None]
        self.assertEqual(len(real), 532 + 15)

    def test_ids_match_mod(self) -> None:
        # docs/MOD.md: box of part p in level v = base + 0x1000 + v*0x40 + p; race = base + v
        self.assertEqual(LOCATION_IDS[race_location(5)], BASE + 5)
        self.assertEqual(LOCATION_IDS["Aircraft Level 1 - Box 5"], 0x47471144)

    def test_first_race_needs_parts(self) -> None:
        self.assertFalse(self.can_reach_location(race_location(0)))
        thr = thresholds(0)
        best = {}
        for pid, g, t, cosmetic, _n in PARTS[0]:
            if not cosmetic and t >= thr[g]:
                best.setdefault(g, PART_NAMES[0][pid])
        self.collect_by_name(list(best.values()))
        self.assertTrue(self.can_reach_location(race_location(0)))

    def test_level_two_locked_until_race(self) -> None:
        self.assertFalse(self.can_reach_region("Automotive Level 2"))


class TestOpen(GizmosTestBase):
    options = {"level_access": "open"}

    def test_all_levels_open(self) -> None:
        for v in range(15):
            self.assertTrue(self.can_reach_region(level_name(v)))


class TestFewRaces(GizmosTestBase):
    options = {"races_required": 3, "cyberchimps": False}

    def test_goal_after_three_wins(self) -> None:
        state = self.multiworld.state
        self.assertFalse(self.multiworld.completion_condition[self.player](state))
        wins = [f"{level_name(v)} Won" for v in (0, 5, 10)]
        from .. import GizmosItem
        from BaseClasses import ItemClassification
        for w in wins:
            state.collect(GizmosItem(w, ItemClassification.progression, None, self.player), True)
        self.assertTrue(self.multiworld.completion_condition[self.player](state))
        self.assertEqual(self.world.fill_slot_data()["races_required"], 3)
        self.assertFalse(self.world.fill_slot_data()["cyberchimps"])


class TestItemProgression(GizmosTestBase):
    options = {"level_access": "item_progression"}

    def test_level_two_needs_its_item(self) -> None:
        from .. import ACCESS_ITEMS, access_item
        self.assertEqual(len(ACCESS_ITEMS), 12)
        pool = [item.name for item in self.multiworld.itempool if item.player == self.player]
        for name in ACCESS_ITEMS:
            self.assertEqual(pool.count(name), 1)
        self.assertTrue(self.can_reach_region("Aircraft Level 1"))
        self.assertFalse(self.can_reach_region("Aircraft Level 3"))
        self.collect_by_name(access_item(7))
        self.assertTrue(self.can_reach_region("Aircraft Level 3"))      # no race or level 2 needed
        self.assertFalse(self.can_reach_region("Aircraft Level 2"))
