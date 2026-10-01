/* mp_signatures_foot.c: the byte patterns for the engine's footstep tick.
 *
 * Two sites, and between them they carry both directions of a far player's footfalls: the tick is
 * hulled where the player really runs, to read the locomotion state the engine passes and stores
 * nowhere, and called where the puppet is shown. See mp_footstep.h for why that is one site and
 * not two.
 */
#include "mp_signatures_foot.h"

/* --- 0x00437AC0  footstep_tick ----------------------------------------------------------------
 *
 * `void footstep_tick(i32 locoState, i32 genderFlag, bapThing *pThing)`, __cdecl. The head, and
 * the reason this feature needs both halves of it:
 *
 *     00437AC0  55                 push ebp
 *     00437AC1  8B EC              mov  ebp,esp
 *     00437AC3  83 EC 08           sub  esp,8                 first boundary past five is 6
 *     00437AC6  8B 45 10           mov  eax,[ebp+10h]         pThing
 *     00437AC9  A3 BC4D6C00        mov  [g_footThing],eax     the operand at +10, one anchor
 *     00437ACE  8B 4D 10           mov  ecx,[ebp+10h]
 *     00437AD1  8B 91 E4000000     mov  edx,[ecx+0E4h]        pThing->pFloorPoly
 *     00437AD7  89 15 C84D6C00     mov  [g_floorPoly],edx
 *
 * The two absolute operands are masked because they name cells. The window stops before the
 * compare and its `74 2F`, so no displacement stands in it.
 *
 * The cell the second instruction reads is the whole of why a puppet is silent: it is zero for
 * the life of a session and the tick leaves on it. */
const uint8_t SIG_MP_FOOTSTEP_TICK[29] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x45, 0x10, 0xA3, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x4D, 0x10, 0x8B, 0x91, 0xE4, 0x00, 0x00, 0x00, 0x89,
    0x15, 0x00, 0x00, 0x00, 0x00
};
const uint8_t MSK_MP_FOOTSTEP_TICK[29] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_MP_FOOTSTEP_TICK == sizeof MSK_MP_FOOTSTEP_TICK,
               "the footstep tick pattern and its mask are different lengths");

/* --- 0x00437C87  foot_run, an anchor for the hero cell -----------------------------------------
 *
 * The only footstep handler that asks which hero is walking, and the only reader of that cell
 * this feature needs:
 *
 *     00437C87  55 8B EC           push ebp / mov ebp,esp
 *     00437C8A  83 EC 0C           sub  esp,0Ch
 *     00437C8D  A1 80D58600        mov  eax,[g_currentPlayer]   the operand at +7
 *     00437C92  89 45 FC           mov  [ebp-4],eax
 *     00437C95  C7 45 F8 00000000  mov  [ebp-8],0
 *     00437C9D  8B 4D FC           mov  ecx,[ebp-4]
 *     00437CA0  89 4D F4           mov  [ebp-0Ch],ecx
 *     00437CA3  83 7D F4 03        cmp  [ebp-0Ch],3
 *
 * The window stops before the `77 3E` that follows, so it holds no displacement. The cell picks
 * the plant frames: 9 and 27 for the two jedi, 5 and 19 for panaka and the queen. A far panaka
 * measured against the jedi's frames never steps at all while running, which is why this is an
 * anchor and not a convenience. */
const uint8_t SIG_MP_FOOT_RUN[31] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x89,
    0x45, 0xFC, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x4D, 0xFC,
    0x89, 0x4D, 0xF4, 0x83, 0x7D, 0xF4, 0x03
};
const uint8_t MSK_MP_FOOT_RUN[31] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MP_FOOT_RUN == sizeof MSK_MP_FOOT_RUN,
               "the foot run pattern and its mask are different lengths");
