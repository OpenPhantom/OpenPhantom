/* mp_enemy_interest.h: the world the enemy block's interest rule reads, on a host.
 *
 * Layer 3. The rule itself is pure (mp_enemy_interest_rule.h) and the block asks it through two
 * callbacks (mp_enemy_sync_set_interest). This module answers them out of the engine and out of
 * the session:
 *
 *   where a peer's player stands is the row the range gate filled for that peer's bank this
 *   substep, the puppet as the gate measured it, so the gate and the block cannot answer the
 *   question at two places a substep apart;
 *
 *   what an enemy is doing is read off its own actor, the placement record its own pointer names
 *   (the one the entity loop measures its keep test with), its cached target and its bolt.
 *
 * The wake radius has a second source besides the record, and it is named here because it is
 * easy to miss: view_distance_fix redirects the activation scan's call to scale the radius by its
 * NpcRangeScale. The rule scales by the same number the same way, read from that module's section
 * once a session, and only when the call is seen redirected; the unit test holds the arithmetic to
 * that module's own.
 */
#ifndef MULTIPLAYER_MP_ENEMY_INTEREST_H
#define MULTIPLAYER_MP_ENEMY_INTEREST_H

#include <stdbool.h>

/* Hands the enemy block this module's answers for a session over a socket, and takes them back
 * for the loopback. Called where the enemies are made one world. */
void mp_enemy_interest_set_armed(bool armed);

#endif /* MULTIPLAYER_MP_ENEMY_INTEREST_H */
