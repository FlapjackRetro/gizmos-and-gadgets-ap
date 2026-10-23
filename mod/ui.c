/*
 * ui.c - dialogs in the game's own style, drawn with the game's own primitives (RE_NOTES §4.12).
 *
 * The game's dialog runner (O0E83_0655) lays buttons out in one row and takes its 0x32-byte struct by
 * value, which api->call_far (8 word arguments) cannot pass. So this file repeats what the runner does,
 * step by step, with the same primitives:
 *   box      O0E83_0389: fill white, two black frames, two grey shadow lines
 *   button   O0E88_0000: white fill, border in black (enabled) or grey (disabled); a disabled label is drawn
 *            in grey too (O0E13_0243 sets the text colour)
 *   focus    O0E88_016B: XOR rectangle + XOR lines (drawing it twice removes it)
 *   press    O0E88_01D6: focus toggled 4 times with a 4-tick pause
 *   input    R098D_0106: next key / mouse event (0 = none). Buttons registered as mouse regions
 *            (R07AB_04BD starts a fresh list, R07AB_0294 adds one) report the button's code | 0x4000
 *            (mouse button down over it), | 0x8000 (hovering), or the plain code (a finished click)
 */
#include <string.h>
#include "ui.h"

static const GGApi *api;

#define R0399 0x0399
#define R0473 0x0473
#define R0581 0x0581
#define R052C 0x052C
#define R07AB 0x07AB
#define R098D 0x098D
#define R0CDD 0x0CDD
#define R0536 0x0536
#define O0E13 0xC096                /* flattened overlay segments (recomp work/ssg_flat.map) */
#define O0E1D 0xC109

#define DS_COLOR 0x01F4

/* our block: regions (0x1C bytes each), scratch, strings */
#define REG_OFF   0
#define SCR_OFF   (UI_REGIONS * 0x1C)
#define STR_OFF   (SCR_OFF + 16)
#define BLOCK     (STR_OFF + UI_STR_SLOTS * UI_STR_LEN)

static uint16_t blk_seg, blk_off;

void ui_init(const GGApi *a) { api = a; }

uint32_t ui_call(uint16_t seg, uint16_t off, int n, uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3,
                 uint16_t a4, uint16_t a5)
{
    uint16_t a[6] = { a0, a1, a2, a3, a4, a5 };
    return api->call_far(seg, off, n, a);
}
#define C0(s, o)                   ui_call(s, o, 0, 0, 0, 0, 0, 0, 0)
#define C1(s, o, a)                ui_call(s, o, 1, (uint16_t)(a), 0, 0, 0, 0, 0)
#define C2(s, o, a, b)             ui_call(s, o, 2, (uint16_t)(a), (uint16_t)(b), 0, 0, 0, 0)
#define C3(s, o, a, b, c)          ui_call(s, o, 3, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), 0, 0, 0)
#define C4(s, o, a, b, c, d)       ui_call(s, o, 4, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), (uint16_t)(d), 0, 0)
#define C6(s, o, a, b, c, d, e, f) ui_call(s, o, 6, (uint16_t)(a), (uint16_t)(b), (uint16_t)(c), (uint16_t)(d), \
                                           (uint16_t)(e), (uint16_t)(f))

int ui_ready(void)
{
    if (blk_seg) return 1;
    uint32_t p = C2(R0536, 0x0007, BLOCK, 0);        /* R0536_0007(size lo, size hi) -> DX:AX */
    if (!p) return 0;
    blk_off = (uint16_t)p; blk_seg = (uint16_t)(p >> 16);
    memset(api->mem() + (uint32_t)blk_seg * 16u + blk_off, 0, BLOCK);
    return 1;
}

uint16_t ui_seg(void) { return blk_seg; }
static uint8_t *blk(int off) { return api->mem() + (uint32_t)blk_seg * 16u + blk_off + (uint32_t)off; }

uint16_t ui_str(int slot, const char *s)
{
    int off = STR_OFF + slot * UI_STR_LEN;
    size_t n = strlen(s);
    if (n > UI_STR_LEN - 1) n = UI_STR_LEN - 1;
    memcpy(blk(off), s, n);
    blk(off)[n] = 0;
    return (uint16_t)(blk_off + off);
}

uint32_t ui_scratch(void) { return (uint32_t)blk_seg << 16 | (uint16_t)(blk_off + SCR_OFF); }

