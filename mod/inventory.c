/*
 * inventory.c - the Archipelago inventory: received parts wait here until the player moves them onto the
 * assembly room's conveyor belt (docs/AP_DESIGN.md).
 *
 * Opening: a click on the Pieces box (hud.c) in the assembly room. The mouse handler (R07A0_0004) leaves a
 * finished click in the record DS:A20 ([A20] = 2, x at A2A, y at A2C) until R098D_0106 reads it; the mod
 * looks at it at the head of the assembly loop (O0E30_0C89, site assembly_tick) and takes it.
 *
 * The dialog (ui.c, the game's style) lists the received parts of the level that is loaded that are not in the
 * game yet (a part only works in its own level; other levels' parts wait for theirs). Picking one checks the
 * game's own limit -- two parts of a kind (R053F_0216 on the player's list, as the box pickup R08F5_0002
 * and R0834_08D7 do) -- and either says to recycle a part first or gives the part (api->give_part, the
 * pickup without the box). The dialog stays open for more picks until the player closes it. Moved parts
 * are the player's like any other: they can go on the floor or into the recycle bin, never back.
 *
 * The conveyor while the room is open (traced 2026-10-23):
 *   - inventory list: far ptr DS:7AFC, items are part table entries (0x8E bytes). Lay-out O0E30_028F(di, x0)
 *     (far): for each item, di = 1 loads its sprite (R05DF_00A1(+0x12) -> +0x68) -- and makes mouse regions
 *     for items with +0x36 & 0x8000, so it runs once per room; R05DF_0414(+0x5E) takes the size from the
 *     sprite; a loose item (+0x1C & 0x8000: none, or the first of a pair) gets the next belt slot (centre
 *     x0, x0 - 0x64, ...; y = 0x71 - h; +0x36 &= ~1; [A0C8]++), the second of a pair sits on its partner
 *     (y = 0x4D - h; +0x36 |= 1). It also clears +0x8A (drag state) of every item.
 *   - the loop (O0E30_0C89) keeps its own copy: si = belt position (the slot in the middle), [bp-0x72] = the
 *     loose items (0xBB2: +0x36 & 1 == 0, at most 24), [bp-0xE] = last position ([A0C8] - 1), [bp-4] / [bp-8]
 *     = the item in the middle and its partner (0xC25). Scrolling (0x880 / 0x986) moves every item by 0x64,
 *     so at position k the slots are centred at 0xA0 + 0x64 k - 0x64 i. [A080] = 1 redraws the belt.
 * So after giving parts the mod loads their sprites, runs the lay-out again with di = 0 and x0 for the
 * current position (keeping +0x8A), and updates the loop's copy from the registers of the loop head.
 */
#include <stdio.h>
#include <string.h>
#include "inventory.h"
#include "ui.h"
#include "hud.h"

static const GGApi *api;

#define C0(s, o)             ui_call(s, o, 0, 0, 0, 0, 0, 0, 0)
#define C1(s, o, a)          ui_call(s, o, 1, (uint16_t)(a), 0, 0, 0, 0, 0)
#define C2(s, o, a, b)       ui_call(s, o, 2, (uint16_t)(a), (uint16_t)(b), 0, 0, 0, 0)
#define C3(s, o, a, b, c)    ui_call(s, o, 3, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), 0, 0, 0)
#define C5(s, o, a, b, c, d, e) ui_call(s, o, 5, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), (uint16_t)(d), \
                                        (uint16_t)(e), 0)
#define O0E30 0xC36E               /* flattened overlay segment */

#define ROWS     6
#define MAX_ITEMS 600
#define BELT_MAX 24                /* the loop's [bp-0x72] array */

/* ---- guest memory --------------------------------------------------------------------------------- */
static uint8_t *lin(uint32_t a) { return api->mem() + a; }
static uint16_t rd16(uint32_t a) { const uint8_t *m = lin(a); return (uint16_t)(m[0] | m[1] << 8); }
static void wr16(uint32_t a, uint16_t v) { uint8_t *m = lin(a); m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); }
static uint32_t flat(uint32_t farp) { return (farp >> 16) * 16u + (farp & 0xFFFF); }
static uint32_t ds_far(uint16_t off) { return (uint32_t)api->ds_read16((uint16_t)(off + 2)) << 16 | api->ds_read16(off); }

