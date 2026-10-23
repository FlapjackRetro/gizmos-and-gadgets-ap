/*
 * ap_mod.c - Archipelago mod for the Gizmos & Gadgets recompilation.
 *
 * Built against the recomp's include/gg_mod.h (API version 1) and loaded from <game dir>/mods/.
 * It turns the game's events into Archipelago location checks, grants received items, and
 * lifts the "one level per building" rule. It talks to the Archipelago client through plain
 * files in <game dir>/ap/ (see docs/MOD.md):
 *
 *   ap/checks.txt    (mod appends)    one location id per line; "goal" when all 15 races are won
 *   ap/received.txt  (client writes)  one item id per line, in the order the server sent them
 *   ap/config.txt    (client/user)    key=value settings, re-read on every level entry
 *   ap/state.txt     (mod)            what the mod must remember between sessions
 *
 * Ids (docs/MOD.md):  base 0x47470000 (1195835392)
 *   race won       base + 0x0000 + v                 v = building*5 + level (0..14)
 *   part box       base + 0x1000 + v*0x40 + part     the box that holds `part` in level v
 *   puzzle door    base + 0x2000 + v*0x40 + rank     rank of the door in the level (see door_rank)
 *   item: part     base + 0x1000 + v*0x40 + part     (same numbers as the box locations)
 *   item: bananas  base + 0x0F00                      +5 bananas
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gg_mod.h"
#include "picker.h"
#include "hud.h"
#include "inventory.h"
#include "ui.h"
#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define MKDIR(p) mkdir(p, 0755)
#endif

#define AP_BASE      0x47470000u
#define LOC_RACE(v)        (AP_BASE + 0x0000u + (unsigned)(v))
#define LOC_BOX(v, p)      (AP_BASE + 0x1000u + (unsigned)(v) * 0x40u + (unsigned)(p))
#define LOC_DOOR(v, r)     (AP_BASE + 0x2000u + (unsigned)(v) * 0x40u + (unsigned)(r))
#define ITEM_BANANAS       (AP_BASE + 0x0F00u)

#define MAX_RECEIVED 4096
#define MAX_DOORS    80

static const GGApi *api;

/* ---- settings (ap/config.txt) ------------------------------------------------------------ */
static struct {
    int open_world;          /* enter any building, even with all 5 races won */
    int box_checks;          /* part boxes are checks: the part goes to the multiworld */
    int door_checks;
    int title_logo;          /* use GIZMOAP1.DAT (Archipelago logo on the title screen) if present */
    int avail[3][5];         /* level open in the picker (client: avail.<b>=11000); -1 = not given */
} cfg;

/* ---- state (ap/state.txt) ---------------------------------------------------------------- */
static struct {
    int wh_level[3];         /* level whose warehouse the building's block in the player record holds, -1 none */
    int won;                 /* bit v: the race of level v was won (-1 = not known yet: take the game's counters) */
    int goal_sent;
} st;

/* checks carried by Cyberchimps (virtual: the chimp's real inventory is not touched; see on_chimp_box_open) */
#define MAX_CARRIED 256
static struct { int v, actor, part; } carried[MAX_CARRIED];
static int ncarried;

/* received items and whether each was put into the game already */
static unsigned rx_item[MAX_RECEIVED];
static unsigned char rx_done[MAX_RECEIVED];
static int rx_n;
static long rx_size;         /* bytes of received.txt already read */

static char dir_ap[1024];
static int picked[3] = { -1, -1, -1 };   /* level chosen in the picker, for the next LEVEL_ENTER */
static int frames, last_grant_frame;
static int in_level = -1;    /* vehicle index of the warehouse that is set up, -1 none */

/* ---- files -------------------------------------------------------------------------------- */
static void path(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", dir_ap, name);
}

static void send_line(const char *line)
{
    char p[1100];
    path(p, sizeof p, "checks.txt");
    FILE *f = fopen(p, "a");
    if (!f) { api->log("ap: cannot write %s", p); return; }
    fprintf(f, "%s\n", line);
    fclose(f);
}

static void send_check(unsigned id, const char *what)
{
    char s[32];
    snprintf(s, sizeof s, "%u", id);
    send_line(s);
    api->log("ap: check %u (%s)", id, what);
}

