/* character_rebind.h: putting another model on a render handle, and the node words that follow.
 *
 * Two bodies are rebound here: the player's own, by the swap in character_model.c, and a far
 * player's, by far_model.c. The rebind itself is the same for both, and so is the reason every
 * step of it is there. What differs is who owns the player block afterwards and what may be
 * written into it, so the node words have one rule per owner.
 *
 * What is NOT here is the player's own weapon half letting go of the outgoing rig: that belongs to
 * the player's body alone, and running it for a far body would take the player's weapon off his
 * own hand every time a far body is dressed. The caller does it, before the rebind.
 */
#ifndef CHARACTER_REBIND_H
#define CHARACTER_REBIND_H

#include "character_model_sites.h"

#include <stdbool.h>
#include <stdint.h>

/* The six skeleton node words a player block caches at spawn, +0x40 to +0x54 in this order: the
 * weapon mount, the chest, the left hand, the blade, the waist and the head. */
#define REBIND_WORDS        6u
#define REBIND_WORD_MOUNT   0u
#define REBIND_WORD_CHEST   1u
#define REBIND_WORD_LHAND   2u
#define REBIND_WORD_SABRE   3u
#define REBIND_WORD_WAIST   4u
#define REBIND_WORD_HEAD    5u

/* Binds `model` on the handle: the pose stamp set to a value the frame counter cannot hold, the
 * puppet set aside while the four per node arrays are released, and the bind, which sizes those
 * arrays for the new rig. False when the handle could not be read or the bind refused; a refused
 * bind has already put the new model's pointer on the handle and left arrays behind, so the caller
 * has to bind something again. */
bool character_rebind_bind(const character_model_sites_t *sites, uintptr_t thing,
                           uintptr_t model);

/* The player's own block, resolved again against the rig that replaced the one they were resolved
 * against. `allow_blade` false keeps the blade word at zero.
 *
 * The left hand word is the one the engine reads for the force push alone, so it is chosen by the
 * same rule the far words choose it by: a node that carries a mesh, and not the hand the weapon
 * hangs on. Answers the node it was given, or -1 when the rig states none and 0 was written
 * instead; `out_push_on_the_hand` says the push had to take the weapon hand after all. */
int32_t character_rebind_local_nodes(uintptr_t block, uintptr_t model, bool allow_blade,
                                     bool *out_push_on_the_hand);

/* The weapon nodes `model` carries of its own, as matrix slots: under the mount named `weapon` and
 * everything whose name is weapon geometry, never the right hand's chain, which is `rhand` or,
 * on a rig that ends at the forearm, `rforarm`. Answers how many were written, at most `max`. */
uint32_t character_rebind_own_weapons(uintptr_t model, uint32_t *slots, uint32_t max);

/* The six words for a far body on `model`, whose own weapon slots are `hidden`: the mount one past
 * the last node, so the engine's hide and show refuse it at their bounds; the chest, the waist and
 * the head by name or 0; the blade 0, which is what the blade guard declines on; and the left hand
 * word, which the engine reads for nothing but the push, on the first of `lhand`, `chest` and
 * `waist` that carries a mesh and is not hidden, neither itself nor through an ancestor, else on
 * the first node of the rig that is so and is not the hand the weapon hangs on, else on that hand.
 * False when no node of the rig will do, and then nothing may be written: the push would leave
 * from a matrix nobody built.
 *
 * `out_push_on_the_hand` is the last of those three, the case where the push starts at the drawn
 * weapon rather than at the fist. It is only meaningful when the answer is true. */
bool character_rebind_far_nodes(uintptr_t model, const uint32_t *hidden, uint32_t hidden_count,
                                int32_t words[REBIND_WORDS], bool *out_push_on_the_hand);

/* Hides the listed slots on the handle while it wears `model`, each measured against that model's
 * own node count. The engine shows a weapon node again by name on every weapon change, so this is
 * done after the rebind and again before every draw. */
void character_rebind_hide(uintptr_t thing, uintptr_t model, const uint32_t *slots,
                           uint32_t count);

/* A node's name on a live model, terminated inside `size`, or false. */
bool character_rebind_node_name(uintptr_t model, int32_t node, char *out, uint32_t size);

#endif /* CHARACTER_REBIND_H */
