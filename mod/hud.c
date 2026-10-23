/*
 * hud.c - Archipelago sprites in the unused bottom-left of the assembly room HUD (x 0..217, y 167..199).
 *
 * The "Pieces" box (the future Archipelago inventory, docs/AP_DESIGN.md) is built at run time from the
 * player's own game files -- no game art ships with the mod:
 *   - background 0x2084 (door puzzle "Put the puzzle pieces together to make a machine", raw 320x155 LPX):
 *     the box is the 66x36 rectangle at (15, 94); indices 198 (green surround) and 211 (shadow) are
 *     transparent; rows 12..14 (plain texture above "Pieces") are dropped so it fits the HUD's 33 rows;
 *   - its colours are the puzzle palette's, so each index is remapped to the assembly room palette (the table
 *     below: nearest identical colour for the greys, the grey of the same brightness for the browns; the
 *     target indices have the same colours in all three buildings' assembly rooms);
 *   - the counter uses the puzzle's own digit sprites (0x204F, ten LPX images, colours 30 and 31 = the same in
 *     the assembly room), centred where the puzzle draws its count.
 * Drawing: an RLE LPX built in a block from the game's allocator, drawn with R0399_02EE(0, off, seg, y, x)
 * (the title screen's sprite routine). The HUD is redrawn by the game at times, so each pass checks a few
 * pixels of video memory (A000:0000) and draws again when they are gone or the number changed.
 */
#include <string.h>
#include "gg_mod.h"
#include "hud.h"

static const GGApi *api;

#define BOX_SRC_X 15
#define BOX_SRC_Y 94
#define BOX_W 66
#define BOX_SRC_H 36
#define BOX_H 33                    /* after dropping rows 12..14 */
#define BOX_X 4
#define BOX_Y 167
#define COUNTER_CX 25.0             /* in the (shortened) sprite */
#define COUNTER_CY 23.5

static const uint8_t remap_from[] = { 7, 8, 29, 30, 112, 113, 114, 115, 116, 117, 118, 119, 120, 123, 124, 125,
                                      174, 180, 188, 189, 191, 192, 193, 194, 196, 199, 200, 201, 202, 204, 205,
                                      208, 209, 212 };
static const uint8_t remap_to[] = { 7, 211, 209, 30, 15, 224, 204, 226, 28, 7, 207, 208, 209, 212, 214, 238,
                                    211, 204, 208, 209, 209, 210, 210, 210, 211, 30, 30, 214, 214, 214, 214,
                                    214, 0, 214 };

static uint16_t box_off, box_seg;           /* our LPX in guest memory */
static uint16_t dig_off[10], dig_seg;       /* digit LPX images inside the loaded 0x204F container */
static uint8_t dig_w[10];
static int ready = -1;                      /* -1 not tried, 0 failed, 1 built */
static uint8_t probe_val[4];
static int probe_x[4], probe_y[4];
static int shown = -1;                      /* number on screen */

static uint32_t call(uint16_t s, uint16_t o, int n, uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4)
{
    uint16_t a[5] = { a0, a1, a2, a3, a4 };
    return api->call_far(s, o, n, a);
}

static uint32_t lin(uint32_t farp) { return (farp >> 16) * 16u + (farp & 0xFFFF); }