static void load_config(void)
{
    char p[1100], line[256];
    cfg.open_world = 1; cfg.box_checks = 1; cfg.door_checks = 0; cfg.title_logo = 1;
    for (int b = 0; b < 3; b++) for (int l = 0; l < 5; l++) cfg.avail[b][l] = -1;
    path(p, sizeof p, "config.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v; char bits[8];
        if (sscanf(line, " avail.%d = %5s", &v, bits) == 2 && v >= 0 && v < 3) {
            for (int l = 0; l < 5; l++) cfg.avail[v][l] = bits[l] == '1';
            continue;
        }
        if (sscanf(line, " %63[^= ] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "open_world")) cfg.open_world = v;
        else if (!strcmp(key, "box_checks")) cfg.box_checks = v;
        else if (!strcmp(key, "door_checks")) cfg.door_checks = v;
        else if (!strcmp(key, "title_logo")) cfg.title_logo = v;
    }
    fclose(f);
}

static void save_state(void)
{
    char p[1100];
    path(p, sizeof p, "state.txt");
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int b = 0; b < 3; b++) fprintf(f, "wh_level.%d=%d\n", b, st.wh_level[b]);
    fprintf(f, "won=%d\n", st.won);
    fprintf(f, "goal_sent=%d\n", st.goal_sent);
    for (int i = 0; i < rx_n; i++) if (rx_done[i]) fprintf(f, "done=%d\n", i);
    for (int i = 0; i < ncarried; i++) fprintf(f, "chimp=%d,%d,%d\n", carried[i].v, carried[i].actor, carried[i].part);
    fclose(f);
}

static void load_state(void)
{
    char p[1100], line[128];
    st.wh_level[0] = st.wh_level[1] = st.wh_level[2] = -1;
    st.won = -1;
    path(p, sizeof p, "state.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int b, v, lv;
        if (sscanf(line, "wh_level.%d=%d", &b, &v) == 2 && b >= 0 && b < 3) st.wh_level[b] = v;
        else if (sscanf(line, "goal_sent=%d", &v) == 1) st.goal_sent = v;
        else if (sscanf(line, "won=%d", &v) == 1) st.won = v;
        else if (sscanf(line, "done=%d", &v) == 1 && v >= 0 && v < MAX_RECEIVED) rx_done[v] = 1;
        else if (sscanf(line, "chimp=%d,%d,%d", &b, &v, &lv) == 3 && ncarried < MAX_CARRIED) {
            carried[ncarried].v = b; carried[ncarried].actor = v; carried[ncarried].part = lv; ncarried++;
        }
    }
    fclose(f);
}

/* New lines in received.txt (the client only ever appends). */
static void poll_received(void)
{
    char p[1100], line[64];
    path(p, sizeof p, "received.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < rx_size) { rx_size = 0; rx_n = 0; }          /* file replaced: read it again */
    if (size == rx_size) { fclose(f); return; }
    fseek(f, rx_size, SEEK_SET);
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (!len || line[len - 1] != '\n') break;            /* partial line: next time */
        rx_size += (long)len;
        unsigned id = (unsigned)strtoul(line, NULL, 10);
        if (id && rx_n < MAX_RECEIVED) rx_item[rx_n++] = id;
    }
    fclose(f);
}

/* ---- game memory -------------------------------------------------------------------------- */
static uint16_t rd16(uint32_t lin) { const uint8_t *m = api->mem(); return (uint16_t)(m[lin] | m[lin + 1] << 8); }
static uint32_t far_at(uint32_t lin) { return (uint32_t)rd16(lin + 2) * 16u + rd16(lin); }   /* off, seg */

static int cur_vehicle(void)
{
    int b = (int16_t)api->ds_read16(GG_DS_BUILDING), l = (int16_t)api->ds_read16(GG_DS_LEVEL);
    return b >= 0 && b < 3 && l >= 0 && l < 5 ? b * 5 + l : -1;
}

/* Mark every part the player holds as "obtained" with the game's own recycled bit (part +0x36 |= 0x4000).
 * Any box the game makes for such a part later -- recycling, or a chimp that took it from the player
 * (R08F5_0180 on collision) dropping it after a banana -- gets the recycled sprite, and on_box_open
 * hands it back as a vanilla pickup. The part's original box was made earlier with the normal sprite,
 * so it stays a location. The game keeps the bit when parts move between inventories (R0834_08D7) and
 * only uses it for the box sprite and to skip the +20 score on picking a part up again.
 * Part table: header far ptr DS:6638 (+2 = count), entries far ptr DS:663C, 0x8E bytes, +0x0C owner
 * (-1 in a box, 0 the player, n actor n). */
static void mark_obtained(void)
{
    uint32_t ds = api->ds_base();
    if (!rd16(ds + 0x663E)) return;                          /* no part table loaded */
    int n = (int16_t)rd16(far_at(ds + 0x6638) + 2);
    uint32_t tab = far_at(ds + 0x663C);
    uint8_t *m = api->mem();
    for (int p = 0; p < n && p < 0x40; p++) {
        uint32_t e = tab + (uint32_t)p * 0x8Eu;
        if ((int16_t)rd16(e + 0x0C) == 0 && !(m[e + 0x37] & 0x40)) m[e + 0x37] |= 0x40;
    }
}

/* A door's rank among the level's puzzle doors. Rooms: far pointers at DS:7A04 (count DS:08C4).
 * Room +0x02 object count, +0x7E far pointer to 32-byte objects. A door object has state (+0x10)
 * 7..11, link (+0x12: low nibble = room on the other side, >>6 = pair number shared by both sides)
 * and kind (+0x16: 8 = puzzle door, 4 = the door that starts open). Both sides of a door give the
 * same key (lower room, higher room, pair number); the rank is its position in the sorted keys. */
static unsigned door_key(int room, uint16_t link)
{
    int other = link & 0x0F, lo = room < other ? room : other, hi = room < other ? other : room;
    return (unsigned)lo << 16 | (unsigned)hi << 8 | (unsigned)(link >> 6);
}

static int door_rank(int room, int door)
{
    uint32_t ds = api->ds_base();
    int nrooms = (int16_t)api->ds_read16(0x08C4);
    unsigned keys[MAX_DOORS], mine = 0;
    int n = 0, found = 0;
    for (int r = 0; r < nrooms && r < 16; r++) {
        uint32_t hdr = far_at(ds + 0x7A04 + 4u * r);
        int cnt = (int16_t)rd16(hdr + 0x02);
        uint32_t obj = far_at(hdr + 0x7E);
        for (int k = 0; k < cnt; k++, obj += 32) {
            int s = (int16_t)rd16(obj + 0x10);
            if (s < 7 || s > 11 || rd16(obj + 0x16) != 8) continue;
            unsigned key = door_key(r, rd16(obj + 0x12));
            if (r == room && k == door) { mine = key; found = 1; }
            int dup = 0;
            for (int i = 0; i < n; i++) if (keys[i] == key) { dup = 1; break; }
            if (!dup && n < MAX_DOORS) keys[n++] = key;
        }
    }
    if (!found) return -1;
    int rank = 0;
    for (int i = 0; i < n; i++) if (keys[i] < mine) rank++;
    return rank;
}

/* ---- granting received items (only at safe points) ---------------------------------------- */
static int is_part_item(unsigned id, int *v, int *part)
{
    if (id < AP_BASE + 0x1000u || id >= AP_BASE + 0x1000u + 15u * 0x40u) return 0;
    unsigned x = id - AP_BASE - 0x1000u;
    *v = (int)(x / 0x40u); *part = (int)(x % 0x40u);
    return 1;
}

static void save_level(void);

/* Bananas go straight into the game, one item per call. Parts wait in the Archipelago inventory until the
 * player moves them onto the conveyor (inventory.c). */
static void grant_pending(void)
{
    for (int i = 0; i < rx_n; i++) {
        if (rx_done[i]) continue;
        unsigned id = rx_item[i];
        int iv, part;
        if (id == ITEM_BANANAS) {
            api->add_bananas(5);
            rx_done[i] = 1; save_state();
            api->log("ap: received 5 bananas");
            return;
        }
        if (is_part_item(id, &iv, &part)) continue;          /* the Archipelago inventory */
        rx_done[i] = 1;                                      /* unknown item: skip it */
        api->log("ap: unknown item %u ignored", id);
    }
}

/* Received parts not in the game yet: the Archipelago inventory. */
static int ap_inventory(InvItem *out, int max)
{
    int n = 0;
    for (int i = 0; i < rx_n && n < max; i++) {
        int iv, part;
        if (rx_done[i] || !is_part_item(rx_item[i], &iv, &part)) continue;
        out[n].rx = i; out[n].v = iv; out[n].part = part;
        n++;
    }
    return n;
}

/* ---- events ------------------------------------------------------------------------------- */
static int on_building_click(void *u, GGEvent *e)
{
    (void)u; (void)e;
    return cfg.open_world ? GG_ALLOW : GG_CONTINUE;
}

/* ---- one saved warehouse per level ------------------------------------------------------------
 * The player record keeps one warehouse per building (block b at DS:47E0 + b*0x6BA, 0x6BA bytes: the
 * 20-byte warehouse header, part states, doors, boxes, inventory; RE_NOTES §3) and the flag DS:459C+2b
 * "block holds a saved warehouse". The game writes the block when it saves (O0E18_01EA: on leaving the
 * warehouse for a race or the Technology Center, and from the menu). The mod keeps a copy per level in
 * ap/levels/<v>.bin and swaps them in and out of the block, so a level is generated once and then always
 * restored. */
#define BLOCK(b)     (0x47E0u + (unsigned)(b) * 0x6BAu)
#define BLOCK_SIZE   0x6BA
#define SAVED(b)     (0x459Cu + 2u * (unsigned)(b))

static void level_path(char *out, size_t n, int v) { snprintf(out, n, "%s/levels/%d.bin", dir_ap, v); }

static void capture(int b, int lv)
{
    char p[1100];
    if (lv < 0 || !api->ds_read16((uint16_t)SAVED(b))) return;
    snprintf(p, sizeof p, "%s/levels", dir_ap);
    MKDIR(p);
    level_path(p, sizeof p, b * 5 + lv);
    FILE *f = fopen(p, "wb");
    if (!f) { api->log("ap: cannot write %s", p); return; }
    fwrite(api->mem() + api->ds_base() + BLOCK(b), 1, BLOCK_SIZE, f);
    fclose(f);
}

static int install(int b, int lv)
{
    char p[1100];
    uint8_t buf[BLOCK_SIZE];
    level_path(p, sizeof p, b * 5 + lv);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, BLOCK_SIZE, f);
    fclose(f);
    if (n != BLOCK_SIZE) return 0;
    memcpy(api->mem() + api->ds_base() + BLOCK(b), buf, BLOCK_SIZE);
    api->ds_write16((uint16_t)SAVED(b), 1);
    return 1;
}

