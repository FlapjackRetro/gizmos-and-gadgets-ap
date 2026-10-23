/*
 * hud.c - Archipelago sprites in the unused bottom-left of the assembly room HUD (x 0..217, y 167..199).
 *
 * Both sprites are built at run time from the player's own game files -- no game art ships with the mod:
 *   - "Pieces" box (the Archipelago inventory, docs/AP_DESIGN.md): background 0x2084 (door puzzle "Put the
 *     puzzle pieces together to make a machine", raw 320x155 LPX), the 66x36 rectangle at (15, 94); indices
 *     198 (green surround) and 211 (shadow) are transparent; rows 12..14 (plain texture above "Pieces") are
 *     dropped so it fits the HUD's 33 rows. Counter: the puzzle's own digit sprites (0x204F), centred where
 *     the puzzle draws its count.
 *   - Cyberchimp (checks held by chimps): container 0x103B, frame 1 (31x31, facing front).
 * Their colours come from other palettes (the puzzle's 0x2085, the warehouse's 0x100A), so each index is
 * remapped to an assembly room index (tables below). Every target index has the same colour in the assembly
 * rooms of all three buildings. Box: greys exact, browns to the grey of the same brightness. Chimp: nearest
 * colour keeping the hue.
 * Drawing: an RLE LPX built in a block from the game's allocator, drawn with R0399_02EE(0, off, seg, y, x)
 * (the title screen's sprite routine). The game repaints the HUD at times, so each pass checks a few pixels
 * of video memory (A000:0000) and draws again when they are gone or the number changed.
 */
#include <string.h>
#include "gg_mod.h"
#include "hud.h"

static const GGApi *api;

#define MAXW 80
#define MAXH 40

typedef struct {
    uint16_t off, seg;              /* LPX in guest memory */
    int x, y, w, h;                 /* where on screen */
    int probe_x[4], probe_y[4];
    uint8_t probe_val[4];
} Sprite;

static Sprite box, chimp;
static uint16_t dig_off[10], dig_seg;       /* digit LPX images inside the loaded 0x204F container */
static uint8_t dig_w[10];
static int ready = -1;                      /* -1 not tried, 0 failed, 1 built */
static int shown = -1;                      /* number on the box */

static const uint8_t box_from[] = { 7, 8, 29, 30, 112, 113, 114, 115, 116, 117, 118, 119, 120, 123, 124, 125,
                                    174, 180, 188, 189, 191, 192, 193, 194, 196, 199, 200, 201, 202, 204, 205,
                                    208, 209, 212 };
static const uint8_t box_to[] = { 7, 211, 209, 30, 15, 224, 204, 226, 28, 7, 207, 208, 209, 212, 214, 238,
                                  211, 204, 208, 209, 209, 210, 210, 210, 211, 30, 30, 214, 214, 214, 214,
                                  214, 0, 214 };
static const uint8_t chimp_from[] = { 64, 66, 68, 69, 71, 72, 76, 77, 78, 79, 101, 102, 103, 104, 105, 106, 107,
                                      108, 109, 110, 111, 112, 113, 114, 115, 117, 118 };
static const uint8_t chimp_to[] = { 0, 204, 195, 216, 7, 211, 199, 198, 14, 15, 200, 193, 200, 194, 244, 14, 195,
                                    194, 26, 216, 216, 195, 246, 210, 212, 199, 195 };

static uint32_t call(uint16_t s, uint16_t o, int n, uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4)
{
    uint16_t a[5] = { a0, a1, a2, a3, a4 };
    return api->call_far(s, o, n, a);
}

