/* menu_scale_sites.c: where every piece of engine code the menu scale patches is, and the
 * evidence for each.
 *
 * The seam: this file answers where the code is and nothing in it decides what to do with the
 * code. It is all pattern bytes and disassembly, the natural half to lift out of a file that had
 * grown to more than twice what this project allows. The offsets and cells the rest of the
 * feature needs are in menu_scale_sites.h.
 *
 * SIZE NOTE: over the 600 line mark. Fifteen sites, each with the disassembly that proves it,
 * and the reads that turn their operands into the engine cells the feature uses; the code is a
 * table and one resolver. Cutting the evidence from the patterns it proves is the one seam there
 * is, and it is the wrong one.
 *
 * ==============================================================================================
 * The sites, and how they are found
 *
 * swrle_blit, retail 0x004616CC. Matched on its own prologue, which is long and distinctive and
 * carries the two canvas immediates inside it:
 *
 *   004616D5  mov  eax,[ebp+0x14]        the destination surface
 *   004616D8  mov  ecx,[eax+0x0C]        its real width
 *   004616DB  mov  [ebp-0x30],ecx
 *   ...                                  and its real height into [ebp-0x50]
 *   004616F9  mov  [ebp-0x30],0x280      both thrown away and hard-coded to 640
 *   00461700  mov  [ebp-0x50],0x1E0      and 480
 *
 * The function reads the surface it is drawing into and then discards what it read. Every later
 * comparison in it reads the two locals, so those two immediates are the whole clip.
 *
 * The origin and scale block, retail 0x0045D69D and 0x0045D7CB. This pattern deliberately matches
 * TWICE and the install requires exactly two, which is a stronger statement than uniqueness: the
 * engine computes the menu origin in exactly two places, swmenu_startup at boot and
 * swmenu_moduleProc's mode-change message. Finding one means a site moved; finding three means the
 * pattern stopped meaning what it says. Both are worth declining over.
 *
 *   +0x00  D9 05 40 A4 86 00   fld  [g_screenW]
 *   +0x06  D8 25 90 88 4A 00   fsub [640.0f]      <- operand at +0x08
 *   +0x0C  D8 35 94 88 4A 00   fdiv [2.0f]
 *   +0x12  E8 .. .. .. ..      call __ftol         <- displacement masked, it differs per site
 *   +0x17  A3 58 FD 6C 00      mov  [g_menuOriginX],eax
 *   +0x1C  D9 05 38 A4 86 00   fld  [g_screenH]
 *   +0x22  D8 25 98 88 4A 00   fsub [480.0f]      <- operand at +0x24
 *   +0x28  D8 35 94 88 4A 00   fdiv [2.0f]
 *   +0x2E  E8 .. .. .. ..      call __ftol         <- masked
 *   +0x33  A3 5C FD 6C 00      mov  [g_menuOriginY],eax
 *   +0x38  D9 05 90 88 4A 00   fld  [640.0f]      <- operand at +0x3A, the g_menuScale numerator
 *   +0x3E  D8 35 40 A4 86 00   fdiv [g_screenW]
 *   +0x44  D9 15 2C 68 4B 00   fst  [g_menuScale]
 *   +0x4A  D8 0D 3C 68 4B 00   fmul [base text size]
 *   +0x50  D9 1D 30 68 4B 00   fstp [g_menuTextScale]
 *
 * The three constants are shared cells the rest of the engine also reads, so the OPERANDS are
 * repointed and the cells are left alone. Writing 640*N into 0x004A8890 would move
 * every other reader of 640.0f in the image. They are matched literally: the constant is the
 * evidence that this is the block, and a build with its pool elsewhere is declined.
 *
 * The seven cells the block reads and writes are the other way round: masked, and read out of
 * the match. The screen size, the origin, g_menuScale, the base text size and g_menuTextScale
 * are everything the feature reads or writes in the engine's data, and the two blocks are
 * required to name the same seven, which is the check that a match is the block and not its
 * shape.
 *
 * swmenu_open, retail 0x0045D9F5. Detoured on an 8 byte prologue, which is three whole
 * instructions: push ebp / mov ebp,esp / mov eax,[g_swMac.pCurrMenu]. The operand of the third is
 * masked; with it masked the old thirteen bytes also matched a dialogue function of the same
 * shape, so the pattern runs on through the early return and the flag the real one sets. The
 * cell is NOT read out of it: the operand sits inside the prologue, and the first DLL to detour
 * the function replaces those bytes with a jump, which the menu art census does before this
 * feature reads anything. The cell comes from the menu stack's pop instead:
 *
 * swmenu_pop, retail 0x0045DB7A, matched from 0x0045DBA2. The one function that clears the cell.
 * Never patched by anything in this tree, and the match sits well past any prologue:
 *
 *   A3 <depth>            mov  [g_swMac.depth],eax       operand at +0x01
 *   83 3D <depth> 01      cmp  [g_swMac.depth],1         operand at +0x07, must be the same cell
 *   0F 8D ..              jge  past the clear
 *   E8 ..                 call swmenu_leaveMenuMode
 *   C7 05 <menu> 0        mov  [g_swMac.pCurrMenu],0     operand at +0x19
 *   83 3D <flag> 01       cmp  [a flag],1
 *   74 0F                 je
 *
 * The depth cell is written and then compared in the same breath, and the two operands have to
 * agree; that agreement tells the pop from another store-and-clear.
 *
 * render_prepareFrame's copy of the focal, retail 0x0041996D. Never patched. It loads the current
 * camera, copies two of its fields elsewhere and then its +0x3C into g_projScale, so one match
 * names both cells the 3-D widget placement reads:
 *
 *   A1 <camera>          mov  eax,[g_currentCamera]     <- operand at +0x01
 *   5E                   pop  esi
 *   8B 48 04 8B 51 08    the camera's own +4, then that record's +8 and +0xC, copied out
 *   89 15 ....
 *   8B 48 04 8B 51 0C
 *   33 C9                xor  ecx,ecx
 *   89 15 ....
 *   8B 40 3C             mov  eax,[eax+0x3C]            the focal in pixels
 *   A3 <projScale>       mov  [g_projScale],eax         <- operand at +0x24
 */
