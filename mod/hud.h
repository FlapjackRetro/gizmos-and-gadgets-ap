/* hud.h - Archipelago sprites on the assembly room HUD (hud.c). */
#ifndef AP_HUD_H
#define AP_HUD_H
#include "gg_mod.h"
void hud_init(const GGApi *api);
/* Each assembly-room pass: (re)draw the Pieces box with `count` when it is missing or the number changed. */
void hud_tick(int count);
/* The screen was replaced (e.g. a new room): draw again next time. */
void hud_forget(void);
/* Where the Pieces box is on screen; 0 if it is not drawn (not built). */
int hud_box_rect(int *x, int *y, int *w, int *h);
#endif