static uint32_t part_entry(int part)              /* far pointer (seg:off) of the part's table entry */
{
    uint32_t t = ds_far(0x663C);
    return (t & 0xFFFF0000u) | (uint16_t)((t & 0xFFFF) + part * 0x8E);
}

/* ---- part names ----------------------------------------------------------------------------------- */
/* Level v's part table is resource 0x2F5 + v (RE_NOTES §3.3): entries at 0x2D + [+6] * 2, [+2] of them,
 * 0x8E bytes each, +0x16 = the name's text resource. Loaded once per level the dialog needs. */
static char names[15][64][40];
static unsigned char names_loaded[15];

static void load_level_names(int v)
{
    if (v < 0 || v >= 15 || names_loaded[v]) return;
    names_loaded[v] = 1;
    for (int p = 0; p < 64; p++) snprintf(names[v][p], sizeof names[v][p], "part %d", p + 1);
    uint32_t t = C1(0x04FF, 0x00FE, 0x2F5 + v);
    if (!t) return;
    uint32_t b = flat(t);
    int count = (int16_t)rd16(b + 2), groups = (int16_t)rd16(b + 6);
    for (int p = 0; p < count && p < 64; p++) {
        uint16_t id = rd16(b + 0x2D + (uint32_t)groups * 2 + (uint32_t)p * 0x8E + 0x16);
        uint32_t s = C1(0x04FF, 0x00FE, id);
        if (!s) continue;
        const char *txt = (const char *)lin(flat(s));
        int k = 0;
        for (; k < 39 && txt[k] >= 32 && txt[k] < 127; k++) names[v][p][k] = txt[k];
        names[v][p][k] = 0;
        C2(0x0536, 0x0047, s & 0xFFFF, s >> 16);
    }
    C2(0x0536, 0x0047, t & 0xFFFF, t >> 16);
}

static const char *bshort[3] = { "Automotive", "Aircraft", "Alt. Energy" };

/* ---- the conveyor --------------------------------------------------------------------------------- */
static uint32_t list_first(void) { return C2(0x09C0, 0x000F, api->ds_read16(0x7AFC), api->ds_read16(0x7AFE)); }
static uint32_t list_next(uint32_t it) { return C2(0x09C0, 0x002E, it & 0xFFFF, it >> 16); }

/* Parts of this kind the player holds (R053F_0216 on the player's list, as R0834_08D7 does). */
static int held_of_kind(int part)
{
    int kind = (int16_t)C1(0x053F, 0x015D, part);
    uint32_t out = ui_scratch();
    return (int16_t)C5(0x053F, 0x0216, api->ds_read16(0x7AFC), api->ds_read16(0x7AFE), kind,
                       out & 0xFFFF, out >> 16);
}

static int belt_count(void)
{
    int n = 0;
    for (uint32_t it = list_first(); it; it = list_next(it))
        if (!(rd16(flat(it) + 0x36) & 1)) n++;
    return n;
}

static void layout(int pos)
{
    uint16_t keep[64];
    int n = 0;
    for (uint32_t it = list_first(); it && n < 64; it = list_next(it)) keep[n++] = rd16(flat(it) + 0x8A);
    uint16_t a[2] = { 0, (uint16_t)(0xA0 + 0x64 * pos) };
    api->call_far(O0E30, 0x028F, 2, a);
    n = 0;
    for (uint32_t it = list_first(); it && n < 64; it = list_next(it)) wr16(flat(it) + 0x8A, keep[n++]);
}