static uint32_t lin(uint32_t farp) { return (farp >> 16) * 16u + (farp & 0xFFFF); }
static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* LPX -> index + mask arrays (format 0 raw, 1 RLE: n>0 = n literal pixels, 0 k = skip k) */
static int lpx_decode(const uint8_t *p, int *w, int *h, uint8_t px[MAXH][MAXW], int fill[MAXH][MAXW],
                      int max_w, int max_h)
{
    if (memcmp(p, "LPX\x1a", 4) != 0) return 0;
    int fmt = rd16(p + 4), W = rd16(p + 6), H = rd16(p + 8);
    if (W > max_w || H > max_h) return 0;
    const uint8_t *d = p + 12 + rd16(p + 12);
    for (int y = 0; y < H; y++) {
        int x = 0;
        if (fmt == 0) {
            for (; x < W; x++) { px[y][x] = *d++; fill[y][x] = 1; }
            continue;
        }
        while (x < W) {
            int n = *d++;
            if (n) { for (int i = 0; i < n && x < W; i++, x++) { px[y][x] = *d++; fill[y][x] = 1; } }
            else { int k = *d++; for (int i = 0; i < k && x < W; i++, x++) fill[y][x] = 0; }
        }
    }
    *w = W; *h = H;
    return 1;
}

static int remap(uint8_t c, const uint8_t *from, const uint8_t *to, size_t n, uint8_t *out)
{
    for (size_t k = 0; k < n; k++) if (from[k] == c) { *out = to[k]; return 1; }
    return 0;
}

/* index + mask -> RLE LPX in a new guest block; fills s->off/seg/w/h and the probes */
static int lpx_build(Sprite *s, int w, int h, uint8_t px[MAXH][MAXW], int fill[MAXH][MAXW])
{
    uint8_t out[16 + MAXH * (MAXW * 2 + 8)];
    size_t n = 0;
    memcpy(out, "LPX\x1a", 4); n = 4;
    uint16_t hdr[5] = { 1, (uint16_t)w, (uint16_t)h, 1, 2 };
    for (int i = 0; i < 5; i++) { out[n++] = (uint8_t)hdr[i]; out[n++] = (uint8_t)(hdr[i] >> 8); }
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < w) {
            int x2 = x;
            if (!fill[y][x]) {
                while (x2 < w && !fill[y][x2]) x2++;
                out[n++] = 0; out[n++] = (uint8_t)(x2 - x);
            } else {
                while (x2 < w && fill[y][x2]) x2++;
                out[n++] = (uint8_t)(x2 - x);
                for (int i = x; i < x2; i++) out[n++] = px[y][i];
            }
            x = x2;
        }
    }
    uint32_t blk = call(0x0536, 0x0007, 2, (uint16_t)n, 0, 0, 0, 0);
    if (!blk) return 0;
    memcpy(api->mem() + lin(blk), out, n);
    s->off = (uint16_t)blk; s->seg = (uint16_t)(blk >> 16); s->w = w; s->h = h;
    for (int i = 0; i < 4; i++) {                         /* opaque pixels spread over the sprite */
        int x = w * (1 + 2 * i) / 8, y = h * (1 + (i * 3) % 4) / 5;
        while (!fill[y][x] && x < w - 1) x++;
        s->probe_x[i] = s->x + x; s->probe_y[i] = s->y + y; s->probe_val[i] = px[y][x];
    }
    return 1;
}

static int build_box(void)
{
    static uint8_t px[MAXH][MAXW];
    static int fill[MAXH][MAXW];
    uint32_t bg = call(0x04FF, 0x00FE, 1, 0x2084, 0, 0, 0, 0);   /* the game's loader; freed below */
    if (!bg) return 0;
    const uint8_t *src = api->mem() + lin(bg);
    int ok = memcmp(src, "LPX\x1a", 4) == 0 && rd16(src + 4) == 0 && rd16(src + 6) == 320;
    if (ok) {
        const uint8_t *pix = src + 12 + rd16(src + 12);
        for (int y = 0, sy = 0; sy < 36; sy++) {
            if (sy >= 12 && sy <= 14) continue;
            for (int x = 0; x < 66; x++) {
                uint8_t c = pix[(94 + sy) * 320 + 15 + x];
                fill[y][x] = c != 198 && c != 211 && remap(c, box_from, box_to, sizeof box_from, &px[y][x]);
            }
            y++;
        }
    }
    call(0x0536, 0x0047, 2, (uint16_t)bg, (uint16_t)(bg >> 16), 0, 0, 0);
    if (!ok) { api->log("ap: hud: unexpected background 0x2084"); return 0; }
    box.x = 4; box.y = 167;
    return lpx_build(&box, 66, 33, px, fill);
}

