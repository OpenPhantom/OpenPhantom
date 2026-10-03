/* mp_scene_scope.h: where a scene's actor stands, read out of the engine.
 *
 * Layer 2, reads only, and the reading guarded: a scene's actor is a live actor of the enemy pool.
 * It is the actor whose script set the scene off, and it stands where its position says. The host
 * is given a place beside it when the place of the player its script meant cannot be stood on
 * (mp_scene_room), so it is read once, at the door, while the actor is certain to be alive: two
 * spawners of the shipped levels remove themselves in the very tick they spawn the hero.
 */
#ifndef MULTIPLAYER_MP_SCENE_SCOPE_H
#define MULTIPLAYER_MP_SCENE_SCOPE_H

#include <stdbool.h>
#include <stdint.h>

/* The position of `actor`. False when it did not read or is not a number. */
bool mp_scene_scope_actor(uintptr_t actor, float at[3]);

#endif /* MULTIPLAYER_MP_SCENE_SCOPE_H */