#include "menu_scale_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

menu_engine_cells_t menu_cells;

/* ---------------------------------------------------------------------------------------------
 * swrle_blit
 */
static const uint8_t SIG_RLE_BLIT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x6C, 0x53, 0x56, 0x57,   /* prologue                        */
    0x8B, 0x45, 0x14, 0x8B, 0x48, 0x0C, 0x89, 0x4D, 0xD0,   /* dst width  -> [ebp-0x30]        */
    0x8B, 0x55, 0x14, 0x8B, 0x42, 0x10, 0x89, 0x45, 0xB0,   /* dst height -> [ebp-0x50]        */
    0x8B, 0x4D, 0x10, 0x8B, 0x51, 0x0C, 0x89, 0x55, 0xFC,   /* src width                       */
    0x8B, 0x45, 0x10, 0x8B, 0x48, 0x10, 0x89, 0x4D, 0xDC,   /* src height                      */
    0xC7, 0x45, 0xD0, 0x80, 0x02, 0x00, 0x00,               /* mov [ebp-0x30],0x280            */
    0xC7, 0x45, 0xB0, 0xE0, 0x01, 0x00, 0x00                /* mov [ebp-0x50],0x1E0            */
};

/* ---------------------------------------------------------------------------------------------
 * The origin and scale block, matched twice
 */
static const uint8_t SIG_MENU_ORIGIN[] = {
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,                      /* fld  [g_screenW]               */
    0xD8, 0x25, 0x90, 0x88, 0x4A, 0x00,                      /* fsub [640.0f]                  */
    0xD8, 0x35, 0x94, 0x88, 0x4A, 0x00,                      /* fdiv [2.0f]                    */
    0xE8, 0x00, 0x00, 0x00, 0x00,                            /* call __ftol                    */
    0xA3, 0x00, 0x00, 0x00, 0x00,                            /* mov  [g_menuOriginX],eax       */
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,                      /* fld  [g_screenH]               */
    0xD8, 0x25, 0x98, 0x88, 0x4A, 0x00,                      /* fsub [480.0f]                  */
    0xD8, 0x35, 0x94, 0x88, 0x4A, 0x00,                      /* fdiv [2.0f]                    */
    0xE8, 0x00, 0x00, 0x00, 0x00,                            /* call __ftol                    */
    0xA3, 0x00, 0x00, 0x00, 0x00,                            /* mov  [g_menuOriginY],eax       */
    0xD9, 0x05, 0x90, 0x88, 0x4A, 0x00,                      /* fld  [640.0f]                  */
    0xD8, 0x35, 0x00, 0x00, 0x00, 0x00,                      /* fdiv [g_screenW]               */
    0xD9, 0x15, 0x00, 0x00, 0x00, 0x00,                      /* fst  [g_menuScale]             */
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,                      /* fmul [base text size]          */
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00                       /* fstp [g_menuTextScale]         */
};
static const uint8_t MSK_MENU_ORIGIN[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,                            /* the __ftol displacement        */
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,                            /* and the second one             */
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_MENU_ORIGIN == sizeof MSK_MENU_ORIGIN,
               "the menu origin pattern and its mask are different lengths");

/* The seven cells, as offsets of their operands from the match. The screen width is read twice
 * in the block, and both reads have to agree, as do the two sites. */
#define ORIGIN_SCREEN_WIDTH_OPERAND       0x02u
#define ORIGIN_X_OPERAND                  0x18u
#define ORIGIN_SCREEN_HEIGHT_OPERAND      0x1Eu
#define ORIGIN_Y_OPERAND                  0x34u
#define ORIGIN_SCREEN_WIDTH_AGAIN_OPERAND 0x40u
#define ORIGIN_MENU_SCALE_OPERAND         0x46u
#define ORIGIN_BASE_TEXT_OPERAND          0x4Cu
#define ORIGIN_MENU_TEXT_SCALE_OPERAND    0x52u

/* ---------------------------------------------------------------------------------------------
 * swlistbx_draw, for the two insets it holds its rows in by
 *
 *     0045CD2B  83 C1 06    add ecx,6      x0 = rect.x + 6
 *     0045CD3A  83 C0 03    add eax,3      y  = rect.y + 3
 *
 * Both are canvas units baked into the code, so they stay 6 and 3 while everything around them
 * grows. Against a 16 pixel row a 3 pixel gap is a fifth of a row; against a 36 pixel row it is
 * a twelfth, and the first line ends up touching the box border. Scaling them is two bytes.
 *
 * This site is optional. If it does not resolve the scale still works and the rows are merely
 * held a little tight, which is worth a note in the log and not worth refusing over.
 */
