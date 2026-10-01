/* character_prop.h: the player's own weapon mesh, drawn at the hand of a borrowed rig.
 *
 * The engine has no weapon model and no attachment. What a hero holds is a mesh node of his own
 * skeleton, and a weapon change is one word in a visibility table. A borrowed rig therefore has
 * nothing to show, and nothing can be hung on it without editing an asset every actor in the level
 * is built from.
 *
 * This module keeps a SECOND render handle bound to the player's OWN model, hides every mesh in it
 * except the equipped weapon, and draws it once per frame with a matrix that puts it in the
 * borrowed rig's hand. Nothing is written into either model.
 */
#ifndef CHARACTER_PROP_H
#define CHARACTER_PROP_H

#include <stdbool.h>
#include <stdint.h>

/* The arithmetic, exported because it is the half that decides where the weapon lands and the
 * only half that can be checked without a game. All three mirror an engine routine exactly:
 * compose is bapmap_matMul3 (0x0047DAD6), point is mat34_transform (0x0047E34D), and the inverse
 * is the one the engine has no routine for. A matrix is twelve floats: three basis rows, then the
 * translation. Row vectors, so `compose(out, parent, local)` reads as "apply local, then
 * parent". */
void  character_prop_mat_compose(float out[12], const float parent[12], const float local[12]);
void  character_prop_mat_point(float out[3], const float point[3], const float matrix[12]);

/* The transpose, with the translation carried back through it. Only correct while the basis is a
 * clean rotation, which is why it answers false above the tolerance rather than returning a matrix
 * that is not an inverse. */
bool  character_prop_mat_invert(float out[12], const float matrix[12]);

/* How far the basis departs from orthonormal, as the worst of the nine row dot products against
 * the identity. Zero for a rotation. */
float character_prop_mat_skew(const float matrix[12]);

/* The engine's own re-parenting factor, and the whole of the placement.
 *
 * `rdThing_buildWorldMatrices` (0x004852B3) builds a node's local transform as
 *
 *     L = translate(node->pivot) * node->restMatrix * translate(-parent->pivot)
 *
 * and then `W = L * W_parent`. The trailing term belongs to the PARENT: it is what carries a point
 * out of the parent's joint space back into the parent mesh's own space, which is the space the
 * parent's world matrix reads. So a weapon subtree moved from the reference rig's `rhand` to the
 * borrowed rig's `rhand` keeps every one of its own terms and exchanges exactly that one factor.
 *
 * `reference_inverse` is the inverse of the reference rig's rest chain to its right hand. Drawing
 * the reference model against `out * H` therefore gives every node under the hand
 *
 *     translate(pivot) * rest * translate(-target_pivot) * H
 *
 * which is the matrix the borrowed rig would build if the weapon node were authored as a child of
 * ITS hand. Nothing else is applied: no anchor, no size rule, no per asset table. The weapon's
 * offset, its angle and the scale the borrowed body is drawn at all ride inside the two matrices
 * the engine already made.
 */
void  character_prop_mat_reparent(float out[12], const float reference_inverse[12],
                                  const float reference_pivot[3], const float target_pivot[3]);

/* The uniform scale a matrix carries, as the length of its first basis row. The engine puts the
 * drawn object's own scale into the matrix it hands the joint builder, so a joint matrix read back
 * out of a render handle is scaled and a mesh radius read beside it is not. */
float character_prop_mat_scale(const float matrix[12]);

/* Grows the sphere `(centre, radius)` until it also contains `(other, other_radius)`.
 *
 * A drawn weapon is several meshes and the engine answers ONE sphere per node, so the meshes that
 * are visible are merged into the sphere that holds them all. Answering for the node the weapon
 * table names alone left the contact point in the fist while the blade did the swinging. */
void  character_prop_sphere_merge(float centre[3], float *radius, const float other[3],
                                  float other_radius);

/* Binds the second render handle to `reference_model`, which is the model the player's own weapon
 * meshes live in, and measures the rest chain from its root to its right hand.
 *
 * `player_record` is the CELL holding the player block pointer, not the block: the block is read
 * again on every frame so a level change cannot leave this module driving a body that is gone.
 * Answers false and logs when either rig cannot carry the feature. */