/* After parts were given: sprites for them, lay-out again, and the loop's own copy (see the top). */
static void refresh_belt(GGRegs *r, const int *given, int ngiven)
{
    for (int i = 0; i < ngiven; i++) {
        uint32_t e = part_entry(given[i]), el = flat(e);
        uint32_t spr = C1(0x05DF, 0x00A1, rd16(el + 0x12));
        wr16(el + 0x68, (uint16_t)spr); wr16(el + 0x6A, (uint16_t)(spr >> 16));
        C2(0x05DF, 0x0414, (e & 0xFFFF) + 0x5E, e >> 16);
    }
    uint32_t frame = (uint32_t)r->ss * 16u;
    uint16_t bp = r->bp;
    uint32_t front = (uint32_t)rd16(frame + (uint16_t)(bp - 2)) << 16 | rd16(frame + (uint16_t)(bp - 4));
    int pos = r->si;
    for (int pass = 0; pass < 2; pass++) {
        layout(pos);
        uint32_t belt[BELT_MAX];
        int n = 0, at = -1;
        for (uint32_t it = list_first(); it; it = list_next(it)) {
            if (rd16(flat(it) + 0x36) & 1) continue;
            if (n < BELT_MAX) { if (it == front) at = n; belt[n++] = it; }
        }
        int want = at >= 0 ? at : (pos < n ? pos : n - 1);
        if (want < 0) want = 0;
        if (want != pos && pass == 0) { pos = want; continue; }   /* lay out again for that position */
        for (int i = 0; i < BELT_MAX; i++) {
            uint32_t v = i < n ? belt[i] : 0;
            wr16(frame + (uint16_t)(bp - 0x72 + 4 * i), (uint16_t)v);
            wr16(frame + (uint16_t)(bp - 0x72 + 4 * i + 2), (uint16_t)(v >> 16));
        }
        front = n ? belt[pos] : 0;
        uint32_t partner = 0;
        if (front) {
            uint16_t link = rd16(flat(front) + 0x1C);
            if (link != 0xFFFF) partner = C1(0x053F, 0x019F, link & 0x7FFF);
        }
        wr16(frame + (uint16_t)(bp - 4), (uint16_t)front); wr16(frame + (uint16_t)(bp - 2), (uint16_t)(front >> 16));
        wr16(frame + (uint16_t)(bp - 8), (uint16_t)partner); wr16(frame + (uint16_t)(bp - 6), (uint16_t)(partner >> 16));
        wr16(frame + (uint16_t)(bp - 0x0E), (uint16_t)(n - 1));
        r->si = (uint16_t)pos;
        break;
    }
    if (front) C1(0x0A04, 0x076B, rd16(flat(front) + 0x8C));    /* its name on the HUD, as after a pickup */
    api->ds_write16(0xA080, 1);                        /* redraw the belt */
    api->ds_write16(0x132C, api->ds_read16(0x132C) | 1 | 2);
}

/* ---- the dialog ----------------------------------------------------------------------------------- */
typedef struct { InvItem it; int usable; } Entry;
static Entry ent[MAX_ITEMS];
static int nent;

static int cmp_entry(const Entry *a, const Entry *b)
{
    if (a->usable != b->usable) return b->usable - a->usable;
    if (a->it.v != b->it.v) return a->it.v - b->it.v;
    return a->it.part - b->it.part;
}

static void sort_entries(void)
{
    for (int i = 1; i < nent; i++) {
        Entry e = ent[i];
        int j = i - 1;
        while (j >= 0 && cmp_entry(&ent[j], &e) > 0) { ent[j + 1] = ent[j]; j--; }
        ent[j + 1] = e;
    }
}

static int X, Y, W, H, fh, rh, top, sel;
static UiBtn row[ROWS], bup, bdown, bclose, *focused;
static uint16_t s_title, s_sub;

enum { HOT_ROW = 0x3100, HOT_UP = 0x3200, HOT_DOWN = 0x3201, HOT_CLOSE = 'c' };   /* below 0x4000: R098D_0106 adds 0x4000 / 0x8000 */

