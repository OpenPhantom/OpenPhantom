/* mp_script_sound_rule.c: what happens to a sound an actor's script plays. See the header. */
#include "mp_script_sound_rule.h"

#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_SCRIPT_MUSIC_BANKS <= 8u, "a player is a bit in a byte");
_Static_assert(MP_SCRIPT_MUSIC_HOLD == MP_WORLD_EVENT_WINDOW,
               "a music claim holds as long as an event may still be performed");
_Static_assert(MP_SCRIPT_SOUND_EDGES <= 0x7FFFu, "a row is named by a signed index");

mp_script_sound_class_t mp_script_sound_classify(uint32_t flags)
{
    if ((flags & MP_SCRIPT_SOUND_FLAG_MUSIC_STATE) != 0u) {
        return MP_SCRIPT_SOUND_MUSIC_STATE;
    }
    if ((flags & MP_SCRIPT_SOUND_FLAG_MUSIC_SEQUENCE) != 0u) {
        return MP_SCRIPT_SOUND_MUSIC_SEQUENCE;
    }
    if ((flags & MP_SCRIPT_SOUND_FLAG_LOOP) != 0u) {
        return MP_SCRIPT_SOUND_LOOP;
    }
    return MP_SCRIPT_SOUND_ONCE;
}

mp_script_sound_route_t mp_script_sound_route(bool hosting, bool client_of_started,
                                              bool hosts_life)
{
    if (hosting) {
        return MP_SCRIPT_SOUND_BY_HOST;
    }
    if (mp_script_sound_output_is_the_hosts(client_of_started, hosts_life)) {
        return MP_SCRIPT_SOUND_WITHHELD;
    }
    return MP_SCRIPT_SOUND_TO_ENGINE;
}

bool mp_script_sound_output_is_the_hosts(bool client_of_started, bool hosts_life)
{
    return client_of_started && hosts_life;
}

bool mp_script_sound_host_plays(mp_script_sound_class_t cls, bool meant_for_this_player)
{
    if (cls == MP_SCRIPT_SOUND_MUSIC_STATE || cls == MP_SCRIPT_SOUND_MUSIC_SEQUENCE) {
        return meant_for_this_player;
    }
    return true;
}

/* ==============================================================================================
 * The edge.
 * ============================================================================================ */

/* A row whose last call is further back than the substep before is an edge the next time anyway,
 * so it holds nothing worth keeping. */
static bool stale(const mp_script_sound_edge_row_t *row, uint32_t now)
{
    return !row->used || (uint32_t)(now - row->last_call) > 1u;
}

static int32_t find_row(const mp_script_sound_edges_t *edges, uintptr_t actor, uint16_t call)
{
    int32_t i;

    for (i = 0; i < (int32_t)MP_SCRIPT_SOUND_EDGES; ++i) {
        const mp_script_sound_edge_row_t *row = &edges->row[i];

        if (row->used && row->actor == actor && row->call == call) {
            return i;
        }
    }
    return -1;
}

static int32_t free_row(const mp_script_sound_edges_t *edges, uint32_t now)
{
    int32_t i;

    for (i = 0; i < (int32_t)MP_SCRIPT_SOUND_EDGES; ++i) {
        if (stale(&edges->row[i], now)) {
            return i;
        }
    }
    return -1;
}

mp_script_sound_edge_t mp_script_sound_edge_note(mp_script_sound_edges_t *edges, uintptr_t actor,
                                                 uint16_t call, uint32_t now, int32_t *row)
{
    mp_script_sound_edge_row_t *r;
    int32_t                     at;

    if (row != NULL) {
        *row = -1;
    }
    if (edges == NULL) {
        return MP_SCRIPT_SOUND_EDGE;
    }
    at = find_row(edges, actor, call);
    if (at >= 0 && !stale(&edges->row[at], now)) {
        r = &edges->row[at];
        r->last_call = now;
        if (row != NULL) {
            *row = at;
        }
        if (!r->out || (uint32_t)(now - r->last_out) >= MP_SCRIPT_SOUND_HELD_REPEAT) {
            return MP_SCRIPT_SOUND_AGAIN;
        }
        return MP_SCRIPT_SOUND_HELD;
    }
    if (at < 0) {
        at = free_row(edges, now);
    }
    if (at < 0) {
        ++edges->full;
        return MP_SCRIPT_SOUND_EDGE;
    }
    r = &edges->row[at];
    memset(r, 0, sizeof *r);
    r->used      = true;
    r->actor     = actor;
    r->call      = call;
    r->last_call = now;
    if (row != NULL) {
        *row = at;
    }
    return MP_SCRIPT_SOUND_EDGE;
}