/* The game's own save (O0E18_01EA, flattened C0DA:01EA): warehouse into the block, record into PLAYERS.DAT. */
static void save_level(void)
{
    int b = (int16_t)api->ds_read16(GG_DS_BUILDING), lv = (int16_t)api->ds_read16(GG_DS_LEVEL);
    if (b < 0 || b > 2 || lv < 0 || lv > 4 || st.wh_level[b] != lv) return;
    api->ds_write16(0x94, 1);                                /* warehouse changed */
    api->call_far(0xC0DA, 0x01EA, 0, NULL);
    capture(b, lv);
}

static int won_mask(void)
{
    if (st.won >= 0) return st.won;
    int m = 0;                                               /* an older save: the game's in-order counters */
    for (int b = 0; b < 3; b++) {
        int last = (int16_t)api->ds_read16((uint16_t)(GG_DS_RACES_WON_IDX + 2 * b));
        for (int l = 0; l <= last && l < 5; l++) m |= 1 << (b * 5 + l);
    }
    return m;
}

static int entering = -1;      /* vehicle index being entered: saved and recorded once it is ready */

static int on_level_enter(void *u, GGEvent *e)
{
    (void)u;
    load_config();
    int b = e->building;
    if (b < 0 || b > 2) return GG_CONTINUE;
    if (picked[b] >= 0) e->level = picked[b];             /* the level picker's choice */
    picked[b] = -1;
    if (e->level > 4) e->level = 4;                          /* all 5 won: the game would load level 6 */
    capture(b, st.wh_level[b]);                              /* keep the block's level before replacing it */
    entering = b * 5 + e->level;
    if (install(b, e->level)) {
        e->restore = 1;                                      /* this level was played before: bring it back */
    } else {
        e->restore = 0;                                      /* never played: generate it (once) */
        for (int i = 0; i < rx_n; i++) {                     /* parts received for it go into the new one */
            int iv, part;
            if (rx_done[i] && is_part_item(rx_item[i], &iv, &part) && iv == entering) rx_done[i] = 0;
        }
        for (int i = 0; i < ncarried; i++)                    /* nor do its chimps carry anything yet */
            if (carried[i].v == entering) carried[i--] = carried[--ncarried];
    }
    /* st.wh_level[b] changes only once the game has saved this level into the block (on_level_ready):
     * it must always describe what PLAYERS.DAT holds, or a later capture would file one level's
     * warehouse under another's number */
    st.wh_level[b] = -1;
    in_level = -1;
    save_state();
    api->log("ap: entering building %d level %d (%s)", b, e->level + 1, e->restore ? "restored" : "new");
    return GG_CONTINUE;
}

