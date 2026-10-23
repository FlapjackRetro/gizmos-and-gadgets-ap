/*
 * picker.c - the level picker: a dialog in the game's own style, drawn with the game's own routines.
 *
 * The game's dialog runner (O0E83_0655) lays buttons out in one row and takes its 0x32-byte struct by
 * value, which api->call_far (8 word arguments) cannot pass. So this file repeats what the runner does,
 * step by step, with the same primitives (RE_NOTES §4.12):
 *   box      O0E83_0389: fill white, two black frames, two grey shadow lines
 *   button   O0E88_0000: white fill, border in black (enabled) or grey (disabled); a disabled label is drawn
 *            in grey too (O0E13_0243 sets the text colour)
 *   focus    O0E88_016B: XOR rectangle + XOR lines (drawing it twice removes it)
 *   press    O0E88_01D6: focus toggled 4 times with a 4-tick pause
 *   input    R098D_0106: next key / mouse event (0 = none). Buttons registered as mouse regions
 *            (R07AB_04BD starts a fresh list, R07AB_0294 adds one) report the button's hotkey | 0x4000
 *            (mouse button down over it), | 0x8000 (hovering), or the plain hotkey (a finished click)
 * The game's own key table for dialogs is Enter, Esc, Left, Right; Up and Down are added here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gg_mod.h"
#include "picker.h"

static const GGApi *api;

/* resident routines (image segment:offset) */
#define R0399 0x0399
#define R0473 0x0473
#define R0581 0x0581
#define R052C 0x052C
#define R07AB 0x07AB
#define R098D 0x098D
#define R0CDD 0x0CDD
#define R04FF 0x04FF
#define R0536 0x0536
/* overlays, flattened (recomp work/ssg_flat.map) */
#define O0E13 0xC096
#define O0E1D 0xC109

#define DS_COLOR   0x01F4      /* current draw colour */
#define DS_BUTTONS 0xBBBC      /* the game's dialog button array, 0x1C bytes each */
#define NBTN 6

static uint16_t ds_seg(void) { return (uint16_t)(api->ds_base() / 16); }

static uint32_t call(uint16_t seg, uint16_t off, int n, uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3,
                     uint16_t a4, uint16_t a5)
{
    uint16_t a[6] = { a0, a1, a2, a3, a4, a5 };
    return api->call_far(seg, off, n, a);
}
#define C0(s, o)                   call(s, o, 0, 0, 0, 0, 0, 0, 0)
#define C1(s, o, a)                call(s, o, 1, (uint16_t)(a), 0, 0, 0, 0, 0)
#define C2(s, o, a, b)             call(s, o, 2, (uint16_t)(a), (uint16_t)(b), 0, 0, 0, 0)
#define C3(s, o, a, b, c)          call(s, o, 3, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), 0, 0, 0)
#define C4(s, o, a, b, c, d)       call(s, o, 4, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), (uint16_t)(d), 0, 0)
#define C6(s, o, a, b, c, d, e, f) call(s, o, 6, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), (uint16_t)(d), \
                                        (uint16_t)(e), (uint16_t)(f))

static void color(int c) { api->ds_write16(DS_COLOR, (uint16_t)c); }
static void fill(int x, int y, int w, int h) { C4(R0399, 0x0100, x, y, w, h); }
static void hline(int x, int y, int len) { C3(R0399, 0x0072, x, y, len); }
static void vline(int x, int y, int len) { C3(R0399, 0x00B3, x, y, len); }
static void frame(int x, int y, int w, int h) { C4(R0473, 0x010D, x, y, w, h); }
static void xor_rect(int x, int y, int w, int h) { C4(R0399, 0x0146, x, y, w, h); }
static void flush(void) { C0(R0581, 0x052A); }

/* ---- guest memory for our strings ---------------------------------------------------------- */
static uint16_t str_seg, str_off;               /* a block from the game's allocator */
#define STR_SIZE 512