static const uint8_t SIG_LISTBOX_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x54, 0x01, 0x00, 0x00,   /* prologue, 0x154 of locals      */
    0xC7, 0x45, 0xD8, 0x00, 0x00, 0x00, 0x00,               /* mov [ebp-0x28],0               */
    0x8B, 0x45, 0x08, 0x8B, 0x48, 0x30, 0x89, 0x4D, 0xE4,   /* pWidget->pLink                 */
    0x8B, 0x55, 0x08, 0x8B, 0x42, 0x34, 0x89, 0x45, 0xE8    /* pWidget->pData                 */
};

/* ---------------------------------------------------------------------------------------------
 * swmenu_open
 */
static const uint8_t SIG_MENU_OPEN[] = {
    0x55, 0x8B, 0xEC,                                        /* push ebp / mov ebp,esp         */
    0xA1, 0x00, 0x00, 0x00, 0x00,                            /* mov eax,[g_swMac.pCurrMenu]    */
    0x3B, 0x45, 0x08,                                        /* cmp eax,[ebp+8]                */
    0x75, 0x0A,                                              /* jne                            */
    0xB8, 0x01, 0x00, 0x00, 0x00,                            /* mov eax,1: already open        */
    0xE9, 0xF5, 0x00, 0x00, 0x00,                            /* jmp out                        */
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, /* mov [a flag],1              */
    0x6A, 0x00, 0xE8                                         /* push 0 / call                  */
};
static const uint8_t MSK_MENU_OPEN[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MENU_OPEN == sizeof MSK_MENU_OPEN,
               "the swmenu_open pattern and its mask are different lengths");
/* ---------------------------------------------------------------------------------------------
 * swmenu_pop, for the current menu cell
 */
static const uint8_t SIG_MENU_POP[] = {
    0xA3, 0x00, 0x00, 0x00, 0x00,                            /* mov [g_swMac.depth],eax        */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,                /* cmp [g_swMac.depth],1          */
    0x0F, 0x8D, 0x00, 0x00, 0x00, 0x00,                      /* jge                            */
    0xE8, 0x00, 0x00, 0x00, 0x00,                            /* call                           */
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* mov [g_swMac.pCurrMenu],0   */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,                /* cmp [a flag],1                 */
    0x74, 0x0F                                               /* je                             */
};
static const uint8_t MSK_MENU_POP[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_MENU_POP == sizeof MSK_MENU_POP,
               "the swmenu_pop pattern and its mask are different lengths");
#define MENU_POP_DEPTH_STORE_OPERAND   0x01u
#define MENU_POP_DEPTH_COMPARE_OPERAND 0x07u
#define MENU_POP_CURRENT_MENU_OPERAND  0x19u

/* ---------------------------------------------------------------------------------------------
 * render_prepareFrame's copy of the focal
 */
static const uint8_t SIG_PROJECTION_COPY[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                            /* mov eax,[g_currentCamera]      */
    0x5E,                                                    /* pop esi                        */
    0x8B, 0x48, 0x04, 0x8B, 0x51, 0x08,                      /* two fields of the camera's +4  */
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x48, 0x04, 0x8B, 0x51, 0x0C,
    0x33, 0xC9,
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x40, 0x3C,                                        /* mov eax,[eax+0x3C]             */
    0xA3, 0x00, 0x00, 0x00, 0x00                             /* mov [g_projScale],eax          */
};
static const uint8_t MSK_PROJECTION_COPY[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_PROJECTION_COPY == sizeof MSK_PROJECTION_COPY,
               "the projection copy pattern and its mask are different lengths");
#define PROJECTION_COPY_CAMERA_OPERAND 0x01u
#define PROJECTION_COPY_SCALE_OPERAND  0x24u

/* ---------------------------------------------------------------------------------------------
 * swpic_draw, retail 0x0045F950. Detoured on a 7 byte prologue for the four animated previews
 * on the main menu; see the long note by preview_upscale for what it does and why it has to.
 */
static const uint8_t SIG_PIC_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x83, 0x78, 0x0C, 0x00, 0x75, 0x05
};

/* ---------------------------------------------------------------------------------------------
 * xswift_drawMenu, retail 0x00462E51. One call per menu per frame, and the only place that walks a
 * built menu's widgets to draw them, which makes it the point where a rectangle the game has
 * rewritten since the last frame can be caught before anything reads it. Detoured on an 8 byte
 * prologue; the call displacement is masked because it is relative.
 */
static const uint8_t SIG_DRAW_MENU[] = {
    0x55, 0x8B, 0xEC, 0x51,                    /* push ebp / mov ebp,esp / push ecx           */
    0x83, 0x7D, 0x08, 0x00,                    /* cmp [ebp+8],0                               */
    0x75, 0x02, 0xEB, 0x69,                    /* jne +2 / jmp the tail                       */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call swrle_lockDraw, displacement masked    */
    0x85, 0xC0, 0x75, 0x02, 0xEB, 0x5E         /* test eax,eax / jne +2 / jmp the tail        */
};
static const uint8_t MSK_DRAW_MENU[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DRAW_MENU == sizeof MSK_DRAW_MENU,
               "the drawn menu pattern and its mask are different lengths");

