/* mp_signatures_scene.h: the byte patterns a scene is found by.
 *
 * Patterns only. The hulls that use the first eight are in mp_cutscene.c, the mirror's hull on the
 * lock's release is in mp_scene_client.c, and the two anchors at the end are read by
 * mp_scene_bind.c. The first eight lived in mp_cutscene.c until the gathering needed room there:
 * patterns move, the tables that use them stay, which is the seam this tree took twice before
 * (mp_cell_sites.c, mp_signatures_enemy.c).
 *
 * The arrays are extern with an explicit size rather than static with an implicit one, which is
 * what moving a pattern out of its table costs. The offline verifier once recognised only the
 * static form and thirty four patterns were outside verification for weeks with nothing reporting
 * it, so every one of these is checked by name against all five shipped images after the move.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_SCENE_H
#define MULTIPLAYER_MP_SIGNATURES_SCENE_H

#include <stdint.h>

/* The five doors of a scene, each hulled by mp_cutscene.c, with the prologue each hull takes. */
extern const uint8_t SIG_SCENE_LOCK_ENTER[20];
extern const uint8_t MSK_SCENE_LOCK_ENTER[20];
extern const uint8_t SIG_SCENE_LETTERBOX[27];
extern const uint8_t MSK_SCENE_LETTERBOX[27];
extern const uint8_t SIG_SCENE_VIEW_OVERRIDE[23];
extern const uint8_t MSK_SCENE_VIEW_OVERRIDE[23];
extern const uint8_t SIG_SCENE_SUSPEND[46];
extern const uint8_t MSK_SCENE_SUSPEND[46];
extern const uint8_t SIG_SCENE_RESUME[27];
extern const uint8_t MSK_SCENE_RESUME[27];

#define SCENE_LOCK_ENTER_PROLOGUE    11u
#define SCENE_LETTERBOX_PROLOGUE     7u
#define SCENE_VIEW_OVERRIDE_PROLOGUE 13u
#define SCENE_SUSPEND_PROLOGUE       8u
#define SCENE_RESUME_PROLOGUE        8u
/* The lock's level cell, named twice past the lock's prologue of eleven, so the hull leaves both
 * standing: the operand of `cmp [cell],0` at +11, and `mov eax,[cell]` at +37. */
#define SCENE_LOCK_LEVEL_OPERAND      0x0Du
#define SCENE_LOCK_LEVEL_LOAD         0x25u
#define SCENE_LOCK_LEVEL_LOAD_OPERAND 0x26u
#define SCENE_LOAD_EAX_OPCODE         0xA1u

/* The three places a script takes the camera, each with the distance from its first byte to the
 * instruction after its call. */
extern const uint8_t SIG_SCENE_DOLLY_TAKE[24];
extern const uint8_t MSK_SCENE_DOLLY_TAKE[24];
extern const uint8_t SIG_SCENE_LOCK_TAKE[24];
extern const uint8_t MSK_SCENE_LOCK_TAKE[24];
extern const uint8_t SIG_SCENE_SPEAK_TAKE[26];
extern const uint8_t MSK_SCENE_SPEAK_TAKE[26];

#define SCENE_DOLLY_TAKE_RETURN 19u
#define SCENE_LOCK_TAKE_RETURN  19u
#define SCENE_SPEAK_TAKE_RETURN 15u

/* Where a script's two releases of the lock return to, past the two camera takes above: the
 * dolly's end `call overrideOff; push 63h; call release` and the lock opcode's end `call
 * overrideOff; push 5; call release`. The mirror proves each by the call in front of it. */
#define SCENE_DOLLY_RELEASE_PAST_THE_TAKE 0x11u
#define SCENE_LOCK_RELEASE_PAST_THE_TAKE  0x1Bu

/* The lock's release, Dialog_LeaveInputLock, the mirror's hull on a client. */
extern const uint8_t SIG_SCENE_LOCK_LEAVE[30];
extern const uint8_t MSK_SCENE_LOCK_LEAVE[30];
#define SCENE_LOCK_LEAVE_PROLOGUE 11u

/* The player task's respawn arms: the fade out it starts, and the cell it reads to know the fade is
 * done. Matched for the call's target and the cell's address, never hulled. */
extern const uint8_t SIG_SCENE_TINT_RESPAWN[96];
extern const uint8_t MSK_SCENE_TINT_RESPAWN[96];
#define SCENE_TINT_CALL         0x0Fu   /* the E8 of fxfade_startTintOpaque */
#define SCENE_TINT_PR_OPERAND   0x19u   /* the player record, a witness for the cell's own rows */
#define SCENE_TINT_DONE_OPERAND 0x59u   /* g_tintExpired, compared with 1 */

/* The three modes the engine's grab takes a player out of for a scene, as player_suspend compares
 * them. Matched for the three descriptors, never hulled; the grab's own head is mp_cutscene's. */
extern const uint8_t SIG_SCENE_SUSPEND_MODES[92];
extern const uint8_t MSK_SCENE_SUSPEND_MODES[92];
#define SCENE_MODES_PR_OPERAND     0x02u
#define SCENE_MODES_SABRE_OPERAND  0x09u
#define SCENE_MODES_PANAKA_OPERAND 0x17u
#define SCENE_MODES_STAND_OPERAND  0x52u

/* The cheats' hero swap calls the respawn this far past its own head, and the address behind that
 * call is how a respawn it asked for is told apart. */
#define SCENE_HERO_SWAP_RESPAWN_RETURN 0x3Fu

#endif /* MULTIPLAYER_MP_SIGNATURES_SCENE_H */
