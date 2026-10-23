/* ui.h - dialogs in the game's own style, drawn with the game's own routines (RE_NOTES §4.12).
 * Shared by the level picker (picker.c) and the Archipelago inventory (inventory.c). */
#ifndef AP_UI_H
#define AP_UI_H
#include <stdint.h>
#include "gg_mod.h"

void ui_init(const GGApi *api);

/* call a resident or flattened-overlay routine */
uint32_t ui_call(uint16_t seg, uint16_t off, int n, uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3,
                 uint16_t a4, uint16_t a5);

/* guest memory of our own (one block from the game's allocator): strings and mouse-region records */
int ui_ready(void);                         /* allocates on first use; 0 = no memory */
uint16_t ui_seg(void);                      /* segment of the block */
uint16_t ui_str(int slot, const char *s);   /* string slot 0..UI_STR_SLOTS-1 (UI_STR_LEN bytes each) */
uint32_t ui_scratch(void);                  /* 16 bytes of guest scratch (far pointer seg:off) */
#define UI_STR_SLOTS 40
#define UI_STR_LEN   64

/* drawing */
void ui_color(int c);
void ui_text_color(int c);                  /* O0E13_0243 */
void ui_fill(int x, int y, int w, int h);
void ui_hline(int x, int y, int len);
void ui_vline(int x, int y, int len);
void ui_frame(int x, int y, int w, int h);
void ui_xor(int x, int y, int w, int h);
void ui_flush(void);
int  ui_text_w(uint16_t s);
int  ui_text_h(uint16_t s);
void ui_text(int x, int y, uint16_t s);
void ui_wait(int ticks);

/* buttons */
typedef struct { int x, y, w, h, enabled; uint16_t label; uint16_t hot; } UiBtn;
void ui_button(const UiBtn *b);             /* O0E88_0000 */
void ui_focus(const UiBtn *b);              /* O0E88_016B: XOR, a second call removes it */
void ui_press(const UiBtn *b);              /* O0E88_01D6 */
/* mouse region `slot` (0..UI_REGIONS-1) for a button: clicks report b->hot, | 0x4000 button down over
 * it, | 0x8000 hovering (R098D_0106) */
#define UI_REGIONS 16
void ui_region(int slot, const UiBtn *b, int add);
/* the same, with separate codes: `press` for hover / button down (0 = report nothing), `click` for a
 * finished click */
void ui_region2(int slot, const UiBtn *b, uint16_t press, uint16_t click, int add);
/* is region `slot` in the current list (DS:A32, next at +0x0C)? */
int ui_region_listed(int slot);

/* a modal session: the runner's set-up and tear-down (O0E83_0655). Regions added in between belong to
 * a fresh list (R07AB_04BD), so the screen below does not react. */
typedef struct { int font; uint16_t s21a, s21c, s96, s865a, s865c; int cursor; } UiSession;
void ui_begin(UiSession *s);
void ui_end(const UiSession *s);

/* the dialog box (O0E83_0389) and saving what is under it (R0399_0194 / 01F1). `buf` = byte offset into
 * the loader's 64 KB scratch block (DS:6574), so nested boxes can use different parts of it. */
void ui_box(int x, int y, int w, int h);
int  ui_save(int x, int y, int w, int h, unsigned buf);   /* 0 = does not fit */
void ui_restore(int x, int y, unsigned buf);

int ui_key(void);                           /* R098D_0106: next event, 0 = none */
void ui_flush_input(void);                  /* R098D_00BF */

/* a message box with lines of text and one OK button (at most 280 wide, centred above the HUD); returns when
 * it is dismissed. buf: where to save the screen under it, or UI_NO_SAVE when the caller redraws instead */
#define UI_NO_SAVE 0xFFFFFFFFu
void ui_message(const char *title, const char *const *lines, int n, unsigned buf);

#endif
