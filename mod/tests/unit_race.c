/*
 * unit_race.c - host-side test of ap_mod's race bookkeeping (no game needed).
 * Build: cc -I <recomp>/include mod/tests/unit_race.c -o unit_race && ./unit_race <scratch dir>
 * Fakes the GGApi over a 1 MB array, calls the mod's GG_EV_RACE_END handler, then applies what the game
 * does after it ([4596+2b] += 1, [5C0E] += 1), and checks the counters for out-of-order wins and replays.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define gg_mod_init ap_gg_mod_init
#include "../ap_mod.c"
#include "../picker.c"
#undef gg_mod_init

static uint8_t M[0x100000];
static GGEventFn handlers[GG_EV_COUNT];
static const char *gdir;
#define DSB (0xEA7u * 16u)
static uint8_t *f_mem(void) { return M; }
static uint32_t f_ds_base(void) { return DSB; }
static uint16_t f_rd(uint16_t o) { return (uint16_t)(M[DSB + o] | M[DSB + o + 1] << 8); }
static void f_wr(uint16_t o, uint16_t v) { M[DSB + o] = (uint8_t)v; M[DSB + o + 1] = (uint8_t)(v >> 8); }
static uint8_t f_rd8(uint16_t o) { return M[DSB + o]; }
static void f_wr8(uint16_t o, uint8_t v) { M[DSB + o] = v; }
static int f_on_event(int t, GGEventFn fn, void *u) { (void)u; handlers[t] = fn; return 1; }
static int f_on_site(uint32_t a, GGSiteFn fn, void *u) { (void)a; (void)fn; (void)u; return 1; }
static uint32_t f_site_addr(const char *n) { (void)n; return 1; }
static void f_nop_i(int n) { (void)n; }
static int f_give(int p) { (void)p; return 0; }
static uint32_t f_call(uint16_t s, uint16_t o, int n, const uint16_t *a) { (void)s; (void)o; (void)n; (void)a; return 0; }
static void f_defer(void (*fn)(void *), void *u) { (void)fn; (void)u; }
static void f_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
static const char *f_dir(void) { return gdir; }

static int fails;
static GGEvent gg_event_zero(void) { GGEvent e; memset(&e, -1, sizeof e); return e; }
static void race(int b, int lv)
{
    GGEvent e = gg_event_zero();
    e.type = GG_EV_RACE_END; e.building = b; e.level = lv; e.won = 1; e.aborted = 0;
    handlers[GG_EV_RACE_END](NULL, &e);
    f_wr((uint16_t)(GG_DS_RACES_WON_IDX + 2 * b), (uint16_t)(f_rd((uint16_t)(GG_DS_RACES_WON_IDX + 2 * b)) + 1));
    f_wr(GG_DS_TOTAL_RACES_WON, (uint16_t)(f_rd(GG_DS_TOTAL_RACES_WON) + 1));
}
static void expect(const char *what, int got, int want)
{
    printf("%-44s %3d %s\n", what, got, got == want ? "ok" : "FAIL");
    if (got != want) { fails++; printf("   (wanted %d)\n", want); }
}

int main(int argc, char **argv)
{
    gdir = argc > 1 ? argv[1] : ".";
    GGApi a = { 1, f_on_event, f_on_site, f_site_addr, f_mem, f_ds_base, f_rd, f_wr, f_rd8, f_wr8,
                f_nop_i, f_nop_i, f_give, f_call, f_defer, f_log, f_dir };
    for (int b = 0; b < 3; b++) f_wr((uint16_t)(GG_DS_RACES_WON_IDX + 2 * b), 0xFFFF);
    ap_gg_mod_init(&a);
    race(1, 0);   expect("Aircraft L1 won: building counter", (int16_t)f_rd(0x4598), 0);
    race(1, 2);   expect("Aircraft L3 won (out of order): counter", (int16_t)f_rd(0x4598), 1);
    expect("total races", f_rd(GG_DS_TOTAL_RACES_WON), 2);
    race(1, 0);   expect("Aircraft L1 replayed: counter unchanged", (int16_t)f_rd(0x4598), 1);
    expect("replay: total unchanged", f_rd(GG_DS_TOTAL_RACES_WON), 2);
    for (int b = 0; b < 3; b++) for (int l = 0; l < 5; l++) race(b, l);
    expect("all 15 won: total", f_rd(GG_DS_TOTAL_RACES_WON), 15);
    expect("all won: Automotive counter", (int16_t)f_rd(0x4596), 4);
    expect("all won: Aircraft counter", (int16_t)f_rd(0x4598), 4);
    expect("won mask", st.won, 0x7FFF);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