static int ensure_strings(void)
{
    if (str_seg) return 1;
    uint32_t p = C2(R0536, 0x0007, STR_SIZE, 0);  /* R0536_0007(size lo, size hi) -> DX:AX */
    if (!p) return 0;
    str_off = (uint16_t)p; str_seg = (uint16_t)(p >> 16);
    return 1;
}

static uint32_t str_lin(int off) { return (uint32_t)str_seg * 16u + str_off + (uint32_t)off; }

/* copy a C string into the block at `off`; returns its guest offset */
static uint16_t put_str(int off, const char *s)
{
    uint8_t *m = api->mem();
    size_t n = strlen(s);
    memcpy(m + str_lin(off), s, n + 1);
    return (uint16_t)(str_off + off);
}

static int text_w(uint16_t off) { return (int16_t)C2(O0E1D, 0x0056, off, str_seg); }
static int text_h(uint16_t off) { return (int16_t)C2(O0E13, 0x0418, off, str_seg); }
static void text(int x, int y, uint16_t off) { C4(O0E13, 0x029C, x, y, off, str_seg); }

/* ---- vehicle names ------------------------------------------------------------------------- */
/* Blueprint layout resources 0x002C+l, 0x00FB+l, 0x0269+l: a type-01 container whose first S element is
 * the vehicle title ("CLASS I GAS-POWERED RACER"). Loaded with the game's loader (R04FF_00FE -> a block
 * from R0536_0007), freed with R0536_0047. */
static char names[15][40];

/* "CLASS II GAS-POWERED RACER" -> "Class II Gas-Powered Racer": words of only I, V and X stay capitals */
static void title_case(char *s)
{
    while (*s) {
        char *w = s;
        while (*s && ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))) s++;
        int roman = s > w;
        for (char *p = w; p < s; p++) if (*p != 'I' && *p != 'V' && *p != 'X') roman = 0;
        if (!roman)
            for (char *p = w + 1; p < s; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
        while (*s && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))) s++;
    }
}

static void load_names(void)
{
    static const uint16_t first[3] = { 0x002C, 0x00FB, 0x0269 };
    if (names[0][0]) return;
    for (int v = 0; v < 15; v++) {
        snprintf(names[v], sizeof names[v], "%s", "?");
        uint32_t p = C1(R04FF, 0x00FE, first[v / 5] + v % 5);
        if (!p) continue;
        const uint8_t *m = api->mem();
        uint32_t base = (p >> 16) * 16u + (p & 0xFFFF);
        int n = (m[base] | m[base + 1] << 8) / 2;
        for (int k = 0; k + 1 < n; k++) {
            uint32_t e = base + (uint32_t)(m[base + 2 * k] | m[base + 2 * k + 1] << 8);
            if (m[e] == 'S') {
                snprintf(names[v], sizeof names[v], "%s", (const char *)m + e + 2);
                title_case(names[v]);
                break;
            }
        }
        C2(R0536, 0x0047, p & 0xFFFF, p >> 16);
    }
}

/* ---- buttons -------------------------------------------------------------------------------- */
typedef struct { int x, y, w, h, enabled; uint16_t label; uint16_t hot; } Btn;

static void draw_button(const Btn *b)
{
    color(0xF);
    fill(b->x - 1, b->y, b->w + 2, b->h);
    fill(b->x, b->y - 1, b->w, 1);
    fill(b->x, b->y + b->h, b->w, 1);
    color(b->enabled ? 0 : 7);
    hline(b->x + 1, b->y, b->w - 2);
    hline(b->x + 1, b->y + b->h - 1, b->w - 2);
    vline(b->x, b->y + 1, b->h - 2);
    vline(b->x + b->w - 1, b->y + 1, b->h - 2);
    hline(b->x + 1, b->y + 1, 1);
    hline(b->x + 1, b->y + b->h - 2, 1);
    hline(b->x + b->w - 2, b->y + 1, 1);
    hline(b->x + b->w - 2, b->y + b->h - 2, 1);
    int th = text_h(b->label), tw = text_w(b->label);
    if (!b->enabled) C1(O0E13, 0x0243, 7);       /* text colour (font +0x0A, DS:1F6C): grey */
    text(b->x + (b->w - tw) / 2, b->y + (b->h - th) / 2 + 1, b->label);
    if (!b->enabled) C1(O0E13, 0x0243, 0);
}

