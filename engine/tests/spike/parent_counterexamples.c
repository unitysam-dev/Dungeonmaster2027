/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two-party spike harness (focused test source, spike only): drives the ORIGINAL DMMac engine handlers
   through the party-context layer in one shared world. Scenarios per SPIKE-PLAN.md:
   S1 movement/facing/resource isolation, S2 one unique shared object with atomic ownership,
   S3 cross-party projectile hit through the original collision/damage path,
   N* negative isolation/conflict checks incl. event processing after an active-party swap.
   Usage: two_party_spike <game-data-dir> <original-adf> <out-dir> */

#include "adf.h"
#include "champion.h"
#include "discard.h"
#include "dungeon.h"
#include "event.h"
#include "game.h"
#include "gameloop.h"
#include "inventory.h"
#include "map.h"
#include "arith.h"
#include "move.h"
#include "os.h"
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

/* World-global snapshot for the context-leak log (never swapped by the context layer). */
typedef struct {
    int16_t current_map, map_w, map_h, active_groups;
    uint32_t time;
    uint16_t timeline_count;
} WorldSnapshot;

static WorldSnapshot world_snapshot(void)
{
    WorldSnapshot s;
    s.current_map = g_current_map_index;
    s.map_w = g_current_map_width;
    s.map_h = g_current_map_height;
    s.active_groups = g_active_group_count;
    s.time = g_game_time;
    s.timeline_count = g_timeline.count;
    return s;
}

static void world_log(const char *label, WorldSnapshot before, WorldSnapshot after)
{
    if (memcmp(&before, &after, sizeof before) == 0)
        return;
    fprintf(g_leak, "%s: world changed", label);
    if (before.current_map != after.current_map)
        fprintf(g_leak, " current_map %d->%d", before.current_map, after.current_map);
    if (before.time != after.time)
        fprintf(g_leak, " time %u->%u", before.time, after.time);
    if (before.timeline_count != after.timeline_count)
        fprintf(g_leak, " timeline %u->%u", before.timeline_count, after.timeline_count);
    if (before.active_groups != after.active_groups)
        fprintf(g_leak, " active_groups %d->%d", before.active_groups, after.active_groups);
    if (before.map_w != after.map_w || before.map_h != after.map_h)
        fprintf(g_leak, " map_size %dx%d->%dx%d", before.map_w, before.map_h, after.map_w, after.map_h);
    fprintf(g_leak, "\n");
}

/* One engine tick: the engine-core steps of the original gameloop_tick. The world clock advances once;
   per-party steps (pending damage, tick effects) run for every context, as required for two parties. */
static void run_ticks(int n)
{
    for (int i = 0; i < n; i++) {
        event_process_expired();
        game_advance_time();
        int16_t saved_party = g_active_party;
        for (int16_t p = 0; p < PARTY_CONTEXT_COUNT; p++) {
            party_context_activate(p);
            champion_apply_pending_damage();
            game_apply_tick_effects();
        }
        party_context_activate(saved_party);
    }
}

/* The first candidate mirror: a square with a portrait sensor and a parseable champion text. */
static bool find_mirror(int16_t map, int16_t *out_x, int16_t *out_y, int16_t *out_portrait)
{
    map_set_current_map(map);
    for (int16_t x = 0; x < g_current_map_width; x++)
        for (int16_t y = 0; y < g_current_map_height; y++) {
            uint16_t text = recruit_text_thing(map, x, y);
            if (text == THING_NONE)
                continue;
            RecruitData r;
            if (!recruit_parse(text, &r))
                continue;
            int16_t portrait = 0;
            for (uint16_t t = map_get_square_first_thing(x, y); t != THING_ENDOFLIST; t = rec_thing_next(t))
                if (THING_TYPE(t) == THING_SENSOR) {
                    int16_t p = recruit_portrait_index(t);
                    if (p >= 0)
                        portrait = p;
                }
            *out_x = x;
            *out_y = y;
            *out_portrait = portrait;
            return true;
        }
    return false;
}