static void label_for(const Entry *e, char *out, size_t n, int maxw, int slot)
{
    const char *nm = names[e->it.v][e->it.part & 63];
    if (e->usable) snprintf(out, n, "%s", nm);
    else snprintf(out, n, "%s %d: %s", bshort[e->it.v / 5], e->it.v % 5 + 1, nm);
    size_t len = strlen(out);
    while (len > 4 && ui_text_w(ui_str(slot, out)) > maxw) {     /* too long: cut, end with "..." */
        len--;
        while (len > 1 && out[len - 1] == ' ') len--;
        if (len + 4 > n) len = n - 4;
        memcpy(out + len, "...", 4);
    }
}

static void set_focus(UiBtn *b)
{
    if (focused == b) return;
    if (focused) ui_focus(focused);
    focused = b;
    if (focused) ui_focus(focused);
}

static void draw_sub(void)
{
    char s[64];
    int usable = 0;
    for (int i = 0; i < nent; i++) usable += ent[i].usable;
    if (!nent) snprintf(s, sizeof s, "No parts waiting.");
    else snprintf(s, sizeof s, "%d part%s for this level", usable, usable == 1 ? "" : "s");
    s_sub = ui_str(1, s);
    ui_color(0xF);
    ui_fill(X + 8, Y + 9 + fh + 2, W - 16, fh + 1);
    ui_text_color(7);
    ui_text(X + (W - ui_text_w(s_sub)) / 2, Y + 9 + fh + 2, s_sub);
    ui_text_color(0);
}

static void draw_rows(void)
{
    if (focused && focused >= row && focused < row + ROWS) focused = NULL;   /* drawn over */
    ui_color(0xF);
    ui_fill(row[0].x - 1, row[0].y - 1, row[0].w + 2, ROWS * rh + 1);
    for (int r = 0; r < ROWS; r++) {
        int j = top + r;
        if (j >= nent) { row[r].enabled = 0; continue; }
        char s[UI_STR_LEN];
        label_for(&ent[j], s, sizeof s, row[r].w - 10, 2 + r);
        row[r].label = ui_str(2 + r, s);
        row[r].enabled = ent[j].usable;
        ui_button(&row[r]);
    }
    bup.enabled = top > 0;
    bdown.enabled = top + ROWS < nent;
    if (focused == &bup && !bup.enabled) focused = NULL;
    if (focused == &bdown && !bdown.enabled) focused = NULL;
    ui_button(&bup);
    ui_button(&bdown);
    if (focused == &bup || focused == &bdown) { UiBtn *f = focused; focused = NULL; set_focus(f); }
    ui_flush();
}

static void draw_all(void)
{
    ui_box(X, Y, W, H);
    ui_text(X + (W - ui_text_w(s_title)) / 2, Y + 9, s_title);
    draw_sub();
    draw_rows();
    ui_button(&bclose);
    ui_flush();
}

/* focus the entry j (scrolling to it), or the Close button for -1 */
static void focus_entry(int j)
{
    if (j < 0 || j >= nent) { sel = -1; set_focus(&bclose); return; }
    sel = j;
    int t = top;
    if (j < t) t = j;
    if (j >= t + ROWS) t = j - ROWS + 1;
    if (t != top) { top = t; draw_rows(); }
    set_focus(&row[j - top]);
}

static int next_usable(int from, int dir)
{
    for (int j = from; j >= 0 && j < nent; j += dir) if (ent[j].usable) return j;
    return -1;
}

static void scroll(int by)
{
    int t = top + by, maxt = nent > ROWS ? nent - ROWS : 0;
    if (t < 0) t = 0;
    if (t > maxt) t = maxt;
    if (t == top) return;
    top = t;
    UiBtn *f = focused;
    draw_rows();
    if (sel >= 0 && (sel < top || sel >= top + ROWS)) {        /* keep the focus on screen */
        int j = next_usable(sel < top ? top : top + ROWS - 1, sel < top ? 1 : -1);
        if (j >= top && j < top + ROWS) focus_entry(j);
        else { sel = -1; set_focus(f >= row && f < row + ROWS ? &bclose : f); }
    } else if (sel >= 0) {
        set_focus(&row[sel - top]);
    } else if (f && !(f >= row && f < row + ROWS)) {
        focused = NULL; set_focus(f->enabled ? f : &bclose);
    }
}

