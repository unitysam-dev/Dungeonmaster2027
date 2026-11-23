/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Phase 3 hardened two-party test suite (focused test source, engine-feasibility scope only).
   Drives the ACTUAL original gameloop route (gameloop_tick) over the real dungeon data, not a test-local
   tick reimplementation. Gates from DMMAC-PHASE3-BRIEF.md:
   P1 stale-active occupancy + bounds/map context correctness (parent counterexample class),
   P2 world tick once + per-party tick effects in the actual gameloop route (pending damage for B while A
      selected, torch burn for both at the original cadence, explicit per-party sleep cadence),
   P3 active party + current map restoration after owner events/target resolution, chained owner followups,
      cross-map event context, A->B and B->A projectile hits through the original collision/damage path,
      target function contract (never leaves the caller's context switched),
   P4 four live champions per party (2x4) with independent movement/facing/stamina/mana/HP and champion
      index-3 isolation across parties.
   Usage: phase3_hardened <game-data-dir> <original-adf> <out-dir> */

#include "adf.h"
#include "champion.h"
#include "discard.h"
#include "dungeon.h"
#include "event.h"
#include "game.h"
#include "gameloop.h"
#include "group.h"
#include "ids.h"
#include "inventory.h"
#include "map.h"
#include "arith.h"
#include "move.h"
#include "os.h"
#include "party.h"
#include "party_context.h"
#include "projectile.h"
#include "record.h"
#include "recruit.h"
#include "tables.h"
#include "timeline.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The tick interval masks of src/engine/game.c (not exported by the engine). */
#define TORCH_BURN_INTERVAL_MASK   0x1ff
#define TIME_EFFECTS_MASK          0x03f
#define TIME_EFFECTS_SLEEPING_MASK 0x00f

static int g_pass, g_fail;
static FILE *g_log, *g_leak;

static void check(bool ok, const char *what)
{
    fprintf(g_log, "%s %s\n", ok ? "PASS" : "FAIL", what);
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (ok)
        g_pass++;
    else
        g_fail++;
}

/* Folder hooks of data/os.c (the SDL platform normally sets them). */
static bool hook_make_folder(const char *path)
{
    struct stat st;
    return mkdir(path, 0755) == 0 || stat(path, &st) == 0;
}

static void hook_list_folder(const char *path, void (*found)(const char *name, void *data), void *data)
{
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            found(e->d_name, data);
    closedir(d);
}

/* n ticks through the ACTUAL original gameloop route. Freeze life (an original game mechanic) is refreshed
   every 64 ticks so dungeon creatures cannot randomly damage the fixtures during long cadence runs. */
static void run_ticks(int n)
{
    for (int i = 0; i < n; i++) {
        if ((i & 63) == 0) {
            int16_t saved = g_active_party;
            for (int16_t p = 0; p < PARTY_CONTEXT_COUNT; p++) {
                party_context_activate(p);
                g_party.freeze_life_ticks = 255;
            }
            party_context_activate(saved);
        }
        gameloop_tick();
    }
}

/* A candidate mirror square with its portrait. */
typedef struct {
    int16_t map, x, y, portrait;
    char name[32];
} Mirror;

/* Collects up to max distinct candidate mirrors (distinct parsed champion names) across all maps. */
static int find_mirrors(Mirror *out, int max)
{
    int found = 0;
    int16_t maps = dungeon_map_count();
    int16_t saved_map = g_current_map_index;
    for (int16_t m = 0; m < maps && found < max; m++) {
        map_set_current_map(m);
        for (int16_t x = 0; x < g_current_map_width && found < max; x++)
            for (int16_t y = 0; y < g_current_map_height && found < max; y++) {
                uint16_t text = recruit_text_thing(m, x, y);
                if (text == THING_NONE)
                    continue;
                RecruitData r;
                if (!recruit_parse(text, &r))
                    continue;
                bool duplicate = false;
                for (int i = 0; i < found; i++)
                    if (!strncmp(out[i].name, r.name, sizeof out[i].name))
                        duplicate = true;
                if (duplicate)
                    continue;
                int16_t portrait = 0;
                for (uint16_t t = map_get_square_first_thing(x, y); t != THING_ENDOFLIST; t = rec_thing_next(t))
                    if (THING_TYPE(t) == THING_SENSOR) {
                        int16_t p = recruit_portrait_index(t);
                        if (p >= 0)
                            portrait = p;
                    }
                out[found].map = m;
                out[found].x = x;
                out[found].y = y;
                out[found].portrait = portrait;
                snprintf(out[found].name, sizeof out[found].name, "%s", r.name);
                found++;
            }
    }
    map_set_current_map(saved_map);
    return found;
}

/* Two open squares with an open square in front of A's facing (the phase-2 layout). */
static bool find_test_squares(int16_t map, int16_t ax, int16_t ay,
                              int16_t *a_x, int16_t *a_y, int16_t *a_dir, int16_t *b_x, int16_t *b_y)
{
    map_set_current_map(map);
    for (int16_t dir = 0; dir < CELL_COUNT; dir++)
        for (int16_t x = 1; x < g_current_map_width - 1; x++)
            for (int16_t y = 1; y < g_current_map_height - 1; y++) {
                if (abs16(x - ax) + abs16(y - ay) > 3)
                    continue;
                int16_t fx = x, fy = y;
                map_step(dir, 1, 0, &fx, &fy);
                if (!map_is_square_open(x, y) || !map_is_square_open(fx, fy))
                    continue;
                *a_x = x;
                *a_y = y;
                *a_dir = dir;
                int16_t bx = fx, by = fy;
                map_step(dir, 1, 0, &bx, &by);
                if (!map_is_square_open(bx, by))
                    bx = fx, by = fy;
                *b_x = bx;
                *b_y = by;
                return true;
            }
    return false;
}

/* A weapon record whose icon is a torch with charges (party_burn_torches recognises the icon range). */
static uint16_t make_torch(void)
{
    uint16_t thing = discard_get_unused_thing(THING_WEAPON);
    if (thing == THING_NONE)
        return THING_NONE;
    for (uint16_t type = 0; type < 64; type++) {
        rec_object_set_type(thing, type);
        rec_weapon_set_charges(thing, 8);
        int16_t icon = object_get_icon_index(thing);
        if (icon >= ICON_TORCH_UNLIT && icon <= ICON_TORCH_LAST)
            return thing;
    }
    return THING_NONE;
}

/* The champions of a party: LIVE globals for the active party, saved context for the inactive ones.
   Reading g_parties[p] for the active party is the stale-context trap the parent counterexample caught. */
static const Champion *champ(int16_t p, int16_t c)
{
    return p == g_active_party ? &g_champions[c] : &g_parties[p].champions[c];
}

static int16_t party_food_water(int16_t p)
{
    int16_t total = 0;
    for (int c = 0; c < g_parties[p].champion_count; c++)
        total += champ(p, c)->food + champ(p, c)->water;
    return total;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <game-data-dir> <original-adf> <out-dir>\n", argv[0]);
        return 2;
    }
    const char *game_dir = argv[1], *adf = argv[2], *out = argv[3];
    char path[1024];
    snprintf(path, sizeof path, "%s/phase3-results.txt", out);
    g_log = fopen(path, "w");
    snprintf(path, sizeof path, "%s/phase3-leak-log.txt", out);
    g_leak = fopen(path, "w");
    g_os_make_folder_hook = hook_make_folder;
    g_os_list_folder_hook = hook_list_folder;

    /* ---- Real data through the ACTUAL gameloop route ---- */
    snprintf(path, sizeof path, "%s/KAOS.FTL", game_dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        check(adf_extract(adf, game_dir), "P2 adf_extract original data (local copy)");
    else {
        fclose(f);
        check(true, "P2 game data already extracted");
    }
    check(gameloop_load_data(game_dir), "P2 gameloop_load_data(original data)");
    check(gameloop_start_new_game(game_dir), "P2 gameloop_start_new_game");
    check(dungeon_map_count() > 0 && g_dungeon_id != 0, "P2 real dungeon loaded (dungeon id != 0)");
    uint32_t t0 = g_game_time;
    run_ticks(3);
    check(g_game_time == t0 + 3, "P2 gameloop_tick advances the world clock exactly once per tick");

    /* ---- Two-party setup: four live champions per party through the original recruit handler ---- */
    party_context_init();
    party_context_set_live_count(2);
    Mirror mirrors[8];
    int mirror_count = find_mirrors(mirrors, 8);
    fprintf(g_leak, "candidate mirrors with distinct champions found: %d\n", mirror_count);
    check(mirror_count >= 1, "P4 at least one candidate mirror found on the real dungeon");
    int16_t start_map = g_party_map_index;

    for (int16_t p = 0; p < 2; p++) {
        party_context_activate(p);
        g_party_map_index = start_map;
        for (int i = 0; i < 4; i++) {
            Mirror *m = &mirrors[i % mirror_count];
            g_party_map_index = m->map;
            g_party_map_x = m->x;
            g_party_map_y = m->y;
            champion_add_candidate_to_party(m->portrait); /* original recruit handler */
            g_session.candidate_champion_index = -1;      /* the candidate panel closes (fixture) */
        }
    }
    party_context_activate(0);
    check(g_party_champion_count == 4, "P4 party A has four live champions");
    check(g_parties[1].champion_count == 4, "P4 party B has four live champions");
    bool alive4 = true;
    for (int16_t p = 0; p < 2; p++)
        for (int c = 0; c < 4; c++)
            if (champ(p, c)->current_health <= 0)
                alive4 = false;
    check(alive4, "P4 all eight champions live (health > 0)");
    bool base_distinct = false;
    for (int c = 1; c < 4; c++) {
        if (champ(0, c)->maximum_health != champ(0, 0)->maximum_health ||
            champ(0, c)->maximum_stamina != champ(0, 0)->maximum_stamina ||
            champ(0, c)->maximum_mana != champ(0, 0)->maximum_mana)
            base_distinct = true;
    }
    fprintf(g_leak, "recruited base values distinct per party: %s (distinct mirrors used: %d)\n",
            base_distinct ? "yes" : "same recruit text", mirror_count < 4 ? mirror_count : 4);

    /* ---- P4 independence of movement/facing/stamina/mana/HP over all four champions ---- */
    for (int16_t p = 0; p < 2; p++) {
        party_context_activate(p);
        for (int c = 0; c < 4; c++) {
            g_champions[c].current_mana = 100 * p + 10 * c + 5; /* fixture values so crosstalk is visible */
            g_champions[c].cell = c;
            g_champions[c].direction = p == 0 ? 0 : 2;
        }
        g_party_direction = p == 0 ? 0 : 2;
    }
    int16_t mana[2][4], stamina[2][4], health[2][4];
    for (int16_t p = 0; p < 2; p++)
        for (int c = 0; c < 4; c++) {
            mana[p][c] = champ(p, c)->current_mana;
            stamina[p][c] = champ(p, c)->current_stamina;
            health[p][c] = champ(p, c)->current_health;
        }
    bool values_differ = true;
    for (int16_t p = 0; p < 2; p++)
        for (int c = 0; c < 4; c++)
            for (int16_t p2 = 0; p2 < 2; p2++)
                for (int c2 = 0; c2 < 4; c2++)
                    if ((p != p2 || c != c2) && mana[p][c] == mana[p2][c2])
                        values_differ = false;
    check(values_differ, "P4: every champion is identifiable by its own value (2x4 distinct values)");
    party_context_activate(0);
    move_turn_party(COMMAND_TURN_RIGHT); /* original handler */
    bool a_turned = true, b_kept = true;
    for (int c = 0; c < 4; c++) {
        if (champ(0, c)->direction != 1)
            a_turned = false;
        if (champ(1, c)->direction != 2)
            b_kept = false;
    }
    check(a_turned, "P4 positive: A's four champions followed A's turn");
    check(b_kept, "P4 positive: B's four champion facings untouched by A's turn");
    champion_decrement_stamina(3, 7); /* original handler on A champion index 3 */
    check(g_champions[3].current_stamina == stamina[0][3] - 7, "P4 positive: A[3] stamina decremented");
    bool stamina_iso = true;
    for (int c = 0; c < 4; c++)
        if (champ(1, c)->current_stamina != stamina[1][c])
            stamina_iso = false;
    if (g_champions[0].current_stamina != stamina[0][0] || g_champions[1].current_stamina != stamina[0][1] ||
        g_champions[2].current_stamina != stamina[0][2])
        stamina_iso = false;
    check(stamina_iso, "P4 positive/negative: stamina change hit A[3] only (2 parties x 4 champions)");
    bool mana_iso = true;
    for (int16_t p = 0; p < 2; p++)
        for (int c = 0; c < 4; c++)
            if (champ(p, c)->current_mana != mana[p][c])
                mana_iso = false;
    check(mana_iso, "P4 negative: all eight mana values isolated (no tick ran since capture)");

    /* HP isolation through the original damage path (pending damage -> tick apply). */
    party_context_activate(1);
    champion_add_pending_damage(2, 25, WOUND_TORSO, ATTACK_SELF); /* original handler on B champion 2 */
    party_context_activate(0);
    run_ticks(1);
    check(champ(1, 2)->current_health < health[1][2], "P4 positive: B[2] HP dropped via the tick");
    bool hp_iso = true;
    for (int c = 0; c < 4; c++) {
        if (c != 2 && champ(1, c)->current_health != health[1][c])
            hp_iso = false;
        if (champ(0, c)->current_health != health[0][c])
            hp_iso = false;
    }
    check(hp_iso, "P4 negative: HP change hit B[2] only");

    /* Event on champion index 3 of B must not affect A index 3. */
    party_context_activate(0);
    g_champions[3].shield_defense = 11;
    party_context_activate(1);
    g_champions[3].shield_defense = 22;
    TimelineEvent shield = {.map_time = EVENT_MAP_TIME(g_party_map_index, g_game_time + 2),
                            .type = EVENT_CHAMPION_SHIELD_END,
                            .priority = 3,
                            .b = 2};
    timeline_add_event(&shield);
    check(shield.owner == 1, "P4: the champion-3 event is stamped owner B");
    party_context_activate(0);
    run_ticks(4);
    check(champ(1, 3)->shield_defense == 20, "P4 positive: B[3] shield event applied to B[3]");
    check(g_champions[3].shield_defense == 11, "P4 negative: event on B champion index 3 left A[3] untouched");

    /* ---- P1 stale-active occupancy, map context and bounds (parent counterexample class) ---- */
    int16_t ax, ay, adir, bx, by;
    int16_t test_map = mirrors[0].map;
    check(find_test_squares(test_map, mirrors[0].x, mirrors[0].y, &ax, &ay, &adir, &bx, &by),
          "P1 find open test squares");
    party_context_activate(0);
    map_set_current_map(test_map);
    g_party_map_index = test_map;
    g_party_map_x = ax;
    g_party_map_y = ay;
    g_party_direction = adir;
    party_context_activate(1);
    map_set_current_map(test_map);
    g_party_map_index = test_map;
    g_party_map_x = bx;
    g_party_map_y = by;
    party_context_activate(0);
    int16_t ox = g_party_map_x, oy = g_party_map_y;
    int16_t nx = ox, ny = oy;
    map_step(adir, 1, 0, &nx, &ny);
    move_party(COMMAND_MOVE_FORWARD); /* original handler; NO context swap afterwards */
    check(g_party_map_x == nx && g_party_map_y == ny, "P1: A moved forward through the original handler");
    check(party_context_is_on(0, g_party_map_x, g_party_map_y),
          "P1 positive: active occupancy reads live state immediately after the move (no context swap)");
    check(!party_context_is_on(0, ox, oy), "P1 negative: the pre-move square is no longer A's");
    check(party_context_is_on(1, bx, by), "P1 positive: inactive B occupancy reads B's saved state");
    check(!party_context_is_on(1, g_party_map_x, g_party_map_y), "P1 negative: inactive B is not on A's square");
    check(party_context_any_on(nx, ny) && party_context_any_on(bx, by), "P1 positive: any_on sees both parties");
    check(!party_context_is_on(0, -1, -1) && !party_context_is_on(1, -1, -1),
          "P1 negative bounds: (-1,-1) is never occupied");
    check(!party_context_is_on(0, g_current_map_width, g_current_map_height) &&
              !party_context_is_on(1, g_current_map_width, g_current_map_height),
          "P1 negative bounds: outside the map is never occupied");
    if (dungeon_map_count() > 1) {
        int16_t other = (test_map + 1) % dungeon_map_count();
        map_set_current_map(other);
        check(!party_context_is_on(0, nx, ny) && !party_context_is_on(1, bx, by),
              "P1 negative: wrong-map squares are not occupied by parties on another map");
        map_set_current_map(test_map);
    } else {
        check(true, "P1 negative: wrong-map case skipped (single-map dungeon)");
    }
    check(g_party_map_x == nx && g_party_map_y == ny && g_active_party == 0,
          "P1: occupancy checks left the caller's state intact");

    /* ---- P3 target function contract: never leaves the caller's context switched ---- */
    /* B stands on (bx,by) with champions in cells 0..3; A steps back to (ax,ay), two squares away, so only
       B is inside the original distance-1 attack range. */
    party_context_activate(1);
    for (int c = 0; c < 4; c++) {
        g_champions[c].cell = c;
        if (g_champions[c].current_health <= 0)
            g_champions[c].current_health = 100;
    }
    party_context_activate(0);
    g_party_map_x = ax;
    g_party_map_y = ay;
    int16_t victim_party = -1;
    int16_t before_ax = g_party_map_x, before_ay = g_party_map_y;
    int16_t victim = group_get_target_champion(bx, by, 0, &victim_party);
    check(victim >= 0 && victim_party == 1, "P3 positive: target resolution found B's champion (B inactive)");
    check(g_active_party == 0 && g_party_map_x == before_ax && g_party_map_y == before_ay,
          "P3 contract: target resolution left the caller's context and globals intact (hit)");
    int16_t missed = group_get_target_champion(bx + 40, by + 40, 0, &victim_party);
    check(missed == -1, "P3 negative: nobody at a far square");
    check(g_active_party == 0 && g_party_map_x == before_ax && g_party_map_y == before_ay,
          "P3 contract: target resolution left the caller's context and globals intact (miss)");
    party_context_activate(1);
    victim_party = -1;
    victim = group_get_target_champion(bx, by, 1, &victim_party);
    check(victim >= 0 && victim_party == 1 && g_active_party == 1,
          "P3 positive: target resolution finds B when B itself is selected, without switching away");
    /* Documented contract (two-party limitation, audit F06): when BOTH parties are in range the walk order
       is context 0 first; full creature aggro/target priority is not redesigned in this scope. */
    party_context_activate(0);
    g_party_map_x = nx;
    g_party_map_y = ny;
    victim_party = -1;
    victim = group_get_target_champion(bx, by, 0, &victim_party);
    check(victim >= 0 && victim_party == 0,
          "P3 contract (documented limitation): both parties in range resolves context 0 first");
    check(g_active_party == 0, "P3 contract: still no context switch with both parties in range");

    /* ---- P3 cross-map event context + owner restoration ---- */
    party_context_activate(0);
    g_champions[3].shield_defense = 31;
    party_context_activate(1);
    g_champions[3].shield_defense = 42;
    int16_t a_map = g_parties[0].map_index;
    int16_t event_map = (a_map + 1) % dungeon_map_count(); /* the event belongs to neither party's map */
    TimelineEvent cross = {.map_time = EVENT_MAP_TIME(event_map, g_game_time + 2),
                           .type = EVENT_CHAMPION_SHIELD_END,
                           .priority = 3,
                           .b = 2};
    timeline_add_event(&cross);
    check(cross.owner == 1, "P3: the cross-map event is stamped owner B");
    party_context_activate(0); /* selector A before the event expires */
    int16_t sel = g_active_party, sel_map = g_party_map_index, sel_x = g_party_map_x;
    run_ticks(4);
    check(champ(1, 3)->shield_defense == 40, "P3 positive: cross-map owner event applied to B[3]");
    check(g_champions[3].shield_defense == 31, "P3 negative: cross-map owner event left A[3] untouched");
    check(g_active_party == sel && g_party_map_index == sel_map && g_party_map_x == sel_x,
          "P3 restoration: active party and globals restored after the owner event");
    check(g_current_map_index == sel_map,
          "P3 restoration: current map cache restored to the SELECTED party's map after the owner event");

    /* ---- P3 owner followups stay with B after selector A (poison chain) ---- */
    party_context_activate(1);
    int16_t b3_health = g_champions[3].current_health;
    uint8_t b3_poison0 = g_champions[3].poison_event_count;
    party_context_activate(0);
    int16_t a3_health = g_champions[3].current_health;
    party_context_activate(1);
    TimelineEvent poison = {.map_time = EVENT_MAP_TIME(g_party_map_index, g_game_time + 2),
                            .type = EVENT_POISON,
                            .priority = 3,
                            .b = 4};
    timeline_add_event(&poison);
    check(poison.owner == 1, "P3: the poison event is stamped owner B");
    party_context_activate(0);
    run_ticks(40); /* POISON_STEP_TICKS = 36: the first step runs and chains the followup */
    bool chain_owner_ok = true, chain_seen = false;
    for (uint16_t i = 0; i < g_timeline.high_water; i++)
        if (g_timeline.events[i].type == EVENT_POISON) {
            chain_seen = true;
            if (g_timeline.events[i].owner != 1)
                chain_owner_ok = false;
        }
    check(chain_seen && chain_owner_ok,
          "P3 positive: B's poison followup event exists and stays owner B while A is selected");
    check(champ(1, 3)->current_health < b3_health || champ(1, 3)->poison_event_count != b3_poison0,
          "P3 positive: the poison reached B[3] through the original damage/event path");
    check(champ(0, 3)->poison_event_count == 0 && champ(0, 3)->current_health == a3_health,
          "P3 negative: B's poison never touched A[3]");
    run_ticks(60); /* let the remaining chain expire */

    /* ---- P2 bidirectional projectile hits through the original collision/damage path ---- */
    /* Layout: A at (ax,ay,adir), B two steps ahead at (bx,by). Each trial moves B's champion cells. */
    for (int dir_back = 0; dir_back < 2; dir_back++) {
        /* dir_back 0: A shoots at B (B inactive); dir_back 1: B shoots at A (A inactive). */
        int16_t shooter = dir_back == 0 ? 0 : 1;
        int16_t target = 1 - shooter;
        party_context_activate(0);
        g_party_map_x = ax;
        g_party_map_y = ay;
        g_party_direction = adir;
        party_context_activate(1);
        g_party_map_x = bx;
        g_party_map_y = by;
        g_party_direction = (adir + 2) % CELL_COUNT;
        bool hit = false;
        for (int trial = 0; trial < 2 && !hit; trial++) {
            party_context_activate(1);
            g_champions[0].cell = trial * 2;
            g_champions[1].cell = trial * 2 + 1;
            for (int c = 0; c < 4; c++)
                if (g_champions[c].current_health <= 0)
                    g_champions[c].current_health = 100;
            party_context_activate(0);
            g_champions[0].cell = 0;
            g_champions[1].cell = 1;
            for (int c = 0; c < 4; c++) {
                if (g_champions[c].current_health <= 0)
                    g_champions[c].current_health = 100;
                g_champions[c].direction = adir;
            }
            int16_t before_shooter[4], before_target[4];
            for (int c = 0; c < 4; c++) {
                before_shooter[c] = champ(shooter, c)->current_health;
                before_target[c] = champ(target, c)->current_health;
            }
            uint16_t shot = discard_get_unused_thing(THING_WEAPON);
            rec_object_set_type(shot, 0);
            party_context_activate(shooter);
            projectile_shoot_from_champion(0, shot, 200, 200, 10); /* original launch handler */
            run_ticks(16);                                        /* the ACTUAL gameloop route flies it */
            for (int c = 0; c < 4; c++)
                if (champ(target, c)->current_health < before_target[c])
                    hit = true;
            bool shooter_clean = true;
            for (int c = 0; c < 4; c++)
                if (champ(shooter, c)->current_health != before_shooter[c])
                    shooter_clean = false;
            check(shooter_clean,
                  dir_back == 0 ? "P3 negative: A's champions took no damage from A's projectile"
                                : "P3 negative: B's champions took no damage from B's projectile");
        }
        check(hit, dir_back == 0 ? "P3 positive: A->B projectile damaged B through the original impact path"
                                 : "P3 positive: B->A projectile damaged A through the original impact path");
    }

    /* ---- P2 pending damage for B applies while A remains selected (actual route) ---- */
    party_context_activate(1);
    int16_t pend_before = g_champions[1].current_health;
    champion_add_pending_damage(1, 40, WOUND_TORSO, ATTACK_SELF); /* original handler */
    party_context_activate(0);
    int16_t a_before = g_champions[1].current_health;
    run_ticks(1);
    check(champ(1, 1)->current_health < pend_before,
          "P2 positive: B's pending damage applied by the gameloop tick while A stayed selected");
    check(g_champions[1].current_health == a_before, "P2 negative: A untouched by B's pending damage");

    /* ---- P2 per-party tick effects (movement delays) in the actual route ---- */
    party_context_activate(0);
    g_disabled_movement_ticks = 3;
    party_context_activate(1);
    g_projectile_disabled_movement_ticks = 3;
    party_context_activate(0);
    run_ticks(1);
    check(g_parties[0].disabled_movement_ticks == 2 && g_parties[1].projectile_disabled_movement_ticks == 2,
          "P2 positive: movement-delay tick effects ran for BOTH parties in one gameloop tick");

    /* ---- P2 torch burn for both parties once at the original cadence ---- */
    uint16_t torch_a = make_torch(), torch_b = make_torch();
    check(torch_a != THING_NONE && torch_b != THING_NONE, "P2 torch fixture created (original object records)");
    party_context_activate(0);
    g_champions[0].slots[SLOT_HAND_ACTION] = torch_a;
    party_context_activate(1);
    g_champions[0].slots[SLOT_HAND_ACTION] = torch_b;
    /* Freeze life keeps the creatures from interfering with the long cadence runs (both parties). */
    party_context_activate(0);
    g_party.freeze_life_ticks = 255;
    party_context_activate(1);
    g_party.freeze_life_ticks = 255;
    party_context_activate(0);
    while (g_game_time & TORCH_BURN_INTERVAL_MASK)
        run_ticks(1); /* align to the original 512-tick cadence */
    uint16_t charges_a = rec_weapon_charges(g_parties[0].champions[0].slots[SLOT_HAND_ACTION]);
    uint16_t charges_b = rec_weapon_charges(g_parties[1].champions[0].slots[SLOT_HAND_ACTION]);
    run_ticks(512);
    uint16_t after_a = rec_weapon_charges(g_parties[0].champions[0].slots[SLOT_HAND_ACTION]);
    uint16_t after_b = rec_weapon_charges(g_parties[1].champions[0].slots[SLOT_HAND_ACTION]);
    check(charges_a >= 1 && after_a == charges_a - 1,
          "P2 positive: A's torch burned exactly one charge per 512 ticks (original cadence)");
    check(charges_b >= 1 && after_b == charges_b - 1,
          "P2 positive: B's torch burned exactly one charge per 512 ticks (same tick as A)");

    /* ---- P2 explicit per-party sleep cadence ---- */
    party_context_activate(0);
    g_session.party_sleeping = true; /* A sleeps; each party's own flag selects its cadence */
    party_context_activate(1);
    g_session.party_sleeping = false;
    party_context_activate(0);
    while (g_game_time & TIME_EFFECTS_MASK)
        run_ticks(1); /* align to a 64-tick time-effect boundary */
    int16_t food_a0 = party_food_water(0), food_b0 = party_food_water(1);
    run_ticks(TIME_EFFECTS_MASK + 1); /* one full awake interval */
    int16_t delta_a = food_a0 - party_food_water(0), delta_b = food_b0 - party_food_water(1);
    fprintf(g_leak, "sleep cadence: A(sleeping) food+water delta %d, B(awake) delta %d over %d ticks\n", delta_a,
            delta_b, TIME_EFFECTS_MASK + 1);
    check(delta_a > delta_b,
          "P2 positive: sleeping A ran time effects on the sleep cadence, awake B on the normal cadence");
    party_context_activate(0);
    g_session.party_sleeping = false;

    fprintf(g_leak, "final: active party %d, current map %d, time %u\n", g_active_party, g_current_map_index,
            g_game_time);
    printf("\nSUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    fprintf(g_log, "\nSUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    fclose(g_log);
    fclose(g_leak);
    return g_fail ? 1 : 0;
}
