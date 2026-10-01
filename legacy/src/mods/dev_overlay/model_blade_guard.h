/* model_blade_guard.h: the one engine assert a borrowed model can reach, and the answer.
 *
 * Resizing the lightsaber asserts that the player's sabre node id is not zero, and that assert
 * ends the process. The site that reaches it, the scripted weapon draw, tests only whether the
 * blade is already out: it has no hero test and it is reached from the script opcode that acts on
 * the player's own body, so wearing a rig without a blade node is not out of its way. A rig with
 * no blade node has no blade mesh to resize, so declining is the correct behaviour rather than a
 * workaround.
 *
 * The second case is the worse one and is invisible from inside the engine: while the player wears
 * somebody else's model, the mesh under the blade belongs to that .baf and not to the player, so a
 * resize lands in an asset he does not own and every actor built from it carries the change for
 * the rest of the level. Switching back does not undo it. The player still has his own blade node
 * there, so the node test cannot see that case and the swap has to be asked.
 *
 * Why this is the only guard. Playing a whole foreign ACTOR would also need guards for a clip
 * table that is too short and an animation slot that is refused, which is a character's problem
 * and not a model's: a model swap keeps the player's own hero, clips and weapons and changes the
 * geometry.
 *
 * It also names the player record. The site it guards carries that address in an operand, which is
 * why the two readers of it, this file and the waterline correction, take it from here rather than
 * from an anchor of their own.
 */
#ifndef MODEL_BLADE_GUARD_H
#define MODEL_BLADE_GUARD_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves the site, reads the player record out of its operand and places the detour. False with
 * a line when either did not resolve, and the model swap then offers nothing: a swap that cannot
 * decline the resize is a swap that ends the process on the first scripted weapon draw. */
bool model_blade_guard_install(void);

/* Whether it is standing. The model swap asks before it offers a row. */
bool model_blade_guard_is_armed(void);

#endif /* MODEL_BLADE_GUARD_H */