static void draw_all(void);

/* A message over the dialog. Both saved screens would not fit into the loader's 64 KB scratch block, so the
 * message is drawn inside the dialog and the dialog is drawn again afterwards. */
static void say(const char *title, const char *a, const char *b, const char *c)
{
    const char *l[3] = { a, b, c };
    int n = c ? 3 : b ? 2 : 1;
    UiBtn *f = focused;
    ui_message(title, l, n, UI_NO_SAVE);
    focused = NULL;
    draw_all();
    if (f) set_focus(f);
}

static void full(void)
{
    say("No room on the conveyor", "You already carry two of this kind.", "Recycle one of them to make space,",
        "then pick this part again.");
}

/* Move entry j onto the conveyor. Returns 1 if it was moved (or was already in the inventory). */
static int deposit(int j, int *given, int *ngiven, void (*cb)(int))
{
    InvItem it = ent[j].it;
    int16_t owner = (int16_t)rd16(flat(part_entry(it.part)) + 0x0C);
    if (owner == 0) {                                  /* already the player's: nothing to give */
        cb(it.rx);
        return 1;
    }
    int held = held_of_kind(it.part);
    if (held >= 2 || (held == 0 && belt_count() >= BELT_MAX)) {
        full();
        return 0;
    }
    if (owner > 0) {
        /* a Cyberchimp carries the physical part (it opened the part's box before the item arrived): take it
         * out of the chimp's inventory first (R0834_0990(actor, part, 0)), or it would be in two lists */
        C3(0x0834, 0x0990, owner, it.part, 0);
        api->log("ap: inventory: part %d was carried by actor %d", it.part, owner);
    }
    int r = api->give_part(it.part);
    if (r < 0) {
        full();
        return 0;
    }
    if (*ngiven < 64) given[(*ngiven)++] = it.part;
    cb(it.rx);
    api->log("ap: inventory: part %d of level %d moved to the conveyor%s", it.part, it.v, r ? " (needed)" : "");
    return 1;
}