static void focus(const Btn *b)                 /* O0E88_016B; a second call removes it */
{
    xor_rect(b->x + 1, b->y + 2, b->w - 2, b->h - 4);
    xor_rect(b->x + 2, b->y + 1, b->w - 4, 1);
    xor_rect(b->x + 2, b->y + b->h - 2, b->w - 4, 1);
    flush();
}

static void press(const Btn *b)                 /* O0E88_01D6 */
{
    for (int i = 0; i < 4; i++) { focus(b); C1(R052C, 0x0072, 4); }
}

/* mouse region for button i: the game reads x/y/w/h at +2..+8 and the hotkey at +0x10 */
static void region(int i, const Btn *b, int add)
{
    uint16_t off = (uint16_t)(DS_BUTTONS + i * 0x1C);
    if (add) {
        uint8_t *m = api->mem() + api->ds_base() + off;
        memset(m, 0, 0x1C);
        uint16_t f[14] = { 0, (uint16_t)b->x, (uint16_t)b->y, (uint16_t)b->w, (uint16_t)b->h, 5, 0, 0,
                           b->hot, b->hot, 0, b->label, str_seg, (uint16_t)b->enabled };
        for (int k = 0; k < 14; k++) { m[2 * k] = (uint8_t)f[k]; m[2 * k + 1] = (uint8_t)(f[k] >> 8); }
        C2(R07AB, 0x0294, off, ds_seg());
    } else {
        C2(R07AB, 0x0314, off, ds_seg());
    }
}