/* Which levels the picker offers: the client's avail.<b>; without it, level 1, each level after one whose race
 * was won, and each won level. */
static void open_levels(int b, int open[5])
{
    int won = won_mask();
    for (int l = 0; l < 5; l++)
        open[l] = cfg.avail[b][l] >= 0 ? cfg.avail[b][l]
                : (l == 0 || (won >> (b * 5 + l - 1) & 1) || (won >> (b * 5 + l) & 1));
}

/* Site building_selected (R02E0_0148 at 0x463: mov [bp-6], 1 = leave the select loop and enter the
 * building, [4592] = the building). Show the picker; "Return to Technology Center" skips the instruction,
 * so the select loop carries on as if nothing was clicked. */
static int on_building_selected(void *u, uint32_t addr, GGRegs *r)
{
    (void)u; (void)addr; (void)r;
    int b = (int16_t)api->ds_read16(GG_DS_BUILDING);
    if (b < 0 || b > 2) return 0;
    load_config();
    int open[5];
    open_levels(b, open);
    int won = won_mask(), first = -1;
    for (int l = 0; l < 5 && first < 0; l++) if (open[l] && !(won >> (b * 5 + l) & 1)) first = l;
    int lv = picker_run(b, open, first);
    if (lv == -2) return 0;                                  /* picker failed: the game's own choice */
    if (lv < 0) { api->log("ap: back to the Technology Center"); return 1; }
    picked[b] = lv;
    api->log("ap: picked building %d level %d", b, lv + 1);
    return 0;
}

