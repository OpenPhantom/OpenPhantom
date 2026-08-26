/* move_mode.h: what a character's movement mode means for collision, with no engine in it.
 *
 * The engine does not collision test every character. FUN_004362c8, the character movement
 * function, tests bit 0 of the mode at character+0x98 before it does anything else:
 *
 *     004363DF  8B 45 98...   mov  eax,[edx+0x98]      the movement mode
 *     004363E5  83 E0 01      and  eax,1
 *     004363EA  0F 85 ...     jnz  0043650B            skip the ENTIRE collision block
 *
 * The jump lands past the swept test, past the slide resolution, and directly on the code that
 * commits the new position. The allow flag it commits against was already reset to permitted at
 * the top of the step by FUN_00435c67, so a character in such a mode moves wherever its velocity
 * takes it, with nothing consulted and nothing able to object.
 *
 * That is a sound optimisation while its assumption holds. These are the static, seated characters
 * that fill out a street and never go anywhere, so testing them would be work for nothing.
 * Measured in one level: most characters are mode 0 and tested; the mode 3 ones are the seated
 * background characters, and mode 1 was a fixed prop.
 *
 * The assumption is what breaks. A contact from the player adds an impulse to a character's
 * velocity without asking whether that character can be moved safely, so a character that is never
 * collision tested acquires a velocity anyway. It then moves, uncontested, through the box it is
 * sitting on and through the floor under that, a fraction of a unit per push, and keeps whatever
 * position it ends at because the landing path clears the velocity and leaves the position alone.
 *
 * The rule taken is to restore the engine's own invariant rather than to argue with it: a
 * character that is exempt from collision must not carry a velocity, because nothing exists that
 * could stop it. Whether it is right that these characters are exempt is a separate question this
 * does not touch, and one the engine has answered the same way since 1999.
 */
#ifndef GROUND_CLIP_FIX_MOVE_MODE_H
#define GROUND_CLIP_FIX_MOVE_MODE_H

#include <stdbool.h>
#include <stdint.h>

/* Bit 0 of the movement mode. Set means the movement function skips its collision test entirely. */
#define MOVE_MODE_SKIPS_COLLISION_BIT 0x1

/* Below this, in world units per second on any axis, a velocity is treated as nothing rather than
 * as motion. Nothing here depends on the exact value: a character that is genuinely stationary
 * reads exactly zero, and this only has to survive a value that rounding left behind. */
#define MOVE_MODE_VELOCITY_EPSILON 0.0001f

/* Whether the engine will skip its collision test for a character in this mode. */
bool move_mode_skips_collision(int32_t move_mode);

/* Whether this character is about to move with nothing able to stop it: exempt from collision and
 * carrying a velocity that would actually take it somewhere. A NaN component counts as carrying
 * one, because a NaN velocity reaching an untested move is the worst case of all and clearing it
 * is the safe answer. */
bool move_mode_move_is_uncontested(int32_t move_mode, const float velocity[3]);

#endif /* GROUND_CLIP_FIX_MOVE_MODE_H */
