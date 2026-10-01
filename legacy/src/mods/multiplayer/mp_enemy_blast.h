/* mp_enemy_blast.h: a blast a host's script sets off at a node of its actor, seen on a client.
 *
 * Layer 2. The director's command 19 finds one of three nodes of the actor's body, takes the centre
 * of its sphere and hands it with a pitch to Plr_ExplodeAt, which is cosmetic from end to end:
 * three sound fields, a spark emitter, a flash sprite and the blast's sound at the point, and
 * nothing that hurts. The bosses of two levels use it. A replica runs no script, so a client never
 * saw it.
 *
 * The host hears command 19 as the director's hand for the class and keeps the actor; the hull on
 * Plr_ExplodeAt, the command's only call, then posts a world event at that actor with the point
 * and the pitch, both as the engine had them. A client calls Plr_ExplodeAt itself with the two, so
 * the engine's own start gate decides who hears it. The replica's pose never enters it: the point
 * is the host's.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BLAST_H
#define MULTIPLAYER_MP_ENEMY_BLAST_H

#include <stdbool.h>
#include <stdint.h>

/* The hull on both sides, and the player of the kind on a client. */
bool mp_enemy_blast_install(void);

/* The director's command 19, as the hand the director's hull asks. On a host that describes its
 * enemies the actor is kept for the blast that follows and the engine's arm runs; on a client of a
 * started session the command of an actor whose life the host describes is withheld, by the one
 * question every script output of a client asks, since the host's event is its one source. */
bool mp_enemy_blast_director(uintptr_t actor, bool client_of_a_session);

void mp_enemy_blast_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_BLAST_H */