void ui_color(int c) { api->ds_write16(DS_COLOR, (uint16_t)c); }
void ui_text_color(int c) { C1(O0E13, 0x0243, c); }
void ui_fill(int x, int y, int w, int h) { C4(R0399, 0x0100, x, y, w, h); }
void ui_hline(int x, int y, int len) { C3(R0399, 0x0072, x, y, len); }
void ui_vline(int x, int y, int len) { C3(R0399, 0x00B3, x, y, len); }
void ui_frame(int x, int y, int w, int h) { C4(R0473, 0x010D, x, y, w, h); }
void ui_xor(int x, int y, int w, int h) { C4(R0399, 0x0146, x, y, w, h); }
void ui_flush(void) { C0(R0581, 0x052A); }
int  ui_text_w(uint16_t s) { return (int16_t)C2(O0E1D, 0x0056, s, blk_seg); }
int  ui_text_h(uint16_t s) { return (int16_t)C2(O0E13, 0x0418, s, blk_seg); }
void ui_text(int x, int y, uint16_t s) { C4(O0E13, 0x029C, x, y, s, blk_seg); }
void ui_wait(int ticks) { C1(R052C, 0x0072, ticks); }
int  ui_key(void) { return (uint16_t)C0(R098D, 0x0106); }
void ui_flush_input(void) { C0(R098D, 0x00BF); }

void ui_button(const UiBtn *b)
{
    ui_color(0xF);
    ui_fill(b->x - 1, b->y, b->w + 2, b->h);
    ui_fill(b->x, b->y - 1, b->w, 1);
    ui_fill(b->x, b->y + b->h, b->w, 1);
    ui_color(b->enabled ? 0 : 7);
    ui_hline(b->x + 1, b->y, b->w - 2);
    ui_hline(b->x + 1, b->y + b->h - 1, b->w - 2);
    ui_vline(b->x, b->y + 1, b->h - 2);
    ui_vline(b->x + b->w - 1, b->y + 1, b->h - 2);
    ui_hline(b->x + 1, b->y + 1, 1);
    ui_hline(b->x + 1, b->y + b->h - 2, 1);
    ui_hline(b->x + b->w - 2, b->y + 1, 1);
    ui_hline(b->x + b->w - 2, b->y + b->h - 2, 1);
    int th = ui_text_h(b->label), tw = ui_text_w(b->label);
    if (!b->enabled) ui_text_color(7);              /* text colour (font +0x0A, DS:1F6C): grey */
    ui_text(b->x + (b->w - tw) / 2, b->y + (b->h - th) / 2 + 1, b->label);
    if (!b->enabled) ui_text_color(0);
}

void ui_focus(const UiBtn *b)
{
    ui_xor(b->x + 1, b->y + 2, b->w - 2, b->h - 4);
    ui_xor(b->x + 2, b->y + 1, b->w - 4, 1);
    ui_xor(b->x + 2, b->y + b->h - 2, b->w - 4, 1);
    ui_flush();
}

void ui_press(const UiBtn *b)
{
    for (int i = 0; i < 4; i++) { ui_focus(b); ui_wait(4); }
}

/* the game reads x/y/w/h at +2..+8, priority +0x0A, codes +0x10/+0x12, label +0x16, enabled +0x1A */
void ui_region2(int slot, const UiBtn *b, uint16_t press, uint16_t click, int add)
{
    if (slot < 0 || slot >= UI_REGIONS) return;
    int off = REG_OFF + slot * 0x1C;
    uint16_t goff = (uint16_t)(blk_off + off);
    if (add) {
        uint8_t *m = blk(off);
        memset(m, 0, 0x1C);
        uint16_t f[14] = { 0, (uint16_t)b->x, (uint16_t)b->y, (uint16_t)b->w, (uint16_t)b->h, 5, 0, 0,
                           press, click, 0, b->label, blk_seg, (uint16_t)b->enabled };
        for (int k = 0; k < 14; k++) { m[2 * k] = (uint8_t)f[k]; m[2 * k + 1] = (uint8_t)(f[k] >> 8); }
        C2(R07AB, 0x0294, goff, blk_seg);
    } else {
        C2(R07AB, 0x0314, goff, blk_seg);
    }
}

void ui_region(int slot, const UiBtn *b, int add) { ui_region2(slot, b, b->hot, b->hot, add); }

int ui_region_listed(int slot)
{
    uint16_t goff = (uint16_t)(blk_off + REG_OFF + slot * 0x1C);
    uint16_t off = api->ds_read16(0x0A32), seg = api->ds_read16(0x0A34);
    for (int guard = 0; (off || seg) && guard < 512; guard++) {
        if (off == goff && seg == blk_seg) return 1;
        const uint8_t *m = api->mem() + (uint32_t)seg * 16u + off;
        off = (uint16_t)(m[0x0C] | m[0x0D] << 8);
        seg = (uint16_t)(m[0x0E] | m[0x0F] << 8);
    }
    return 0;
}