/* ---------------------------------------------------------------------------------------------
 * sw3d_rectToViewOffset, retail 0x0045C3D8, matched from 0x0045C3DE.
 *
 * Where a 3-D widget's model is put in the world, from its rectangle:
 *
 *     f = depth * (g_menuScale / 554.256f)
 *     x =  f * ((rect.x + rect.width / 2)      - 320.0f)
 *     z = -f * ((rect.y + rect.height - 5.0f)  - 240.0f)
 *
 * 554.256 is 320 / tan(30 deg), the same lens as the game camera, and it brings the shipped
 * arithmetic out at exactly one world unit per canvas pixel. 320 and 240 are the authored
 * canvas centre.
 *
 * Working the projection through: a canvas pixel px lands at
 *
 *     screenX = W/2 + (px - centre) * g_menuScale * focalPx / C
 *
 * where focalPx is the camera's own focal length and C is the cell above. The shipped game is
 * correct because 640/W times 554.256... over 554.256 is exactly one. What is wanted here is the
 * same identity in scaled canvas units, so C = g_menuScale * focalPx and the centre moves to the
 * middle of the scaled canvas. The model KEEPS its new size, because that comes from g_menuScale
 * straight into rdMatrix_scale and never passes through this function.
 *
 * The focal is NOT a constant and must not be guessed. focalPx is halfWidth / tan(hFOV / 2), so it
 * moves with the resolution AND with the field of view the reader has chosen; at 3840x2160 and 98
 * degrees it is 1669.69, where a fixed 60 degree lens would say 3325. Assuming a lens put every
 * model at half the distance from the centre it should have been. So the cell is refreshed from
 * the live camera on every menu frame instead, which also means it follows the FOV slider while
 * the options screen is open.
 *
 * None of the engine's constants is repointed for this; the function is detoured and the hook
 * computes the placement itself, reading the focal at the instant of the call. An earlier
 * version repointed the 554.256 operand and refreshed the cell once per menu frame, which sampled
 * a lens that had not changed yet, so the hero slid sideways while the field of view slider
 * moved. variable_fov repoints that same operand for its own reason, and with nothing here
 * reading it any more that repoint is harmless and is left to it.
 *
 * The 5.0f inset that stands a model up off the bottom edge of its box scales with the box; the
 * hook applies it in canvas units. The halving stays a halving.
 */
static const uint8_t SIG_SW3D_PROJECT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18,        /* push ebp / mov ebp,esp / sub esp,0x18 */
    0x8B, 0x45, 0x0C, 0xDB, 0x40, 0x08, 0xD8, 0x35, 0x78, 0x88, 0x4A, 0x00,
    0xD9, 0x5D, 0xF4, 0x8B, 0x4D, 0x0C, 0xDB, 0x41, 0x0C, 0xD8, 0x05, 0x7C,
    0x88, 0x4A, 0x00, 0xD9, 0x5D, 0xEC, 0x8B, 0x55, 0x0C, 0xDB, 0x02, 0xD8,
    0x45, 0xF4, 0xD9, 0x5D, 0xE8, 0x8B, 0x45, 0x0C, 0xDB, 0x40, 0x04, 0xD8,
    0x45, 0xEC, 0xD9, 0x5D, 0xF8, 0xD9, 0x45, 0xE8, 0xD8, 0x25, 0x80, 0x88,
    0x4A, 0x00, 0xD9, 0x5D, 0xE8, 0xD9, 0x45, 0xF8, 0xD8, 0x25, 0x84, 0x88,
    0x4A, 0x00, 0xD9, 0x5D, 0xF8, 0xD9, 0x05, 0x2C, 0x68, 0x4B, 0x00, 0xD8,
    0x35, 0x88, 0x88, 0x4A, 0x00, 0xD9, 0x5D, 0xFC
};

/* ---------------------------------------------------------------------------------------------
 * font3d_queryFont, retail 0x0046B780. Detoured on a 10 byte prologue.
 *
 * The font's line height, and the reason menu text sat high in its box at a scaled canvas.
 *
 * swtext_draw centres a line with
 *
 *     v = (rect.y + originY - 2 + (rect.height + lineH) / 2) / screenHeight
 *
 * which is correct only while lineH is the height the glyphs are actually DRAWN at. It is not.
 * Unlike font3d_measureGlyph immediately below it in the same file, which pushes the glyph scale to
 * the renderer first and says so in its own comment, this one is a straight pass-through to a field
 * on the font resource and knows nothing about scale. The shipped game gets away with it because
 * g_menuScale is 640/W there, which makes the drawn height and the raw field agree at every
 * resolution. Multiply g_menuScale by the canvas ratio, as this file does so that menu text grows
 * with the canvas, and they stop agreeing: the glyphs are ratio_y times taller than the number used
 * to place them, and the baseline lands (ratio_y - 1) * lineH / 2 too high.
 *
 * Scaling the answer here rather than patching swtext_draw fixes the left and right aligned cases
 * too, which place their baseline at `rect.y + lineH` and were high by the same reasoning.
 *
 * It is GATED on a menu being open. That gate was not there at first, and its absence was a bug.
 *
 * The reasoning for leaving it out was that swtext_draw and the list box's SWMSG_RESET are the only
 * callers in the image. That came from grepping the decompilation, and the decompilation says of
 * itself, in game/dialog.c, that text_emitRow is "the part that was not reconstructed", the very
 * function that places a row of subtitle text. In game subtitles came out mis-positioned and the
 * cause was invisible to a search of the source, because the calling code is not in the source.
 * They came right the moment the converted artwork was removed, and that proved it was ours.
 *
 * So the answer is only scaled when a menu is actually open. Subtitles, the HUD and anything else
 * the game draws during play get the raw field the engine has always had. The list box case is
 * WANTED: it makes the engine derive its own row height correctly, and the floor it is compared
 * against is scaled to match, see SIG_LISTBOX_FLOOR.
 */
