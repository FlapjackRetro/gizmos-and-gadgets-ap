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
    int level[3];            /* level to load per building (0-based), -1 = the game's choice */
} cfg;

/* ---- state (ap/state.txt) ---------------------------------------------------------------- */
static struct {
    int wh_level[3];         /* level the building's saved warehouse belongs to, -1 none */
    int goal_sent;
} st;

/* received items and whether each was put into the game already */
static unsigned rx_item[MAX_RECEIVED];
static unsigned char rx_done[MAX_RECEIVED];
static int rx_n;
static long rx_size;         /* bytes of received.txt already read */

static char dir_ap[1024];
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
    cfg.open_world = 1; cfg.box_checks = 1; cfg.door_checks = 1;
    cfg.level[0] = cfg.level[1] = cfg.level[2] = -1;
    path(p, sizeof p, "config.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v;
        if (sscanf(line, " %63[^= ] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "open_world")) cfg.open_world = v;
        else if (!strcmp(key, "box_checks")) cfg.box_checks = v;
        else if (!strcmp(key, "door_checks")) cfg.door_checks = v;
        else if (!strncmp(key, "level.", 6)) {
            int b = atoi(key + 6);
            if (b >= 0 && b < 3) cfg.level[b] = v < 0 ? -1 : (v > 4 ? 4 : v);
        }
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
    fprintf(f, "goal_sent=%d\n", st.goal_sent);
    for (int i = 0; i < rx_n; i++) if (rx_done[i]) fprintf(f, "done=%d\n", i);
    fclose(f);
}

static void load_state(void)
{
    char p[1100], line[128];
    st.wh_level[0] = st.wh_level[1] = st.wh_level[2] = -1;
    path(p, sizeof p, "state.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int b, v;
        if (sscanf(line, "wh_level.%d=%d", &b, &v) == 2 && b >= 0 && b < 3) st.wh_level[b] = v;
        else if (sscanf(line, "goal_sent=%d", &v) == 1) st.goal_sent = v;
        else if (sscanf(line, "done=%d", &v) == 1 && v >= 0 && v < MAX_RECEIVED) rx_done[v] = 1;
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

/* One item per call, so each name stays on screen for a moment. */
static void grant_pending(void)
{
    int v = cur_vehicle();
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
        if (is_part_item(id, &iv, &part)) {
            if (iv != v || in_level != v) continue;          /* parts belong to their level */
            int r = api->give_part(part);
            if (r < 0) {                                     /* already holding two of that kind */
                api->log("ap: part %d (level %d) waits: inventory full for that kind", part, iv);
                continue;
            }
            mark_obtained();
            rx_done[i] = 1; save_state();
            api->log("ap: received part %d of level %d%s", part, iv, r ? " (needed)" : "");
            return;
        }
        rx_done[i] = 1;                                      /* unknown item: skip it */
        api->log("ap: unknown item %u ignored", id);
    }
}

/* ---- events ------------------------------------------------------------------------------- */
static int on_building_click(void *u, GGEvent *e)
{
    (void)u; (void)e;
    return cfg.open_world ? GG_ALLOW : GG_CONTINUE;
}

static int on_level_enter(void *u, GGEvent *e)
{
    (void)u;
    load_config();
    int b = e->building;
    if (b < 0 || b > 2) return GG_CONTINUE;
    if (cfg.level[b] >= 0) e->level = cfg.level[b];
    if (e->level > 4) e->level = 4;                          /* all 5 won: the game would load level 6 */
    if (e->restore && st.wh_level[b] != e->level) e->restore = 0;   /* the saved warehouse is another level's */
    if (!e->restore) {
        /* a fresh warehouse: parts received for this level must be given again */
        for (int i = 0; i < rx_n; i++) {
            int iv, part;
            if (rx_done[i] && is_part_item(rx_item[i], &iv, &part) && iv == b * 5 + e->level) rx_done[i] = 0;
        }
    }
    st.wh_level[b] = e->level;
    in_level = -1;
    save_state();
    api->log("ap: entering building %d level %d (%s)", b, e->level + 1, e->restore ? "restored" : "fresh");
    return GG_CONTINUE;
}

static int on_level_ready(void *u, GGEvent *e)
{
    (void)u; (void)e;
    in_level = cur_vehicle();
    return GG_CONTINUE;
}

static int on_assembly_tick(void *u, GGEvent *e)
{
    (void)u; (void)e;
    mark_obtained();
    return GG_CONTINUE;
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
    int v = e->building * 5 + e->level;
    if (v < 0 || v > 14) return GG_CONTINUE;
    char what[32];
    snprintf(what, sizeof what, "race: level %d", v);
    send_check(LOC_RACE(v), what);
    in_level = -1;                                           /* the warehouse is gone after a race */
    return GG_CONTINUE;
}

static int on_frame(void *u, GGEvent *e)
{
    (void)u; (void)e;
    frames++;
    if (frames % 30 == 0) {
        poll_received();
        /* +1 for the race being counted right now is not needed: the counter updates first */
        if (!st.goal_sent && api->ds_read16(GG_DS_TOTAL_RACES_WON) >= 15) {
            st.goal_sent = 1; save_state();
            send_line("goal");
            api->log("ap: goal reached");
        }
    }
    return GG_CONTINUE;
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
    api->on_event(GG_EV_BUILDING_CLICK, on_building_click, NULL);
    api->on_event(GG_EV_LEVEL_ENTER, on_level_enter, NULL);
    api->on_event(GG_EV_LEVEL_READY, on_level_ready, NULL);
    api->on_event(GG_EV_WAREHOUSE_TICK, on_warehouse_tick, NULL);
    api->on_event(GG_EV_ASSEMBLY_TICK, on_assembly_tick, NULL);
    api->on_event(GG_EV_BOX_OPEN, on_box_open, NULL);
    api->on_event(GG_EV_DOOR_OPEN, on_door_open, NULL);
    api->on_event(GG_EV_RACE_END, on_race_end, NULL);
    api->on_event(GG_EV_FRAME, on_frame, NULL);
    api->log("ap: Archipelago mod ready (%s, %d item(s) received so far)", dir_ap, rx_n);
    return 0;
}
