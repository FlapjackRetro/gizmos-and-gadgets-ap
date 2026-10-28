/*
 * ap_mod.c - Archipelago mod for the Gizmos & Gadgets recompilation.
 *
 * Built against the recomp's include/gg_mod.h (API version 1) and loaded from <game dir>/mods/.
 * It turns the game's events into Archipelago location checks, grants received items, and
 * lifts the "one level per building" rule. It talks to the Archipelago client through plain
 * files in <game dir>/ap/ (see docs/MOD.md). Each seed and slot has its own folder, ap/<seed>_<team>_<slot>/;
 * ap/current.txt (client) names the one the client is connected to:
 *
 *   ap/current.txt         (client writes)  the folder of the connected seed/slot
 *   ap/game.txt            (mod)            heartbeat for the client
 *   ap/players.txt         (mod)            "<in-game player>=<folder>": which player plays which seed
 *   ap/<s>/checks.txt      (mod appends)    one location id per line; "goal" once enough races are won
 *   ap/<s>/received.txt    (client writes)  one item id per line, in the order the server sent them
 *   ap/<s>/config.txt      (client)         key=value settings, re-read on every level entry
 *   ap/<s>/state.txt       (mod)            what the mod must remember between sessions
 *   ap/<s>/levels/<v>.bin  (mod)            each level's saved warehouse
 *
 * Ids (docs/MOD.md):  base 0x47470000 (1195835392)
 *   race won       base + 0x0000 + v                 v = building*5 + level (0..14)
 *   part box       base + 0x1000 + v*0x40 + part     the box that holds `part` in level v
 *   puzzle door    base + 0x2000 + v*0x40 + rank     rank of the door in the level (see door_rank)
 *   item: part     base + 0x1000 + v*0x40 + part     (same numbers as the box locations)
 *   item: bananas  base + 0x0F00                      +5 bananas
 *   item: access   base + 0x0E00 + v                  opens level v (item_progression; the client writes avail)
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
#define ITEM_ACCESS(v)     (AP_BASE + 0x0E00u + (unsigned)(v))   /* level_access: item_progression */

#define MAX_RECEIVED 4096
#define GG_DS_CHIMPS 0x4578   /* player record +0x16: 1 = with Cyberchimps (set by the new-player question) */
#define MAX_DOORS    80

static const GGApi *api;

/* ---- settings (ap/config.txt) ------------------------------------------------------------ */
static struct {
    int open_world;          /* enter any building, even with all 5 races won */
    int box_checks;          /* part boxes are checks: the part goes to the multiworld */
    int door_checks;
    int title_logo;          /* use GIZMOAP1.DAT (Archipelago logo on the title screen) if present */
    int chimps;              /* Cyberchimps from the YAML: 1 with, 0 without, -1 not given (the game asks) */
    int races_required;      /* goal: this many different races won (YAML, default 15) */
    int avail[3][5];         /* level open in the picker (client: avail.<b>=11000); -1 = not given */
} cfg;

