/* mp_player_sound.h: the sounds this machine's player's body makes, and the shield it wears, told
 * to the far side as moments of that body.
 *
 * The engine plays these for the local player alone: the death cry, the burning cry of a death by
 * fire, a pickup, a key on a lock, the plunge into water, the burning ground; and it puts a shield
 * round the body for as long as a shield pickup lasts. On a far machine the same player is a
 * puppet that runs none of the functions that make them, so without a word from here the others
 * saw a death and heard nothing. This half catches each one where the engine makes it and queues
 * it with the player's other moments, a shot or a weapon change, so it travels the same reliable
 * way and in the same order: to the host, which plays it at its puppet and passes it on to every
 * other player (mp_player_sound_play.h is the far side).
 *
 * Where each one is caught, and why there:
 *
 *   the death and the burning cry   the death hull (mp_damage), after the engine's own death, and
 *                                   only for an entry the hull let through: a corpse that is asked
 *                                   into its death again sends nothing twice
 *   a pickup and the shield         the pickup hull (mp_pickup_relay), once the engine's pickup has
 *                                   run and raised the item's taken bit, on the machine the pickup
 *                                   landed on, the claimant's
 *   the key, the water, the ground  the engine's own call of the named sound in each of the three
 *   and the shield's end            functions, repointed, so the moment is exactly the call. The
 *                                   ground's name is the same entry the pain of a hit plays, and it
 *                                   is caught at the ground's own call and nowhere else, so a hit,
 *                                   which the far side voices from the contact itself, is never
 *                                   told twice
 *
 * Nothing is sent without a session, and nothing for a body a bank window has swapped in: the
 * call passes through to the engine and is counted as left to it.
 */
#ifndef MULTIPLAYER_MP_PLAYER_SOUND_H
#define MULTIPLAYER_MP_PLAYER_SOUND_H

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a moment goes: the queue of the local player's moments, which stamps it with the substep
 * and drains it onto the reliable channel with this machine's world slot written in. Handed in
 * rather than linked, so the death and the pickup hulls reach this file without the bridge. */
typedef void (*mp_player_sound_queue_fn_t)(const mp_event_t *moment);

/* Reads the sites (mp_signatures_player_sound.h) and repoints the four calls, each on its own: a
 * call that does not resolve or does not move leaves that one sound untold and says so. Answers
 * whether all four stand. Idempotent. */
bool mp_player_sound_install(mp_player_sound_queue_fn_t queue);

/* The death hull let an entry through and the engine's death has run for this machine's player,
 * with `cause`, on the record at `record`: the death cry goes out with the hero's row and the
 * cause, and for fire the burning cry behind it. */
void mp_player_sound_note_death(int32_t cause, uintptr_t record);

/* The engine's pickup has run for this machine's player and taken an item of `kind`: the pickup's
 * sound goes out, and for the shield pickup the shield's rise with its length. */
void mp_player_sound_note_pickup(int32_t kind);

/* What went out from here, for the report. */
typedef struct mp_player_sound_sent {
    uint32_t sent[MP_PLAYER_SOUND_KIND_MAX + 1u];
    uint32_t left;        /* no session, or not this player's body: the engine's alone */
    uint32_t unread;      /* the record or the timer did not read */
    uint32_t unsited;     /* the name's site did not resolve, so there was nothing to tell */
    uint32_t unqueued;    /* installed without a queue */
    uint32_t redirected;  /* of the four calls */
} mp_player_sound_sent_t;

void mp_player_sound_sent_counts(mp_player_sound_sent_t *out);

#endif /* MULTIPLAYER_MP_PLAYER_SOUND_H */