static const uint8_t SIG_QUERY_FONT[] = {
    0x55, 0x8B, 0xEC,                                  /* push ebp / mov ebp,esp                */
    0x83, 0x3D, 0x00, 0x62, 0x6D, 0x00, 0x00,          /* cmp [g_pCurFont],0                    */
    0x75, 0x04, 0x33, 0xC0, 0xEB, 0x11,                /* jne +4 / xor eax,eax / jmp the tail   */
    0xA1, 0x00, 0x62, 0x6D, 0x00,                      /* mov eax,[g_pCurFont]                  */
    0x8B, 0x48, 0x08, 0x51                             /* mov ecx,[eax+8] / push ecx            */
};


/* ---------------------------------------------------------------------------------------------
 * The list box row height floor, inside swlistbx_input's SWMSG_RESET at 0x0045C9A7.
 *
 *     h = font3d_queryFont();
 *     lineHeight = (h <= 16) ? 16 : font3d_queryFont();
 *
 * A row is never shorter than 16, and on this game's fonts the raw field is 9, so the floor is what
 * actually decides every list box in the game. It is 16 AUTHORED pixels, so at a scaled canvas it
 * has to be 16 times the ratio, and both immediates move together: the comparison and the value.
 *
 * WHY NOT CORRECT THIS AFTERWARDS. An earlier version of this file multiplied the row height after
 * the reset had run, which produced the right spacing but left the engine's own snap,
 * `rect.height = numLines * lineHeight + 6`, computed from the SMALLER height. Reset runs on
 * every open, so the box lost a row every time it was opened. Moving the floor instead means the
 * engine derives the row height, the row count and the box height from one consistent number,
 * which is stable across opens and, unlike a correction of ours, is also what the row hit test
 * reads.
 */
static const uint8_t SIG_LISTBOX_FLOOR[] = {
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call font3d_queryFont, displacement masked   */
    0x83, 0xF8, 0x10,                          /* cmp eax,16                    <- the compare */
    0x7E, 0x0A,                                /* jle the floor arm                            */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call font3d_queryFont again, masked          */
    0x89, 0x45, 0xCC,                          /* mov [ebp-0x34],eax                           */
    0xEB, 0x07,                                /* jmp past the floor arm                       */
    0xC7, 0x45, 0xCC, 0x10, 0x00, 0x00, 0x00   /* mov [ebp-0x34],16             <- the value   */
};
static const uint8_t MSK_LISTBOX_FLOOR[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_LISTBOX_FLOOR == sizeof MSK_LISTBOX_FLOOR,
               "the list box row height floor pattern and its mask are different lengths");


/* ---------------------------------------------------------------------------------------------
 * swpic_setWidgetImage, retail 0x0045FC5E. One byte, and it is what lets the save game thumbnails
 * scale with everything else.
 *
 *     if (bCompress == 1) swrle_compressVBuffer(pImage);
 *     else if (pWidget->fontIndex < 2) pWidget->fontIndex += 2;
 *     pWidget->pData = pImage;
 *
 * The four save screens plant their 160x120 thumbnail with bCompress set, which run length encodes
 * it in place and leaves fontIndex below 2, so it is drawn by swrle_blit. That blitter copies one
 * source pixel to one destination pixel and has no scale term, so the thumbnail is the one thing on
 * a scaled canvas that cannot be made bigger, and it sits small in a large frame.
 *
 * Rather than expand the run length stream ourselves, the compression is simply declined: the
 * compare is against an immediate, so changing it to a value the flag never takes sends every
 * caller down the else arm. fontIndex goes to 2, swpic_blit takes the plain surface copy, and the
 * preview upscaler above then handles the thumbnail exactly as it handles the four Bink buttons,
 * because that path is the one it already gates on.
 *
 * WHAT THIS COSTS. Two things, both small and both deliberate. The thumbnail stays uncompressed,
 * which is 38400 bytes for a 160x120 16-bit surface. And an exactly black pixel is a SKIP in the
 * run length format but an ordinary black pixel in a surface copy, so a thumbnail that happened to
 * contain pure black no longer shows the panel through it. For a captured screenshot that is the
 * more faithful of the two.
 *
 * WHAT IT DOES NOT AFFECT. The other caller of this function is swmenu_setWidgetImage, which the
 * game only ever calls with bCompress already 0, for the four main menu previews. Those take the
 * else arm today and are unchanged.
 */
static const uint8_t SIG_SET_WIDGET_IMAGE[] = {
    0x55, 0x8B, 0xEC,                          /* push ebp / mov ebp,esp                        */
    0x83, 0x7D, 0x10, 0x01,                    /* cmp [ebp+0x10],1        <- the flag, at +0x06 */
    0x75, 0x0E,                                /* jne the else arm                              */
    0x8B, 0x45, 0x0C, 0x50,                    /* mov eax,[ebp+0xc] / push eax                  */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call swrle_compressVBuffer, masked            */
    0x83, 0xC4, 0x04, 0xEB, 0x18,              /* add esp,4 / jmp the tail                      */
    0x8B, 0x4D, 0x08, 0x83, 0x79, 0x18, 0x02   /* mov ecx,[ebp+8] / cmp [ecx+0x18],2            */
};
static const uint8_t MSK_SET_WIDGET_IMAGE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SET_WIDGET_IMAGE == sizeof MSK_SET_WIDGET_IMAGE,
               "setWidgetImage pattern and its mask are different lengths");


