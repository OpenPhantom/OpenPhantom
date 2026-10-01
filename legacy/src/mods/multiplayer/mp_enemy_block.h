/* mp_enemy_block.h: an NPC's blade clang, on the machine that only watches.
 *
 * Layer 2. A blocking NPC answers a bolt or a blade with a clang, a spark and a flash at its
 * weapon node, all out of one engine function the contact handler calls. On a client every NPC
 * is a parked replica, and a parked actor's contact handler returns before it reaches that call,
 * so a client saw Qui-Gon block and heard nothing.
 *
 * An event, not a state, and the host decides it. The host's cooldown is the one clock: only a call
 * it let through played anything there, so only that call becomes an event. It travels as a world
 * event of its own (mp_world_event.h), and the replica plays the engine's own function once per
 * event.
 *
 * The client does not decide again. Its own cooldown would hold a second clang inside 0.2 s of
 * the first, on a clock the host never saw, so the playing is handed to mp_sound, which opens the
 * cell before the call and puts back what it held afterwards. This module never links against
 * mp_sound: the two functions it plays through are handed to it when mp_sound installs, and
 * without them nothing is played and every event is counted as having no site.
 *
 * The world events perform it behind the bodies the host's blocks wrote, so the pose the engine
 * builds the weapon node from is the host's.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BLOCK_H
#define MULTIPLAYER_MP_ENEMY_BLOCK_H

#include "mp_enemy_block_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* Whether a clang can be played here, and the playing itself: both from mp_sound. The first is
 * the only answer this module has to that question. The second answers false when it did not
 * reach the engine. Handing them in is also what makes this module the player of the kind. */
typedef bool (*mp_enemy_block_ready_fn_t)(void);
typedef bool (*mp_enemy_block_replay_fn_t)(uintptr_t actor, int32_t kind);
void mp_enemy_block_set_replay(mp_enemy_block_ready_fn_t ready, mp_enemy_block_replay_fn_t replay);

/* On the host, from the clang hull, once for every call in a session with the cooldown's verdict.
 * Posted only for a call the cooldown let through, and only while this side describes its
 * enemies. */
void mp_enemy_block_note(uint32_t actor, int32_t kind, mp_enemy_block_gate_t verdict);

/* Printed always, on both sides. */
void mp_enemy_block_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_BLOCK_H */
