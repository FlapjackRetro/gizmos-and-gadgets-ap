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

static int give_at = -1, give_part_id = -1;
static int on_asm(void *u, GGEvent *e)
{
    (void)u; (void)e;
    static int t;
    if (give_at >= 0 && ++t == give_at) {
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
    api->on_event(GG_EV_ASSEMBLY_TICK, on_asm, NULL);
    s = getenv("TEST_PASS_PUZZLES"); pass = s && *s == '1';
    api->on_event(GG_EV_WAREHOUSE_TICK, on_tick, NULL);
    api->on_event(GG_EV_PUZZLE_RESULT, on_puzzle, NULL);
    return 0;
}
