/* move_mode.h: what a character's movement mode means for collision, with no engine in it.
 *
 * The engine does not collision test every character. FUN_004362C8, the character movement
 * function, tests bit 0 of the mode at character+0x98 before it does anything else:
 *
 *     004363DF  8B 82 98 00 00 00  mov  eax,[edx+0x98]      the movement mode
 *     004363E5  83 E0 01           and  eax,1
 *     004363EA  0F 85 1B 01 00 00  jnz  0043650B            skip the ENTIRE collision block
 *
 * The jump lands past the swept test, past the slide resolution, and directly on the code that
 * commits the new position. The allow flag it commits against was already reset to permitted at
 * the top of the step by FUN_00435c67, so such a character moves wherever its velocity takes it,
 * with nothing consulted and nothing able to object.
 *
 * ============================ Exempt does NOT mean it should not move =========================
 *
 * This is the correction that matters, and it cost a shipped regression to learn. The first
 * version of this fix read "the engine does not collision test this character" as "this character
 * is static", and cleared the velocity of every exempt character. Two populations are exempt and
 * only one of them is static:
 *
 *   The seated background characters that fill out a street. They never move, so the exemption
 *   costs nothing and clearing a velocity they never have costs nothing either.
 *
 *   Ships, birds and the droids on flying platforms. They are exempt PRECISELY BECAUSE they fly,
 *   and they move by velocity like anything else. Clearing it froze every one of them in place.
 *
 * There is no field that separates the two, and this file no longer pretends there is. What
 * separates the cases is not the character, it is where the velocity came from: a scripted mover
 * sets its own, while contact with the player adds an impulse through the handler in enemy.c. The
 * fix therefore undoes a CONTACT impulse on an exempt character and never touches a velocity the
 * character set for itself. See ground_clip_fix.c.
 */
#ifndef GROUND_CLIP_FIX_MOVE_MODE_H
#define GROUND_CLIP_FIX_MOVE_MODE_H

#include <stdbool.h>
#include <stdint.h>

/* Bit 0 of the movement mode. Set means the movement function skips its collision test entirely,
 * so nothing can stop whatever the velocity does next. */
#define MOVE_MODE_SKIPS_COLLISION_BIT 0x1

/* Below this, in world units per second on any axis, a change in velocity is treated as rounding
 * rather than as a push. */
#define MOVE_MODE_VELOCITY_EPSILON 0.0001f

/* Whether the engine will skip its collision test for a character in this mode. */
bool move_mode_skips_collision(int32_t move_mode);

/* Whether a contact just changed this character's velocity in a way that has to be undone: the
 * character is exempt from collision, so nothing could stop the resulting move, AND the velocity
 * differs from what it held before the contact handler ran.
 *
 * Comparing before against after is what keeps a ship flying. A scripted mover's velocity is the
 * same on both sides of the handler, so there is nothing to undo and nothing is touched. Only a
 * velocity the handler itself changed is seen as a push.
 *
 * A NaN on either side counts as a change, because a NaN velocity on a character nothing will
 * collision test is the worst case available and restoring the known-good value is the safe answer.
 */
bool move_mode_contact_pushed(int32_t move_mode, const float before[3], const float after[3]);

#endif /* GROUND_CLIP_FIX_MOVE_MODE_H */