/* Two open squares with an open square in front of A's facing, on the map of the mirror. */
static bool find_test_squares(int16_t map, int16_t ax, int16_t ay,
                              int16_t *a_x, int16_t *a_y, int16_t *a_dir, int16_t *b_x, int16_t *b_y)
{
    map_set_current_map(map);
    for (int16_t dir = 0; dir < CELL_COUNT; dir++)
        for (int16_t x = 1; x < g_current_map_width - 1; x++)
            for (int16_t y = 1; y < g_current_map_height - 1; y++) {
                if (abs16(x - ax) + abs16(y - ay) > 3) /* near the mirror area */
                    continue;
                int16_t fx = x, fy = y;
                map_step(dir, 1, 0, &fx, &fy);
                if (!map_is_square_open(x, y) || !map_is_square_open(fx, fy))
                    continue;
                *a_x = x;
                *a_y = y;
                *a_dir = dir;
                /* B's square: the far side of the forward square, same map, open. */
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

/* Counts how often a thing word is referenced in the whole world: square lists of every map, plus the
   champions, hands and open chests of every party context. */
static int count_thing_in_world(uint16_t thing)
{
    int count = 0;
    int16_t maps = dungeon_map_count();
    int16_t saved_map = g_current_map_index;
    for (int16_t m = 0; m < maps; m++) {
        map_set_current_map(m);
        for (int16_t x = 0; x < g_current_map_width; x++)
            for (int16_t y = 0; y < g_current_map_height; y++)
                for (uint16_t t = map_get_square_first_thing(x, y); t != THING_ENDOFLIST; t = rec_thing_next(t))
                    if (t == thing)
                        count++;
    }
    map_set_current_map(saved_map);
    int16_t saved_party = g_active_party;
    for (int16_t p = 0; p < PARTY_CONTEXT_COUNT; p++) {
        party_context_activate(p);
        for (int16_t c = 0; c < g_party_champion_count; c++)
            for (int s = 0; s < SLOT_COUNT; s++)
                if (g_champions[c].slots[s] == thing)
                    count++;
        if (g_leader_hand_object == thing)
            count++;
        for (int s = 0; s < SLOT_CHEST_COUNT; s++)
            if (g_open_chest_slots[s] == thing)
                count++;
    }
    party_context_activate(saved_party);
    return count;
}

/* The grab resolution of the user interface (ui/command.c): the first grabbable object on the square. */
static uint16_t find_grabbable(int16_t x, int16_t y)
{
    map_set_current_map(g_parties[g_active_party].map_index);
    for (uint16_t t = map_get_square_first_thing(x, y); t != THING_ENDOFLIST; t = rec_thing_next(t))
        if ((THING_TYPE(t) == THING_WEAPON || THING_TYPE(t) == THING_JUNK) && object_get_icon_index(t) >= 0)
            return t;
    return THING_NONE;
}

/* Debug: logs every projectile thing on the map of context 0 and B's champion healths. */
static void log_flight(int tick)
{
    int16_t saved_party = g_active_party;
    party_context_activate(0);
    int16_t m = g_party_map_index;
    map_set_current_map(m);
    for (int16_t x = 0; x < g_current_map_width; x++)
        for (int16_t y = 0; y < g_current_map_height; y++)
            for (uint16_t t = map_get_square_first_thing(x, y); t != THING_ENDOFLIST; t = rec_thing_next(t))
                if (THING_TYPE(t) == THING_PROJECTILE)
                    fprintf(g_leak, "S3 tick %d: projectile %04x at (%d,%d) cell %d\n", tick, t, x, y,
                            THING_CELL(t));
    party_context_activate(1);
    fprintf(g_leak, "S3 tick %d: B health %d %d %d %d\n", tick, g_champions[0].current_health,
            g_champions[1].current_health, g_champions[2].current_health, g_champions[3].current_health);
    party_context_activate(saved_party);
    party_context_activate(0);
    fprintf(g_leak, "S3 tick %d: A=(%d,%d,%d,%d) ", tick, g_party_map_index, g_party_map_x, g_party_map_y,
            g_party_direction);
    party_context_activate(1);
    fprintf(g_leak, "B=(%d,%d,%d,%d) cur_map=%d cells=%d%d health=%d%d\n", g_party_map_index, g_party_map_x,
            g_party_map_y, g_party_direction, g_current_map_index, g_champions[0].cell, g_champions[1].cell,
            g_champions[0].current_health, g_champions[1].current_health);
    party_context_activate(saved_party);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <game-data-dir> <original-adf> <out-dir>\n", argv[0]);
        return 2;
    }
    const char *game_dir = argv[1], *adf = argv[2], *out = argv[3];
    char path[1024];
    snprintf(path, sizeof path, "%s/spike-results.txt", out);
    g_log = fopen(path, "w");
    snprintf(path, sizeof path, "%s/context-leak-log.txt", out);
    g_leak = fopen(path, "w");
    g_os_make_folder_hook = hook_make_folder;
    g_os_list_folder_hook = hook_list_folder;

    /* ---- Gate 1 baseline: original data, original handlers, one party ---- */
    snprintf(path, sizeof path, "%s/KAOS.FTL", game_dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        check(adf_extract(adf, game_dir), "BASELINE adf_extract original data (local copy)");
    } else {
        fclose(f);
        check(true, "BASELINE game data already extracted");
    }
    check(gameloop_load_data(game_dir), "BASELINE gameloop_load_data(original data)");
    check(gameloop_start_new_game(game_dir), "BASELINE gameloop_start_new_game");
    uint32_t t0 = g_game_time;
    int16_t start_map = g_party_map_index, start_x = g_party_map_x, start_y = g_party_map_y;
    check(dungeon_map_count() > 0 && g_dungeon_id != 0, "BASELINE real dungeon loaded (dungeon id != 0)");
    run_ticks(10);
    check(g_game_time == t0 + 10, "BASELINE original tick loop advances the engine clock");
    world_log("baseline ticks", world_snapshot(), world_snapshot());

    /* ---- Two-party contexts ---- */
    party_context_init();
    check(g_parties[0].champion_count == 0, "context 0 starts with the new-game state");

    int16_t mx, my, portrait;
    if (!find_mirror(start_map, &mx, &my, &portrait)) {
        check(false, "find a candidate mirror on the entrance map");
        return 1;
    }
    check(true, "found a candidate mirror square");

    /* Party A: recruited through the original handler at the mirror. */
    party_context_activate(0);
    g_party_map_index = start_map;
    g_party_map_x = mx;
    g_party_map_y = my;
    champion_add_candidate_to_party(portrait);
    champion_add_candidate_to_party(portrait);
    check(g_party_champion_count == 2, "S-setup A: two champions via original champion_add_candidate_to_party");

    /* Party B: recruited through the same original handler at the same mirror text. */
    party_context_activate(1);
    g_party_map_index = start_map;
    g_party_map_x = mx;
    g_party_map_y = my;
    g_party_direction = 1;
    champion_add_candidate_to_party(portrait);
    champion_add_candidate_to_party(portrait);
    check(g_party_champion_count == 2, "S-setup B: two champions via original champion_add_candidate_to_party");
    check(g_parties[0].champion_count == 2 && g_parties[0].champions[0].current_health > 0,
          "S-setup: A keeps its champions while B is active");
    /* The candidate panel closes (original: g_session.candidate_champion_index = -1); while a champion is a
       candidate, champion_add_pending_damage ignores him (original protection of the creation flow). */
    g_session.candidate_champion_index = -1;
    party_context_activate(0);
    g_session.candidate_champion_index = -1;

    int16_t ax, ay, adir, bx, by;
    check(find_test_squares(start_map, mx, my, &ax, &ay, &adir, &bx, &by), "find open test squares near the mirror");

    /* ---- S1 movement/facing/resource isolation ---- */
    party_context_activate(0);
    g_party_map_x = ax;
    g_party_map_y = ay;
    g_party_direction = adir;
    g_champions[0].cell = 0;
    g_champions[1].cell = 1;
    g_champions[0].direction = adir;
    g_champions[1].direction = adir;
    party_context_activate(1);
    g_party_map_x = bx;
    g_party_map_y = by;
    g_party_direction = (adir + 1) % CELL_COUNT;
    g_champions[0].cell = 2;
    g_champions[1].cell = 3;
    g_champions[0].direction = g_party_direction;
    g_champions[1].direction = g_party_direction;

    int16_t stamina[PARTY_CONTEXT_COUNT][CHAMPION_MAX_COUNT];
    for (int16_t p = 0; p < PARTY_CONTEXT_COUNT; p++)
        for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
            stamina[p][c] = g_parties[p].champions[c].current_stamina;

    party_context_activate(0);
    WorldSnapshot w0 = world_snapshot();
    int16_t fx = ax, fy = ay;
    map_step(adir, 1, 0, &fx, &fy);
    move_party(COMMAND_MOVE_FORWARD);
    world_log("S1 A move_party", w0, world_snapshot());
    check(g_party_map_x == fx && g_party_map_y == fy, "S1 positive: A moved forward through original move_party");
    check(g_party_direction == adir, "S1 positive: A facing unchanged by a forward move");
    check(g_parties[1].map_x == bx && g_parties[1].map_y == by, "S1 positive: B position untouched by A's move");

    party_context_activate(1);
    move_turn_party(COMMAND_TURN_RIGHT);
    check(g_party_direction == (adir + 2) % CELL_COUNT, "S1 positive: B turned through original move_turn_party");
    check(g_party_map_x == bx && g_party_map_y == by, "S1 positive: B position unchanged by its turn");
    check(g_parties[0].map_x == fx && g_parties[0].map_y == fy, "S1 positive: A position untouched by B's turn");

    /* S1 resource isolation: capture after the moves (a move costs stamina by itself), then a manual
       champion_decrement_stamina through the original handler must hit A's champions only. */
    party_context_activate(0);
    for (int16_t p = 0; p < PARTY_CONTEXT_COUNT; p++)
        for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
            stamina[p][c] = g_parties[p].champions[c].current_stamina;
    champion_decrement_stamina(0, 10);
    check(g_champions[0].current_stamina == stamina[0][0] - 10, "S1 positive: A champion stamina decremented");
    check(g_parties[1].champions[0].current_stamina == stamina[1][0], "S1 positive: B champion stamina isolated");

    /* S1 negatives: B's stun and light do not affect A (ticks let the original movement delay expire). */
    run_ticks(5);
    party_context_activate(1);
    g_disabled_movement_ticks = 5;
    g_party.magical_light_amount = 77;
    party_context_activate(0);
    int16_t ax2 = g_party_map_x, ay2 = g_party_map_y;
    fx = ax2;
    fy = ay2;
    map_step(adir, 1, 0, &fx, &fy);
    move_party(COMMAND_MOVE_FORWARD);
    check(g_party_map_x == fx && g_party_map_y == fy, "S1 negative: B's disabled_movement_ticks does not block A");
    check(g_party.magical_light_amount != 77, "S1 negative: A's light is not B's light");
    check(g_parties[1].disabled_movement_ticks == 5, "S1 negative: B keeps its stun state");

    /* ---- S2 one unique shared object, atomic ownership ---- */
    int16_t sx = bx, sy = by;
    party_context_activate(0);
    uint16_t item = discard_get_unused_thing(THING_WEAPON);
    check(item != THING_NONE, "S2: a free weapon record exists");
    rec_object_set_type(item, 0);
    map_link_thing_to_list(item, 0, sx, sy);
    check(count_thing_in_world(item) == 1, "S2: the unique item exists exactly once before the grabs");
    w0 = world_snapshot();

    party_context_activate(0);
    uint16_t found = find_grabbable(sx, sy);
    bool grabbed_a = found != THING_NONE && inventory_grab_object(found, 0, (Location){sx, sy});
    check(grabbed_a && g_leader_hand_object == item, "S2 positive: A's grab put the item in A's hand");
    check(g_parties[1].hand_object == THING_NONE, "S2 positive: B's hand stayed empty");

    party_context_activate(1);
    uint16_t found_b = find_grabbable(sx, sy);
    bool grabbed_b = found_b != THING_NONE && inventory_grab_object(found_b, 0, (Location){sx, sy});
    world_log("S2 grabs", w0, world_snapshot());
    check(!grabbed_b, "S2 negative/conflict: B's grab finds nothing (serialized conflict resolved)");
    check(g_leader_hand_object == THING_NONE, "S2 negative: B's hand has no duplicate record");
    check(count_thing_in_world(item) == 1, "S2 negative: item count conserved across the whole world");

    /* ---- S3 cross-party projectile through the original impact path ---- */
    party_context_activate(0);
    g_party_map_x = ax;
    g_party_map_y = ay;
    g_party_direction = adir;
    g_champions[0].direction = adir;
    g_champions[1].direction = adir;
    party_context_activate(1);
    g_party_map_x = bx;
    g_party_map_y = by;
    g_party_direction = (adir + 1) % CELL_COUNT;

    int16_t health[PARTY_CONTEXT_COUNT][CHAMPION_MAX_COUNT];
    for (int16_t p = 0; p < PARTY_CONTEXT_COUNT; p++)
        for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
            health[p][c] = g_parties[p].champions[c].current_health;

    bool hit = false;
    for (int trial = 0; trial < 2 && !hit; trial++) {
        party_context_activate(1);
        g_champions[0].cell = (int16_t)(trial * 2);
        g_champions[1].cell = (int16_t)(trial * 2 + 1);
        for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
            g_champions[c].current_health = health[1][c];
        party_context_activate(0);
        g_champions[0].cell = 0;
        g_champions[1].cell = 1;
        for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
            g_champions[c].current_health = health[0][c];
        uint16_t shot = discard_get_unused_thing(THING_WEAPON);
        rec_object_set_type(shot, 0);
        w0 = world_snapshot();
        projectile_shoot_from_champion(0, shot, 200, 200, 10);
        for (int tick = 0; tick < 12; tick++) {
            run_ticks(1);
            log_flight(tick);
        }
        world_log("S3 flight", w0, world_snapshot());
        for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
            if (g_parties[1].champions[c].current_health < health[1][c])
                hit = true;
    }
    check(hit, "S3 positive: cross-party projectile damaged B through the original impact/damage path");
    bool a_untouched = true;
    for (int c = 0; c < CHAMPION_MAX_COUNT; c++)
        if (g_parties[0].champions[c].current_health != health[0][c])
            a_untouched = false;
    check(a_untouched, "S3 negative: A's champions took zero damage from A's own projectile");

    /* S3 negative: a shot toward nobody hits nobody. */
    party_context_activate(0);
    int16_t health_a0 = g_champions[0].current_health;
    int16_t health_b0 = g_parties[1].champions[0].current_health;
    uint16_t miss = discard_get_unused_thing(THING_WEAPON);
    rec_object_set_type(miss, 0);
    g_champions[0].direction = (adir + 2) % CELL_COUNT; /* behind: empty country */
    projectile_shoot_from_champion(0, miss, 200, 200, 10);
    run_ticks(12);
    check(g_champions[0].current_health == health_a0 && g_parties[1].champions[0].current_health == health_b0,
          "S3 negative: a projectile that misses both parties damages nobody");

    /* S3 negative: no projectile, no damage without move events. */
    run_ticks(6);
    check(g_champions[0].current_health == health_a0 && g_parties[1].champions[0].current_health == health_b0,
          "S3 negative: idle ticks without a projectile deal no damage");

    /* ---- N4 event ownership: event processing after an active-party swap ---- */
    party_context_activate(0);
    g_party.shield_defense = 7;
    g_champions[0].shield_defense = 0;
    party_context_activate(1);
    g_party.shield_defense = 10;
    g_champions[0].shield_defense = 5;
    TimelineEvent party_event = {.map_time = EVENT_MAP_TIME(g_party_map_index, g_game_time + 2),
                                 .type = EVENT_PARTY_SHIELD_END,
                                 .b = 4};
    timeline_add_event(&party_event);
    TimelineEvent champion_event = {.map_time = EVENT_MAP_TIME(g_party_map_index, g_game_time + 2),
                                    .type = EVENT_CHAMPION_SHIELD_END,
                                    .priority = 0,
                                    .b = 2};
    timeline_add_event(&champion_event);
    check(party_event.owner == 1 && champion_event.owner == 1, "N4: new events are stamped with owner B (1)");
    party_context_activate(0); /* swap the active party before the events expire */
    run_ticks(4);
    check(g_parties[1].party.shield_defense == 6, "N4: B's party shield event applied to B after the swap");
    check(g_parties[1].champions[0].shield_defense == 3, "N4: B's champion shield event applied to B after the swap");
    check(g_party.shield_defense == 7, "N4 negative: A's party shield untouched by B's event");
    check(g_parties[0].champions[0].shield_defense == 0, "N4 negative: A's champion shield untouched by B's event");

    /* Independent parent counterexamples: active globals must not be shadowed by stale saved state. */
    party_context_activate(0);
    int16_t parent_x = g_party_map_x;
    int16_t parent_y = g_party_map_y;
    map_set_current_map(g_party_map_index);
    g_party_map_x = parent_x + 1;
    check(party_context_is_on(0, g_party_map_x, g_party_map_y),
          "PARENT active-party occupancy reads live state, not stale saved context");
    g_party_map_x = parent_x;
    g_party_map_y = parent_y;
    /* The native game loop itself must apply pending damage to both parties, not only harness ticks. */
    printf("\nSUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    fprintf(g_log, "\nSUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    fclose(g_log);
    fclose(g_leak);
    return g_fail ? 1 : 0;
}