void mp_script_sound_edge_out(mp_script_sound_edges_t *edges, int32_t row, uint32_t now)
{
    if (edges == NULL || row < 0 || row >= (int32_t)MP_SCRIPT_SOUND_EDGES ||
        !edges->row[row].used) {
        return;
    }
    edges->row[row].out      = true;
    edges->row[row].last_out = now;
}

/* ==============================================================================================
 * Music.
 * ============================================================================================ */

static uint8_t bank_bit(uint8_t bank)
{
    return bank < MP_SCRIPT_MUSIC_BANKS ? (uint8_t)(1u << bank) : 0u;
}

uint8_t mp_script_music_meant(const mp_script_music_ask_t *ask, mp_script_music_why_t *why)
{
    mp_script_music_why_t reason;
    uint8_t               meant;

    if (ask == NULL) {
        return 0u;
    }
    if (ask->answered && ask->answered_player && ask->answered_age <= ask->fresh) {
        reason = MP_SCRIPT_MUSIC_FOR_THE_ANSWER;
        meant  = bank_bit(ask->answered_bank);
    } else if (ask->attacked) {
        reason = MP_SCRIPT_MUSIC_FOR_THE_ATTACKER;
        meant  = bank_bit(ask->attacker_bank);
    } else if (!ask->answered) {
        reason = MP_SCRIPT_MUSIC_FOR_EVERYBODY;
        meant  = ask->listeners;
    } else {
        reason = MP_SCRIPT_MUSIC_FOR_THE_NEAREST;
        meant  = bank_bit(ask->nearest);
    }
    if (why != NULL) {
        *why = reason;
    }
    return (uint8_t)((meant | ask->awake_for) & ask->listeners);
}

uint8_t mp_script_music_nearest(const float at[3], const float positions[][3],
                                const bool placed[], size_t count)
{
    uint8_t best = MP_SCRIPT_MUSIC_NO_BANK;
    float   best_distance = 0.0f;
    size_t  i;

    if (at == NULL || positions == NULL || placed == NULL) {
        return MP_SCRIPT_MUSIC_NO_BANK;
    }
    for (i = 0; i < count && i < MP_SCRIPT_MUSIC_BANKS; ++i) {
        float dz;
        float dy;
        float dx;
        float distance;

        if (!placed[i]) {
            continue;
        }
        /* Summed z, y, x, the order the engine's range test sums in, so a tie reads the same. */
        dz       = positions[i][2] - at[2];
        dy       = positions[i][1] - at[1];
        dx       = positions[i][0] - at[0];
        distance = dz * dz + dy * dy + dx * dx;
        if (best == MP_SCRIPT_MUSIC_NO_BANK || distance < best_distance) {
            best          = (uint8_t)i;
            best_distance = distance;
        }
    }
    return best;
}

uint16_t mp_script_music_held(bool claimed, uint16_t call, uint32_t made, uint32_t now)
{
    if (!claimed || (uint32_t)(now - made) >= MP_SCRIPT_MUSIC_HOLD) {
        return 0u;
    }
    return call;
}

/* ==============================================================================================
 * A loop on a replica.
 * ============================================================================================ */

mp_actor_loop_step_t mp_actor_loop_step(bool wanted, uint16_t want, bool started,
                                        uint16_t playing_call, bool alive)
{
    if (!wanted) {
        if (!started) {
            return MP_ACTOR_LOOP_NOTHING;
        }
        return alive ? MP_ACTOR_LOOP_STOP : MP_ACTOR_LOOP_FORGET;
    }
    if (!started || !alive) {
        return MP_ACTOR_LOOP_START;
    }
    return playing_call == want ? MP_ACTOR_LOOP_KEEP : MP_ACTOR_LOOP_REPLACE;
}
