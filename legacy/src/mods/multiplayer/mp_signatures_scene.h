/* mp_signatures_scene.h: the byte patterns a scene is found by.
 *
 * Patterns only. The hulls that use the first eight are in mp_cutscene.c, and where their sites
 * are found is mp_cutscene_sites.c. The two releases a script gives a scene back through, the
 * lock's and the camera's, are hulled in mp_cutscene.c as well and are called at their heads by
 * mp_scene_free.c. The patterns at the end are read by mp_scene_bind.c. The first eight lived in
 * mp_cutscene.c until that file needed the room: patterns move, the tables that use them stay,
 * which is the seam this tree took twice before (mp_cell_sites.c, mp_signatures_enemy.c).
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
/* The bars' target cell, g_boxTarget at 0x008815E0 in fxfade_setLetterbox 0x004396CF, named
 * twice past the letterbox's prologue of seven, so its hull leaves both standing: the operand
 * of the second `cmp [cell],0` at +0x14, and `mov [cell],eax` at +0x4A, the store every call
 * ends in. A savegame writes the cell directly (fxfade_loadDone 0x004397B5), past the hull.
 *
 *   004396CF  55 8B EC 83 7D 08 00   push ebp; mov ebp,esp; cmp dword [ebp+8],0  (the prologue)
 *   004396E1  83 3D E0 15 88 00 00   cmp dword [0x008815E0], 0
 *   00439719  A3 E0 15 88 00         mov [0x008815E0], eax
 */
#define SCENE_BARS_TARGET_OPERAND       0x14u
#define SCENE_BARS_TARGET_STORE         0x4Au
#define SCENE_BARS_TARGET_STORE_OPERAND 0x4Bu
#define SCENE_STORE_EAX_OPCODE          0xA3u

/* The input mode and its setter, out of the menu's own two calls: swmenu_open 0x0045D9F5 asks
 * control_getState 0x00465933 for the mode it remembers, with the call at +0x4F, and
 * swmenu_close 0x0045DB7A hands that mode back through control_setState 0x004658C1, with the
 * call at +0x59. Both callees load the one cell first, `mov eax,[cell]` at +3 and at +4.
 *
 *   0045DA44  E8 EA 7E 00 00         call control_getState
 *   0045DA49  A3 34 68 4B 00         mov [g_savedInputMode], eax
 *   0045DBCC  8B 0D 34 68 4B 00      mov ecx, [g_savedInputMode]
 *   0045DBD3  E8 E9 7C 00 00         call control_setState
 *   00465936  A1 5C 5D 6D 00         mov eax, [0x006D5D5C]       (in control_getState)
 *   004658C5  A1 5C 5D 6D 00         mov eax, [0x006D5D5C]       (in control_setState)
 */
#define SCENE_MENU_GET_MODE_CALL 0x4Fu
#define SCENE_MENU_SET_MODE_CALL 0x59u
#define SCENE_GET_MODE_LOAD      0x03u
#define SCENE_SET_MODE_LOAD      0x04u

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
 * overrideOff; push 5; call release`. Each is proved by the call in front of it. The clearing of
 * the camera's override in front of each returns seven bytes earlier: the push is two bytes and
 * the call of the release five. */
#define SCENE_DOLLY_RELEASE_PAST_THE_TAKE 0x11u
#define SCENE_LOCK_RELEASE_PAST_THE_TAKE  0x1Bu
#define SCENE_CAMERA_OFF_BEFORE_THE_RELEASE 7u

/* The lock's release, Dialog_LeaveInputLock: hulled on a host for the two script ends, and called
 * at its head by the release of what a scene holds. */
extern const uint8_t SIG_SCENE_LOCK_LEAVE[30];
extern const uint8_t MSK_SCENE_LOCK_LEAVE[30];
#define SCENE_LOCK_LEAVE_PROLOGUE 11u

/* The clearing of the camera's override, bapview_overrideOff: fifteen bytes end to end, proved at
 * the address the two script ends name and never searched for. */
extern const uint8_t SIG_SCENE_VIEW_RELEASE[15];
extern const uint8_t MSK_SCENE_VIEW_RELEASE[15];
#define SCENE_VIEW_RELEASE_PROLOGUE 13u

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

/* The table of the fourteen mode descriptors, which the save file turns the mode pointer into an
 * index with. Its address is the operand of a push in the save and of an indexed load in the
 * restore; the two have to name the same table. Matched for the operand, never hulled. */
extern const uint8_t SIG_SCENE_MODE_TABLE_SAVE[34];
extern const uint8_t MSK_SCENE_MODE_TABLE_SAVE[34];
extern const uint8_t SIG_SCENE_MODE_TABLE_RESTORE[27];
extern const uint8_t MSK_SCENE_MODE_TABLE_RESTORE[27];
#define SCENE_MODE_TABLE_SAVE_OPERAND    0x0Au
#define SCENE_MODE_TABLE_RESTORE_OPERAND 0x14u

/* The cheats' hero swap calls the respawn this far past its own head, and the address behind that
 * call is how a respawn it asked for is told apart. */
#define SCENE_HERO_SWAP_RESPAWN_RETURN 0x3Fu

#endif /* MULTIPLAYER_MP_SIGNATURES_SCENE_H */