static int on_level_ready(void *u, GGEvent *e)
{
    (void)u; (void)e;
    in_level = cur_vehicle();
    if (entering >= 0 && entering == in_level) {
        /* save at once (the game's own save: snapshot into the block, PLAYERS.DAT), so the block and the
         * file agree with st.wh_level from here on; a new level is thereby generated exactly once */
        int b = entering / 5, lv = entering % 5;
        st.wh_level[b] = lv;
        save_level();
        save_state();
        api->log("ap: level %d saved", entering);
    }
    entering = -1;
    return GG_CONTINUE;
}

static int on_race_start(void *u, GGEvent *e)
{
    (void)u;
    /* the game saved the warehouse on the way out (R02E0 0x9F8); a won race discards the block, so copy it */
    if (e->building >= 0 && e->building < 3) capture(e->building, st.wh_level[e->building]);
    return GG_CONTINUE;
}

/* Received parts of the current level still in the Archipelago inventory: the number on the Pieces box. */
static int waiting_parts(void)
{
    int v = cur_vehicle(), n = 0;
    for (int i = 0; i < rx_n; i++) {
        int iv, part;
        if (!rx_done[i] && is_part_item(rx_item[i], &iv, &part) && iv == v) n++;
    }
    return n;
}

static int on_assembly_tick(void *u, GGEvent *e)
{
    (void)u; (void)e;
    mark_obtained();
    hud_tick(waiting_parts());
    if (frames - last_grant_frame >= 90) { last_grant_frame = frames; grant_pending(); }
    return GG_CONTINUE;
}

static int carried_in(int v)
{
    int n = 0;
    for (int i = 0; i < ncarried; i++) n += carried[i].v == v;
    return n;
}

static void deposited(int rx)
{
    if (rx >= 0 && rx < rx_n) rx_done[rx] = 1;
    save_state();
}

/* Site assembly_tick (the loop head of O0E30_0C89): a click on the Pieces box opens the inventory. The
 * registers are the loop's, which the conveyor update needs (inventory.c). */
static int on_assembly_site(void *u, uint32_t addr, GGRegs *r)
{
    (void)u; (void)addr;
    int v = cur_vehicle();
    if (v < 0 || in_level != v) return 0;
    static InvItem items[MAX_RECEIVED];
    int n = ap_inventory(items, MAX_RECEIVED);
    if (inventory_site(r, items, n, v, carried_in(v), deposited) > 0) {
        mark_obtained();
        save_level();                                        /* moved parts are never lost to quitting */
        hud_forget();
    }
    return 0;
}

static int on_warehouse_tick(void *u, GGEvent *e)
{
    (void)u; (void)e;
    mark_obtained();
    if (frames - last_grant_frame >= 90) {                   /* ~1.5 s between items */
        last_grant_frame = frames;
        grant_pending();
    }
    return GG_CONTINUE;
}

