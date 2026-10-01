/* mp_enemy_burst.h: an enemy that bursts into its pieces on the host bursts on a client too.
 *
 * Layer 2. A script's death arm bursts a droid's body, `shot_shatterThing`, and asks for its
 * removal in the same tick. A client's replica runs no script and received only the removal, so
 * it vanished where the host's droid flew apart.
 *
 * The burst rides the REMOVAL. It cannot ride the actor's record the way the limb and the emitter
 * do: the record is read at the end of the substep, and by then the actor has left the engine's
 * chain. Nor can the removal wait for it, since the engine frees the actor in the same pass. So the
 * removal note carries the speed in its last byte, and a client performs both in one call, the
 * burst first. That is the shape Unreal gives a dying pawn with TearOff: the cause arrives with
 * the end of the replication, in one update. The Quake 3 shape, an entity that stays in the
 * snapshots for the window of its event, needs a server that still holds the entity, and this
 * engine does not. A note of its own in front of the removal would work on this ordered channel;
 * it was turned down because it is a second message and a second identity check for one outcome,
 * and a burst whose removal never followed would leave an invisible replica standing.
 *
 * The host watches rather than asks. The hull always calls the engine and reads bit 0 of the
 * body's flags before and after: drawn before and hidden after is a burst, whatever the engine's
 * own gates decided. A burst waits in a ring of this substep until the removal of the same body
 * takes it, and the removal hull reads the body before its call and takes the entry only once it
 * knows whether the note went out. Rockets, bolts of the shattering kind and a burning player
 * burst their own bodies too; no removal takes those, and the substep's end counts them by caller.
 *
 * A client bursts only the replica of the note's own life while it is alive, never a corpse it
 * keeps, never an actor that carries the player's body, and only through the trampoline, inside a
 * substep, on the main thread. Before the burst it takes the body's class and shadow away, as the
 * host's death arm does before its own, so nothing invisible with a class stands until a copy's
 * asynchronous removal. The pieces are class 0 on both machines and never travel.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BURST_H
#define MULTIPLAYER_MP_ENEMY_BURST_H

#include "mp_enemy_bind.h"
#include "mp_enemy_burst_rule.h"
#include "mp_events.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolves the site and detours it, on both roles. Called by the enemy relay's install, because
 * the burst is part of a removal. False with a line when the site did not resolve. */
bool mp_enemy_burst_install(void);

/* Whether this side describes the level: only a host keeps a burst for its removal. */
void mp_enemy_burst_set_host(bool host);

/* The substep a burst is stamped with, once per substep. */
void mp_enemy_burst_set_tick(uint32_t substep);

/* On the host, in the removal hull. The speed in quarters to attach to the removal of the actor
 * with this body and these state flags, 0 for none; and afterwards, the entry taken, with whether
 * the removal went out. Both ask the same rule. */
uint8_t mp_enemy_burst_speed_for(uint32_t body, uint32_t actor_flags);
void    mp_enemy_burst_claim(uint32_t body, uint32_t actor_flags, uint32_t key, uint8_t generation,
                             bool travelled);

/* At the end of a substep: the bursts no removal took are dropped, counted by caller. */
void mp_enemy_burst_census_done(void);

/* On a client: the decision for one removal the host sent, from the slot of its replica. The
 * relay calls exactly this. */
mp_enemy_burst_verdict_t mp_enemy_burst_decide_for(mp_enemy_slot_t slot, bool newer,
                                                   uint8_t quarters);

/* On a client: burst the replica before the removal the note asks for. Only for a note with a
 * burst; the actor is the replica of the note's own life, alive. */
void mp_enemy_burst_replica(uintptr_t actor, const mp_event_t *event);

/* On a client: a note with a burst that bursts nothing, and why. Nothing for a note without one. */
typedef enum mp_enemy_burst_unmet {
    MP_ENEMY_BURST_UNMET_NO_REPLICA = 0,
    MP_ENEMY_BURST_UNMET_MOVED_ON,
    MP_ENEMY_BURST_UNMET_KEPT
} mp_enemy_burst_unmet_t;

void mp_enemy_burst_unmet(const mp_event_t *event, mp_enemy_burst_unmet_t why);

/* Drops the ring, counted, on every way out of a level or a session. */
void mp_enemy_burst_reset(void);

/* Both lines, on both sides, bound or not. */
void mp_enemy_burst_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_BURST_H */
