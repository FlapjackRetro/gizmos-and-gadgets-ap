/*
 * picker.c - the level picker: a dialog in the game's own style (ui.c), one button per level plus
 * "Return to Technology Center". Keys: the game's Enter, Esc, Left, Right, plus Up and Down.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gg_mod.h"
#include "picker.h"
#include "ui.h"

static const GGApi *api;

#define R04FF 0x04FF
#define R0536 0x0536
#define C1(s, o, a)    ui_call(s, o, 1, (uint16_t)(a), 0, 0, 0, 0, 0)
#define C2(s, o, a, b) ui_call(s, o, 2, (uint16_t)(a), (uint16_t)(b), 0, 0, 0, 0)
#define NBTN 6

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

/* ---- the dialog ----------------------------------------------------------------------------- */
int picker_run(int building, const int open[5], int focus_level)
{
    static const char *bname[3] = { "Automotive Building", "Aircraft Building", "Alternative Energy Building" };
    if (!ui_ready()) { api->log("ap: picker: no memory"); return -2; }
    load_names();

    UiSession ses;
    ui_begin(&ses);

    /* strings and layout */
    char buf[64];
    UiBtn b[NBTN];
    int maxw = 0;
    uint16_t title = ui_str(0, bname[building]);
    for (int i = 0; i < NBTN; i++) {
        if (i < 5) snprintf(buf, sizeof buf, "Level %d - %s", i + 1, names[building * 5 + i]);
        else snprintf(buf, sizeof buf, "Return to Technology Center");
        b[i].label = ui_str(1 + i, buf);
        b[i].enabled = i < 5 ? open[i] : 1;
        b[i].hot = (uint16_t)(i < 5 ? '1' + i : 'r');
        int w = ui_text_w(b[i].label);
        if (w > maxw) maxw = w;
    }
    int fh = ui_text_h(b[0].label);
    int bw = maxw + 0x14, bh = fh + 4, gap = 4;
    int tw = ui_text_w(title);
    int W = (bw > tw ? bw : tw) + 28;
    int H = 14 + fh + 8 + NBTN * (bh + gap) + 4 + 10;
    int X = (320 - W) / 2, Y = (200 - H) / 2;
    for (int i = 0; i < NBTN; i++) {
        b[i].w = bw; b[i].h = bh;
        b[i].x = X + (W - bw) / 2;
        b[i].y = Y + 14 + fh + 8 + i * (bh + gap) + (i == 5 ? 4 : 0);
    }

    ui_save(X, Y, W, H, 0);                     /* O0E83_033E: into the loader's scratch block */
    ui_box(X, Y, W, H);
    ui_text(X + (W - tw) / 2, Y + 9, title);
    for (int i = 0; i < NBTN; i++) ui_button(&b[i]);
    for (int i = 0; i < NBTN; i++) ui_region(i, &b[i], 1);

    int cur = focus_level >= 0 && focus_level < 5 && b[focus_level].enabled ? focus_level : -1;
    for (int i = 0; cur < 0 && i < NBTN; i++) if (b[i].enabled) cur = i;
    ui_focus(&b[cur]);
    ui_flush_input();

    /* input loop (O0E83_0655) */
    int result = -2;
    while (result == -2) {
        int k = ui_key();
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
                    if (b[i].enabled && i != cur) { ui_focus(&b[cur]); cur = i; ui_focus(&b[cur]); }
                } else if (key == b[i].hot) {
                    pick = i;
                }
            }
        }
        if (dir) {
            int n = cur;
            do { n = (n + dir + NBTN) % NBTN; } while (!b[n].enabled);
            if (n != cur) { ui_focus(&b[cur]); cur = n; ui_focus(&b[cur]); }
        }
        if (pick >= 0 && b[pick].enabled) {
            if (pick != cur) { ui_focus(&b[cur]); cur = pick; ui_focus(&b[cur]); }
            ui_press(&b[cur]);
            result = cur < 5 ? cur : -1;
        }
    }

    /* tear down (O0E83_0655 exit) */
    ui_restore(X, Y, 0);
    ui_flush();
    for (int i = 0; i < NBTN; i++) ui_region(i, &b[i], 0);
    ui_end(&ses);
    return result;
}

void picker_init(const GGApi *a) { api = a; }