bool character_prop_arm(uintptr_t player_record, uintptr_t player_obj, uintptr_t reference_model);

void character_prop_disarm(void);
bool character_prop_is_armed(void);

/* Called before the body is rebound to another model, and it has to be, because this module holds
 * NODE INDICES of the model the body is wearing.
 *
 * The borrowed rig carries weapon nodes of its own, and they are hidden on the body's render handle
 * so that only the player's own weapon is drawn. Those are indices into a table rdThing_SetModel
 * sizes by the node count of the model it binds. A rebind replaces the table without replacing the
 * indices, and writing the old rig's slots into the new rig's table is a write past the end of an
 * engine allocation: eleven hero slots into a twenty-one word table put a zero on the pool header
 * of the block behind it, and the process died in the next rdThing_freeArrays.
 *
 * So the words are given back while the rig that owns them is still on the handle, and the indices
 * are forgotten. The next draw resolves them again against whatever the body then wears. */
void character_prop_body_model_changing(void);

/* How many of `count` node slots fall outside a table of `words` words. Exported because it is the
 * bound the crash above was missing, and the only part of it a test can drive without a game. */
uint32_t character_prop_slots_outside(const uint32_t *slots, uint32_t count, uint32_t words);

/* From a detour on bapthing_dispatch, BEFORE the original call. Draws nothing unless `thing` is
 * the player's own render handle, so the weapon is culled, hidden and killed with the body rather
 * than by a rule of ours. `root` is the matrix the caller is about to draw the body with. */
void character_prop_before_thing_draw(const void *thing, const float *root);

/* From a detour on bapobj_nodeSphere, BEFORE the original call. Answers the world sphere of every
 * mesh this module is drawing, for the queries the fire path and the swing make, so that a shot
 * leaves the weapon and a blow lands along it rather than on the borrowed rig's root. False means
 * the caller wanted something else and the original must run. */
bool character_prop_node_sphere(const void *obj, int32_t node_index, float out_centre[3],
                                float *out_radius);

/* The node of the borrowed rig the player's weapon is hanging on, and the one node index this
 * feature will ever put into an engine lookup.
 *
 * It is this module's because this module is what decides where the weapon is drawn, and what
 * fails when the borrowed rig has no right hand. A second owner computing the same index from the
 * same model is how the contact point and the picture come to disagree.
 *
 * False when `obj` is not the body being carried on, when nothing has been placed on it yet, or
 * when the model on that body is no longer the one the hand was resolved against. The last of
 * those is what keeps the answer away from the player spawn, which allocates a fresh body and
 * resolves six node names on it. */
bool character_prop_weapon_node(const void *obj, int32_t *out_node);

/* For the lookup hook, when the engine found `wanted` at `found` on the body being carried on: the
 * node to answer instead, or -1 (MOUNT_NO_NODE) to leave the engine's answer. A node with no mesh,
 * or one under the borrowed rig's own hidden weapon, is replaced by the node this module answers
 * the drawn weapon's sphere for (character_mount_answer_found). Said once per model worn. */
int32_t character_prop_found_node(const void *obj, const char *wanted, int32_t found);

/* The two detours that make any of the above visible: the handle dispatch, which is where the body
 * is drawn and therefore where the weapon has to be drawn with it, and the node sphere, which is
 * where a shot asks the weapon where its barrel is. They live in character_prop_draw.c because this
 * file is at its size limit and because they are the only part of the feature that changes what the
 * engine does rather than what this module computes.
 *
 * False leaves the engine untouched, and then no weapon is offered rather than one drawn without a
 * muzzle to fire from. */
bool character_prop_draw_install(void);
bool character_prop_draw_is_armed(void);

/* A second drawer of a handle of its own, called from the same detour for every object dispatched,
 * inside the lock that makes its own draw pass through once: the placement mode's ghost, which
 * draws beside the player's body. Registered, not called by name, so this file links without it.
 * NULL takes it off. */
typedef void (*character_prop_draw_extra_t)(const void *thing, const float *root);
void character_prop_draw_set_extra(character_prop_draw_extra_t extra);

#endif /* CHARACTER_PROP_H */