/* ---- state (ap/state.txt) ---------------------------------------------------------------- */
static struct {
    int wh_level[3];         /* level whose warehouse the building's block in the player record holds, -1 none */
    int won;                 /* bit v: the race of level v was won (-1 = not known yet: take the game's counters) */
    int goal_sent;
    char player[17];         /* the in-game player that plays this seed (bound when first entering a building) */
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

static char dir_root[1024];  /* <game dir>/ap */
static char dir_ap[1200];    /* this seed/slot's folder; "" = no client connection yet: the mod stays out */
static char session[128];    /* its name (ap/current.txt) */
static int picked[3] = { -1, -1, -1 };   /* level chosen in the picker, for the next LEVEL_ENTER */
static int last_grant_frame;
static int in_level = -1;    /* vehicle index of the warehouse that is set up, -1 none */

/* ---- files -------------------------------------------------------------------------------- */
static void path(char *out, size_t n, const char *name)
{
    if (!*dir_ap) { out[0] = 0; return; }                    /* no seed folder: nothing to read or write */
    snprintf(out, n, "%s/%s", dir_ap, name);
}

static void send_line(const char *line)
{
    char p[1300];
    path(p, sizeof p, "checks.txt");
    FILE *f = fopen(p, "ab");                               /* binary: "\n" only, the client counts bytes */
    if (!f) { api->log("ap: cannot write %s", p); return; }
    fprintf(f, "%s\n", line);
    fclose(f);
}

/* ---- item labels (ap/labels.txt, client: "<location id>=<text>") ------------------------------------ */
#define MAX_LABELS 700
static struct { unsigned loc; char text[64]; } labels[MAX_LABELS];
static int nlabels, labels_frame = -1000;
static int frames;

static void load_labels(void)
{
    char p[1300], line[256];
    if (!*dir_ap) return;
    snprintf(p, sizeof p, "%s/labels.txt", dir_ap);
    FILE *f = fopen(p, "r");
    if (!f) return;
    nlabels = 0;
    while (nlabels < MAX_LABELS && fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *t = eq + 1, *nl = strpbrk(t, "\r\n");
        if (nl) *nl = 0;
        labels[nlabels].loc = (unsigned)strtoul(line, NULL, 10);
        snprintf(labels[nlabels].text, sizeof labels[nlabels].text, "%s", t);
        nlabels++;
    }
    fclose(f);
}

static const char *label_of(unsigned loc)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nlabels; i++) if (labels[i].loc == loc) return labels[i].text;
        if (pass || frames - labels_frame < 60) break;        /* re-read at most once a second */
        labels_frame = frames;
        load_labels();
    }
    return NULL;
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
    char p[1300], line[256];
    cfg.open_world = 1; cfg.box_checks = 1; cfg.door_checks = 0; cfg.title_logo = 1; cfg.chimps = -1;
    cfg.races_required = 15;
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
        else if (!strcmp(key, "chimps")) cfg.chimps = v != 0;
        else if (!strcmp(key, "races_required") && v >= 1 && v <= 15) cfg.races_required = v;
    }
    fclose(f);
}

static void save_state(void)
{
    char p[1300];
    path(p, sizeof p, "state.txt");
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int b = 0; b < 3; b++) fprintf(f, "wh_level.%d=%d\n", b, st.wh_level[b]);
    fprintf(f, "won=%d\n", st.won);
    fprintf(f, "goal_sent=%d\n", st.goal_sent);
    if (st.player[0]) fprintf(f, "player=%s\n", st.player);
    for (int i = 0; i < rx_n; i++) if (rx_done[i]) fprintf(f, "done=%d\n", i);
    for (int i = 0; i < ncarried; i++) fprintf(f, "chimp=%d,%d,%d\n", carried[i].v, carried[i].actor, carried[i].part);
    fclose(f);
}

static void load_state(void)
{
    char p[1300], line[128];
    st.wh_level[0] = st.wh_level[1] = st.wh_level[2] = -1;
    st.won = -1;
    path(p, sizeof p, "state.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int b, v, lv;
        if (sscanf(line, "wh_level.%d=%d", &b, &v) == 2 && b >= 0 && b < 3) st.wh_level[b] = v;
        else if (sscanf(line, "goal_sent=%d", &v) == 1) st.goal_sent = v;
        else if (!strncmp(line, "player=", 7)) {
            snprintf(st.player, sizeof st.player, "%.16s", line + 7);
            st.player[strcspn(st.player, "\r\n")] = 0;
        }
        else if (sscanf(line, "won=%d", &v) == 1) st.won = v;
        else if (sscanf(line, "done=%d", &v) == 1 && v >= 0 && v < MAX_RECEIVED) rx_done[v] = 1;
        else if (sscanf(line, "chimp=%d,%d,%d", &b, &v, &lv) == 3 && ncarried < MAX_CARRIED) {
            carried[ncarried].v = b; carried[ncarried].actor = v; carried[ncarried].part = lv; ncarried++;
        }
    }
    fclose(f);
}

/* New lines in received.txt (the client only ever appends). Read in binary: rx_size is a byte offset into the
 * file, and in text mode Windows turns "\r\n" into "\n", so the count would fall one byte short per line and
 * the next read would start inside a line already read (lines read twice: duplicated items). */
