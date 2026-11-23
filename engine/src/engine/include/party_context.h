/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM_ENGINE_PARTY_CONTEXT_H
#define DM_ENGINE_PARTY_CONTEXT_H
/* Prefixes: party_context_ */

/* Two-party spike layer: the PARTY-class globals of the original single-party engine live in per-party
   PartyContext records. The engine handlers stay original: party_context_activate installs one context into
   the original globals and saves the previous one back (selected-party compatibility context). WORLD state
   (dungeon records, thing lists, g_timeline, g_game_time) is never copied or swapped. */

#include "champion.h"
#include "party.h"
#include <stdbool.h>
#include <stdint.h>

#define PARTY_CONTEXT_COUNT 2

/* The PARTY-class state that PARTY-WORLD-MAP.md classifies as per-party, plus the two session party flags,
   the candidate/inventory selection and the leader hand (per-player UI state, treated per-party in the spike;
   a real multi-client port must move these to the player session). */
typedef struct {
    Party party;
    int16_t map_x, map_y, direction, map_index;
    int32_t last_movement_time;
    int16_t disabled_movement_ticks, projectile_disabled_movement_ticks;
    int16_t last_projectile_disabled_movement_direction;
    int16_t light_level;
    int16_t teleport_count;
    Champion champions[CHAMPION_MAX_COUNT];
    int16_t champion_count, leader_index, magic_caster_champion_index;
    uint16_t dead_champions;
    bool sleeping, dead;
    int16_t candidate_champion_index, inventory_champion_index;
    uint16_t hand_object;
    bool empty_handed;
} PartyContext;

extern PartyContext g_parties[PARTY_CONTEXT_COUNT];
/* The party whose state the original globals currently hold. */
extern int16_t g_active_party;
extern bool g_party_connected[PARTY_CONTEXT_COUNT];
bool party_context_enabled(int16_t index);
int16_t party_context_nearest(int16_t map, int16_t x, int16_t y);
/* The owner stamped into newly created timeline events (the active party, or the owner of the event being
   processed so that chained events inherit the owner). */
extern int16_t g_event_owner;

/* Snapshots the current globals into context 0 (after game_start_new) and initializes the other contexts. */
void party_context_init(void);
/* Saves the active globals into their context and installs context index. Also sets g_event_owner.
   Contract: index out of range is ignored (a corrupt timeline owner must not index the contexts).
   Every caller that must preserve g_event_owner across a context swap saves and restores it around this. */
void party_context_activate(int16_t index);
int16_t party_context_active(void);
/* The number of party contexts in play (1: original single-party tick behaviour; 2: two-party host). */
void party_context_set_live_count(int16_t count);
int16_t party_context_live_count(void);
/* True when context index stands on square x, y of the current map (map_is_party_on for one context).
   The active context reads the LIVE globals (handlers move the party without re-activating a context);
   inactive contexts read their saved state. Out-of-map squares and out-of-range indexes are never "on". */
bool party_context_is_on(int16_t index, int16_t x, int16_t y);
/* True when at least one context stands on square x, y of the current map. */
bool party_context_any_on(int16_t x, int16_t y);
/* True for timeline event types that touch party or champion state and must run in their owner's context. */
bool party_context_event_is_party_scoped(uint8_t type);

#endif
