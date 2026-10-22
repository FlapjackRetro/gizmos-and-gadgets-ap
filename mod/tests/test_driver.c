/*
 * test_driver.c - helper mod for headless tests of ap_mod (not for play).
 *
 * TEST_OPEN_BOX=n     after n warehouse ticks, open box TEST_BOX_OBJ (default 0) of the player's room the way the player's
 *                     own box handler does: R08F5_0002(actor) with [7ACA] = 0, actor = DS:7A98.
 * TEST_PASS_PUZZLES=1 every door puzzle counts as solved (so Go Back opens the door).
 */
#include <stdlib.h>
#include "gg_mod.h"

static const GGApi *api;
static int open_at, box_obj, pass, ticks;

static int on_tick(void *u, GGEvent *e)
{
    (void)u; (void)e;
    if (open_at && ++ticks == open_at) {
        uint16_t ds = (uint16_t)(api->ds_base() / 16);
        uint16_t args[2] = { 0x7A98, ds };
        api->ds_write16(0x7ACA, (uint16_t)box_obj);
        api->log("test: opening box %d in room %d", box_obj, api->ds_read16(GG_DS_PLAYER_ROOM));
        api->call_far(0x08F5, 0x0002, 2, args);
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
    s = getenv("TEST_PASS_PUZZLES"); pass = s && *s == '1';
    api->on_event(GG_EV_WAREHOUSE_TICK, on_tick, NULL);
    api->on_event(GG_EV_PUZZLE_RESULT, on_puzzle, NULL);
    return 0;
}