/* ---------------------------------------------------------------------------------------------
 * swpic_drawCursor, retail 0x0045FD01. The size of the drawn menu pointer.
 *
 *     swrle_getCursor(&x, &y);
 *     texture_drawSprite(pCursorDraw, x, x + 0x20, y, y + 0x20, 0xf0ffffff, 1.0f);
 *
 * A 32 pixel cursor on a 4K screen is about a third of the size it appeared at when the menus were
 * 640x480, and it is the last thing on these screens still drawn at its authored size.
 *
 * Unlike every other menu picture, this one can simply be made BIGGER. It does not go through
 * swrle_blit, the run length blitter with no scale term that made converting the artwork necessary
 * in the first place; it goes through texture_drawSprite, which takes the destination extents as
 * arguments. So the two `+ 0x20` immediates are all of it.
 *
 * ONE RATIO, NOT TWO. Scaling width and height separately would stretch the pointer on a display
 * that is not 4:3, and a stretched arrow reads as a rendering fault rather than as a design. The
 * vertical ratio is used for both, which is the one the text already follows.
 *
 * The ceiling is 127 and it is the INSTRUCTION'S. Both are `add reg,imm8` with a signed byte, so
 * 127 is as large as this can go without moving code: at 3840x2160 the proportional answer would be
 * 144. The difference is not worth relocating a function over, and a clamped cursor is still three
 * times the size it would otherwise have been.
 */
static const uint8_t SIG_DRAW_CURSOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,        /* push ebp / mov ebp,esp / sub esp,0x10        */
    0x8D, 0x45, 0xF8, 0x50,                    /* lea eax,[ebp-8] / push eax                   */
    0x8D, 0x4D, 0xFC, 0x51,                    /* lea ecx,[ebp-4] / push ecx                   */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call swrle_getCursor, displacement masked    */
    0x83, 0xC4, 0x08,                          /* add esp,8                                    */
    0x68, 0x00, 0x00, 0x80, 0x3F,              /* push 1.0f                                    */
    0x68, 0xFF, 0xFF, 0xFF, 0xF0,              /* push 0xf0ffffff                              */
    0x8B, 0x55, 0xF8, 0x83, 0xC2, 0x20,        /* mov edx,[ebp-8] / add edx,32   <- at +0x25   */
    0x89, 0x55, 0xF4,
    0xDB, 0x45, 0xF4, 0x51, 0xD9, 0x1C, 0x24,
    0xDB, 0x45, 0xF8, 0x51, 0xD9, 0x1C, 0x24,
    0x8B, 0x45, 0xFC, 0x83, 0xC0, 0x20         /* mov eax,[ebp-4] / add eax,32   <- at +0x3C   */
};
static const uint8_t MSK_DRAW_CURSOR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DRAW_CURSOR == sizeof MSK_DRAW_CURSOR,
               "the drawn cursor pattern and its mask are different lengths");


/* ---------------------------------------------------------------------------------------------
 * sw3d_draw, retail 0x0045C23B. How BIG a 3-D widget's model is drawn.
 *
 *     pos[0] = pos[1] = pos[2] = g_menuScale;
 *     rdMatrix_scale(mat, pos);
 *
 * A model's size on screen is its world size times focalPx over depth, exactly like anything else
 * in the world, so a wider field of view makes it smaller. That is not a fault in the placement:
 * the hero really is a 3-D object sitting at a fixed distance, and a wider lens really does shrink
 * it. It is still wrong for a menu, where the hero should be the same size whatever lens the
 * reader prefers for the game.
 *
 * Dividing the matrix scale by the lens cancels it, and the reference lens is the one the game
 * would have at its AUTHORED vertical field of view for this canvas: 554.256 is that focal at
 * 640x480, and it scales with the canvas the same way everything else here does. So at the
 * default field of view the model is exactly the size it is today, and at any other it stays
 * that size instead of following the lens.
 *
 * Three reads of the same global, one per axis, all repointed at one cell.
 */
static const uint8_t SIG_SW3D_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44,        /* push ebp / mov ebp,esp / sub esp,0x44        */
    0x8B, 0x45, 0x08, 0x8B, 0x48, 0x34,        /* mov eax,[pWidget] / mov ecx,[eax+0x34]       */
    0x89, 0x4D, 0xBC, 0x8B, 0x55, 0xBC, 0x52,  /* stash it and push it                          */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call sw3d_getLoadState, masked                */
    0x83, 0xC4, 0x04, 0x85, 0xC0               /* add esp,4 / test eax,eax                      */
};
static const uint8_t MSK_SW3D_DRAW[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SW3D_DRAW == sizeof MSK_SW3D_DRAW,
               "the 3-D widget draw pattern and its mask are different lengths");