static int on_box_open(void *u, GGEvent *e)
{
    (void)u;
    int v = cur_vehicle();
    if (!cfg.box_checks || v < 0 || e->contents == GG_BOX_EMPTY || e->contents < 0 || e->contents >= 0x40)
        return GG_CONTINUE;                                  /* empty boxes keep their bananas */
    /* A box made for a part the player already obtained is the player's own part coming back, not a
     * location: let the game hand it over as usual. Recycling (HUD recycle bin R0A04_0A82, the
     * assembly room, or the Archipelago inventory later) sets part +0x36 bit 0x4000, mark_obtained()
     * sets it on every part the player holds, and every routine that creates a box
     * (R0622_0A76, R0622_0DF0) then gives it sprite 0x333 / 0x344 instead of 0x332 / 0x343. The
     * mark is checked on the box, not the part: the part's own original box (its location) may still
     * be unopened if the player recycled a received copy of it. Box = R0622_1265(room, object). */
    uint16_t args[2] = { (uint16_t)e->room, (uint16_t)e->object };
    uint32_t bp = api->call_far(0x0622, 0x1265, 2, args);
    uint32_t box = (bp >> 16) * 16u + (bp & 0xFFFFu);
    uint16_t sprite = rd16(box + 0x1A);
    if (sprite == 0x333 || sprite == 0x344) return GG_CONTINUE;
    char what[48];
    snprintf(what, sizeof what, "box: level %d part %d", v, e->contents);
    send_check(LOC_BOX(v, e->contents), what);
    return GG_SUPPRESS;                                      /* the box goes; the item comes from the server */
}

/* ---- Cyberchimps and check boxes ------------------------------------------------------------------
 * A box holds a part number, and the part table has one record per part: the record of an unopened check
 * box's part is only a stand-in for the box's location. The player's box opening is intercepted
 * (on_box_open); a chimp opening a box is intercepted here, so the stand-in record never moves into a
 * chimp's inventory (it may be in the player's, given through the Archipelago inventory):
 *   site chimp_box_open, O0E0D_0000(far actor) at `or ax, ax` after ax = R053F_02BB(actor) = parts the
 *   chimp holds (a chimp only takes a box while holding nothing). [bp-4] = the box (R0622_1265(actor+0x1A
 *   room, actor+0x40 object)), box +0x1E = its part, +0x1A sprite; actor +0x42 = actor number.
 *   For a check box: the mod removes the box (R0622_0D8F(room, box), as the game does), notes that this
 *   chimp carries the box's check, and makes the routine leave (ZF = 0: `jnz` to the exit). A chimp that
 *   carries a check counts as holding something, as in vanilla.
 *   site chimp_drop, O0E0D_00A1(actor) at `or ax, [bp-6]` after dx:ax = O0E20_0101(list, actor, 0) = the
 *   part the chimp drops when a banana hits it (0 = none). [bp-4] = the actor (DS:7A8A + 0xA0 n).
 *   Carried checks drop first, one per hit: the mod makes the box at the chimp's feet as the routine
 *   would (R0622_0DF0(room, +0x14, +0x1C, part, 0)) with the normal sprite, and leaves (ZF = 1).
 * Both changes are saved at once (state.txt + the game's save at the next safe point). */
static int chimp_save_pending;

static void chimp_save(void *u)
{
    (void)u;
    chimp_save_pending = 0;
    if (in_level >= 0 && in_level == cur_vehicle()) save_level();
}

static void chimp_changed(void)
{
    save_state();
    if (!chimp_save_pending) { chimp_save_pending = 1; api->defer(chimp_save, NULL); }
}

static int carried_by(int v, int actor)
{
    int n = 0;
    for (int i = 0; i < ncarried; i++) n += carried[i].v == v && carried[i].actor == actor;
    return n;
}

static int on_chimp_box_open(void *u, uint32_t addr, GGRegs *r)
{
    (void)u; (void)addr;
    int v = cur_vehicle();
    if (r->ax != 0 || v < 0 || !cfg.box_checks) return 0;        /* holds a part: vanilla leaves the box */
    uint32_t frame = (uint32_t)r->ss * 16u;
    uint32_t actor_far = (uint32_t)rd16(frame + (uint16_t)(r->bp + 8)) << 16 | rd16(frame + (uint16_t)(r->bp + 6));
    uint32_t box_far = (uint32_t)rd16(frame + (uint16_t)(r->bp - 2)) << 16 | rd16(frame + (uint16_t)(r->bp - 4));
    uint32_t actor = (actor_far >> 16) * 16u + (actor_far & 0xFFFF), box = (box_far >> 16) * 16u + (box_far & 0xFFFF);
    int n = (int16_t)rd16(actor + 0x42);
    if (carried_by(v, n)) { r->flags &= (uint16_t)~GG_FLAG_ZF; return 1; }   /* already carries a check */
    int part = (int16_t)rd16(box + 0x1E);
    uint16_t sprite = rd16(box + 0x1A);
    if (part < 0 || part >= 0x40 || sprite == 0x333 || sprite == 0x344) return 0;   /* recycled: a real part */
    if (ncarried >= MAX_CARRIED) return 0;
    uint16_t a[3] = { rd16(actor + 0x1A), (uint16_t)box_far, (uint16_t)(box_far >> 16) };
    api->call_far(0x0622, 0x0D8F, 3, a);                     /* the box goes, as when the chimp opens it */
    carried[ncarried].v = v; carried[ncarried].actor = n; carried[ncarried].part = part; ncarried++;
    api->log("ap: chimp %d took the check box of part %d (level %d)", n, part, v);
    chimp_changed();
    r->flags &= (uint16_t)~GG_FLAG_ZF;                       /* skip `or ax, ax`; jnz -> exit */
    return 1;
}