void ui_begin(UiSession *s)
{
    s->font = (int16_t)C1(O0E13, 0x0256, 0);
    s->s21a = api->ds_read16(0x21A); s->s21c = api->ds_read16(0x21C);
    s->s96 = api->ds_read16(0x96);
    api->ds_write16(0x96, 0);
    s->cursor = (int16_t)C1(R07AB, 0x07DD, 1);
    s->s865a = api->ds_read16(0x865A); s->s865c = api->ds_read16(0x865C);
    api->ds_write16(0x865A, 0); api->ds_write16(0x865C, 0);
    C0(R0CDD, 0x05DB);
    C0(R07AB, 0x04BD);                               /* a fresh mouse-region list */
}

void ui_end(const UiSession *s)
{
    api->ds_write16(0x865A, s->s865a); api->ds_write16(0x865C, s->s865c);
    api->ds_write16(0x96, s->s96);
    C0(R0CDD, 0x05E5);
    C0(R07AB, 0x04E8);                               /* back to the screen's own regions */
    ui_flush_input();
    C1(R07AB, 0x07DD, s->cursor);
    C1(O0E13, 0x0256, s->font);
    api->ds_write16(0x21A, s->s21a); api->ds_write16(0x21C, s->s21c);
}

void ui_box(int x, int y, int w, int h)
{
    ui_color(0xF);
    ui_fill(x, y, w, h);
    ui_color(0);
    ui_frame(x + 3, y + 3, w - 8, h - 8);
    ui_frame(x + 2, y + 2, w - 6, h - 6);
    ui_color(7);
    for (int i = 0; i < 2; i++) {
        ui_vline(x + 2 + w - 6 + i, y + 4, h - 6);
        ui_hline(x + 4, y + 2 + h - 6 + i, w - 6);
    }
    ui_color(0);
    ui_text_color(0);
}

/* the loader's scratch block (DS:6574, 0xFFFF bytes from R0536_0007) + buf, as a normalised far pointer so
 * a save never wraps around its segment; R0399_0194 writes w, h, then w*h pixels */
static uint32_t scratch_at(unsigned buf)
{
    uint32_t a = (uint32_t)api->ds_read16(0x6576) * 16u + api->ds_read16(0x6574) + buf;
    return (a >> 4) << 16 | (a & 0xF);
}

int ui_save(int x, int y, int w, int h, unsigned buf)
{
    if (buf + 4u + (unsigned)(w * h) > 0xFFF0u) { api->log("ap: ui: no room to save %dx%d at %u", w, h, buf); return 0; }
    uint32_t p = scratch_at(buf);
    C6(R0399, 0x0194, x, y, w, h, (uint16_t)p, (uint16_t)(p >> 16));
    return 1;
}

void ui_restore(int x, int y, unsigned buf)
{
    uint32_t p = scratch_at(buf);
    C4(R0399, 0x01F1, x, y, (uint16_t)p, (uint16_t)(p >> 16));
}

/* Strings use the last string slots, so a caller's labels in the lower slots stay valid. */
void ui_message(const char *title, const char *const *lines, int n, unsigned buf)
{
    int keep = buf != UI_NO_SAVE;
    buf = (buf + 15u) & ~15u;
    if (n > 6) n = 6;
    int base = UI_STR_SLOTS - 8;
    uint16_t t = ui_str(base, title), l[6];
    int W = ui_text_w(t), fh = ui_text_h(t);
    for (int i = 0; i < n; i++) {
        l[i] = ui_str(base + 1 + i, lines[i]);
        int w = ui_text_w(l[i]);
        if (w > W) W = w;
    }
    UiBtn ok = { 0, 0, 0, 0, 1, ui_str(base + 7, "OK"), 0x0D };
    W += 28;
    if (W > 280) W = 280;
    int H = 12 + fh + 6 + n * (fh + 2) + 8 + fh + 4 + 12;
    int X = (320 - W) / 2, Y = (166 - H) / 2;
    ok.w = ui_text_w(ok.label) + 0x14 + 16; ok.h = fh + 4;
    ok.x = X + (W - ok.w) / 2; ok.y = Y + H - 12 - ok.h;

    if (keep && !ui_save(X, Y, W, H, buf)) return;
    ui_box(X, Y, W, H);
    ui_text(X + (W - ui_text_w(t)) / 2, Y + 9, t);
    for (int i = 0; i < n; i++) ui_text(X + (W - ui_text_w(l[i])) / 2, Y + 12 + fh + 6 + i * (fh + 2), l[i]);
    ui_button(&ok);
    ui_region(UI_REGIONS - 1, &ok, 1);
    ui_focus(&ok);
    ui_flush_input();
    for (;;) {
        int k = ui_key();
        if (k == 0x0D || k == 0x1B || k == ' ') break;
    }
    ui_press(&ok);
    ui_region(UI_REGIONS - 1, &ok, 0);
    if (keep) ui_restore(X, Y, buf);
    ui_flush();
    ui_flush_input();
}