/* ---------------------------------------------------------------------------------------------
 * swmenu_freeBitmaps, retail 0x0045DF05. Called, never patched.
 *
 *   55 8B EC 51                 push ebp / mov ebp,esp / push ecx
 *   C7 45 FC 00000000           i = 0
 *   EB 09 / 8B 45 FC 83 C0 01   the for loop's increment, jumped over on the first pass
 *   89 45 FC
 *   8B 4D 08 8B 55 FC 3B 51 14  cmp i,pMenu->nBitmaps
 *   7D 4D                       jge out
 *   ... res_markUnused(res,1); res_Free(res); pMenu->apBmpRes[i] = 0
 *
 * The whole body is those three lines, so it drops a screen's bitmap cache and does nothing more.
 * It is safe to call on a screen that is currently open because the engine does exactly that
 * itself: modal_window_stack_push calls it on the outgoing screen when one screen is pushed over
 * another, and the screen underneath goes on being drawn. A picture whose slot is zero is reloaded
 * by name on the next draw, which is where the load hook resamples it to the new canvas.
 *
 * Unique unmasked in both shipped WMAIN.EXE builds. */
static const uint8_t SIG_FREE_BITMAPS[] = {
    0x55, 0x8B, 0xEC, 0x51,                                  /* prologue                       */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,                /* i = 0                          */
    0xEB, 0x09, 0x8B, 0x45, 0xFC, 0x83, 0xC0, 0x01,          /* the increment, jumped over     */
    0x89, 0x45, 0xFC,
    0x8B, 0x4D, 0x08, 0x8B, 0x55, 0xFC, 0x3B, 0x51, 0x14,    /* cmp i,pMenu->nBitmaps          */
    0x7D, 0x4D                                               /* jge out                        */
};

/* ---------------------------------------------------------------------------------------------
 * swmenu_sendWidget, retail 0x00462773. Called, never patched.
 *
 *   55 8B EC 51                 prologue
 *   83 7D 08 00 74 33           a null widget answers 0
 *   8B 45 08 8B 08              type = pWidget->type
 *   6B C9 0C                    imul ecx,ecx,12          the handler table's stride
 *   8B 91 C8 6C 4B 00           mov edx,[ecx+0x004B6CC8] the handler for that type
 *   83 7D FC 00 74 1C           an unhandled type answers 0 as well
 *   ... five arguments pushed, call edx
 *
 * The table address is an absolute operand rather than a relative one, so it is matched rather
 * than masked and the pattern names the table as well as the function. Reading that table is where
 * WIDGET_LISTBOX_TYPE comes from.
 *
 * Unique unmasked in both shipped WMAIN.EXE builds. */
static const uint8_t SIG_SEND_WIDGET[] = {
    0x55, 0x8B, 0xEC, 0x51,                                  /* prologue                       */
    0x83, 0x7D, 0x08, 0x00, 0x74, 0x33,                      /* a null widget answers 0        */
    0x8B, 0x45, 0x08, 0x8B, 0x08,                            /* type = pWidget->type           */
    0x6B, 0xC9, 0x0C,                                        /* imul ecx,ecx,12                */
    0x8B, 0x91, 0xC8, 0x6C, 0x4B, 0x00                       /* the handler for that type      */
};