static int run_dialog(GGRegs *rg, void (*cb)(int))
{
    int given[64], ngiven = 0;
    UiSession ses;
    ui_begin(&ses);

    s_title = ui_str(0, "Archipelago Inventory");
    fh = ui_text_h(s_title);
    rh = fh + 6;
    W = 280; X = (320 - W) / 2;
    int list_y = 9 + 2 * fh + 10;
    H = list_y + ROWS * rh + 6 + (fh + 4) + 12;
    Y = (166 - H) / 2;
    if (Y < 2) Y = 2;
    for (int r = 0; r < ROWS; r++) {
        row[r] = (UiBtn){ X + 14, Y + list_y + r * rh, W - 28, fh + 4, 0, ui_str(2 + r, ""), (uint16_t)(HOT_ROW + r) };
    }
    int by = Y + H - 12 - (fh + 4), bw = 58;
    bup = (UiBtn){ X + 14, by, bw, fh + 4, 0, ui_str(9, "Up"), HOT_UP };
    bdown = (UiBtn){ X + 14 + bw + 8, by, bw, fh + 4, 0, ui_str(10, "Down"), HOT_DOWN };
    bclose = (UiBtn){ X + W - 14 - bw, by, bw, fh + 4, 1, ui_str(11, "Close"), HOT_CLOSE };

    ui_save(X, Y, W, H, 0);
    focused = NULL;
    top = 0;
    draw_all();
    for (int r = 0; r < ROWS; r++) ui_region(r, &row[r], 1);
    ui_region(ROWS, &bup, 1);
    ui_region(ROWS + 1, &bdown, 1);
    ui_region(ROWS + 2, &bclose, 1);
    focus_entry(next_usable(0, 1));
    ui_flush_input();

    for (;;) {
        int k = ui_key();
        if (!k) continue;
        int act = 0;                                   /* 1 = activate the focused widget */
        UiBtn *target = NULL;
        if (k == 0x1B) break;                          /* Esc */
        if (k == 0x0D || k == ' ') act = 1;
        else if (k == 0x148) {                         /* Up */
            if (sel >= 0) { int j = next_usable(sel - 1, -1); if (j >= 0) focus_entry(j); else if (top) scroll(-ROWS); }
            else { int j = next_usable(nent - 1 < top + ROWS - 1 ? nent - 1 : top + ROWS - 1, -1); if (j >= 0) focus_entry(j); }
        } else if (k == 0x150) {                       /* Down */
            if (sel >= 0) focus_entry(next_usable(sel + 1, 1));
            else if (focused != &bclose) set_focus(&bclose);
        } else if (k == 0x14B || k == 0x14D) {         /* Left / Right: along the buttons */
            UiBtn *ring[3] = { &bup, &bdown, &bclose };
            int at = focused == &bup ? 0 : focused == &bdown ? 1 : 2, d = k == 0x14B ? 2 : 1;
            for (int s = 0; s < 3; s++) { at = (at + d) % 3; if (ring[at]->enabled) break; }
            sel = -1; set_focus(ring[at]);
        } else if (k == 0x149) scroll(-ROWS);           /* PgUp */
        else if (k == 0x151) scroll(ROWS);              /* PgDn */
        else if (k == 0x147) { scroll(-nent); focus_entry(next_usable(0, 1)); }      /* Home */
        else if (k == 0x14F) { scroll(nent); }                                       /* End */
        else {
            int code = k & 0x3FFF, hover = (k & 0xC000) != 0;
            if (code >= HOT_ROW && code < HOT_ROW + ROWS) target = &row[code - HOT_ROW];
            else if (code == HOT_UP) target = &bup;
            else if (code == HOT_DOWN) target = &bdown;
            else if (code == HOT_CLOSE || code == 'C') target = &bclose;
            if (target && target->enabled) {
                if (target >= row && target < row + ROWS) { sel = top + (int)(target - row); }
                else sel = -1;
                set_focus(target);
                if (!hover) act = 1;
            }
        }
        if (!act || !focused) continue;
        UiBtn *f = focused;
        if (f == &bclose) { ui_press(f); break; }
        if (f == &bup) { ui_press(f); scroll(-ROWS); continue; }
        if (f == &bdown) { ui_press(f); scroll(ROWS); continue; }
        if (sel < 0 || sel >= nent || !ent[sel].usable) continue;
        ui_press(f);
        if (!deposit(sel, given, &ngiven, cb)) continue;
        memmove(&ent[sel], &ent[sel + 1], (size_t)(nent - sel - 1) * sizeof ent[0]);
        nent--;
        if (top > 0 && top + ROWS > nent) top = nent > ROWS ? nent - ROWS : 0;
        draw_sub();
        draw_rows();
        int j = next_usable(sel, 1);
        if (j < 0) j = next_usable(sel - 1, -1);
        focus_entry(j);
    }

    focused = NULL;
    ui_restore(X, Y, 0);
    ui_flush();
    for (int r = 0; r < ROWS + 3; r++) ui_region(r, r < ROWS ? &row[r] : r == ROWS ? &bup : r == ROWS + 1 ? &bdown : &bclose, 0);
    ui_end(&ses);
    if (ngiven) refresh_belt(rg, given, ngiven);
    return ngiven;
}

/* ---- the Cyberchimps dialog -------------------------------------------------------------------------- */
/* How many checks this level's chimps carry (ap_mod: chimp_box_open), and how many of the player's own parts
 * (part table owner +0x0C = an actor > 0: stolen on collision, or a recycled box a chimp opened). Those are
 * never checks: every recycled or stolen part has been the player's already. */