static int on_chimp_drop(void *u, uint32_t addr, GGRegs *r)
{
    (void)u; (void)addr;
    int v = cur_vehicle(), n = (int16_t)r->si, k = -1;
    for (int i = ncarried - 1; i >= 0 && k < 0; i--) if (carried[i].v == v && carried[i].actor == n) k = i;
    if (k < 0) return 0;                                     /* nothing virtual: the game drops a real part */
    uint32_t frame = (uint32_t)r->ss * 16u;
    uint32_t actor = (uint32_t)rd16(frame + (uint16_t)(r->bp - 2)) * 16u + rd16(frame + (uint16_t)(r->bp - 4));
    int part = carried[k].part;
    /* the box takes the recycled sprite when the part record has bit 0x4000 (the player may hold the
     * real part by now): clear it while the box is made, so it is a check box again */
    uint32_t e = (uint32_t)api->ds_read16(0x663E) * 16u + api->ds_read16(0x663C) + (uint32_t)part * 0x8Eu;
    uint8_t *m = api->mem();
    uint8_t keep = m[e + 0x37];
    m[e + 0x37] &= (uint8_t)~0x40;
    uint16_t a[5] = { rd16(actor + 0x1A), rd16(actor + 0x14), rd16(actor + 0x1C), (uint16_t)part, 0 };
    api->call_far(0x0622, 0x0DF0, 5, a);
    m[e + 0x37] = keep;
    carried[k] = carried[--ncarried];
    api->log("ap: chimp %d dropped the check box of part %d (level %d)", n, part, v);
    chimp_changed();
    r->flags |= GG_FLAG_ZF;                                  /* skip `or ax, [bp-6]`; jz -> exit */
    return 1;
}

static int on_door_open(void *u, GGEvent *e)
{
    (void)u;
    int v = cur_vehicle();
    if (!cfg.door_checks || v < 0) return GG_CONTINUE;
    int rank = door_rank(e->room, e->door);
    if (rank < 0) { api->log("ap: door %d in room %d not found", e->door, e->room); return GG_CONTINUE; }
    char what[48];
    snprintf(what, sizeof what, "door: level %d #%d", v, rank);
    send_check(LOC_DOOR(v, rank), what);
    return GG_CONTINUE;
}

static int on_race_end(void *u, GGEvent *e)
{
    (void)u;
    if (!e->won || e->aborted) return GG_CONTINUE;
    int b = e->building, v = b * 5 + e->level;
    if (v < 0 || v > 14) return GG_CONTINUE;
    char what[32];
    snprintf(what, sizeof what, "race: level %d", v);
    send_check(LOC_RACE(v), what);
    /* After this the game counts the win: [4596+2b] += 1 (last race won in the building) and [5C0E] += 1
     * (15 = game complete). With levels played in any order, keep them = distinct wins: the building's
     * counter = its wins - 1 before the increment, and a race won before does not count again. */
    int won = won_mask();
    int n = 0;
    for (int l = 0; l < 5; l++) n += won >> (b * 5 + l) & 1;
    if (won >> v & 1) {
        api->ds_write16((uint16_t)(GG_DS_RACES_WON_IDX + 2 * b), (uint16_t)(n - 2));    /* +1 -> n - 1 */
        api->ds_write16(GG_DS_TOTAL_RACES_WON, (uint16_t)(api->ds_read16(GG_DS_TOTAL_RACES_WON) - 1));
    } else {
        api->ds_write16((uint16_t)(GG_DS_RACES_WON_IDX + 2 * b), (uint16_t)(n - 1));    /* +1 -> n */
        won |= 1 << v;
    }
    st.won = won;
    save_state();
    in_level = -1;                                           /* the warehouse is gone after a race */
    return GG_CONTINUE;
}

/* ap/game.txt, about once a second: tells the client the game is running with the mod, who is signed in,
 * where they are and how many received items are in the game. Rewritten whole (temp file + rename). */