static void poll_received(void)
{
    char p[1300], line[64];
    path(p, sizeof p, "received.txt");
    FILE *f = fopen(p, "rb");
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
        if (id >= ITEM_ACCESS(0) && id < ITEM_ACCESS(15)) {  /* opens a level: the client's avail.<b> */
            rx_done[i] = 1; save_state();
            api->log("ap: received access to level %u", id - ITEM_ACCESS(0));
            continue;
        }
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
/* ---- one folder per seed/slot ---------------------------------------------------------------------------
 * The client writes ap/current.txt when it connects. The mod follows it at startup, while no seed is set yet,
 * and at the Technology Center (a building click) -- never inside a level, whose warehouse belongs to a seed.
 * A seed is played by one in-game player, and a player plays one seed (ap/players.txt): entering a building with
 * another player is refused with a message, so an old player file can't pick up a new seed by accident. */
static int read_current(char *out, size_t n)
{
    char p[1300], line[160];
    snprintf(p, sizeof p, "%s/current.txt", dir_root);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    int ok = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    size_t k = 0;
    for (const char *s = line; *s && *s != '\r' && *s != '\n' && k + 1 < n; s++)
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-')
            out[k++] = *s;
    out[k] = 0;
    return k > 0;
}

static void use_session(const char *name)
{
    snprintf(session, sizeof session, "%s", name);
    snprintf(dir_ap, sizeof dir_ap, "%s/%s", dir_root, name);
    MKDIR(dir_ap);
    memset(&st, 0, sizeof st);
    memset(rx_done, 0, sizeof rx_done);
    rx_n = 0; rx_size = 0; nlabels = 0; labels_frame = -1000; ncarried = 0;
    picked[0] = picked[1] = picked[2] = -1;
    load_config();
    load_state();
    poll_received();
    api->log("ap: seed folder ap/%s (%d item(s) received so far%s%s)", name, rx_n,
             st.player[0] ? ", player " : ", no player yet", st.player);
}

static int follow_current(void)
{
    char name[128];
    if (!read_current(name, sizeof name) || !strcmp(name, session)) return 0;
    use_session(name);
    return 1;
}

static void signed_in(char *name, size_t n)
{
    const uint8_t *m = api->mem() + api->ds_base() + GG_DS_PLAYER_NAME;
    size_t i = 0;
    for (; i < 16 && i + 1 < n && m[i] >= 32 && m[i] < 127; i++) name[i] = (char)m[i];
    name[i] = 0;
}

/* ap/players.txt: the folder the player plays, "" if none */
static void player_session(const char *player, char *out, size_t n)
{
    char p[1300], line[200];
    out[0] = 0;
    snprintf(p, sizeof p, "%s/players.txt", dir_root);
    FILE *f = fopen(p, "r");
    if (!f) return;
    size_t len = strlen(player);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, player, len) && line[len] == '=') {
            snprintf(out, n, "%s", line + len + 1);
            out[strcspn(out, "\r\n")] = 0;
        }
    fclose(f);
}

static void ui_say2(const char *title, const char *a, const char *b, const char *c);

/* 1 = this player may play this seed (binding a new pair), 0 = refused (a message was shown) */
static int check_player(void)
{
    char name[17], other[160];
    signed_in(name, sizeof name);
    if (!name[0]) return 1;
    if (st.player[0]) {
        if (!strcmp(st.player, name)) return 1;
        char l1[64];
        snprintf(l1, sizeof l1, "This seed is played as '%s'.", st.player);
        ui_say2("Archipelago", l1, "Sign in as that player to go on,", "or connect the client to your seed.");
        api->log("ap: refused: '%s' signed in, this seed is played as '%s'", name, st.player);
        return 0;
    }
    player_session(name, other, sizeof other);
    if (other[0] && strcmp(other, session)) {
        char l1[64];
        snprintf(l1, sizeof l1, "'%s' already plays another seed.", name);
        ui_say2("New seed", l1, "Create a new player for this seed:", "Sign In, then New Player.");
        api->log("ap: refused: '%s' plays seed folder %s, not %s", name, other, session);
        return 0;
    }
    snprintf(st.player, sizeof st.player, "%s", name);
    save_state();
    char p[1300];
    snprintf(p, sizeof p, "%s/players.txt", dir_root);
    FILE *f = fopen(p, "a");
    if (f) { fprintf(f, "%s=%s\n", name, session); fclose(f); }
    api->log("ap: player '%s' now plays seed folder %s", name, session);
    return 1;
}