static void chimp_dialog(int checks)
{
    int own = 0, count = (int16_t)rd16(flat(ds_far(0x6638)) + 2);
    for (int p = 0; p < count && p < 64; p++) if ((int16_t)rd16(flat(part_entry(p)) + 0x0C) > 0) own++;
    char a[64], b[64];
    const char *l[4];
    int n = 0;
    if (checks) {
        snprintf(a, sizeof a, "%d Archipelago check%s.", checks, checks == 1 ? "" : "s");
        l[n++] = "The Cyberchimps in this level carry";
        l[n++] = a;
    } else {
        l[n++] = "The Cyberchimps in this level carry";
        l[n++] = "no Archipelago checks.";
    }
    if (own) {
        snprintf(b, sizeof b, "They also have %d of your parts.", own);
        if (own == 1) snprintf(b, sizeof b, "They also have 1 of your parts.");
        l[n++] = b;
    }
    if (checks || own) l[n++] = "Hit one with a banana to make it drop.";
    UiSession ses;
    ui_begin(&ses);
    ui_message("Cyberchimps", l, n, 0);
    ui_end(&ses);
    api->log("ap: chimps dialog (%d check(s), %d own part(s))", checks, own);
}

/* ---- the loop head ---------------------------------------------------------------------------------- */
/* The Pieces box and the Cyberchimp are mouse regions in the room's own list (DS:A32), like the room's buttons: press code 0
 * (nothing on hover or button down, as the room's part regions do), click code BOX_CODE. The loop keeps the
 * last event in [bp-0x22] and sends codes it does not know to 0x1330, which ignores codes below 0xFA0
 * (0xFA0 + n drags part n); so at the next loop head [bp-0x22] = BOX_CODE means the box was clicked. The
 * region is added again whenever it is not in the list (a new room starts a new list). */
#define BOX_CODE     0x0F00
#define CHIMP_CODE   0x0F01
#define BOX_REGION   (UI_REGIONS - 2)
#define CHIMP_REGION (UI_REGIONS - 3)

/* 0 = nothing, 1 = the Pieces box was clicked, 2 = the Cyberchimp */
static int clicked(GGRegs *r)
{
    int x, y, w, h;
    if (!ui_ready()) return 0;
    if (hud_box_rect(&x, &y, &w, &h) && !ui_region_listed(BOX_REGION)) {
        UiBtn b = { x, y, w, h, 1, 0, BOX_CODE };
        ui_region2(BOX_REGION, &b, 0, BOX_CODE, 1);
    }
    if (hud_chimp_rect(&x, &y, &w, &h) && !ui_region_listed(CHIMP_REGION)) {
        UiBtn b = { x, y, w, h, 1, 0, CHIMP_CODE };
        ui_region2(CHIMP_REGION, &b, 0, CHIMP_CODE, 1);
    }
    uint32_t code_at = (uint32_t)r->ss * 16u + (uint16_t)(r->bp - 0x22);
    uint16_t code = rd16(code_at);
    if (code != BOX_CODE && code != CHIMP_CODE) return 0;
    wr16(code_at, 0);
    if (api->ds_read16(0x22A4) != 0xFFFF || api->ds_read16(0xA086) != 0) return 0;   /* dragging / belt moving */
    return code == BOX_CODE ? 1 : 2;
}

int inventory_site(GGRegs *r, InvItem *items, int n, int cur_v, int chimp_checks, void (*deposit_cb)(int rx))
{
    int what = clicked(r);
    if (what == 2) { chimp_dialog(chimp_checks); return 0; }
    if (what != 1) return 0;
    nent = 0;
    for (int i = 0; i < n && nent < MAX_ITEMS; i++) {
        if (items[i].v != cur_v) continue;             /* only this level's parts: they only work here */
        ent[nent].it = items[i];
        ent[nent].usable = 1;
        load_level_names(items[i].v);                  /* the loader's scratch is free until the box is saved */
        nent++;
    }
    sort_entries();
    int usable = 0;
    for (int i = 0; i < nent; i++) usable += ent[i].usable;
    api->log("ap: inventory opened (%d part(s), %d for this level)", nent, usable);
    int moved = run_dialog(r, deposit_cb);
    api->log("ap: inventory closed (%d moved)", moved);
    return moved;
}

void inventory_init(const GGApi *a) { api = a; }
