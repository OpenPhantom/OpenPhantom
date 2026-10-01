/* mp_puppet_shot.h: a far player's shot, spawned from the puppet's weapon.
 *
 * A shot arrives as an event, with its muzzle measured in the sender's body frame and its yaw
 * against the sender's heading. It is spawned through the engine's own spawner inside the puppet's
 * window, where the shot hull stamps the puppet's class on the projectile, from the muzzle turned
 * back out by the puppet's pose as it is drawn. Nothing here is kept per far body: the spawner, two
 * counters and one line.
 */
#ifndef MULTIPLAYER_MP_PUPPET_SHOT_H
#define MULTIPLAYER_MP_PUPPET_SHOT_H

#include "mp_events.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolve the shot spawner. False when it did not resolve, which leaves every far shot unspawned
 * and uncounted. */
bool mp_puppet_shot_resolve(void);

/* One far shot from the puppet whose object is `object`, inside that puppet's window. */
void mp_puppet_shot_perform(size_t bank, uint32_t object, const mp_event_t *event);

/* The kinds a far player's shots were spawned as here, one bit per kind. Without it the
 * total says a shot flew and nothing says which, so every rule this feature holds about a
 * kind (what travels, what detonates, what flashes) stands unmeasured against the field. */
uint64_t mp_puppet_shot_kinds(void);

/* Shots spawned, and shots not spawned because the puppet's object did not read. */
uint32_t mp_puppet_shot_spawned(void);
uint32_t mp_puppet_shot_unplaced(void);

#endif /* MULTIPLAYER_MP_PUPPET_SHOT_H */
