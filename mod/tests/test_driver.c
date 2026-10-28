/*
 * test_driver.c - helper mod for headless tests of ap_mod (not for play).
 *
 * TEST_OPEN_BOX=n     after n warehouse ticks, open box TEST_BOX_OBJ (default 0) of the player's room the way the player's
 *                     own box handler does: R08F5_0002(actor) with [7ACA] = box, actor = DS:7A8A (actor 0, the player).
 * TEST_RECYCLE=part   with TEST_OPEN_BOX: just before opening, mark `part` recycled (+0x36 |= 0x4000) and
 *                     drop it as a new box at the player's feet (R0622_0DF0, as a chimp drop does);
 *                     that new box (the room's last) is the one opened.
 * TEST_DROP=part      the same, but without marking it: drop `part` as a box as a chimp would (to test that
 *                     ap_mod marked a part the player obtained).
 * TEST_SAVE=1         after opening the box, save the game the way leaving the warehouse does (DS:94 = 1,
 *                     O0E18_01EA)
 * TEST_GIVE_ASM=t:p   at assembly-room pass t, give part p (api->give_part), to see what the room does
 * TEST_CHIMP=t:n:obj  at warehouse tick t, chimp actor n opens box obj of the player's room (O0E0D_0000, flat
 *                     C000:0000, with the actor's room/object set); 40 ticks later it is hit by a banana
 *                     (O0E0D_00A1(n)); 40 ticks after that the player opens the room's last box (the dropped one).
 * TEST_CHIMP_FULL=t:n  the chimp cap: from warehouse tick t, the player picks up two recycled parts (0 and 1); chimp n
 *                     opens box 0 of the player's room twice, then a new check box of part 7 (it should be full
 *                     and move that box to another room); then chimp n
 *                     touches the player (R08F5_0180(player) with player +0x6E = n; refused when full) and so
 *                     does chimp n+1 (allowed).
 * TEST_RACES_IDX=b:n  keep the building's races-won counter [4596+2b] at n (every frame), e.g. to draw the parking lot
 * TEST_COMPLETE=t    at assembly-room pass t, count the vehicle as complete ([4584] = slots, as if every slot
 *                     were filled), so the race door opens
 * TEST_WIN=1          every race counts as won (race_end: won = 1)
 * TEST_PASS_PUZZLES=1 every door puzzle counts as solved (so Go Back opens the door).
 */
#include <stdlib.h>
#include <string.h>
#include "gg_mod.h"

static const GGApi *api;
static int open_at, box_obj, recycle = -1, mark = 1, pass, ticks, save_after;

static int on_tick(void *u, GGEvent *e)
{
    (void)u; (void)e;
    if (open_at && ++ticks == open_at) {
        uint16_t ds = (uint16_t)(api->ds_base() / 16);
        uint16_t args[2] = { 0x7A8A, ds };
        if (recycle >= 0) {
            const uint8_t *m = api->mem();
            uint32_t tab = (uint32_t)api->ds_read16(0x663E) * 16u + api->ds_read16(0x663C) + (uint32_t)recycle * 0x8E;
            uint16_t f = (uint16_t)(m[tab + 0x36] | m[tab + 0x37] << 8) | (mark ? 0x4000 : 0);
            api->mem()[tab + 0x36] = (uint8_t)f; api->mem()[tab + 0x37] = (uint8_t)(f >> 8);
            uint16_t room = api->ds_read16(GG_DS_PLAYER_ROOM);
            uint16_t a[5] = { room, api->ds_read16(0x7A8A + 0x14), api->ds_read16(0x7A8A + 0x1C), (uint16_t)recycle, 0 };
            api->call_far(0x0622, 0x0DF0, 5, a);
            uint32_t hdr = (uint32_t)api->ds_read16(0x7A06 + 4 * room) * 16u + api->ds_read16(0x7A04 + 4 * room);
            box_obj = (m[hdr + 0x9E] | m[hdr + 0x9F] << 8) - 1;
            api->log("test: part %d dropped as box %d (%s)", recycle, box_obj, mark ? "marked recycled" : "unmarked, as a chimp drop");
        }
        api->ds_write16(0x7ACA, (uint16_t)box_obj);
        api->log("test: opening box %d in room %d", box_obj, api->ds_read16(GG_DS_PLAYER_ROOM));
        api->call_far(0x08F5, 0x0002, 2, args);
        if (save_after) { api->ds_write16(0x94, 1); api->call_far(0xC0DA, 0x01EA, 0, NULL); api->log("test: saved"); }
    }
    return GG_CONTINUE;
}

