/* picker.h - the level picker dialog (picker.c). */
#ifndef AP_PICKER_H
#define AP_PICKER_H
#include "gg_mod.h"
void picker_init(const GGApi *api);
/* Show the picker for `building`; open[l] = level l can be chosen; focus_level = button focused first.
 * Returns the chosen level (0-4), -1 for "Return to Technology Center", -2 on failure.
 * Call only from an event or site callback (it runs the game's own routines). */
int picker_run(int building, const int open[5], int focus_level);
#endif
