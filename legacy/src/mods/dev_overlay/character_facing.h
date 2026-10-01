/* character_facing.h: the backface drop, taken off the borrowed body for the length of its draw.
 *
 * The engine culls in SOFTWARE, per face, at one place in rdMesh_draw, and the device is told to
 * cull nothing. A face whose normal points away from the camera is dropped unless the ASSET says
 * otherwise: bit 0 of the face's flags word skips the facing test altogether, and that bit is
 * authored per face by whoever built the model.
 *
 * The hero rigs the player wears in the original carry that bit where their own animations expose
 * the open end of a limb shell. Most of the rigs the swap offers do not, because their own clips
 * never reach that far and nobody ever looked at them from where a third person camera looks. Worn
 * and driven by the player's clips, those shells turn their open side to the camera and draw
 * nothing at all, which is what looking through the body is.
 *
 * The bit cannot be fixed where it belongs. It lives in the model, res_Alloc hands models out
 * shared and refcounted, and every actor in the level is built from the same one. So the drop is
 * taken off the DRAW instead: the engine's own global switch is lowered for the length of the one
 * call that draws the borrowed body and put back immediately after. That switch has exactly one
 * reader in the whole image, the model mesh cull, so no wall and no world polygon can notice.
 */
#ifndef CHARACTER_FACING_H
#define CHARACTER_FACING_H

#include <stdbool.h>
#include <stdint.h>

/* Bit 0 of the engine's backface switch: set means a face pointing away from the camera is
 * dropped. The shipped value is 0x103, so clearing the whole byte would also drop two bits this
 * module has no business touching. */
#define CHARACTER_FACING_CULL_BIT 0x01u

/* Nothing has to be written and nothing has to be put back. Deliberately above every byte value,
 * so it cannot be mistaken for one. */
#define CHARACTER_FACING_KEEP     0x100u

/* What the switch has to become so that a face pointing away from the camera is drawn rather than
 * dropped, or CHARACTER_FACING_KEEP when it already says that. Pure, and the only part of this
 * module that can be checked without the game. */
uint32_t character_facing_lower(uint8_t cull_word);

/* Draws the body on `thing` two sided for as long as the translation's row for it stands. The row
 * is character_nodemap's, and it goes with the body: taking the translation down takes this with
 * it, so there is nothing of its own to disarm. Whether the handle still wears what was put on it
 * is asked of the engine again on every draw: a level change frees the handle and the allocator is
 * free to hand the same address back, so a pointer alone would let this outlive the swap.
 *
 * Resolves and hooks on the first call rather than at load. Answers false when it cannot or when
 * the handle has no row, and a false answer is not a reason to refuse the swap: the body is worn
 * either way, it just keeps the engine's drop. */
bool character_facing_arm(uintptr_t thing);

/* Whether any body is drawn two sided. */
bool character_facing_is_armed(void);

#endif /* CHARACTER_FACING_H */
