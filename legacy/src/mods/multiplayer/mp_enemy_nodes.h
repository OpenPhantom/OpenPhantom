/* mp_enemy_nodes.h: which nodes and meshes of an enemy's drawn thing are hidden, as the host has
 * them, on the machine that only watches.
 *
 * Layer 2. A script hides and shows nodes of its actor's model: a weapon put away and drawn, the
 * halves of a blade, a limb taken off. The engine keeps two arrays for it on the drawn thing, one
 * flag per node and one per mesh, and saves them as two words each; the enemy record carries them
 * in that form, entries 0 to 31 in the low word and 32 to 63 in the high one, with one presence bit
 * per array in the state word. A replica runs no script, so without them it shows every node the
 * host has hidden. The rule is mp_enemy_nodes_rule.h.
 *
 * An actor that carries a player's body is left out on the host: its nodes are the player's, and
 * the player's own code draws and puts away that weapon. A client never writes such an actor
 * anyway, because the binding refuses it before this is asked.
 */
#ifndef MULTIPLAYER_MP_ENEMY_NODES_H
#define MULTIPLAYER_MP_ENEMY_NODES_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* Forgets what the host last read of every key. Called on a level change. */
void mp_enemy_nodes_reset(void);

/* On the host, from the binding's read of `actor` under `key`, whose body is `body`: the four mask
 * words and their presence bits, or nothing for an actor that carries a player's body or a thing
 * that did not read. */
void mp_enemy_nodes_read(uintptr_t actor, uint32_t body, uint32_t key, mp_enemy_record_t *record);

/* On a client, from the binding's write of the host's `record` onto the replica `actor`. Only the
 * entries that differ from the host's are written. `previous` is not read: the baseline is this
 * side's own entries, which are what a local writer would have moved. */
void mp_enemy_nodes_apply(uintptr_t actor, uint32_t body, uint32_t key,
                          const mp_enemy_record_t *record, const mp_enemy_record_t *previous);

/* The masks as this body has them, and as a record says the host has them, for the replica line.
 * Each answers its own buffer, so both can stand in one line. */
const char *mp_enemy_nodes_describe(uint32_t body);
const char *mp_enemy_nodes_said(const mp_enemy_record_t *record);

/* The director's weapon commands, 8 and 9, as the hand the director's hull asks. On a client of a
 * started session the command of an actor whose life the host describes is withheld, by the one
 * question every script output of a client asks: the host's masks are the one writer of its
 * nodes. Everywhere else the engine's arm runs, and on a host the masks read what it did. */
bool mp_enemy_nodes_director(uintptr_t actor, bool client_of_a_session);

void mp_enemy_nodes_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_NODES_H */