static int build(void)
{
    uint8_t *m = api->mem();
    /* the background, through the game's loader (a block from R0536_0007; freed below) */
    uint32_t bg = call(0x04FF, 0x00FE, 1, 0x2084, 0, 0, 0, 0);
    if (!bg) return 0;
    const uint8_t *src = m + lin(bg);
    if (memcmp(src, "LPX\x1a", 4) != 0 || (src[4] | src[5] << 8) != 0 || (src[6] | src[7] << 8) != 320) {
        call(0x0536, 0x0047, 2, (uint16_t)bg, (uint16_t)(bg >> 16), 0, 0, 0);
        api->log("ap: hud: unexpected background 0x2084");
        return 0;
    }
    const uint8_t *pix = src + 12 + (src[12] | src[13] << 8);
    uint8_t box[BOX_H][BOX_W];              /* 0 = transparent marker below; real index 0 does not occur */
    int filled[BOX_H][BOX_W];
    for (int y = 0, sy = 0; sy < BOX_SRC_H; sy++) {
        if (sy >= 12 && sy <= 14) continue;
        for (int x = 0; x < BOX_W; x++) {
            uint8_t c = pix[(BOX_SRC_Y + sy) * 320 + BOX_SRC_X + x];
            int keep = c != 198 && c != 211;
            uint8_t t = c;
            if (keep) {
                size_t k = 0;
                while (k < sizeof remap_from && remap_from[k] != c) k++;
                if (k < sizeof remap_from) t = remap_to[k];
                else keep = 0;                     /* a colour we did not plan for: leave it out */
            }
            box[y][x] = t; filled[y][x] = keep;
        }
        y++;
    }
    call(0x0536, 0x0047, 2, (uint16_t)bg, (uint16_t)(bg >> 16), 0, 0, 0);

    /* RLE LPX (R0399_0465): per row, n>0 = n literal pixels, 0 k = skip k */
    uint8_t out[16 + BOX_H * (BOX_W * 2 + 8)];
    size_t n = 0;
    memcpy(out, "LPX\x1a", 4); n = 4;
    uint16_t hdr[5] = { 1, BOX_W, BOX_H, 1, 2 };
    for (int i = 0; i < 5; i++) { out[n++] = (uint8_t)hdr[i]; out[n++] = (uint8_t)(hdr[i] >> 8); }
    for (int y = 0; y < BOX_H; y++) {
        int x = 0;
        while (x < BOX_W) {
            int x2 = x;
            if (!filled[y][x]) {
                while (x2 < BOX_W && !filled[y][x2]) x2++;
                out[n++] = 0; out[n++] = (uint8_t)(x2 - x);
            } else {
                while (x2 < BOX_W && filled[y][x2]) x2++;
                out[n++] = (uint8_t)(x2 - x);
                for (int i = x; i < x2; i++) out[n++] = box[y][i];
            }
            x = x2;
        }
    }
    uint32_t blk = call(0x0536, 0x0007, 2, (uint16_t)n, 0, 0, 0, 0);
    if (!blk) return 0;
    memcpy(m + lin(blk), out, n);
    box_off = (uint16_t)blk; box_seg = (uint16_t)(blk >> 16);

    /* probes: opaque pixels spread over the sprite */
    static const int px[4] = { 10, 30, 50, 20 }, py[4] = { 5, 12, 20, 28 };
    for (int i = 0; i < 4; i++) {
        int x = px[i], y = py[i];
        while (!filled[y][x] && x < BOX_W - 1) x++;
        probe_x[i] = BOX_X + x; probe_y[i] = BOX_Y + y; probe_val[i] = box[y][x];
    }

    /* the digits: keep the container loaded (it is small) */
    uint32_t dg = call(0x04FF, 0x00FE, 1, 0x204F, 0, 0, 0, 0);
    if (!dg) return 0;
    const uint8_t *d = m + lin(dg);
    int cnt = (d[0] | d[1] << 8) / 2;
    if (cnt < 11) return 0;
    dig_seg = (uint16_t)(dg >> 16);
    for (int k = 0; k < 10; k++) {
        int o = d[2 * k] | d[2 * k + 1] << 8;
        if (d[o] != 'G' || memcmp(d + o + 2, "LPX\x1a", 4) != 0) return 0;
        dig_off[k] = (uint16_t)((uint16_t)dg + o + 2);
        dig_w[k] = d[o + 2 + 6];
    }
    return 1;
}

static void draw(int count)
{
    call(0x0399, 0x02EE, 5, 0, box_off, box_seg, BOX_Y, BOX_X);
    char s[8];
    int len = 0;
    if (count < 0) count = 0;
    if (count > 99) count = 99;
    if (count >= 10) s[len++] = (char)('0' + count / 10);
    s[len++] = (char)('0' + count % 10);
    int total = -1;
    for (int i = 0; i < len; i++) total += dig_w[s[i] - '0'] + 1;
    int x = BOX_X + (int)(COUNTER_CX - total / 2.0 + 0.5);
    int y = BOX_Y + (int)(COUNTER_CY - 2.5 + 0.5);            /* digits are 5 rows */
    for (int i = 0; i < len; i++) {
        int k = s[i] - '0';
        call(0x0399, 0x02EE, 5, 0, dig_off[k], dig_seg, (uint16_t)y, (uint16_t)x);
        x += dig_w[k] + 1;
    }
    call(0x0581, 0x052A, 0, 0, 0, 0, 0, 0);                   /* flush, as the game does after drawing */
    shown = count;
}

void hud_init(const GGApi *a) { api = a; }

void hud_tick(int count)
{
    if (ready < 0) {
        ready = build();
        if (!ready) api->log("ap: hud: Pieces box not available");
    }
    if (!ready) return;
    const uint8_t *vga = api->mem() + 0xA0000;
    int missing = 0;
    for (int i = 0; i < 4; i++) missing += vga[probe_y[i] * 320 + probe_x[i]] != probe_val[i];
    if (missing >= 3 || count != shown) draw(count);
}

void hud_forget(void) { shown = -1; }
