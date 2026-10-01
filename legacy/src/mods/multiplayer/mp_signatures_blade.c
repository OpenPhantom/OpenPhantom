/* mp_signatures_blade.c: the byte patterns for the two heads a far Jedi's blade is drawn through.
 *
 * One head draws a render handle and one draws the glow cards an object owns, and both read the
 * blade's four vertices out of the mesh of the model the handle draws. A far body's blade is drawn
 * from vertices of its own by pointing that mesh at them for the length of exactly these two calls.
 */
#include "mp_signatures_blade.h"

/* --- 0x00417930  bapthing_dispatch -------------------------------------------------------------
 *
 * `int bapthing_dispatch(rdThing *pThing, f32 *pPose)`, __cdecl, and the answer is the handle's
 * visibility, which the object draw keeps and hands to the glow card pass. The head:
 *
 *     00417930  55                 push ebp
 *     00417931  8B EC              mov  ebp,esp
 *     00417933  51                 push ecx
 *     00417934  83 3D m32 00       cmp  [the render module gate],0    first boundary past five: 11
 *     0041793B  75 04              jne
 *     0041793D  33 C0              xor  eax,eax
 *     0041793F  EB 4E              jmp  the epilogue
 *     00417941  8B 45 08           mov  eax,[ebp+8]                   pThing
 *     00417944  8B 08              mov  ecx,[eax]                     its type
 *
 * The cell is masked because it names data. The whole pattern and its tail past the prologue each
 * match once in the retail image, so the second stage finds the head under another module's jump:
 * the developer overlay hooks the same head when it draws a borrowed weapon. */
const uint8_t SIG_MP_THING_DISPATCH[22] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75,
    0x04, 0x33, 0xC0, 0xEB, 0x4E, 0x8B, 0x45, 0x08, 0x8B, 0x08
};
const uint8_t MSK_MP_THING_DISPATCH[22] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MP_THING_DISPATCH == sizeof MSK_MP_THING_DISPATCH,
               "the handle dispatch pattern and its mask are different lengths");

/* --- 0x00439A54  halo_drawForThing --------------------------------------------------------------
 *
 * `void halo_drawForThing(bapObj *obj)`, __cdecl: every glow card the object owns, each drawn
 * between two vertices of its node's mesh. The head:
 *
 *     00439A54  55                 push ebp
 *     00439A55  8B EC              mov  ebp,esp
 *     00439A57  51                 push ecx
 *     00439A58  83 7D 08 00        cmp  [ebp+8],0                     first boundary past five: 8
 *     00439A5C  75 02              jne
 *     00439A5E  EB 52              jmp  the epilogue
 *     00439A60  8B 45 08           mov  eax,[ebp+8]
 *     00439A63  8B 08              mov  ecx,[eax]                     the object's flag word
 *     00439A65  83 E1 10           and  ecx,10h                       the owns-a-card flag
 *     00439A68  85 C9              test ecx,ecx
 *
 * No absolute operand, so no mask. The pattern and its tail each match once in the retail image. */
const uint8_t SIG_MP_HALO_DRAW_FOR_THING[22] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x75, 0x02, 0xEB, 0x52,
    0x8B, 0x45, 0x08, 0x8B, 0x08, 0x83, 0xE1, 0x10, 0x85, 0xC9
};