signature_t menu_scale_sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("swrle_blit", SIG_RLE_BLIT),
    SIGNATURE_ENTRY_DETOUR_MASKED("swmenu_open", SIG_MENU_OPEN, MSK_MENU_OPEN, MENU_OPEN_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("render_prepareFrame focal copy", SIG_PROJECTION_COPY,
                           MSK_PROJECTION_COPY),
    SIGNATURE_ENTRY_MASKED("swmenu_pop", SIG_MENU_POP, MSK_MENU_POP),
    SIGNATURE_ENTRY("swlistbx_draw", SIG_LISTBOX_DRAW),
    SIGNATURE_ENTRY_DETOUR("swpic_draw", SIG_PIC_DRAW, PIC_DRAW_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("xswift_drawMenu", SIG_DRAW_MENU, MSK_DRAW_MENU,
                                  DRAW_MENU_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("sw3d_rectToViewOffset", SIG_SW3D_PROJECT, SW3D_PROJECT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("font3d_queryFont", SIG_QUERY_FONT, QUERY_FONT_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("swlistbx_input row floor", SIG_LISTBOX_FLOOR, MSK_LISTBOX_FLOOR),
    SIGNATURE_ENTRY_MASKED("swpic_setWidgetImage", SIG_SET_WIDGET_IMAGE, MSK_SET_WIDGET_IMAGE),
    SIGNATURE_ENTRY_MASKED("swpic_drawCursor", SIG_DRAW_CURSOR, MSK_DRAW_CURSOR),
    SIGNATURE_ENTRY_DETOUR_MASKED("sw3d_draw", SIG_SW3D_DRAW, MSK_SW3D_DRAW, SW3D_DRAW_PROLOGUE),
    SIGNATURE_ENTRY("swmenu_freeBitmaps", SIG_FREE_BITMAPS),
    SIGNATURE_ENTRY("swmenu_sendWidget", SIG_SEND_WIDGET)
};

/* Exactly two matches are expected here; see the note above the pattern for why one or three is
 * worth declining over. */
size_t menu_scale_find_origin_sites(uintptr_t *addresses, size_t max_addresses)
{
    return signature_count_matches(SIG_MENU_ORIGIN, MSK_MENU_ORIGIN, sizeof SIG_MENU_ORIGIN,
                                   addresses, max_addresses);
}

/* One operand, checked to name a cell inside the image, and checked against the value an earlier
 * read of the same cell produced when there was one. */
static bool read_cell(uintptr_t site, size_t operand, size_t size, uint32_t *cell,
                      const char *what)
{
    uint32_t value = 0;

    if (!memory_read_u32(site + operand, &value) || !memory_is_inside_image(value, size)) {
        log_warning("the %s operand at %08X names %08X, which is not inside the image, so the "
                    "menus are left at their authored size", what, (unsigned)(site + operand),
                    (unsigned)value);
        return false;
    }
    if (*cell != 0 && *cell != value) {
        log_warning("the %s is %08X at one site and %08X at another, so this is not the block "
                    "expected and the menus are left at their authored size", what,
                    (unsigned)*cell, (unsigned)value);
        return false;
    }
    *cell = value;
    return true;
}

bool menu_scale_resolve_cells(const uintptr_t *origin_sites, size_t origin_count)
{
    uint32_t  screen_width = 0;
    uint32_t  screen_height = 0;
    uint32_t  origin_x = 0;
    uint32_t  origin_y = 0;
    uint32_t  menu_scale = 0;
    uint32_t  base_text = 0;
    uint32_t  menu_text_scale = 0;
    uint32_t  current_menu = 0;
    uint32_t  depth = 0;
    uint32_t  camera = 0;
    uint32_t  proj_scale = 0;
    uintptr_t pop  = menu_scale_sites[SITE_MENU_POP].address;
    uintptr_t copy = menu_scale_sites[SITE_PROJECTION_COPY].address;
    size_t    index;

    for (index = 0; index < origin_count; ++index) {
        uintptr_t site = origin_sites[index];

        if (!read_cell(site, ORIGIN_SCREEN_WIDTH_OPERAND, sizeof(float), &screen_width,
                       "screen width cell") ||
            !read_cell(site, ORIGIN_SCREEN_WIDTH_AGAIN_OPERAND, sizeof(float), &screen_width,
                       "screen width cell") ||
            !read_cell(site, ORIGIN_SCREEN_HEIGHT_OPERAND, sizeof(float), &screen_height,
                       "screen height cell") ||
            !read_cell(site, ORIGIN_X_OPERAND, sizeof(int32_t), &origin_x, "menu origin X") ||
            !read_cell(site, ORIGIN_Y_OPERAND, sizeof(int32_t), &origin_y, "menu origin Y") ||
            !read_cell(site, ORIGIN_MENU_SCALE_OPERAND, sizeof(float), &menu_scale,
                       "g_menuScale") ||
            !read_cell(site, ORIGIN_BASE_TEXT_OPERAND, sizeof(float), &base_text,
                       "base text size") ||
            !read_cell(site, ORIGIN_MENU_TEXT_SCALE_OPERAND, sizeof(float), &menu_text_scale,
                       "g_menuTextScale")) {
            return false;
        }
    }
    if (pop == 0 || copy == 0) {
        log_warning("%s did not resolve, so the menus are left at their authored size",
                    (pop == 0) ? "swmenu_pop" : "render_prepareFrame's focal copy");
        return false;
    }
    if (!read_cell(pop, MENU_POP_DEPTH_STORE_OPERAND, sizeof(int32_t), &depth,
                   "menu stack depth cell") ||
        !read_cell(pop, MENU_POP_DEPTH_COMPARE_OPERAND, sizeof(int32_t), &depth,
                   "menu stack depth cell") ||
        !read_cell(pop, MENU_POP_CURRENT_MENU_OPERAND, sizeof(void *), &current_menu,
                   "current menu cell") ||
        !read_cell(copy, PROJECTION_COPY_CAMERA_OPERAND, sizeof(void *), &camera,
                   "current camera cell") ||
        !read_cell(copy, PROJECTION_COPY_SCALE_OPERAND, sizeof(float), &proj_scale,
                   "projection scale cell")) {
        return false;
    }

    menu_cells.screen_width    = (volatile float *)(uintptr_t)screen_width;
    menu_cells.screen_height   = (volatile float *)(uintptr_t)screen_height;
    menu_cells.origin_x        = (volatile int32_t *)(uintptr_t)origin_x;
    menu_cells.origin_y        = (volatile int32_t *)(uintptr_t)origin_y;
    menu_cells.menu_scale      = (volatile float *)(uintptr_t)menu_scale;
    menu_cells.menu_text_scale = (volatile float *)(uintptr_t)menu_text_scale;
    menu_cells.base_text       = (const volatile float *)(uintptr_t)base_text;
    menu_cells.current_menu    = (void *const volatile *)(uintptr_t)current_menu;
    menu_cells.proj_scale      = (const volatile float *)(uintptr_t)proj_scale;
    menu_cells.current_camera  = (const char *const volatile *)(uintptr_t)camera;
    log_info("engine cells: screen %08X x %08X, origin %08X,%08X, g_menuScale %08X, base text "
             "%08X, g_menuTextScale %08X, current menu %08X, camera %08X, g_projScale %08X, all "
             "read out of the matched code",
             (unsigned)screen_width, (unsigned)screen_height, (unsigned)origin_x,
             (unsigned)origin_y, (unsigned)menu_scale, (unsigned)base_text,
             (unsigned)menu_text_scale, (unsigned)current_menu, (unsigned)camera,
             (unsigned)proj_scale);
    return true;
}