/* TEST_DUMP_OWNERS=n: log every part's owner (+0x0C) and flags (+0x36) at warehouse tick n */
static int dump_at;
static int on_tick_dump(void *u, GGEvent *e)
{
    (void)u; (void)e;
    static int t;
    if (!dump_at || ++t != dump_at) return GG_CONTINUE;
    const uint8_t *m = api->mem();
    uint32_t ds = api->ds_base();
    uint32_t hdr = (uint32_t)(m[ds + 0x663A] | m[ds + 0x663B] << 8) * 16u + (m[ds + 0x6638] | m[ds + 0x6639] << 8);
    uint32_t tab = (uint32_t)(m[ds + 0x663E] | m[ds + 0x663F] << 8) * 16u + (m[ds + 0x663C] | m[ds + 0x663D] << 8);
    int n = m[hdr + 2] | m[hdr + 3] << 8;
    for (int p = 0; p < n; p++) {
        uint32_t e2 = tab + (uint32_t)p * 0x8E;
        api->log("test: part %2d owner %d flags %04X", p, (int16_t)(m[e2 + 0x0C] | m[e2 + 0x0D] << 8), m[e2 + 0x36] | m[e2 + 0x37] << 8);
    }
    return GG_CONTINUE;
}

static int chimp_t = -1, chimp_n = 1, chimp_obj = 0;
static uint16_t r16(uint32_t a) { const uint8_t *m = api->mem(); return (uint16_t)(m[a] | m[a + 1] << 8); }
static void w16(uint32_t a, uint16_t v) { uint8_t *m = api->mem(); m[a] = (uint8_t)v; m[a + 1] = (uint8_t)(v >> 8); }
static int room_boxes(void)
{
    uint16_t room = api->ds_read16(GG_DS_PLAYER_ROOM);
    uint32_t hdr = (uint32_t)api->ds_read16(0x7A06 + 4 * room) * 16u + api->ds_read16(0x7A04 + 4 * room);
    return (int16_t)r16(hdr + 0x9E);
}
static int on_tick_chimp(void *u, GGEvent *e)
{
    (void)u; (void)e;
    static int t;
    if (chimp_t < 0) return GG_CONTINUE;
    ++t;
    uint16_t ds = (uint16_t)(api->ds_base() / 16);
    uint32_t actor = api->ds_base() + 0x7A8A + (uint32_t)chimp_n * 0xA0;
    if (t == chimp_t) {
        uint16_t room = api->ds_read16(GG_DS_PLAYER_ROOM);
        api->log("test: chimp %d (room %d, holds %d) opens box %d in room %d; boxes %d", chimp_n,
                 (int16_t)r16(actor + 0x1A), (int16_t)r16(actor + 0x70), chimp_obj, room, room_boxes());
        w16(actor + 0x1A, room); w16(actor + 0x40, (uint16_t)chimp_obj);
        uint16_t a[2] = { (uint16_t)(0x7A8A + chimp_n * 0xA0), ds };
        api->call_far(0xC000, 0x0000, 2, a);
        api->log("test: after: chimp holds %d, boxes %d", (int16_t)r16(actor + 0x70), room_boxes());
    } else if (t == chimp_t + 40) {
        uint16_t a[1] = { (uint16_t)chimp_n };
        api->call_far(0xC000, 0x00A1, 1, a);
        api->log("test: banana: chimp holds %d, boxes %d", (int16_t)r16(actor + 0x70), room_boxes());
    } else if (t == chimp_t + 80) {
        int last = room_boxes() - 1;
        api->ds_write16(0x7ACA, (uint16_t)last);
        uint16_t args[2] = { 0x7A8A, ds };
        api->log("test: player opens box %d", last);
        api->call_far(0x08F5, 0x0002, 2, args);
        api->log("test: boxes %d", room_boxes());
    }
    return GG_CONTINUE;
}

