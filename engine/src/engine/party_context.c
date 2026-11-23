/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two-party spike layer: per-party contexts around the original single-party globals. See party_context.h. */

#include "party_context.h"
#include "event.h"
#include "game.h"
#include "inventory.h"
#include "map.h"
#include "move.h"
#include <string.h>

PartyContext g_parties[PARTY_CONTEXT_COUNT];
int16_t g_active_party = 0;
bool g_party_connected[PARTY_CONTEXT_COUNT] = {true, true};
bool party_context_enabled(int16_t index) { return index >= 0 && index < party_context_live_count() && g_party_connected[index]; }
int16_t party_context_nearest(int16_t map, int16_t x, int16_t y)
{
 int16_t saved=g_active_party, best=-1, distance=32767;
 for(int16_t p=0;p<party_context_live_count();p++){
  if(!party_context_enabled(p)) continue;
  party_context_activate(p);
  if(g_party_map_index==map && !g_session.party_dead){
   int d=(x>g_party_map_x?x-g_party_map_x:g_party_map_x-x)+(y>g_party_map_y?y-g_party_map_y:g_party_map_y-y);
   if(d<distance){distance=d;best=p;}
  }
 }
 party_context_activate(saved); return best;
}
int16_t g_event_owner = 0;
/* The number of party contexts in play. 1 preserves the original single-party behaviour of every tick step;
   a two-party host sets 2 (party_context_set_live_count). */
static int16_t g_live_party_count = 1;

static void save_globals(PartyContext *ctx)
{
    ctx->party = g_party;
    ctx->map_x = g_party_map_x;
    ctx->map_y = g_party_map_y;
    ctx->direction = g_party_direction;
    ctx->map_index = g_party_map_index;
    ctx->last_movement_time = g_last_party_movement_time;
    ctx->disabled_movement_ticks = g_disabled_movement_ticks;
    ctx->projectile_disabled_movement_ticks = g_projectile_disabled_movement_ticks;
    ctx->last_projectile_disabled_movement_direction = g_last_projectile_disabled_movement_direction;
    ctx->light_level = g_party_light_level;
    ctx->teleport_count = g_party_teleport_count;
    for (int i = 0; i < CHAMPION_MAX_COUNT; i++)
        ctx->champions[i] = g_champions[i];
    ctx->champion_count = g_party_champion_count;
    ctx->leader_index = g_leader_index;
    ctx->magic_caster_champion_index = g_magic_caster_champion_index;
    ctx->dead_champions = g_dead_champions;
    ctx->sleeping = g_session.party_sleeping;
    ctx->dead = g_session.party_dead;
    ctx->candidate_champion_index = g_session.candidate_champion_index;
    ctx->inventory_champion_index = g_session.inventory_champion_index;
    ctx->hand_object = g_leader_hand_object;
    ctx->empty_handed = g_leader_empty_handed;
}

static void load_globals(const PartyContext *ctx)
{
    g_party = ctx->party;
    g_party_map_x = ctx->map_x;
    g_party_map_y = ctx->map_y;
    g_party_direction = ctx->direction;
    g_party_map_index = ctx->map_index;
    g_last_party_movement_time = ctx->last_movement_time;
    g_disabled_movement_ticks = ctx->disabled_movement_ticks;
    g_projectile_disabled_movement_ticks = ctx->projectile_disabled_movement_ticks;
    g_last_projectile_disabled_movement_direction = ctx->last_projectile_disabled_movement_direction;
    g_party_light_level = ctx->light_level;
    g_party_teleport_count = ctx->teleport_count;
    for (int i = 0; i < CHAMPION_MAX_COUNT; i++)
        g_champions[i] = ctx->champions[i];
    g_party_champion_count = ctx->champion_count;
    g_leader_index = ctx->leader_index;
    g_magic_caster_champion_index = ctx->magic_caster_champion_index;
    g_dead_champions = ctx->dead_champions;
    g_session.party_sleeping = ctx->sleeping;
    g_session.party_dead = ctx->dead;
    g_session.candidate_champion_index = ctx->candidate_champion_index;
    g_session.inventory_champion_index = ctx->inventory_champion_index;
    g_leader_hand_object = ctx->hand_object;
    g_leader_empty_handed = ctx->empty_handed;
}

void party_context_init(void)
{
    save_globals(&g_parties[g_active_party]);
    for (int p = 0; p < PARTY_CONTEXT_COUNT; p++) {
        if (p == g_active_party)
            continue;
        PartyContext *ctx = &g_parties[p];
        memset(ctx, 0, sizeof *ctx);
        ctx->leader_index = -1;
        ctx->magic_caster_champion_index = -1;
        ctx->candidate_champion_index = -1;
        ctx->inventory_champion_index = -1;
        ctx->hand_object = THING_NONE;
        ctx->empty_handed = true;
    }
}

void party_context_set_live_count(int16_t count)
{
    if (count >= 1 && count <= PARTY_CONTEXT_COUNT)
        g_live_party_count = count;
}

int16_t party_context_live_count(void)
{
    return g_live_party_count;
}

void party_context_activate(int16_t index)
{
    if (index < 0 || index >= PARTY_CONTEXT_COUNT) /* a corrupt event owner must not index the contexts */
        return;
    if (index == g_active_party) {
        g_event_owner = index;
        return;
    }
    save_globals(&g_parties[g_active_party]);
    load_globals(&g_parties[index]);
    g_active_party = index;
    g_event_owner = index;
}

int16_t party_context_active(void)
{
    return g_active_party;
}

bool party_context_is_on(int16_t index, int16_t x, int16_t y)
{
    /* Parent counterexample fix: the ACTIVE party's live globals are authoritative. Handlers move the party
       through the globals without re-activating a context, so g_parties[g_active_party] lags behind right
       after movement; only inactive contexts are read from saved state. Bounds/map context: a party is only
       "on" a square of the CURRENT map, and only squares inside the current map have an occupancy. */
    if (!party_context_enabled(index))
        return false;
    if (x < 0 || y < 0 || x >= g_current_map_width || y >= g_current_map_height)
        return false;
    int16_t map, px, py;
    if (index == g_active_party) {
        map = g_party_map_index;
        px = g_party_map_x;
        py = g_party_map_y;
    } else {
        const PartyContext *ctx = &g_parties[index];
        map = ctx->map_index;
        px = ctx->map_x;
        py = ctx->map_y;
    }
    return map == g_current_map_index && px == x && py == y;
}

bool party_context_any_on(int16_t x, int16_t y)
{
    for (int16_t p = 0; p < party_context_live_count(); p++)
        if (party_context_is_on(p, x, y))
            return true;
    return false;
}

bool party_context_event_is_party_scoped(uint8_t type)
{
    switch (type) {
    case EVENT_ENABLE_CHAMPION_ACTION:
    case EVENT_HIDE_DAMAGE_RECEIVED:
    case EVENT_VI_ALTAR_REBIRTH:
    case EVENT_LIGHT:
    case EVENT_INVISIBILITY_END:
    case EVENT_CHAMPION_SHIELD_END:
    case EVENT_THIEVES_EYE_END:
    case EVENT_PARTY_SHIELD_END:
    case EVENT_POISON:
    case EVENT_SPELL_SHIELD_END:
    case EVENT_FIRE_SHIELD_END:
    case EVENT_FOOTPRINTS_END:
    case EVENT_MAGIC_MAP_DAIN_END:
    case EVENT_MAGIC_MAP_ROS_END:
    case EVENT_MAGIC_MAP_KU_END:
    case EVENT_MAGIC_MAP_NETA_END:
        return true;
    default:
        return false;
    }
}