/* ---- the dialog ----------------------------------------------------------------------------- */
int picker_run(int building, const int open[5], int focus_level)
{
    static const char *bname[3] = { "Automotive Building", "Aircraft Building", "Alternative Energy Building" };
    if (!ensure_strings()) { api->log("ap: picker: no memory"); return -2; }
    load_names();

    /* the runner's set-up (O0E83_0655) */
    int old_font = (int16_t)C1(O0E13, 0x0256, 0);
    uint16_t s21a = api->ds_read16(0x21A), s21c = api->ds_read16(0x21C);
    uint16_t s96 = api->ds_read16(0x96);
    api->ds_write16(0x96, 0);
    int old_cur = (int16_t)C1(R07AB, 0x07DD, 1);
    uint16_t s865a = api->ds_read16(0x865A), s865c = api->ds_read16(0x865C);
    api->ds_write16(0x865A, 0); api->ds_write16(0x865C, 0);
    C0(R0CDD, 0x05DB);

    /* strings and layout */
    char buf[64];
    Btn b[NBTN];
    int off = 0, maxw = 0;
    uint16_t title = put_str(off, bname[building]); off += (int)strlen(bname[building]) + 1;
    for (int i = 0; i < NBTN; i++) {
        if (i < 5) snprintf(buf, sizeof buf, "Level %d - %s", i + 1, names[building * 5 + i]);
        else snprintf(buf, sizeof buf, "Return to Technology Center");
        b[i].label = put_str(off, buf); off += (int)strlen(buf) + 1;
        b[i].enabled = i < 5 ? open[i] : 1;
        b[i].hot = (uint16_t)(i < 5 ? '1' + i : 'r');
        int w = text_w(b[i].label);
        if (w > maxw) maxw = w;
    }
    int fh = text_h(b[0].label);
    int bw = maxw + 0x14, bh = fh + 4, gap = 4;
    int tw = text_w(title);
    int W = (bw > tw ? bw : tw) + 28;
    int H = 14 + fh + 8 + NBTN * (bh + gap) + 4 + 10;
    int X = (320 - W) / 2, Y = (200 - H) / 2;
    for (int i = 0; i < NBTN; i++) {
        b[i].w = bw; b[i].h = bh;
        b[i].x = X + (W - bw) / 2;
        b[i].y = Y + 14 + fh + 8 + i * (bh + gap) + (i == 5 ? 4 : 0);
    }

    /* save what is under the box (O0E83_033E: into the loader's scratch block) */
    uint16_t sv_off = api->ds_read16(0x6574), sv_seg = api->ds_read16(0x6576);
    C6(R0399, 0x0194, X, Y, W, H, sv_off, sv_seg);

    /* the box (O0E83_0389) */
    color(0xF);
    fill(X, Y, W, H);
    color(0);
    frame(X + 3, Y + 3, W - 8, H - 8);
    frame(X + 2, Y + 2, W - 6, H - 6);
    color(7);
    for (int i = 0; i < 2; i++) {
        vline(X + 2 + W - 6 + i, Y + 4, H - 6);
        hline(X + 4, Y + 2 + H - 6 + i, W - 6);
    }
    color(0);
    C1(O0E13, 0x0243, 0);
    text(X + (W - tw) / 2, Y + 9, title);
    for (int i = 0; i < NBTN; i++) draw_button(&b[i]);
    C0(R07AB, 0x04BD);                          /* a fresh mouse-region list for the dialog (then 0x5D7) */
    for (int i = 0; i < NBTN; i++) region(i, &b[i], 1);

    int cur = focus_level >= 0 && focus_level < 5 && b[focus_level].enabled ? focus_level : -1;
    for (int i = 0; cur < 0 && i < NBTN; i++) if (b[i].enabled) cur = i;
    focus(&b[cur]);
    C0(R098D, 0x00BF);

    /* input loop (O0E83_0655) */
    int result = -2;
    while (result == -2) {
        int k = (uint16_t)C0(R098D, 0x0106);
        if (!k) continue;
        int dir = 0, pick = -1;
        if (k == 0x148 || k == 0x14B) dir = -1;               /* Up, Left */
        else if (k == 0x150 || k == 0x14D) dir = 1;           /* Down, Right */
        else if (k == 0x0D) pick = cur;                        /* Enter */
        else if (k == 0x1B) pick = 5;                          /* Esc = Return */
        else {
            /* the game's rule (O0E83_0655): code | 0x4000 (button down over it) or | 0x8000 (hovering) moves
             * the focus there; the plain code (a finished click, or the hotkey typed) presses the button */
            int key = k < 0x80 && k >= 'A' && k <= 'Z' ? k - 'A' + 'a' : k;
            for (int i = 0; i < NBTN; i++) {
                if (k == (b[i].hot | 0x4000) || k == (b[i].hot | 0x8000)) {
                    if (b[i].enabled && i != cur) { focus(&b[cur]); cur = i; focus(&b[cur]); }
                } else if (key == b[i].hot) {
                    pick = i;
                }
            }
        }
        if (dir) {
            int n = cur;
            do { n = (n + dir + NBTN) % NBTN; } while (!b[n].enabled);
            if (n != cur) { focus(&b[cur]); cur = n; focus(&b[cur]); }
        }
        if (pick >= 0 && b[pick].enabled) {
            if (pick != cur) { focus(&b[cur]); cur = pick; focus(&b[cur]); }
            press(&b[cur]);
            result = cur < 5 ? cur : -1;
        }
    }

    /* tear down (O0E83_0655 exit) */
    C4(R0399, 0x01F1, X, Y, sv_off, sv_seg);
    flush();
    api->ds_write16(0x865A, s865a); api->ds_write16(0x865C, s865c);
    api->ds_write16(0x96, s96);
    C0(R0CDD, 0x05E5);
    for (int i = 0; i < NBTN; i++) region(i, &b[i], 0);
    C0(R07AB, 0x04E8);
    C0(R098D, 0x00BF);
    C1(R07AB, 0x07DD, old_cur);
    C1(O0E13, 0x0256, old_font);
    api->ds_write16(0x21A, s21a); api->ds_write16(0x21C, s21c);
    return result;
}

void picker_init(const GGApi *a) { api = a; }