static int full_t = -1, full_n = 1;
static void pick_recycled(int part)
{
    uint16_t ds = (uint16_t)(api->ds_base() / 16);
    uint8_t *m = api->mem();
    uint32_t tab = (uint32_t)api->ds_read16(0x663E) * 16u + api->ds_read16(0x663C) + (uint32_t)part * 0x8E;
    m[tab + 0x37] |= 0x40;
    uint16_t room = api->ds_read16(GG_DS_PLAYER_ROOM);
    uint16_t a[5] = { room, api->ds_read16(0x7A8A + 0x14), api->ds_read16(0x7A8A + 0x1C), (uint16_t)part, 0 };
    api->call_far(0x0622, 0x0DF0, 5, a);
    api->ds_write16(0x7ACA, (uint16_t)(room_boxes() - 1));
    uint16_t args[2] = { 0x7A8A, ds };
    api->call_far(0x08F5, 0x0002, 2, args);
}
static int on_tick_full(void *u, GGEvent *e)
{
    (void)u; (void)e;
    static int t;
    if (full_t < 0) return GG_CONTINUE;
    int k = ++t - full_t;
    uint16_t ds = (uint16_t)(api->ds_base() / 16);
    uint32_t player = api->ds_base() + 0x7A8A;
    if (k == 0 || k == 10) {
        pick_recycled(k / 10);
        api->log("test: player picked up part %d, holds %d", k / 10, (int16_t)r16(player + 0x70));
    } else if (k == 20 || k == 30 || k == 40) {
        uint32_t actor = api->ds_base() + 0x7A8A + (uint32_t)full_n * 0xA0;
        uint16_t room = api->ds_read16(GG_DS_PLAYER_ROOM);
        api->log("test: chimp %d opens box 0 in room %d (boxes %d, chimp holds %d)", full_n, room, room_boxes(),
                 (int16_t)r16(actor + 0x70));
        int obj = 0;
        if (k == 40) {                                       /* a fresh check box (part 7, normal sprite) */
            uint16_t c[5] = { room, api->ds_read16(0x7A8A + 0x14), api->ds_read16(0x7A8A + 0x1C), 7, 0 };
            api->call_far(0x0622, 0x0DF0, 5, c);
            obj = room_boxes() - 1;
        }
        w16(actor + 0x1A, room); w16(actor + 0x40, (uint16_t)obj);
        uint16_t a[2] = { (uint16_t)(0x7A8A + full_n * 0xA0), ds };
        api->call_far(0xC000, 0x0000, 2, a);
        api->log("test: after: boxes %d", room_boxes());
    } else if (k == 45) {                                    /* where did the moved box of part 7 go? */
        int nrooms = (int16_t)api->ds_read16(0x08C4);
        for (int r = 0; r < nrooms && r < 16; r++) {
            uint32_t hdr = (uint32_t)api->ds_read16(0x7A06 + 4 * r) * 16u + api->ds_read16(0x7A04 + 4 * r);
            int cnt = (int16_t)r16(hdr + 0x9E);
            for (int o = 0; o < cnt; o++) {
                uint16_t a[2] = { (uint16_t)r, (uint16_t)o };
                uint32_t bp = api->call_far(0x0622, 0x1265, 2, a);
                uint32_t box = (bp >> 16) * 16u + (bp & 0xFFFFu);
                if ((int16_t)r16(box + 0x1E) == 7)
                    api->log("test: box of part 7 in room %d (object %d), sprite %03X", r, o, r16(box + 0x1A));
            }
        }
    } else if (k == 50 || k == 60) {
        int n = k == 50 ? full_n : full_n + 1;
        uint32_t actor = api->ds_base() + 0x7A8A + (uint32_t)n * 0xA0;
        w16(player + 0x6E, (uint16_t)n);
        uint16_t a[2] = { 0x7A8A, ds };
        api->call_far(0x08F5, 0x0180, 2, a);
        api->log("test: chimp %d touched the player: player holds %d, chimp holds %d", n,
                 (int16_t)r16(player + 0x70), (int16_t)r16(actor + 0x70));
    }
static int races_b = -1, races_n;
static int on_frame_races(void *u, GGEvent *e)
{
    (void)u; (void)e;
    if (races_b >= 0) api->ds_write16((uint16_t)(0x4596 + 2 * races_b), (uint16_t)races_n);
    return GG_CONTINUE;
}

static int complete_at = -1, force_win;
static int on_race_end_t(void *u, GGEvent *e) { (void)u; if (force_win) { e->won = 1; e->aborted = 0; api->log("test: race forced won"); } return GG_CONTINUE; }
static int give_at = -1, give_part_id = -1;
static int on_asm(void *u, GGEvent *e)
{
    (void)u; (void)e;
    static int t;
    ++t;
    if (complete_at >= 0 && t == complete_at) {
        const uint8_t *m = api->mem();
        uint32_t hdr = (uint32_t)api->ds_read16(0x663A) * 16u + api->ds_read16(0x6638);
        api->ds_write16(0x4584, (uint16_t)(m[hdr + 6] | m[hdr + 7] << 8));
        api->log("test: vehicle counted complete (%d slots)", api->ds_read16(0x4584));
    }
    if (give_at >= 0 && t == give_at) {
        int r = api->give_part(give_part_id);
        api->log("test: give_part(%d) in the assembly room -> %d", give_part_id, r);
    }
    return GG_CONTINUE;
}

static int on_puzzle(void *u, GGEvent *e)
{
    (void)u;
    if (pass) { api->log("test: puzzle forced to pass"); e->result = 1; }
    return GG_CONTINUE;
}

GG_MOD_EXPORT int gg_mod_init(const GGApi *a)
{
    api = a;
    const char *s = getenv("TEST_OPEN_BOX"); open_at = s ? atoi(s) : 0;
    s = getenv("TEST_BOX_OBJ"); box_obj = s ? atoi(s) : 0;
    s = getenv("TEST_RECYCLE"); recycle = s ? atoi(s) : -1;
    s = getenv("TEST_SAVE"); save_after = s && *s == '1';
    s = getenv("TEST_DROP"); if (s) { recycle = atoi(s); mark = 0; }
    s = getenv("TEST_DUMP_OWNERS"); dump_at = s ? atoi(s) : 0;
    api->on_event(GG_EV_WAREHOUSE_TICK, on_tick_dump, NULL);
    s = getenv("TEST_GIVE_ASM");
    if (s) { give_at = atoi(s); const char *c = strchr(s, ':'); give_part_id = c ? atoi(c + 1) : 0; }
    s = getenv("TEST_COMPLETE"); complete_at = s ? atoi(s) : -1;
    s = getenv("TEST_WIN"); force_win = s && *s == '1';
    api->on_event(GG_EV_RACE_END, on_race_end_t, NULL);
    api->on_event(GG_EV_ASSEMBLY_TICK, on_asm, NULL);
    s = getenv("TEST_RACES_IDX");
    if (s) { races_b = atoi(s); const char *c1 = strchr(s, ':'); races_n = c1 ? atoi(c1 + 1) : 0; }
    api->on_event(GG_EV_FRAME, on_frame_races, NULL);
    s = getenv("TEST_CHIMP");
    if (s) { chimp_t = atoi(s); const char *c1 = strchr(s, ':'); if (c1) { chimp_n = atoi(c1 + 1); c1 = strchr(c1 + 1, ':'); if (c1) chimp_obj = atoi(c1 + 1); } }
    api->on_event(GG_EV_WAREHOUSE_TICK, on_tick_chimp, NULL);
    s = getenv("TEST_CHIMP_FULL");
    if (s) { full_t = atoi(s); const char *c1 = strchr(s, ':'); if (c1) full_n = atoi(c1 + 1); }
    api->on_event(GG_EV_WAREHOUSE_TICK, on_tick_full, NULL);
    s = getenv("TEST_PASS_PUZZLES"); pass = s && *s == '1';
    api->on_event(GG_EV_WAREHOUSE_TICK, on_tick, NULL);
    api->on_event(GG_EV_PUZZLE_RESULT, on_puzzle, NULL);
    return 0;
}
