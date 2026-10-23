/* inventory.h - the Archipelago inventory dialog (inventory.c). */
#ifndef AP_INVENTORY_H
#define AP_INVENTORY_H
#include "gg_mod.h"

typedef struct { int rx; int v; int part; } InvItem;   /* received.txt line, level, part */

void inventory_init(const GGApi *api);

/* Called at the assembly-room loop head (site assembly_tick) with the loop's registers. If the Pieces box
 * was clicked, runs the dialog: parts of level `cur_v` can be moved onto the conveyor; deposit(rx) is called
 * for each one moved. Returns how many were moved (the caller saves). If the Cyberchimp was clicked, shows
 * how many checks (`chimp_checks`) and own parts this level's chimps carry. */
int inventory_site(GGRegs *r, InvItem *items, int n, int cur_v, int chimp_checks, void (*deposit)(int rx));

#endif