static void heartbeat(void)
{
    char p[1100], tmp[1110], name[17];
    const uint8_t *m = api->mem() + api->ds_base() + GG_DS_PLAYER_NAME;
    int i = 0;
    for (; i < 16 && m[i] >= 32 && m[i] < 127; i++) name[i] = (char)m[i];
    name[i] = 0;
    static InvItem inv[MAX_RECEIVED];
    int waiting = ap_inventory(inv, MAX_RECEIVED);
    path(p, sizeof p, "game.txt");
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "mod=1\nframe=%d\nplayer=%s\nbuilding=%d\nlevel=%d\nin_level=%d\nitems=%d\nreceived=%d\n"
            "ap_inventory=%d\n",
            frames, name, (int16_t)api->ds_read16(GG_DS_BUILDING), (int16_t)api->ds_read16(GG_DS_LEVEL),
            in_level >= 0, rx_n, rx_n, waiting);
    fclose(f);
    remove(p);                                               /* Windows rename does not replace */
    rename(tmp, p);
}

static int on_frame(void *u, GGEvent *e)
{
    (void)u; (void)e;
    frames++;
    if (frames % 30 == 0) {
        poll_received();
        if (!st.goal_sent && st.won >= 0 && (st.won & 0x7FFF) == 0x7FFF) {
            st.goal_sent = 1; save_state();
            send_line("goal");
            api->log("ap: goal reached");
        }
    }
    if (frames % 60 == 0) heartbeat();
    return GG_CONTINUE;
}

/* Title-screen Archipelago logo: tools/title_logo.py builds GIZMOAP1.DAT, a copy of gizmo001.dat
 * whose title sprite has the logo in it. If that file is in the game folder, point the game's archive
 * 1 name (DS:057E, "gizmo001.dat", in the table at DS:0550) at it. The image is already loaded when
 * mods start and the archives are opened later, so this is a plain memory write. */
static void title_logo(void)
{
    static const char orig[] = "gizmo001.dat", repl[] = "gizmoap1.dat";
    char p[1100];
    FILE *f = NULL;
    snprintf(p, sizeof p, "%s/GIZMOAP1.DAT", api->game_dir());
    if (!(f = fopen(p, "rb"))) {
        snprintf(p, sizeof p, "%s/gizmoap1.dat", api->game_dir());
        f = fopen(p, "rb");
    }
    if (!f) { api->log("ap: no GIZMOAP1.DAT in the game folder: title logo off"); return; }
    fclose(f);
    uint8_t *m = api->mem() + api->ds_base() + 0x057E;
    if (memcmp(m, orig, 12) != 0) { api->log("ap: archive name not where expected: title logo off"); return; }
    memcpy(m, repl, 12);
    api->log("ap: title logo on (archive 1 = GIZMOAP1.DAT)");
}

GG_MOD_EXPORT int gg_mod_init(const GGApi *a)
{
    if (a->version < 1) return 1;
    api = a;
    snprintf(dir_ap, sizeof dir_ap, "%s/ap", api->game_dir());
    MKDIR(dir_ap);                                           /* fine if it exists */
    load_config();
    load_state();
    poll_received();
    if (cfg.title_logo) title_logo();
    picker_init(api);
    hud_init(api);
    ui_init(api);
    inventory_init(api);
    api->on_event(GG_EV_BUILDING_CLICK, on_building_click, NULL);
    if (!api->on_site(api->site_addr("building_selected"), on_building_selected, NULL))
        api->log("ap: building_selected site missing: no level picker");
    api->on_event(GG_EV_LEVEL_ENTER, on_level_enter, NULL);
    api->on_event(GG_EV_LEVEL_READY, on_level_ready, NULL);
    api->on_event(GG_EV_WAREHOUSE_TICK, on_warehouse_tick, NULL);
    api->on_event(GG_EV_ASSEMBLY_TICK, on_assembly_tick, NULL);
    if (!api->on_site(api->site_addr("chimp_box_open"), on_chimp_box_open, NULL) ||
        !api->on_site(api->site_addr("chimp_drop"), on_chimp_drop, NULL))
        api->log("ap: chimp sites missing (recomp too old): chimps take check boxes as in vanilla");
    if (!api->on_site(api->site_addr("assembly_tick"), on_assembly_site, NULL))
        api->log("ap: assembly_tick site missing: no Archipelago inventory");
    api->on_event(GG_EV_BOX_OPEN, on_box_open, NULL);
    api->on_event(GG_EV_DOOR_OPEN, on_door_open, NULL);
    api->on_event(GG_EV_RACE_START, on_race_start, NULL);
    api->on_event(GG_EV_RACE_END, on_race_end, NULL);
    api->on_event(GG_EV_FRAME, on_frame, NULL);
    api->log("ap: Archipelago mod ready (%s, %d item(s) received so far)", dir_ap, rx_n);
    return 0;
}
