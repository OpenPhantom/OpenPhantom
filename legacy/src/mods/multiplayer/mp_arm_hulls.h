/* mp_arm_hulls.h: the hulls a session puts on the engine when it is armed.
 *
 * Both ways into a session, the menu and the ini, arm it through one common step, and these are
 * the two runs of hulls that step installs: the hits with the NPCs' bolts and the gate between
 * players, and the world with its enemies. Each run installs in the order it is written, and a new
 * hull of the world or of the enemies belongs in the second one.
 */
#ifndef MULTIPLAYER_MP_ARM_HULLS_H
#define MULTIPLAYER_MP_ARM_HULLS_H

#include "mp_hit_relay_death.h"
#include "multiplayer.h"

#include <stdbool.h>

/* The hit relay with every listener it answers, the NPCs' bolts, and the gate a contact between
 * two players is judged by. `on_death` is where a death goes once it is known on this machine. */
void mp_arm_hulls_hits(bool as_client, mp_hit_relay_death_fn_t on_death);

/* The enemies' hulls and the world's: the node map, the pose, the pool binding, the spawner, the
 * targets, the sounds, the effects, the removals, the range gate, the level's switches, the
 * pickups and the follow. */
void mp_arm_hulls_world(const multiplayer_config_t *config, bool as_client);

#endif /* MULTIPLAYER_MP_ARM_HULLS_H */