static int on_building_click(void *u, GGEvent *e)
{
    (void)u; (void)e;
    in_level = -1;                                           /* at the Technology Center */
    follow_current();                                        /* the client may have connected to another seed */
    if (!*dir_ap) return GG_CONTINUE;
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
    char p[1300];
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
    char p[1300];
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
    /* The save clears [94], and the game sets it again only when a room is entered (O0E30 0x1AAB, R02E0
     * 0xA14): left at 0, a vehicle built after this save in the same room visit was not saved on the way
     * to the Technology Center, and the level came back as it was here (seen in play). In the game [94]
     * stays 1 while the player is in the building. */
    api->ds_write16(0x94, 1);
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
    (void)u;
    load_config();
    int b = e->building;
    if (b < 0 || b > 2) return GG_CONTINUE;
    if (cfg.chimps >= 0) api->ds_write16(GG_DS_CHIMPS, (uint16_t)cfg.chimps);   /* the YAML's, for every level */
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

/* Site chimps_prompt (O0E2D 0x1307: call O0E83_0C5E, the new player's "With Chimps / Without Chimps" question;
 * ax != 0 -> [4578] = 1). With the YAML's setting known, tell the player instead of asking and skip the call. */
static int on_chimps_prompt(void *u, uint32_t addr, GGRegs *r)
{
    if (!*dir_ap) return 0;
    (void)u; (void)addr;
    load_config();
    if (cfg.chimps < 0 || !ui_ready()) return 0;               /* no client yet: the game asks */
    const char *l[3] = { "Set by your Archipelago settings:",
                         cfg.chimps ? "this game has Cyberchimps." : "this game has no Cyberchimps.",
                         cfg.chimps ? "They may carry off parts and checks." : "No chimps will get in your way." };
    UiSession s;
    ui_begin(&s);
    ui_message("Cyberchimps", l, 3, 0);
    ui_end(&s);
    r->ax = (uint16_t)cfg.chimps;
    api->log("ap: new player: Cyberchimps %s (from the YAML)", cfg.chimps ? "on" : "off");
    return 1;
}

/* Site building_selected (R02E0_0148 at 0x463: mov [bp-6], 1 = leave the select loop and enter the
 * building, [4592] = the building). Show the picker; "Return to Technology Center" skips the instruction,
 * so the select loop carries on as if nothing was clicked. */
static int on_building_selected(void *u, uint32_t addr, GGRegs *r)
{
    (void)u; (void)addr; (void)r;
    if (!*dir_ap) return 0;
    int b = (int16_t)api->ds_read16(GG_DS_BUILDING);
    if (b < 0 || b > 2) return 0;
    if (!check_player()) return 1;                           /* stay at the Technology Center */
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

/* ---- won levels: enterable, but never raced again ---------------------------------------------------
 * The race door (O0E30_00A6) opens while [4584] (vehicle slots filled) = the level's slot count (part header
 * +6), and clicking it (region DS:223C, code 0x3F0 -> O0E30 0x12FA) starts the race under the same test;
 * filling the last slot also gives +100 and sets [4586], which makes the room show hint 0x1053 ("finished
 * building your vehicle ... click on the doorway"). In a won level the mod keeps [4584] RACE_LOCK above the
 * real count (it still goes up and down as parts are placed and taken off, and is saved with the warehouse),
 * so the door, the race and the bonus never come; keeps [4586] = 0 (no hint); and gives the door region its
 * own code, which shows a message instead. */
#define RACE_LOCK      0x100
#define DS_SLOTS_DONE  0x4584
#define DS_DOOR_BONUS  0x4586
#define DS_RACE_DOOR   0x223C                 /* the assembly room's race door region (+0x12 click code) */
#define DOOR_CODE      0x0F02

static int level_won(int v) { return v >= 0 && (won_mask() >> v & 1); }

static void lock_race(int v)
{
    if (!level_won(v)) return;
    uint16_t n = api->ds_read16(DS_SLOTS_DONE);
    if (n < RACE_LOCK) api->ds_write16(DS_SLOTS_DONE, (uint16_t)(n + RACE_LOCK));
    api->ds_write16(DS_DOOR_BONUS, 0);       /* else the room repeats "finished building ... click the doorway" */
}

static void race_door_region(int v)
{
    uint16_t want = level_won(v) ? DOOR_CODE : 0x3F0, have = api->ds_read16(DS_RACE_DOOR + 0x12);
    if (have == 0x3F0 || have == DOOR_CODE) api->ds_write16(DS_RACE_DOOR + 0x12, want);
}

static void ui_say2(const char *title, const char *a, const char *b, const char *c)
{
    if (!ui_ready()) return;
    const char *l[3] = { a, b, c };
    UiSession s;
    ui_begin(&s);
    ui_message(title, l, 3, 0);
    ui_end(&s);
}

static void say(const char *title, const char *a, const char *b)
{
    if (!ui_ready()) return;
    const char *l[2] = { a, b };
    UiSession s;
    ui_begin(&s);
    ui_message(title, l, b ? 2 : 1, 0);
    ui_end(&s);
}

/* Site podium_choice (O0E36_0573, the podium after a race, `sub ax, 7D0h`): ax = the sign clicked. After a win:
 * Rerun Race (7D0) is refused; Next Level (7D1) goes to this building's next level when it is open (the
 * picker's rule), else to the Technology Center (7D3). After a loss everything stays as in the game. */
static int on_podium(void *u, uint32_t addr, GGRegs *r)
{
    if (!*dir_ap) return 0;
    (void)u; (void)addr;
    if (api->ds_read16(0x4560) != 0) return 0;                /* lost */
    int b = (int16_t)api->ds_read16(GG_DS_BUILDING), lv = (int16_t)api->ds_read16(GG_DS_LEVEL);
    if (b < 0 || b > 2 || lv < 0 || lv > 4) return 0;
    if (r->ax == 0x7D0) {
        say("Race won", "You won this race already.", "Pick another sign.");
        r->ax = 0;                                            /* not a sign: ignored */
    } else if (r->ax == 0x7D1) {
        load_config();
        int open[5];
        open_levels(b, open);
        if (lv < 4 && open[lv + 1]) {
            picked[b] = lv + 1;
            api->log("ap: podium: next level %d", lv + 2);
        } else {
            char s[48];
            snprintf(s, sizeof s, "Level %d is not open yet.", lv + 2);
            say("Next Level", lv < 4 ? s : "This was the building's last level.", "Back to the Technology Center.");
            r->ax = 0x7D3;
            api->log("ap: podium: next level not open, Technology Center");
        }
    }
    return 0;
}

static int on_level_ready(void *u, GGEvent *e)
{
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
    (void)u; (void)e;
    in_level = cur_vehicle();
    lock_race(in_level);                                     /* before the assembly room draws its door */
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
    (void)u; (void)e;
    mark_obtained();
    hud_tick(waiting_parts(), api->ds_read16(GG_DS_CHIMPS) != 0);
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
    if (!*dir_ap) return 0;
    (void)u; (void)addr;
    int v = cur_vehicle();
    if (v < 0 || in_level != v) return 0;
    lock_race(v);
    race_door_region(v);
    uint32_t code_at = (uint32_t)r->ss * 16u + (uint16_t)(r->bp - 0x22);
    if (rd16(code_at) == DOOR_CODE) {
        api->mem()[code_at] = 0; api->mem()[code_at + 1] = 0;
        say("Race track", "You won this level's race already.", "It cannot be raced again.");
    }
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
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
    const char *lbl = label_of(LOC_BOX(v, e->contents));
    ui_hud_name(lbl ? lbl : "Archipelago item");             /* what the box held, where a part's name goes */
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
 *   chimp carries the box's check, and makes the routine leave (ZF = 0: `jnz` to the exit). A chimp may carry
 *   several checks (its real inventory stays empty, so it keeps going for boxes).
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

static int on_chimp_box_open(void *u, uint32_t addr, GGRegs *r)
{
    if (!*dir_ap) return 0;
    (void)u; (void)addr;
    int v = cur_vehicle();
    if (r->ax != 0 || v < 0 || !cfg.box_checks) return 0;        /* holds a part: vanilla leaves the box */
    uint32_t frame = (uint32_t)r->ss * 16u;
    uint32_t actor_far = (uint32_t)rd16(frame + (uint16_t)(r->bp + 8)) << 16 | rd16(frame + (uint16_t)(r->bp + 6));
    uint32_t box_far = (uint32_t)rd16(frame + (uint16_t)(r->bp - 2)) << 16 | rd16(frame + (uint16_t)(r->bp - 4));
    uint32_t actor = (actor_far >> 16) * 16u + (actor_far & 0xFFFF), box = (box_far >> 16) * 16u + (box_far & 0xFFFF);
    int n = (int16_t)rd16(actor + 0x42);
    /* A chimp that carries a check still takes check boxes: its real inventory is empty, so the game's chimp
     * logic keeps walking to boxes, and refusing here made it pick at the same box forever (seen in play). */
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
    if (!*dir_ap) return 0;
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
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
    if (!*dir_ap) return GG_CONTINUE;                       /* no seed yet: the game as it is */
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
    char p[1300], tmp[1310], name[17];
    const uint8_t *m = api->mem() + api->ds_base() + GG_DS_PLAYER_NAME;
    int i = 0;
    for (; i < 16 && m[i] >= 32 && m[i] < 127; i++) name[i] = (char)m[i];
    name[i] = 0;
    static InvItem inv[MAX_RECEIVED];
    int waiting = *dir_ap ? ap_inventory(inv, MAX_RECEIVED) : 0;
    snprintf(p, sizeof p, "%s/game.txt", dir_root);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "mod=2\nframe=%d\nplayer=%s\nbuilding=%d\nlevel=%d\nin_level=%d\nitems=%d\nreceived=%d\n"
            "ap_inventory=%d\nsession=%s\n",
            frames, name, (int16_t)api->ds_read16(GG_DS_BUILDING), (int16_t)api->ds_read16(GG_DS_LEVEL),
            in_level >= 0, rx_n, rx_n, waiting, session);
    fclose(f);
    remove(p);                                               /* Windows rename does not replace */
    rename(tmp, p);
}

static int popcount15(int m)
{
    int n = 0;
    for (int v = 0; v < 15; v++) n += m >> v & 1;
    return n;
}

static int on_frame(void *u, GGEvent *e)
{
    (void)u; (void)e;
    frames++;
    if (frames % 30 == 0 && !*dir_ap) follow_current();      /* the client connected: memory writes only */
    if (frames % 30 == 0 && *dir_ap) {
        poll_received();
        if (!st.goal_sent && st.won >= 0 && popcount15(st.won) >= cfg.races_required) {
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
    char p[1300];
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
    snprintf(dir_root, sizeof dir_root, "%s/ap", api->game_dir());
    MKDIR(dir_root);                                         /* fine if it exists */
    dir_ap[0] = 0;
    load_config();                                           /* defaults until a seed folder is known */
    if (!follow_current())
        api->log("ap: no seed yet (connect the Gizmos and Gadgets Client): the game runs as it is until then");
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
    if (!api->on_site(api->site_addr("podium_choice"), on_podium, NULL))
        api->log("ap: podium_choice site missing (recomp too old): the podium works as in the game");
    if (!api->on_site(api->site_addr("chimps_prompt"), on_chimps_prompt, NULL))
        api->log("ap: chimps_prompt site missing (recomp too old): the game asks about chimps");
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
    api->log("ap: Archipelago mod ready (%s)", *dir_ap ? dir_ap : dir_root);
    return 0;
}