static int build_chimp(void)
{
    static uint8_t px[MAXH][MAXW];
    static int fill[MAXH][MAXW];
    uint32_t c = call(0x04FF, 0x00FE, 1, 0x103B, 0, 0, 0, 0);
    if (!c) return 0;
    const uint8_t *d = api->mem() + lin(c);
    int w = 0, h = 0, ok = rd16(d) / 2 > 2;
    if (ok) {
        int o = rd16(d + 2);                                      /* element 1 */
        ok = d[o] == 'G' && lpx_decode(d + o + 2, &w, &h, px, fill, MAXW, 33);
    }
    call(0x0536, 0x0047, 2, (uint16_t)c, (uint16_t)(c >> 16), 0, 0, 0);
    if (!ok) { api->log("ap: hud: unexpected chimp sprites 0x103B"); return 0; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (fill[y][x]) fill[y][x] = remap(px[y][x], chimp_from, chimp_to, sizeof chimp_from, &px[y][x]);
    chimp.x = 76; chimp.y = 200 - h - 1;
    return lpx_build(&chimp, w, h, px, fill);
}

static int build_digits(void)
{
    uint32_t dg = call(0x04FF, 0x00FE, 1, 0x204F, 0, 0, 0, 0);    /* small: kept loaded */
    if (!dg) return 0;
    const uint8_t *d = api->mem() + lin(dg);
    if (rd16(d) / 2 < 11) return 0;
    dig_seg = (uint16_t)(dg >> 16);
    for (int k = 0; k < 10; k++) {
        int o = rd16(d + 2 * k);
        if (d[o] != 'G' || memcmp(d + o + 2, "LPX\x1a", 4) != 0) return 0;
        dig_off[k] = (uint16_t)((uint16_t)dg + o + 2);
        dig_w[k] = d[o + 2 + 6];
    }
    return 1;
}

static void draw_sprite(const Sprite *s) { call(0x0399, 0x02EE, 5, 0, s->off, s->seg, (uint16_t)s->y, (uint16_t)s->x); }

static int missing(const Sprite *s)
{
    const uint8_t *vga = api->mem() + 0xA0000;
    int n = 0;
    for (int i = 0; i < 4; i++) n += vga[s->probe_y[i] * 320 + s->probe_x[i]] != s->probe_val[i];
    return n >= 3;
}

static void draw_box(int count)
{
    draw_sprite(&box);
    char s[4];
    int len = 0;
    if (count < 0) count = 0;
    if (count > 99) count = 99;
    if (count >= 10) s[len++] = (char)('0' + count / 10);
    s[len++] = (char)('0' + count % 10);
    int total = -1;
    for (int i = 0; i < len; i++) total += dig_w[s[i] - '0'] + 1;
    int x = box.x + (int)(25.0 - total / 2.0 + 0.5);              /* counter centre (25, 23.5) */
    int y = box.y + 21;                                           /* 23.5 - 2.5: digits are 5 rows */
    for (int i = 0; i < len; i++) {
        int k = s[i] - '0';
        call(0x0399, 0x02EE, 5, 0, dig_off[k], dig_seg, (uint16_t)y, (uint16_t)x);
        x += dig_w[k] + 1;
    }
    shown = count;
}

void hud_init(const GGApi *a) { api = a; }

void hud_tick(int count)
{
    if (ready < 0) {
        ready = build_box() && build_digits() && build_chimp();
        if (!ready) api->log("ap: hud: sprites not available");
    }
    if (!ready) return;
    int drew = 0;
    if (missing(&box) || count != shown) { draw_box(count); drew = 1; }
    if (missing(&chimp)) { draw_sprite(&chimp); drew = 1; }
    if (drew) call(0x0581, 0x052A, 0, 0, 0, 0, 0, 0);             /* flush, as the game does after drawing */
}

void hud_forget(void) { shown = -1; }

int hud_box_rect(int *x, int *y, int *w, int *h)
{
    if (ready != 1) return 0;
    *x = box.x; *y = box.y; *w = box.w; *h = box.h;
    return 1;
}
