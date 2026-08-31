/* menu_scale.c: the sites, the writes and the one detour. See menu_scale.h for what and why.
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
 *
 * The three constants are shared cells the rest of the engine also reads, which is exactly why the
 * OPERANDS are repointed and the cells are left alone. Writing 640*N into 0x004A8890 would move
 * every other reader of 640.0f in the image.
 *
 * swmenu_open, retail 0x0045D9F5. Detoured on an 8 byte prologue, which is three whole
 * instructions: push ebp / mov ebp,esp / mov eax,[g_swMac.pCurrMenu].
 */
#include "menu_scale.h"

#include "menu_art_source.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bitmap asked how big the converted artwork is. The front end background: it is authored
 * 640x480, it is always present in a converted set, and it is the first thing drawn, so a
 * mismatch between it and the layout is visible immediately rather than three screens in. */
/* The one file whose size is read to learn what the artwork was converted to. It is the front
 * end's background, it is always converted if anything was, and reading the ratio out of the
 * artwork rather than out of a setting is what stops the layout and the artwork disagreeing. */
#define MENU_SCALE_WITNESS_BITMAP "splash3.BMP"

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
#define RLE_BLIT_WIDTH_IMMEDIATE  0x30u
#define RLE_BLIT_HEIGHT_IMMEDIATE 0x37u

/* ---------------------------------------------------------------------------------------------
 * The origin and scale block, matched twice
 */
static const uint8_t SIG_MENU_ORIGIN[] = {
    0xD9, 0x05, 0x40, 0xA4, 0x86, 0x00,
    0xD8, 0x25, 0x90, 0x88, 0x4A, 0x00,
    0xD8, 0x35, 0x94, 0x88, 0x4A, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0xA3, 0x58, 0xFD, 0x6C, 0x00,
    0xD9, 0x05, 0x38, 0xA4, 0x86, 0x00,
    0xD8, 0x25, 0x98, 0x88, 0x4A, 0x00,
    0xD8, 0x35, 0x94, 0x88, 0x4A, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0xA3, 0x5C, 0xFD, 0x6C, 0x00,
    0xD9, 0x05, 0x90, 0x88, 0x4A, 0x00
};
static const uint8_t MSK_MENU_ORIGIN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,                            /* the __ftol displacement        */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,                            /* and the second one             */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define ORIGIN_WIDTH_OPERAND  0x08u
#define ORIGIN_HEIGHT_OPERAND 0x24u
#define ORIGIN_SCALE_OPERAND  0x3Au
#define ORIGIN_SITE_COUNT     2u

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
#define LISTBOX_DRAW_X_INSET 0xA8u
#define LISTBOX_DRAW_Y_INSET 0xB7u
#define LISTBOX_SHIPPED_X_INSET 6
#define LISTBOX_SHIPPED_Y_INSET 3

/* The TOP inset is deliberately more than proportional, and this is a judgement rather than a
 * derivation. Scaling the authored 3 gives 7 at 2.25x, which is correct arithmetic and still
 * reads tight: at 16 pixel rows the glyph very nearly filled its row, so 3 pixels was the whole
 * visible gap, while at 36 pixel rows the eye reads the space above the first line against a much
 * larger letter. Judged on screen at 1080p. The left inset needs no such help, because nothing
 * sits above a letter to crowd it. */
#define LISTBOX_TOP_INSET_BASE  7

/* ---------------------------------------------------------------------------------------------
 * swmenu_open
 */
static const uint8_t SIG_MENU_OPEN[] = {
    0x55, 0x8B, 0xEC,                                        /* push ebp / mov ebp,esp         */
    0xA1, 0x70, 0xD3, 0x86, 0x00,                            /* mov eax,[g_swMac.pCurrMenu]    */
    0x3B, 0x45, 0x08,                                        /* cmp eax,[ebp+8]                */
    0x75, 0x0A                                               /* jne                            */
};
#define MENU_OPEN_PROLOGUE 8u

/* ---------------------------------------------------------------------------------------------
 * swpic_draw, retail 0x0045F950. Detoured on a 7 byte prologue for the four animated previews
 * on the main menu; see the long note by preview_upscale for what it does and why it has to.
 */
static const uint8_t SIG_PIC_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x83, 0x78, 0x0C, 0x00, 0x75, 0x05
};
#define PIC_DRAW_PROLOGUE 7u

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
#define DRAW_MENU_PROLOGUE 8u

/* ---------------------------------------------------------------------------------------------
 * sw3d_rectToViewOffset, retail 0x0045C3D8, matched from 0x0045C3DE.
 *
 * Where a 3-D widget's model is put in the world, from its rectangle:
 *
 *     f = depth * (g_menuScale / 554.256f)
 *     x =  f * ((rect.x + rect.width / 2)      - 320.0f)
 *     z = -f * ((rect.y + rect.height - 5.0f)  - 240.0f)
 *
 * 554.256 is 320 / tan(30 deg), the same lens as the game camera, and that is what makes the
 * shipped arithmetic come out at exactly one world unit per canvas pixel. 320 and 240 are the
 * authored canvas centre.
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
 * THE FOCAL IS NOT A CONSTANT AND MUST NOT BE GUESSED. focalPx is halfWidth / tan(hFOV / 2), so it
 * moves with the resolution AND with the field of view the reader has chosen; at 3840x2160 and 98
 * degrees it is 1669.69, where a fixed 60 degree lens would say 3325. Assuming a lens put every
 * model at half the distance from the centre it should have been. So the cell is refreshed from
 * the live camera on every menu frame instead, which also means it follows the FOV slider while
 * the options screen is open.
 *
 * THIS TAKES AN OPERAND variable_fov ALSO WANTS. That mod repoints the same fdiv for the same
 * reason, from the other side: it knows the focal and assumes the canvas is 640 wide. This file
 * loads first, so this repoint wins, and variable_fov then reports `menu_3d_focal NOT RESOLVED,
 * this patch is DISABLED` because the bytes it scans for are the ones this changed. That warning is
 * expected and is not a fault: the two are doing the same job and only one of them can know both
 * halves of the answer, which is this one, because it reads the focal live.
 *
 * The 5.0f is the inset that stands a model up off the bottom edge of its box, so it scales with
 * the box. The 2.0f is a halving and stays 2.0f.
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
#define SW3D_PROJECT_PROLOGUE 6u

/* The bottom inset, authored in canvas pixels: a model stands 5 pixels up from the bottom edge of
 * its box rather than on it. */
#define SW3D_BOTTOM_INSET 5.0f

/* Read, never written. g_menuScale is the engine's own live float, the one the repointed numerator
 * feeds; the camera is whatever rdCamera_BuildProjection last produced, and +0x3C is the focal in
 * pixels it derived from the field of view. */
#define ENGINE_MENU_SCALE_CELL 0x004B682CU

/* The display the engine settled on, as floats, and the menu origin it derived from them. Read to
 * check the canvas still fits, and the origin is WRITTEN when it does not. See menu_scale_stand_down.
 */
#define ENGINE_SCREEN_WIDTH_CELL  0x0086A440U
#define ENGINE_SCREEN_HEIGHT_CELL 0x0086A438U
#define ENGINE_MENU_ORIGIN_X_CELL 0x006CFD58U
#define ENGINE_MENU_ORIGIN_Y_CELL 0x006CFD5CU
#define ENGINE_CURRENT_CAMERA  0x006F83E4U
#define CAMERA_FOCAL_PIXELS    0x3Cu

/* THE number the projection actually multiplies by, `s = g_projScale / depth` in
 * bapvrt_projectVertex. It is COPIED from rdCamera+0x3C once per frame, in render_prepareFrame, and
 * that copy is the whole point of reading it here instead of reading the camera: the field of view
 * can be applied to the camera between the copy and the draw, and then the camera says one lens
 * while the renderer is still using another. Reading the camera is what made the hero slide
 * sideways while the field of view slider moved. */
#define ENGINE_PROJ_SCALE_CELL 0x005BF9E8U

/* g_swMac.pCurrMenu: null whenever no menu is open, which is the test for "this text belongs to a
 * menu". See the note by hook_query_font for why that gate exists. */
#define ENGINE_CURRENT_MENU_CELL 0x0086D370U

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
 * IT IS GATED ON A MENU BEING OPEN, and that gate was not there at first, which was a bug.
 *
 * The reasoning for leaving it out was that swtext_draw and the list box's SWMSG_RESET are the only
 * callers in the image. That came from grepping the decompilation, and the decompilation says of
 * itself, in game/dialog.c, that text_emitRow is "the part that was not reconstructed" - the very
 * function that places a row of subtitle text. In game subtitles came out mis-positioned and the
 * cause was invisible to a search of the source, because the calling code is not in the source.
 * They came right the moment the converted artwork was removed, which is what proved it was ours.
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
#define QUERY_FONT_PROLOGUE 10u

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
 * the reset had run, which produced the right spacing but left the engine's own snap, `rect.height =
 * numLines * lineHeight + 6`, computed from the SMALLER height. Reset runs on every open, so the box
 * lost a row every time it was opened. Moving the floor instead means the engine derives the row
 * height, the row count and the box height from one consistent number, which is stable across opens
 * and, unlike a correction of ours, is also what the row hit test reads.
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
#define LISTBOX_FLOOR_COMPARE 0x07u   /* the imm8 of cmp eax,16  */
#define LISTBOX_FLOOR_VALUE   0x17u   /* the imm32 of mov ...,16 */
#define LISTBOX_SHIPPED_FLOOR 16

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
#define SET_WIDGET_IMAGE_COMPRESS 0x06u

/* bCompress is only ever 0 or 1, so any other value declines compression without changing what the
 * comparison means to a reader of the disassembly. */
#define COMPRESS_NEVER 0x7F

/* ---------------------------------------------------------------------------------------------
 * swpic_drawCursor, retail 0x0045FD01. The size of the drawn menu pointer.
 *
 *     swrle_getCursor(&x, &y);
 *     texture_drawSprite(pCursorDraw, x, x + 0x20, y, y + 0x20, 0xf0ffffff, 1.0f);
 *
 * A 32 pixel cursor on a 4K screen is about a third of the size it appeared at when the menus were
 * 640x480, and it is the last thing on these screens still drawn at its authored size.
 *
 * UNLIKE EVERY OTHER MENU PICTURE, THIS ONE CAN SIMPLY BE MADE BIGGER. It does not go through
 * swrle_blit, the run length blitter with no scale term that made converting the artwork necessary
 * in the first place; it goes through texture_drawSprite, which takes the destination extents as
 * arguments. So the two `+ 0x20` immediates are the whole of it.
 *
 * ONE RATIO, NOT TWO. Scaling width and height separately would stretch the pointer on a display
 * that is not 4:3, and a stretched arrow reads as a rendering fault rather than as a design. The
 * vertical ratio is used for both, which is the one the text already follows.
 *
 * THE CEILING IS 127 AND IT IS THE INSTRUCTION'S. Both are `add reg,imm8` with a signed byte, so
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
#define DRAW_CURSOR_WIDTH   0x25u
#define DRAW_CURSOR_HEIGHT  0x3Cu
#define DRAW_CURSOR_SHIPPED 32

/* ---------------------------------------------------------------------------------------------
 * sw3d_draw, retail 0x0045C23B. How BIG a 3-D widget's model is drawn.
 *
 *     pos[0] = pos[1] = pos[2] = g_menuScale;
 *     rdMatrix_scale(mat, pos);
 *
 * A model's size on screen is its world size times focalPx over depth, exactly like anything else in
 * the world, so a wider field of view makes it smaller. That is not a fault in the placement: the
 * hero really is a 3-D object sitting at a fixed distance, and a wider lens really does shrink it.
 * It is still wrong for a menu, where the hero should be the same size whatever lens the reader
 * prefers for the game.
 *
 * Dividing the matrix scale by the lens cancels it, and the reference lens is the one the game would
 * have at its AUTHORED vertical field of view for this canvas: 554.256 is that focal at 640x480, and
 * it scales with the canvas the same way everything else here does. So at the default field of view
 * the model is exactly the size it is today, and at any other it stays that size instead of
 * following the lens.
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
#define SW3D_DRAW_PROLOGUE 6u

/* The three `mov reg,[g_menuScale]` operands, one per axis of the scale. */
#define SW3D_SCALE_OPERAND_X 0xAEu
#define SW3D_SCALE_OPERAND_Y 0xB6u
#define SW3D_SCALE_OPERAND_Z 0xBFu

/* 320 / tan(30 deg): the focal length, in pixels, of the lens the menus were authored under. */
#define SW3D_AUTHORED_FOCAL 554.256f

/* HOW FAR THE SIZE COMPENSATION IS ALLOWED TO GO, and it has to stop somewhere.
 *
 * Holding a model's apparent size while the lens widens means growing it at a fixed distance, and a
 * model that grows far enough pushes its own front face through the near plane and is culled
 * entirely. Moving it further away does not rescue it: the compensation grows the model in
 * proportion to the extra distance, so the two cancel and the sign of the near margin never changes.
 * Past some lens the model cannot be both the right size and in front of the camera.
 *
 * Measured rather than guessed: at a field of view above about 108 degrees the hero and the
 * inventory vanished. That works out at a factor near 1.79, and this sits below it with room.
 *
 * The factor depends only on the field of view, not on the resolution, which is why one number
 * serves every setup: with a fixed vertical field of view the focal length is proportional to the
 * screen height, and so is the reference above, so the ratio cancels the resolution out.
 *
 * Past the clamp the models resume shrinking as the lens widens, which is the shipped behaviour and
 * is visibly better than their disappearing. */
#define SW3D_MAX_SIZE_COMPENSATION 1.6f

enum {
    SITE_RLE_BLIT,
    SITE_MENU_OPEN,
    SITE_LISTBOX_DRAW,
    SITE_PIC_DRAW,
    SITE_DRAW_MENU,
    SITE_SW3D_PROJECT,
    SITE_QUERY_FONT,
    SITE_LISTBOX_FLOOR,
    SITE_SET_WIDGET_IMAGE,
    SITE_DRAW_CURSOR,
    SITE_SW3D_DRAW,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("swrle_blit", SIG_RLE_BLIT),
    SIGNATURE_ENTRY_DETOUR("swmenu_open", SIG_MENU_OPEN, MENU_OPEN_PROLOGUE),
    SIGNATURE_ENTRY("swlistbx_draw", SIG_LISTBOX_DRAW),
    SIGNATURE_ENTRY_DETOUR("swpic_draw", SIG_PIC_DRAW, PIC_DRAW_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("xswift_drawMenu", SIG_DRAW_MENU, MSK_DRAW_MENU,
                                  DRAW_MENU_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("sw3d_rectToViewOffset", SIG_SW3D_PROJECT, SW3D_PROJECT_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("font3d_queryFont", SIG_QUERY_FONT, QUERY_FONT_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("swlistbx_input row floor", SIG_LISTBOX_FLOOR, MSK_LISTBOX_FLOOR),
    SIGNATURE_ENTRY_MASKED("swpic_setWidgetImage", SIG_SET_WIDGET_IMAGE, MSK_SET_WIDGET_IMAGE),
    SIGNATURE_ENTRY_MASKED("swpic_drawCursor", SIG_DRAW_CURSOR, MSK_DRAW_CURSOR),
    SIGNATURE_ENTRY_DETOUR_MASKED("sw3d_draw", SIG_SW3D_DRAW, MSK_SW3D_DRAW, SW3D_DRAW_PROLOGUE)
};

/* The widget record, from the engine's own layout. Stride and field offsets are byte proven. */
#define WIDGET_STRIDE        0x38u
#define WIDGET_TYPE          0x00u
#define WIDGET_RECT_X        0x20u
#define WIDGET_RECT_Y        0x24u
#define WIDGET_RECT_WIDTH    0x28u
#define WIDGET_RECT_HEIGHT   0x2Cu
#define WIDGET_TERMINATOR    (-1)
#define MENU_WIDGET_ARRAY    0x08u
#define WIDGET_DATA          0x34u
#define WIDGET_FONT_INDEX    0x18u


/* A widget array that walks past this many entries without finding its terminator is not a widget
 * array, and scaling whatever it really is would corrupt memory rather than draw a menu. The
 * largest shipped screen holds well under a hundred. */
#define WIDGET_SANITY_LIMIT  512u

/* The engine reopens the same static menu structures over and over, so every one has to be scaled
 * exactly once. There are 22 screens in the shipped game; this is sized well past that and a menu
 * arriving when it is full is declined rather than scaled twice. */
#define SCALED_MENU_CAPACITY 64u

typedef int32_t(__cdecl *menu_open_fn_t)(void *menu);
typedef void(__cdecl *draw_menu_fn_t)(void *menu);
typedef uint32_t(__cdecl *query_font_fn_t)(void);
typedef void(__cdecl *sw3d_project_fn_t)(float *offset, const int32_t *rect);
typedef void(__cdecl *sw3d_draw_fn_t)(void *widget);

/* One scaled menu, and the x and y this last wrote into each of its widgets.
 *
 * The shadow is what lets a rectangle the GAME has written be told apart from one this left there,
 * which is the whole of the pause screen fix. See the note by hook_draw_menu. */
typedef struct scaled_menu {
    const void *menu;
    int32_t    *shadow;          /* two ints per widget, x then y */
    size_t      widgets;
} scaled_menu_t;

typedef struct menu_scale_state {
    bool      installed;
    float     ratio_x;
    float     ratio_y;
    int32_t   canvas_width;
    int32_t   canvas_height;

    detour_t  menu_open_detour;
    detour_t  pic_draw_detour;
    detour_t  draw_menu_detour;
    detour_t  query_font_detour;
    detour_t  sw3d_project_detour;
    detour_t  sw3d_draw_detour;

    scaled_menu_t scaled_menus[SCALED_MENU_CAPACITY];
    size_t        scaled_menu_count;
    bool        warned_capacity;
    bool        warned_sanity;
    bool        warned_shadow;
    bool        logged_focal;
    bool        stood_down;
    bool        warned_compensation;
    bool        warned_outside_menu;
} menu_scale_state_t;

static menu_scale_state_t scale_state;

/* The cells the three operands are repointed at. Written once, at install, and read by the engine
 * on every mode change afterwards. They are floats because the instructions reading them are
 * fsub and fld on dword operands. */
static float menu_scaled_width  = (float)MENU_SCALE_CANVAS_WIDTH;
static float menu_scaled_height = (float)MENU_SCALE_CANVAS_HEIGHT;

/* g_menuScale gets a cell of its OWN, and this is not a tidiness choice.
 *
 * It is computed as `cell / g_screenW` and drives glyph size, the base font size and the 3-D
 * widgets. Glyphs are scaled UNIFORMLY by that one number, so it has to be the ratio the layout
 * advances vertically by: text that is scaled horizontally but laid out vertically comes out too
 * tall for the row it sits in, and the lines pile into each other.
 *
 * So this holds 640 * the VERTICAL ratio, while the origin cells above hold the real canvas.
 * When the artwork is scaled uniformly the two are equal and this changes nothing; they differ
 * only when a 4:3 canvas has been stretched onto a wider display. */
static float menu_text_scale_numerator = (float)MENU_SCALE_CANVAS_WIDTH;



float menu_scale_ratio(void)
{
    /* The vertical one. Height is what the artwork is really scaled by; the horizontal ratio
     * only differs when the display is not 4:3 and the artwork was stretched to fill it. */
    return scale_state.installed ? scale_state.ratio_y : 1.0f;
}

void menu_scale_canvas(int32_t *out_width, int32_t *out_height)
{
    if (out_width != NULL) {
        *out_width = scale_state.installed ? scale_state.canvas_width
                                           : (int32_t)MENU_SCALE_CANVAS_WIDTH;
    }
    if (out_height != NULL) {
        *out_height = scale_state.installed ? scale_state.canvas_height
                                            : (int32_t)MENU_SCALE_CANVAS_HEIGHT;
    }
}

/* Rounds a canvas coordinate to the scaled one. Half away from zero, and the sign is handled
 * explicitly because the pause panel parks rows at large positive x and the engine is free to use
 * negative coordinates for a widget scrolled off the left.
 *
 * X and Y scale independently because the artwork does. A 4:3 canvas on a 16:9 display is either
 * pillarboxed or stretched, and if the artwork is stretched then the layout must be stretched by
 * exactly the same two numbers or the two stop agreeing. Both are read from the artwork. */
static int32_t scaled_coordinate(int32_t value, float ratio)
{
    float scaled = (float)value * ratio;

    return (scaled >= 0.0f) ? (int32_t)(scaled + 0.5f) : -(int32_t)(-scaled + 0.5f);
}

/* ---------------------------------------------------------------------------------------------
 * The four animated previews on the main menu
 *
 * title_main_menu creates four 232x100 video surfaces, opens ss0.bik..ss3.bik into them as looping
 * previews, and plants each surface into widgets 1..4 through swmenu_setWidgetImage. That is the
 * ONLY call to that function in the game, so a picture widget carrying a pData is one of these four
 * and nothing else, which is what makes the test below safe.
 *
 * They are the one part of the front end the canvas scale cannot reach on its own. Every other menu
 * bitmap comes out of the archives and is whatever size the converted artwork made it, but these
 * are decoded at run time into a surface whose 232 and 100 are immediates inside title_main_menu.
 * So the scale moves the four buttons to their new places and leaves them at their authored size,
 * which reads as a bug even though nothing has failed.
 *
 * WHY THE SURFACE IS NOT SIMPLY CREATED BIGGER. Patching those two immediates is easy, and wrong.
 * Bink decodes at the video's own 232x100 whatever it is given, so a larger surface would hold a
 * small picture in the corner of a large black rectangle, and the only way to fill it would be to
 * upscale the surface in place. Three of the four clips are paused at any moment, so an in place
 * upscale would run again next frame over its own output, and again, compounding into mush inside a
 * second.
 *
 * So the engine's surface is left exactly as it is, always holding one pristine native frame, and
 * the draw is handed a larger buffer of ours filled from it. The swap is undone the moment the draw
 * returns, so no other code ever sees it.
 *
 * The one thing that does persist is the widget rectangle, because swpic_draw writes the frame's
 * size into it before blitting. That is the wanted outcome rather than a leak: the hit box then
 * matches what the reader can see, so the whole face of each button is clickable.
 *
 * WHY THE MODE IS TESTED AND NOT JUST pData. The save game screens plant thumbnails through the
 * same pData with bCompress set, and swrle_compressVBuffer FREES the pixel buffer, puts a much
 * shorter run length stream in its place, and leaves rasterInfo completely alone. So a compressed
 * surface still claims to be 232x100 raw pixels and nothing on it says otherwise: reading it as raw
 * walks off the end of the allocation, which is a crash on opening Load Game.
 *
 * The discriminator is the widget's fontIndex, which doubles as the blit mode. swpic_setWidgetImage
 * raises it to 2 or more exactly when it leaves the image raw, and swpic_blit branches on that same
 * field to choose between the run length blitter and the plain surface copy. Testing it here is the
 * engine's own test, read from the engine's own field, rather than a guess about the buffer.
 *
 * The consequence is that save game thumbnails are NOT scaled: they are compressed, so they go
 * through swrle_blit, which has no scale term. They stay at their authored size inside a scaled
 * screen. That is the shipped behaviour, not a regression.
 *
 * Nearest neighbour, deliberately. A 232x100 clip on a 4K button is a six times blow up with no
 * extra detail in it, so a smoothing filter would buy blur rather than sharpness while costing
 * arithmetic per pixel per frame on four buffers. Whole pixel replication also means each distinct
 * source row is expanded once and then copied for its repeats, which is what keeps this off the
 * frame time.
 */
#define VBUFFER_WIDTH          0x0Cu   /* these five are contiguous, and are saved and restored */
#define VBUFFER_HEIGHT         0x10u   /* as one block in the hook below                        */
#define VBUFFER_SIZE           0x14u
#define VBUFFER_BYTES_PER_LINE 0x18u
#define VBUFFER_PITCH_PIXELS   0x1Cu
#define VBUFFER_HEADER_INTS    5
#define VBUFFER_COLOR_INFO     0x20u   /* rasterInfo at +0x0C, colorInfo at +0x14 */

/* Indices into that colorInfo, in dwords. The three channel fields are consecutive, so a loop over
 * red, green and blue can index them off the first. */
#define COLOR_INFO_MODE        0        /* 1 = direct RGB, 0 = palette indexed */
#define COLOR_INFO_BPP         1
#define COLOR_INFO_RED_BITS    2        /* then green, then blue */
#define COLOR_INFO_RED_SHIFT   5        /* the LEFT shift, i.e. the channel's bit position */
#define VBUFFER_PIXELS         0x5Cu

/* Only four previews are ever live. The spare room is for the title screen being torn down and
 * rebuilt, which leaves four stale surfaces behind; past that the slot drawn longest ago is reused,
 * so this cannot grow however many times the reader leaves the front end and comes back. */
#define PREVIEW_CAPACITY 8u

/* A preview surface is 232x100. Anything outside these bounds is not one, and is left alone. */
#define PREVIEW_MAX_SOURCE_EDGE 2048
#define PREVIEW_MAX_TARGET_EDGE 8192

/* How to take a pixel apart, read from the surface rather than assumed. */
typedef struct preview_format {
    int32_t  bytes_per_pixel;
    int32_t  shift[3];            /* red, green, blue bit positions */
    uint32_t mask[3];
    bool     smooth;              /* false for palette indexed: an index cannot be interpolated */
} preview_format_t;

typedef struct preview_slot {
    const void      *frame;       /* the engine's surface, a key here and never dereferenced */
    uint8_t         *pixels;
    int32_t         *column;      /* destination x -> (source x << 8) | fraction */
    uint16_t        *scratch;     /* two horizontally resampled rows, three channels per pixel */
    size_t           pixel_bytes;
    int32_t          source_width;
    int32_t          source_height;
    int32_t          width;
    int32_t          height;
    int32_t          bytes_per_line;
    preview_format_t format;
    uint32_t         source_hash; /* of the source the buffer currently holds */
    bool             has_content;
    uint32_t         used;        /* the clock reading at its last draw, for reuse order */
} preview_slot_t;

static preview_slot_t previews[PREVIEW_CAPACITY];

static void preview_release(preview_slot_t *slot)
{
    free(slot->pixels);
    free(slot->column);
    free(slot->scratch);
    slot->pixels      = NULL;
    slot->column      = NULL;
    slot->scratch     = NULL;
    slot->frame       = NULL;
    slot->has_content = false;
}
static uint32_t       preview_clock;
static bool           warned_preview;

typedef void(__cdecl *pic_draw_fn_t)(void *widget, void *menu);

/* Finds the slot for this surface, or takes one, and makes sure its buffers are the right size.
 * Returns NULL only when memory could not be had, in which case the preview is drawn at its
 * authored size, which is what happens without this file at all. */
static preview_slot_t *preview_claim(const void *frame, int32_t source_width,
                                     int32_t source_height, int32_t width, int32_t height,
                                     const preview_format_t *format)
{
    preview_slot_t *slot = NULL;
    size_t          index;
    int32_t         bytes_per_line;
    size_t          bytes;

    for (index = 0; index < PREVIEW_CAPACITY; ++index) {
        if (previews[index].frame == frame) {
            slot = &previews[index];
            break;
        }
        if (previews[index].frame == NULL) {
            slot = &previews[index];          /* a free one, kept in case nothing matches */
        }
    }
    if (slot == NULL) {
        slot = &previews[0];                  /* all taken: reuse the one drawn longest ago */
        for (index = 1; index < PREVIEW_CAPACITY; ++index) {
            if (previews[index].used < slot->used) {
                slot = &previews[index];
            }
        }
    }

    slot->used = ++preview_clock;
    if (slot->frame == frame && slot->pixels != NULL &&
        slot->source_width == source_width && slot->source_height == source_height &&
        slot->width == width && slot->height == height &&
        slot->format.bytes_per_pixel == format->bytes_per_pixel &&
        slot->format.smooth == format->smooth) {
        return slot;                          /* the usual case, from the second frame onwards */
    }

    bytes_per_line = (width * format->bytes_per_pixel + 3) & ~3;
    bytes          = (size_t)bytes_per_line * (size_t)height;

    {
        uint8_t  *pixels = (uint8_t *)realloc(slot->pixels, bytes);
        int32_t  *column;
        uint16_t *scratch;

        if (pixels == NULL) {
            preview_release(slot);
            return NULL;
        }
        slot->pixels = pixels;

        column = (int32_t *)realloc(slot->column, (size_t)width * sizeof(int32_t));
        if (column == NULL) {
            preview_release(slot);
            return NULL;
        }
        slot->column = column;

        /* Two rows of horizontally resampled channel values, so the expensive half of a separable
         * filter runs once per SOURCE row rather than once per destination row. At six times that
         * is most of the work saved. */
        scratch = (uint16_t *)realloc(slot->scratch,
                                      (size_t)width * 3u * 2u * sizeof(uint16_t));
        if (scratch == NULL) {
            preview_release(slot);
            return NULL;
        }
        slot->scratch = scratch;
    }

    /* The source position of every destination column, as an index and an eight bit fraction.
     * Sampling from pixel CENTRES, which is what keeps the resampled picture from drifting half a
     * source pixel up and left of where the nearest neighbour version put it. */
    {
        int32_t step = (source_width << 8) / width;
        int32_t x;

        for (x = 0; x < width; ++x) {
            int32_t position = x * step + (step >> 1) - 128;
            int32_t source_x;

            if (position < 0) {
                position = 0;
            }
            source_x = position >> 8;
            if (source_x >= source_width - 1) {
                source_x = (source_width > 0) ? source_width - 1 : 0;
                position = source_x << 8;                 /* clamp, and do not read past the edge */
            }
            slot->column[x] = (source_x << 8) | (position & 0xFF);
        }
    }

    slot->frame          = frame;
    slot->pixel_bytes    = bytes;
    slot->source_width   = source_width;
    slot->source_height  = source_height;
    slot->width          = width;
    slot->height         = height;
    slot->bytes_per_line = bytes_per_line;
    slot->format         = *format;
    slot->has_content    = false;             /* the buffers are new, so the hash means nothing */
    return slot;
}

static uint32_t preview_read_pixel(const uint8_t *row, int32_t index, int32_t bytes_per_pixel)
{
    if (bytes_per_pixel == 2) {
        return *(const uint16_t *)(row + (size_t)index * 2u);
    }
    if (bytes_per_pixel == 4) {
        return *(const uint32_t *)(row + (size_t)index * 4u);
    }
    return row[(size_t)index * (size_t)bytes_per_pixel];
}

static void preview_write_pixel(uint8_t *row, int32_t index, int32_t bytes_per_pixel, uint32_t value)
{
    if (bytes_per_pixel == 2) {
        *(uint16_t *)(row + (size_t)index * 2u) = (uint16_t)value;
    } else if (bytes_per_pixel == 4) {
        *(uint32_t *)(row + (size_t)index * 4u) = value;
    } else {
        row[(size_t)index * (size_t)bytes_per_pixel] = (uint8_t)value;
    }
}

/* One source row, resampled across into three channel values per destination pixel, each carried at
 * eight extra bits of precision so the vertical blend that follows does not quantise twice. */
static void preview_expand_row(const preview_slot_t *slot, const uint8_t *source, uint16_t *out)
{
    const preview_format_t *format = &slot->format;
    int32_t                 x;

    for (x = 0; x < slot->width; ++x) {
        int32_t  packed   = slot->column[x];
        int32_t  source_x = packed >> 8;
        uint32_t fraction = (uint32_t)(packed & 0xFF);
        int32_t  next     = (source_x + 1 < slot->source_width) ? source_x + 1 : source_x;
        uint32_t left     = preview_read_pixel(source, source_x, format->bytes_per_pixel);
        uint32_t right    = preview_read_pixel(source, next,     format->bytes_per_pixel);
        int      channel;

        for (channel = 0; channel < 3; ++channel) {
            uint32_t a = (left  >> format->shift[channel]) & format->mask[channel];
            uint32_t b = (right >> format->shift[channel]) & format->mask[channel];

            out[x * 3 + channel] = (uint16_t)(a * (256u - fraction) + b * fraction);
        }
    }
}

/* Bilinear, separable, and only ever called when the source has actually changed.
 *
 * Nearest neighbour was the first version of this and it is what a six times blow-up of a 160x120
 * save thumbnail looks like: every source pixel becomes a visible 6x4 block. Bilinear costs about
 * thirty operations per destination pixel, which would be real frame time at a hundred frames a
 * second, so the caller hashes the source first and skips this entirely while the picture is
 * standing still. On the save screen that means once per row selected; on the main menu, once per
 * frame of the one clip that is playing.
 *
 * PALETTE SURFACES ARE NOT SMOOTHED. Interpolating two palette INDICES produces a third index whose
 * colour has nothing to do with either, so those fall back to whole pixel replication. No surface
 * this actually meets is palettised; the test is there so that one never comes out as confetti. */
static void preview_fill(preview_slot_t *slot, const uint8_t *source, int32_t source_bytes_per_line)
{
    const preview_format_t *format = &slot->format;
    uint16_t               *rows[2];
    int32_t                 held[2];
    int32_t                 step;
    int32_t                 y;

    if (!format->smooth) {
        int32_t previous = -1;

        for (y = 0; y < slot->height; ++y) {
            uint8_t *out = slot->pixels + (size_t)y * (size_t)slot->bytes_per_line;
            int32_t  source_y = (int32_t)(((int64_t)y * slot->source_height) / slot->height);
            const uint8_t *from;
            int32_t  x;

            if (source_y == previous) {
                memcpy(out, out - slot->bytes_per_line, (size_t)slot->bytes_per_line);
                continue;
            }
            previous = source_y;
            from     = source + (size_t)source_y * (size_t)source_bytes_per_line;
            for (x = 0; x < slot->width; ++x) {
                preview_write_pixel(out, x, format->bytes_per_pixel,
                                    preview_read_pixel(from, slot->column[x] >> 8,
                                                       format->bytes_per_pixel));
            }
        }
        return;
    }

    rows[0] = slot->scratch;
    rows[1] = slot->scratch + (size_t)slot->width * 3u;
    held[0] = -1;
    held[1] = -1;
    step    = (slot->source_height << 8) / slot->height;

    for (y = 0; y < slot->height; ++y) {
        uint8_t *out = slot->pixels + (size_t)y * (size_t)slot->bytes_per_line;
        int32_t  position = y * step + (step >> 1) - 128;
        int32_t  top;
        int32_t  bottom;
        uint32_t fraction;
        int32_t  x;
        int      i;

        if (position < 0) {
            position = 0;
        }
        top = position >> 8;
        if (top >= slot->source_height - 1) {
            top      = (slot->source_height > 0) ? slot->source_height - 1 : 0;
            position = top << 8;
        }
        fraction = (uint32_t)(position & 0xFF);
        bottom   = (top + 1 < slot->source_height) ? top + 1 : top;

        /* Keep whichever of the two scratch rows already holds a source row we still need, so a
         * vertical scale of N re-expands each source row once rather than N times. */
        for (i = 0; i < 2; ++i) {
            int32_t want = (i == 0) ? top : bottom;

            if (held[0] != want && held[1] != want) {
                int32_t victim = (held[0] == top || held[0] == bottom) ? 1 : 0;

                preview_expand_row(slot, source + (size_t)want * (size_t)source_bytes_per_line,
                                   rows[victim]);
                held[victim] = want;
            }
        }

        {
            const uint16_t *above = (held[0] == top)    ? rows[0] : rows[1];
            const uint16_t *below = (held[0] == bottom) ? rows[0] : rows[1];

            for (x = 0; x < slot->width; ++x) {
                uint32_t value = 0;
                int      channel;

                for (channel = 0; channel < 3; ++channel) {
                    uint32_t a = above[x * 3 + channel];
                    uint32_t b = below[x * 3 + channel];
                    uint32_t c = (a * (256u - fraction) + b * fraction) >> 16;

                    value |= c << format->shift[channel];
                }
                preview_write_pixel(out, x, format->bytes_per_pixel, value);
            }
        }
    }
}

/* Cheap enough to run every frame on every preview, which is the point: it is what buys the right
 * to run a real resampler at all. Four 32-bit words at a time over the source, which for the four
 * main menu clips is about 185 KiB a frame. */
static uint32_t preview_source_hash(const uint8_t *source, int32_t width, int32_t height,
                                    int32_t bytes_per_line, int32_t bytes_per_pixel)
{
    uint32_t hash = 2166136261u;
    size_t   used = (size_t)width * (size_t)bytes_per_pixel;
    int32_t  y;

    for (y = 0; y < height; ++y) {
        const uint8_t *row = source + (size_t)y * (size_t)bytes_per_line;
        size_t         i   = 0;

        while (i + 4u <= used) {
            uint32_t word;
            memcpy(&word, row + i, sizeof word);
            hash = (hash ^ word) * 16777619u;
            i += 4u;
        }
        while (i < used) {
            hash = (hash ^ row[i]) * 16777619u;
            ++i;
        }
    }
    return hash;
}

/* Returns the filled slot to draw this surface from, or NULL to draw it as the engine would. */
static preview_slot_t *preview_upscale(const char *frame)
{
    int32_t        source_width  = *(const int32_t *)(frame + VBUFFER_WIDTH);
    int32_t        source_height = *(const int32_t *)(frame + VBUFFER_HEIGHT);
    int32_t        source_stride = *(const int32_t *)(frame + VBUFFER_BYTES_PER_LINE);
    const int32_t *colour        = (const int32_t *)(frame + VBUFFER_COLOR_INFO);
    const uint8_t *pixels        = *(const uint8_t *const *)(frame + VBUFFER_PIXELS);
    preview_format_t format;
    int32_t          bpp;
    int32_t          width;
    int32_t          height;
    uint32_t         hash;
    preview_slot_t  *slot;
    int              channel;

    if (pixels == NULL) {
        return NULL;
    }
    bpp = colour[COLOR_INFO_BPP];
    if (bpp <= 0 || (bpp & 7) != 0 || bpp > 32) {
        return NULL;
    }
    format.bytes_per_pixel = bpp / 8;

    /* The channel layout is read from the surface rather than assumed, because 555 and 565 both
     * occur and the difference is the whole picture. colorMode 1 is direct RGB; 0 is palette
     * indexed, which cannot be interpolated. */
    for (channel = 0; channel < 3; ++channel) {
        int32_t bits = colour[COLOR_INFO_RED_BITS + channel];

        format.shift[channel] = colour[COLOR_INFO_RED_SHIFT + channel];
        format.mask[channel]  = (bits > 0 && bits <= 8) ? ((1u << bits) - 1u) : 0u;
        if (format.mask[channel] == 0u || format.shift[channel] < 0 ||
            format.shift[channel] > 31) {
            return NULL;
        }
    }
    format.smooth = (colour[COLOR_INFO_MODE] == 1);

    if (source_width  <= 0 || source_width  > PREVIEW_MAX_SOURCE_EDGE ||
        source_height <= 0 || source_height > PREVIEW_MAX_SOURCE_EDGE ||
        source_stride < source_width * format.bytes_per_pixel) {
        return NULL;
    }

    width  = scaled_coordinate(source_width,  scale_state.ratio_x);
    height = scaled_coordinate(source_height, scale_state.ratio_y);
    if (width <= source_width && height <= source_height) {
        return NULL;                          /* nothing to gain, so nothing is copied */
    }
    if (width > PREVIEW_MAX_TARGET_EDGE || height > PREVIEW_MAX_TARGET_EDGE) {
        return NULL;
    }

    slot = preview_claim(frame, source_width, source_height, width, height, &format);
    if (slot == NULL) {
        if (!warned_preview) {
            warned_preview = true;
            log_warning("a menu preview could not be given a %dx%d buffer, so it stays at its "
                        "authored %dx%d. Nothing else is affected",
                        (int)width, (int)height, (int)source_width, (int)source_height);
        }
        return NULL;
    }

    hash = preview_source_hash(pixels, source_width, source_height, source_stride,
                               format.bytes_per_pixel);
    if (slot->has_content && slot->source_hash == hash) {
        return slot;                          /* the picture has not moved; the buffer still holds it */
    }
    slot->source_hash = hash;
    slot->has_content = true;
    preview_fill(slot, pixels, source_stride);
    return slot;
}

static void __cdecl hook_pic_draw(void *widget, void *menu)
{
    pic_draw_fn_t   original = (pic_draw_fn_t)scale_state.pic_draw_detour.original;
    char           *frame;
    int32_t         mode;
    preview_slot_t *slot;
    int32_t         saved[VBUFFER_HEADER_INTS];
    uint8_t        *saved_pixels;

    if (original == NULL) {
        return;
    }
    frame = *(char *const *)((char *)widget + WIDGET_DATA);
    mode  = *(const int32_t *)((const char *)widget + WIDGET_FONT_INDEX);
    slot  = (frame != NULL && mode >= 2) ? preview_upscale(frame) : NULL;
    if (slot == NULL) {
        original(widget, menu);
        return;
    }

    memcpy(saved, frame + VBUFFER_WIDTH, sizeof saved);
    saved_pixels = *(uint8_t **)(frame + VBUFFER_PIXELS);

    *(int32_t *)(frame + VBUFFER_WIDTH)          = slot->width;
    *(int32_t *)(frame + VBUFFER_HEIGHT)         = slot->height;
    *(int32_t *)(frame + VBUFFER_SIZE)           = (int32_t)slot->pixel_bytes;
    *(int32_t *)(frame + VBUFFER_BYTES_PER_LINE) = slot->bytes_per_line;
    *(int32_t *)(frame + VBUFFER_PITCH_PIXELS)   = slot->bytes_per_line / slot->format.bytes_per_pixel;
    *(uint8_t **)(frame + VBUFFER_PIXELS)        = slot->pixels;

    original(widget, menu);

    memcpy(frame + VBUFFER_WIDTH, saved, sizeof saved);
    *(uint8_t **)(frame + VBUFFER_PIXELS) = saved_pixels;
}

/* See the long note by SIG_QUERY_FONT. The glyphs are drawn ratio_y taller than this used to say
 * they were, so this says so. */
static uint32_t __cdecl hook_query_font(void)
{
    query_font_fn_t original = (query_font_fn_t)scale_state.query_font_detour.original;
    uint32_t        raw;

    if (original == NULL) {
        return 0;
    }
    raw = original();
    if (raw == 0 || !(scale_state.ratio_y > 1.0f)) {
        return raw;
    }

    if (*(void *const *)(uintptr_t)ENGINE_CURRENT_MENU_CELL == NULL) {
        /* Not a menu. Reported once, because it is the evidence that a caller exists which the
         * decompilation does not contain, and the next person to widen this needs to know. */
        if (!scale_state.warned_outside_menu) {
            scale_state.warned_outside_menu = true;
            log_info("font3d_queryFont was called with no menu open, so something outside the "
                     "menus uses it: it is answered unscaled there. This is what mis-placed the "
                     "subtitles before the gate existed");
        }
        return raw;
    }
    return (uint32_t)((float)raw * scale_state.ratio_y + 0.5f);
}

/* The inverse of scaled_coordinate, for putting a rectangle back. Rounding means this is not exact
 * to the pixel on every value, which is acceptable: it runs once, when the scale is abandoned, and
 * the alternative is leaving the screen laid out for a canvas that no longer exists. */
static int32_t unscaled_coordinate(int32_t value, float ratio)
{
    float restored;

    if (!(ratio > 0.0f)) {
        return value;
    }
    restored = (float)value / ratio;
    return (restored >= 0.0f) ? (int32_t)(restored + 0.5f) : -(int32_t)(-restored + 0.5f);
}

/* Has this menu already been scaled? The engine hands back the same pointers for the life of the
 * process, so pointer identity is the whole test. */
static scaled_menu_t *find_scaled_menu(const void *menu)
{
    size_t index;

    for (index = 0; index < scale_state.scaled_menu_count; ++index) {
        if (scale_state.scaled_menus[index].menu == menu) {
            return &scale_state.scaled_menus[index];
        }
    }
    return NULL;
}

static bool scaled_menu_room_left(void)
{
    if (scale_state.scaled_menu_count < SCALED_MENU_CAPACITY) {
        return true;
    }
    if (!scale_state.warned_capacity) {
        scale_state.warned_capacity = true;
        log_warning("more than %u distinct menus have been opened, so this one is left at its "
                    "authored size rather than risk scaling another one twice",
                    (unsigned)SCALED_MENU_CAPACITY);
    }
    return false;
}

/* A list box decides its row spacing and row count when the menu is BUILT, from the font height and
 * the box height as authored:
 *
 *     pLBox->lineHeight = (h < 17) ? 16 : font3d_queryFont();
 *     pLBox->numLines   = (pWidget->rect.height - 3) / pLBox->lineHeight;
 *     pWidget->rect.height = pLBox->numLines * pLBox->lineHeight + 6;
 *
 * All of that has already happened by the time anything here runs. So scaling the box on its own
 * leaves the rows 16 pixels apart inside a box two or three times taller, while the glyphs drawn
 * into them have grown with g_menuScale. The rows pile into each other, which is what the
 * resolution list, the sound providers and the keyboard controls all look like.
 *
 * The repair is the engine's own three lines, re-run against the scaled box. lineHeight moves by
 * the VERTICAL ratio, the same one the glyphs moved by, because a row has to be as tall as the text
 * in it. Then the row count and the box height follow from the engine's own arithmetic rather than
 * from anything invented here.
 *
 * The box height is passed in rather than re-read, so this cannot disagree with the rectangle the
 * caller just wrote. */
static void scale_widgets(void *menu)
{
    char     *widgets;
    uint32_t  count;
    uint32_t  index;
    int32_t  *shadow;

    widgets = *(char *const *)((char *)menu + MENU_WIDGET_ARRAY);
    if (widgets == NULL) {
        return;
    }

    /* Counted first, and nothing is touched until the terminator has been found. An array that
     * walks past the limit without one is not a widget array, and scaling whatever it really is
     * would corrupt memory rather than draw a menu. */
    for (count = 0; count < WIDGET_SANITY_LIMIT; ++count) {
        if (*(const int32_t *)(widgets + (size_t)count * WIDGET_STRIDE + WIDGET_TYPE)
                == WIDGET_TERMINATOR) {
            break;
        }
    }
    if (count >= WIDGET_SANITY_LIMIT) {
        if (!scale_state.warned_sanity) {
            scale_state.warned_sanity = true;
            log_warning("a menu's widget array ran past %u entries with no terminator, so it was "
                        "left alone entirely", (unsigned)WIDGET_SANITY_LIMIT);
        }
        return;
    }

    /* The shadow is optional. Without it the menu is still scaled, and only the screens that
     * rewrite their own rectangles go uncorrected, which is where this file stood before. */
    shadow = (int32_t *)malloc((size_t)count * 2u * sizeof(int32_t));
    if (shadow == NULL && !scale_state.warned_shadow) {
        scale_state.warned_shadow = true;
        log_warning("a menu could not be given its %u entry rectangle shadow, so the pause and "
                    "credits screens will slide back to their authored places. Every other screen "
                    "is unaffected", (unsigned)count);
    }

    for (index = 0; index < count; ++index) {
        int32_t *rect = (int32_t *)(widgets + (size_t)index * WIDGET_STRIDE + WIDGET_RECT_X);

        rect[0] = scaled_coordinate(rect[0], scale_state.ratio_x);   /* x      */
        rect[1] = scaled_coordinate(rect[1], scale_state.ratio_y);   /* y      */
        rect[2] = scaled_coordinate(rect[2], scale_state.ratio_x);   /* width  */
        rect[3] = scaled_coordinate(rect[3], scale_state.ratio_y);   /* height */

        if (shadow != NULL) {
            shadow[index * 2u]      = rect[0];
            shadow[index * 2u + 1u] = rect[1];
        }
    }

    scale_state.scaled_menus[scale_state.scaled_menu_count].menu    = menu;
    scale_state.scaled_menus[scale_state.scaled_menu_count].shadow  = shadow;
    scale_state.scaled_menus[scale_state.scaled_menu_count].widgets = count;
    scale_state.scaled_menu_count++;
}

/* THE SCREENS THAT MOVE THEIR OWN WIDGETS.
 *
 * The pause screen and its four siblings slide in from the right: every frame while the panel is
 * moving, pausemenu_run and friends write rect.x on the backdrop and on the sixteen inventory slots
 * as `x + colX[i]`, where x comes from a float and colX is a table of authored constants. The
 * credits do the same to rect.y, once per row per frame, for as long as the crawl runs. All of it
 * is in authored 640x480 units, and all of it lands AFTER swmenu_open has scaled the array, so a
 * one-shot scale is simply overwritten and the screen ends up part scaled and part not. That is
 * exactly what a dump of the parked pause screen showed: every rectangle correct except the
 * seventeen x values the slide had touched.
 *
 * The correction is possible because the game is WRITE ONLY on those fields. x is derived from the
 * slide's own float, never read back out of the rectangle, so nothing downstream depends on what is
 * stored there between frames. That means a value which differs from the one this last wrote can
 * only have come from the game, and can be scaled on sight.
 *
 * Hence the shadow. Each frame, a rectangle still holding the value this wrote is left alone; one
 * holding anything else is treated as freshly authored and scaled. The slide animates correctly
 * because every intermediate position is scaled as it appears.
 *
 * The false negative is a rectangle the game writes that happens to equal the scaled value already
 * there. It costs one widget one frame in the wrong place during a slide, because the next frame
 * writes something different and corrects it, and it cannot happen at rest: the values the game
 * writes are canvas units and the values this writes are canvas units times the ratio, and the only
 * number where those two agree is zero, which scales to itself.
 *
 * Width and height are deliberately NOT shadowed. swpic_draw writes the frame's size into them on
 * every single draw, by design, and that size is already correct because it comes from the artwork.
 */
/* Defined below, next to the install it rolls back. */
static void restore_canvas_clip(uintptr_t site);

/* Puts everything back and stops scaling, because the canvas no longer fits the screen.
 *
 * THIS IS A MEMORY SAFETY MEASURE, not a cosmetic one. swrle_blit clips against the canvas rather
 * than against the surface it is drawing into: it reads the destination's real width and height and
 * then throws both away for the two immediates this file writes. So a canvas wider or taller than
 * the back buffer does not merely draw off the edge, it writes PAST THE END OF THE BUFFER, and the
 * game crashes. The report that led to this was exactly that: artwork converted for 3840x2160 and
 * obi.ini left at something smaller.
 *
 * The origin is written too, and it has to be. Restoring the clip alone leaves g_menuOrigin holding
 * the value the engine derived for the old canvas, which is NEGATIVE when the canvas was wider than
 * the screen, and a 640 wide clip added to a negative origin writes before the START of the buffer.
 * The engine only recomputes the origin on a mode change, so waiting for one is not an option.
 *
 * Everything undone here is undone completely, because this runs before any widget rectangle has
 * been scaled: swmenu_open scales them, and this is the first thing swmenu_open's hook does. A
 * screen already scaled by an earlier open keeps its rectangles, which is wrong-looking and safe,
 * and that is the right way round.
 *
 * The cursor cage keeps the clamp it was installed with, so the drawn pointer can still travel
 * outside the picture. That is cosmetic and pointer_cage owns those immediates, so it is said in the
 * log rather than reached into from here. */
static void menu_scale_stand_down(int32_t screen_width, int32_t screen_height)
{
    int32_t origin_x = (screen_width  - MENU_SCALE_CANVAS_WIDTH)  / 2;
    int32_t origin_y = (screen_height - MENU_SCALE_CANVAS_HEIGHT) / 2;

    if (scale_state.stood_down) {
        return;
    }
    scale_state.stood_down = true;

    log_warning("the menu artwork is %dx%d but the game is running at %dx%d, so the menus are NOT "
                "scaled. Drawing a canvas larger than the screen writes past the end of the frame "
                "buffer and crashes. Convert the artwork for %dx%d with tools\\Convert Menu Art.bat, "
                "or set the game back to the size it was converted for",
                (int)scale_state.canvas_width, (int)scale_state.canvas_height,
                (int)screen_width, (int)screen_height, (int)screen_width, (int)screen_height);

    if (sites[SITE_RLE_BLIT].address != 0) {
        restore_canvas_clip(sites[SITE_RLE_BLIT].address);
    }

    /* The cells the repointed operands read. The operands stay repointed; holding the authored
     * numbers makes them behave exactly as the constants they replaced. */
    menu_scaled_width         = (float)MENU_SCALE_CANVAS_WIDTH;
    menu_scaled_height        = (float)MENU_SCALE_CANVAS_HEIGHT;
    menu_text_scale_numerator = (float)MENU_SCALE_CANVAS_WIDTH;

    if (sites[SITE_LISTBOX_FLOOR].address != 0) {
        (void)patch_write_u8(sites[SITE_LISTBOX_FLOOR].address + LISTBOX_FLOOR_COMPARE,
                             (uint8_t)LISTBOX_SHIPPED_FLOOR);
        (void)patch_write_u32(sites[SITE_LISTBOX_FLOOR].address + LISTBOX_FLOOR_VALUE,
                              (uint32_t)LISTBOX_SHIPPED_FLOOR);
    }
    if (sites[SITE_SET_WIDGET_IMAGE].address != 0) {
        (void)patch_write_u8(sites[SITE_SET_WIDGET_IMAGE].address + SET_WIDGET_IMAGE_COMPRESS, 1);
    }
    if (sites[SITE_DRAW_CURSOR].address != 0) {
        (void)patch_write_u8(sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_WIDTH,
                             (uint8_t)DRAW_CURSOR_SHIPPED);
        (void)patch_write_u8(sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_HEIGHT,
                             (uint8_t)DRAW_CURSOR_SHIPPED);
    }

    /* Every menu already scaled is put back to its authored rectangles. Without this the fallback
     * is merely non-fatal rather than usable: a screen scaled for a 3840 canvas, drawn against a 640
     * clip, is a heap of widgets in the top left corner. The engine re-derives the parts it owns on
     * the next open anyway - a picture adopts its bitmap's size, a list box re-runs SWMSG_RESET - so
     * only the authored positions have to be restored here. */
    {
        size_t index;

        for (index = 0; index < scale_state.scaled_menu_count; ++index) {
            scaled_menu_t *tracked = &scale_state.scaled_menus[index];
            char          *widgets;
            size_t         widget;

            if (tracked->menu == NULL) {
                continue;
            }
            widgets = *(char *const *)((const char *)tracked->menu + MENU_WIDGET_ARRAY);
            for (widget = 0; widgets != NULL && widget < tracked->widgets; ++widget) {
                int32_t *rect = (int32_t *)(widgets + widget * WIDGET_STRIDE + WIDGET_RECT_X);

                rect[0] = unscaled_coordinate(rect[0], scale_state.ratio_x);
                rect[1] = unscaled_coordinate(rect[1], scale_state.ratio_y);
                rect[2] = unscaled_coordinate(rect[2], scale_state.ratio_x);
                rect[3] = unscaled_coordinate(rect[3], scale_state.ratio_y);
            }
            free(tracked->shadow);
            tracked->shadow = NULL;
            tracked->menu   = NULL;
        }
        scale_state.scaled_menu_count = 0;
    }

    /* Ratio 1 is what switches off everything that is not a patched byte: the widget scaling, the
     * per-frame correction, the font height, the preview upscaler and the 3-D placement all test it
     * or multiply by it, and the 3-D hook hands the engine its own body back once stood_down is up. */
    scale_state.ratio_x       = 1.0f;
    scale_state.ratio_y       = 1.0f;
    scale_state.canvas_width  = MENU_SCALE_CANVAS_WIDTH;
    scale_state.canvas_height = MENU_SCALE_CANVAS_HEIGHT;

    if (origin_x < 0) { origin_x = 0; }
    if (origin_y < 0) { origin_y = 0; }
    *(int32_t *)(uintptr_t)ENGINE_MENU_ORIGIN_X_CELL = origin_x;
    *(int32_t *)(uintptr_t)ENGINE_MENU_ORIGIN_Y_CELL = origin_y;

    log_info("  menu origin put back to %d,%d. The drawn cursor keeps the wider area it was caged "
             "to, which is cosmetic", (int)origin_x, (int)origin_y);
}

/* True when the canvas still fits the screen. Checked on every menu open rather than once, because
 * the reader can change resolution in the options screen at any time and the artwork cannot follow
 * them. */
static bool canvas_still_fits(void)
{
    float screen_width  = *(const float *)(uintptr_t)ENGINE_SCREEN_WIDTH_CELL;
    float screen_height = *(const float *)(uintptr_t)ENGINE_SCREEN_HEIGHT_CELL;

    if (!(screen_width > 0.0f) || !(screen_height > 0.0f)) {
        return true;                  /* no mode yet: nothing has drawn, so nothing is at risk */
    }
    if ((float)scale_state.canvas_width  <= screen_width &&
        (float)scale_state.canvas_height <= screen_height) {
        return true;
    }
    menu_scale_stand_down((int32_t)screen_width, (int32_t)screen_height);
    return false;
}

/* The cell the three matrix-scale reads in sw3d_draw are repointed at. Written immediately before
 * each of those reads, by the hook below, so it can never be a frame behind the lens. */
static float menu_sw3d_model_scale = 1.0f;

/* Holds a 3-D widget's model at one size whatever the field of view is. See the note by
 * SIG_SW3D_DRAW for why the size follows the lens at all, and what the reference lens is. */
static void __cdecl hook_sw3d_draw(void *widget)
{
    sw3d_draw_fn_t original = (sw3d_draw_fn_t)scale_state.sw3d_draw_detour.original;
    float          engine_scale = *(const float *)(uintptr_t)ENGINE_MENU_SCALE_CELL;
    float          projection   = *(const float *)(uintptr_t)ENGINE_PROJ_SCALE_CELL;

    if (original == NULL) {
        return;
    }

    if (scale_state.stood_down || !(projection > 0.0f)) {
        menu_sw3d_model_scale = engine_scale;      /* exactly what the engine would have read */
    } else {
        float compensation = (SW3D_AUTHORED_FOCAL * scale_state.ratio_y) / projection;

        if (compensation > SW3D_MAX_SIZE_COMPENSATION) {
            compensation = SW3D_MAX_SIZE_COMPENSATION;
            if (!scale_state.warned_compensation) {
                scale_state.warned_compensation = true;
                log_info("the field of view is wide enough that holding the 3-D models at their "
                         "authored size would push them through the near plane and cull them, so "
                         "the compensation stops at %.2f. Past this they shrink as the lens widens, "
                         "which is what the unpatched game does",
                         (double)SW3D_MAX_SIZE_COMPENSATION);
            }
        }
        menu_sw3d_model_scale = engine_scale * compensation;
    }
    original(widget);
}

/* Where a 3-D widget's model goes, computed here rather than by repointing the engine's constants.
 *
 * The engine's own arithmetic is
 *
 *     f = depth * (g_menuScale / 554.256f)
 *     x =  f * ((rect.x + rect.width / 2)     - 320.0f)
 *     z = -f * ((rect.y + rect.height - 5.0f) - 240.0f)
 *
 * and it lands a canvas pixel on the screen pixel it names, because the projection is focalPx over
 * depth and 554.256 is that focal at the size the menus were authored for. Written without the
 * coincidence, what it means is
 *
 *     offset = (canvas pixel - canvas centre) * depth / focalPx
 *
 * which is what this computes, with the canvas centre being the scaled one and focalPx read from the
 * camera AT THIS INSTANT.
 *
 * WHY NOT KEEP REPOINTING THE CONSTANTS. That was the first version and it was wrong in a way no
 * amount of care about the arithmetic would have fixed: the cell holding the focal has to be written
 * before the placement happens, and the placement happens after the camera has been rebuilt for the
 * frame. Refreshing it once per menu frame sampled a lens that had not changed yet, so dragging the
 * field of view slider slid the hero sideways, out by exactly the ratio between the lens we had
 * sampled and the one actually projecting. A probe proved it: the lens was logged once, at startup,
 * and never again while the slider moved. Reading it here removes the ordering question entirely,
 * because there is no longer a stored value to be stale.
 *
 * It also hands the [0x4A8888] operand back to variable_fov, which wants it for the same reason and
 * whose own compensation is now harmless: nothing here reads that constant any more.
 *
 * The engine's own body is called when there is no camera to read, and when the scale has stood
 * down, so the untouched game is always the fallback rather than an approximation of it. */
static void __cdecl hook_sw3d_project(float *offset, const int32_t *rect)
{
    sw3d_project_fn_t  original = (sw3d_project_fn_t)scale_state.sw3d_project_detour.original;
    const char *const *camera_slot = (const char *const *)(uintptr_t)ENGINE_CURRENT_CAMERA;
    const char        *camera = NULL;
    float              focal;
    float              depth;

    if (original == NULL) {
        return;
    }
    /* g_projScale first, because it is what will be multiplied by. The camera is the fallback for
     * the first frame, before render_prepareFrame has copied anything into it. */
    focal = *(const float *)(uintptr_t)ENGINE_PROJ_SCALE_CELL;
    if (!(focal > 0.0f)) {
        camera = *camera_slot;
        focal  = (camera != NULL) ? *(const float *)(camera + CAMERA_FOCAL_PIXELS) : 0.0f;
    }

    if (scale_state.stood_down || offset == NULL || rect == NULL || !(focal > 0.0f)) {
        original(offset, rect);
        return;
    }

    depth = offset[1];
    {
        float centre_x = (float)scale_state.canvas_width  * 0.5f;
        float centre_y = (float)scale_state.canvas_height * 0.5f;
        float across   = (float)rect[0] + (float)rect[2] * 0.5f;
        float down     = (float)rect[1] + (float)rect[3]
                       - SW3D_BOTTOM_INSET * scale_state.ratio_y;
        float per_pixel = depth / focal;

        offset[0] =  (across - centre_x) * per_pixel;
        offset[1] =  depth;                     /* the caller's own, deliberately preserved */
        offset[2] = -(down   - centre_y) * per_pixel;
    }

    if (!scale_state.logged_focal) {
        scale_state.logged_focal = true;
        log_info("3-D widgets are placed about %.1f,%.1f from the camera's live focal length "
                 "(%.2f right now), so they hold still while the field of view changes",
                 (double)((float)scale_state.canvas_width * 0.5f),
                 (double)((float)scale_state.canvas_height * 0.5f), (double)focal);
    }
}

static void __cdecl hook_draw_menu(void *menu)
{
    draw_menu_fn_t original = (draw_menu_fn_t)scale_state.draw_menu_detour.original;
    scaled_menu_t *tracked;
    char          *widgets;

    if (original == NULL) {
        return;
    }

    /* HERE AND NOT ONLY AT swmenu_open, because the resolution is changed FROM a menu. The options
     * screen is open the whole time: the mode changes, the engine recomputes the origin from the
     * cells this file owns, and the very next frame blits a canvas wider than the new back buffer.
     * Nothing reopens, so a check on open never runs, and the first thing that happens is the write
     * past the end of the buffer. Two float reads and two compares, before anything is drawn. */
    if (!scale_state.stood_down) {
        (void)canvas_still_fits();
    }
    tracked = (menu != NULL) ? find_scaled_menu(menu) : NULL;
    widgets = (menu != NULL) ? *(char *const *)((char *)menu + MENU_WIDGET_ARRAY) : NULL;

    if (tracked != NULL && tracked->shadow != NULL && widgets != NULL) {
        size_t index;

        for (index = 0; index < tracked->widgets; ++index) {
            int32_t *rect = (int32_t *)(widgets + index * WIDGET_STRIDE + WIDGET_RECT_X);

            if (rect[0] != tracked->shadow[index * 2u]) {
                rect[0] = scaled_coordinate(rect[0], scale_state.ratio_x);
                tracked->shadow[index * 2u] = rect[0];
            }
            if (rect[1] != tracked->shadow[index * 2u + 1u]) {
                rect[1] = scaled_coordinate(rect[1], scale_state.ratio_y);
                tracked->shadow[index * 2u + 1u] = rect[1];
            }
        }
    }
    original(menu);
}

/* The ratio the converted artwork was made at, or 0 when there is none.
 *
 * The front end background is authored 640 wide, so a loose copy of it that is 1440 wide was made
 * at 2.25. Reading it rather than being told keeps the layout and the artwork from ever
 * disagreeing: there is one number, and it lives in the file the engine will draw.
 *
 * A loose file in the game directory is what the engine itself would find. Its resource layer
 * promotes the working directory to the head of its source chain, ahead of big.lab, so a converter
 * writes here and this reads exactly what the engine will read.
 *
 * Only the width is used. The artwork is resampled uniformly, and taking one axis avoids deciding
 * what to do about a file whose two axes disagree: such a file is not something this produced.
 */
static bool ratio_from_artwork(float *out_x, float *out_y)
{
    unsigned char header[26];
    int32_t       width;
    int32_t       height;
    size_t        read;
    char          path[256];
    FILE         *file;

    /* The converted folder first, then the game directory itself. The second is not a fallback so
     * much as the older arrangement: before the folder existed this was proven by dropping the
     * converted files loose beside WMAIN.EXE, and an install still set up that way keeps working
     * rather than silently losing its scale. */
    _snprintf(path, sizeof path - 1, "%s\\%s", menu_art_source_directory(),
              MENU_SCALE_WITNESS_BITMAP);
    path[sizeof path - 1] = '\0';

    file = fopen(path, "rb");
    if (file == NULL) {
        file = fopen(MENU_SCALE_WITNESS_BITMAP, "rb");
    }
    if (file == NULL) {
        return false;
    }
    read = fread(header, 1, sizeof header, file);
    (void)fclose(file);

    if (read != sizeof header || header[0] != 'B' || header[1] != 'M') {
        return false;
    }

    /* biWidth and biHeight, little endian, at offsets 18 and 22 of a BITMAPINFOHEADER bitmap.
     * Height is signed and negative for a top-down bitmap, so its magnitude is what counts. */
    width = (int32_t)((uint32_t)header[18] | ((uint32_t)header[19] << 8) |
                      ((uint32_t)header[20] << 16) | ((uint32_t)header[21] << 24));
    height = (int32_t)((uint32_t)header[22] | ((uint32_t)header[23] << 8) |
                       ((uint32_t)header[24] << 16) | ((uint32_t)header[25] << 24));
    if (height < 0) {
        height = -height;
    }

    if (width < MENU_SCALE_CANVAS_WIDTH || height < MENU_SCALE_CANVAS_HEIGHT) {
        return false;                         /* not converted, or converted downwards */
    }
    *out_x = (float)width  / (float)MENU_SCALE_CANVAS_WIDTH;
    *out_y = (float)height / (float)MENU_SCALE_CANVAS_HEIGHT;
    return true;
}

/* The list boxes, AFTER the engine has opened the menu.
 *
 * They cannot be done in the same walk as the rectangles. A list box keeps its row height and row
 * count in a record hung off the widget, and that record does not exist yet when swmenu_open is
 * entered: swmenu_open is what sends SWMSG_RESET, and the reset is what allocates it, measures the
 * font and derives both numbers. Running earlier finds a null pointer, which is what it did.
 *
 * Worse than nothing, in fact: the reset derives `numLines` from `(rect.height - 3) / lineHeight`,
 * so once the box has been scaled it computes how many SIXTEEN pixel rows fit in a box two or three
 * times taller, then draws that many rows of text that is no longer sixteen pixels tall. That is
 * the crammed list, and it is caused by the scale rather than merely left unfixed by it.
 *
 * So this runs last, with the record populated and the engine's own arithmetic already done, and
 * redoes that arithmetic with a row height that matches the text actually being drawn. */
static int32_t __cdecl hook_menu_open(void *menu)
{
    menu_open_fn_t original = (menu_open_fn_t)scale_state.menu_open_detour.original;
    int32_t        result;

    if (original == NULL) {
        return 0;                                    /* the un-armed instant between write and
                                                      * state, the same guard every detour here
                                                      * carries */
    }

    /* The rectangles BEFORE the original, because the original is what makes the menu current and
     * starts drawing from it, and because its own reset pass reads the box heights this writes.
     * Scaling afterwards would leave one frame at the authored size. */
    if (!scale_state.stood_down) {
        (void)canvas_still_fits();
    }

    if (menu != NULL && !scale_state.stood_down &&
        find_scaled_menu(menu) == NULL && scaled_menu_room_left()) {
        scale_widgets(menu);
    }

    result = original(menu);

    return result;
}

/* Puts the two canvas immediates back. Used only to roll back a half-done install. */
static void restore_canvas_clip(uintptr_t site)
{
    (void)patch_write_u32(site + RLE_BLIT_WIDTH_IMMEDIATE,  (uint32_t)MENU_SCALE_CANVAS_WIDTH);
    (void)patch_write_u32(site + RLE_BLIT_HEIGHT_IMMEDIATE, (uint32_t)MENU_SCALE_CANVAS_HEIGHT);
}

bool menu_scale_install(float configured_ratio, bool cursor_cage_widens)
{
    uintptr_t origin_sites[ORIGIN_SITE_COUNT];
    size_t    origin_hits;
    size_t    index;
    uintptr_t blit_site;
    float     ratio_x;
    float     ratio_y;

    if (scale_state.installed) {
        return true;
    }

    if (configured_ratio > 0.0f) {
        ratio_x = ratio_y = configured_ratio;  /* an explicit setting, which exists for testing */
    } else {
        if (!ratio_from_artwork(&ratio_x, &ratio_y)) {
            log_info("MenuScale is automatic and no converted menu artwork was found beside the "
                     "game, so the menus stay at their authored 640x480 canvas. Convert the "
                     "artwork for your display and this follows it.");
            return true;
        }
    }

    if (ratio_x < MENU_SCALE_MIN_RATIO) { ratio_x = MENU_SCALE_MIN_RATIO; }
    if (ratio_y < MENU_SCALE_MIN_RATIO) { ratio_y = MENU_SCALE_MIN_RATIO; }
    if (ratio_x > MENU_SCALE_MAX_RATIO) {
        log_info("a menu width scale of %.3f is past the %.3f the run length encoder allows, "
                 "using %.3f", (double)ratio_x, (double)MENU_SCALE_MAX_RATIO,
                 (double)MENU_SCALE_MAX_RATIO);
        ratio_x = MENU_SCALE_MAX_RATIO;
    }
    if (ratio_y > MENU_SCALE_MAX_RATIO) { ratio_y = MENU_SCALE_MAX_RATIO; }

    if (ratio_x <= 1.0f && ratio_y <= 1.0f) {
        log_info("the menu scale is 1, so the menus stay at their authored 640x480 canvas");
        return true;
    }

    /* Declined rather than installed, and this is a correctness gate and not a preference: with the
     * cage shut, every widget the scale moves outside the old 607x447 box becomes unreachable and
     * nothing reports it. See the header. */
    if (!cursor_cage_widens) {
        log_warning("a menu scale of %.3f was asked for, but WidenMenuCursorArea=0, so the "
                    "drawn cursor would keep the engine's 607x447 clamp while the widgets "
                    "move outside it and become unreachable. NOT scaling. Set "
                    "WidenMenuCursorArea=1 to use this", (double)ratio_y);
        return false;
    }

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_RLE_BLIT].address == 0 || sites[SITE_MENU_OPEN].address == 0) {
        log_warning("a menu scale of %.3f was asked for, but %s did not resolve, so the menus "
                    "are left at their authored size", (double)ratio_y,
                    (sites[SITE_RLE_BLIT].address == 0) ? "swrle_blit" : "swmenu_open");
        return false;
    }
    blit_site = sites[SITE_RLE_BLIT].address;

    /* Exactly two, because the engine computes the menu origin in exactly two places. */
    origin_hits = signature_count_matches(SIG_MENU_ORIGIN, MSK_MENU_ORIGIN, sizeof SIG_MENU_ORIGIN,
                                          origin_sites, ORIGIN_SITE_COUNT);
    if (origin_hits != ORIGIN_SITE_COUNT) {
        log_warning("the menu origin block matched %u times rather than %u, so the menus are left "
                    "at their authored size", (unsigned)origin_hits, (unsigned)ORIGIN_SITE_COUNT);
        return false;
    }

    scale_state.ratio_x       = ratio_x;
    scale_state.ratio_y       = ratio_y;
    scale_state.canvas_width  = (int32_t)((float)MENU_SCALE_CANVAS_WIDTH  * ratio_x + 0.5f);
    scale_state.canvas_height = (int32_t)((float)MENU_SCALE_CANVAS_HEIGHT * ratio_y + 0.5f);

    /* The cells the operands read. Rounded to the same integers as the clip, so the origin
     * centres exactly the canvas that is clipped rather than one half a pixel wider. */
    menu_scaled_width  = (float)scale_state.canvas_width;
    menu_scaled_height = (float)scale_state.canvas_height;
    menu_text_scale_numerator = (float)MENU_SCALE_CANVAS_WIDTH * ratio_y;

    /* The canvas clip first. On its own it changes nothing visible, because nothing yet draws past
     * canvas 640, which makes it the safest of the writes to have applied on its own. */
    if (patch_write_u32(blit_site + RLE_BLIT_WIDTH_IMMEDIATE,
                        (uint32_t)scale_state.canvas_width) != PATCH_RESULT_OK ||
        patch_write_u32(blit_site + RLE_BLIT_HEIGHT_IMMEDIATE,
                        (uint32_t)scale_state.canvas_height) != PATCH_RESULT_OK) {
        restore_canvas_clip(blit_site);
        log_warning("the canvas clip could not be widened, so the menus are left at their authored "
                    "size");
        return false;
    }

    /* Then the three operands at each of the two origin sites. A failure here rolls the clip back
     * as well: a widened canvas with a 1x origin draws the menu off centre, which is a worse state
     * than either end of the change. */
    for (index = 0; index < ORIGIN_SITE_COUNT; ++index) {
        uintptr_t site = origin_sites[index];

        if (patch_write_pointer32(site + ORIGIN_WIDTH_OPERAND,  &menu_scaled_width)
                != PATCH_RESULT_OK ||
            patch_write_pointer32(site + ORIGIN_HEIGHT_OPERAND, &menu_scaled_height)
                != PATCH_RESULT_OK ||
            patch_write_pointer32(site + ORIGIN_SCALE_OPERAND,  &menu_text_scale_numerator)
                != PATCH_RESULT_OK) {
            restore_canvas_clip(blit_site);
            log_warning("a menu origin operand at %08X could not be repointed, so nothing is "
                        "scaled", (unsigned)site);
            return false;
        }
    }

    /* Last, because it is the only piece that changes what is in memory rather than what the image
     * reads, and because the two above are what make its arithmetic correct. */
    if (!detour_install(&scale_state.menu_open_detour, sites[SITE_MENU_OPEN].address,
                        (const void *)hook_menu_open, MENU_OPEN_PROLOGUE)) {
        restore_canvas_clip(blit_site);
        log_warning("swmenu_open could not be hooked, so nothing is scaled. The origin operands "
                    "stay repointed and are harmless on their own: they only recentre a canvas "
                    "nothing draws past");
        return false;
    }

    /* Everything below is optional, and comes after everything that can still fail, so none of it
     * can cost the scale itself.
     *
     * The per-frame corrector goes first because the 3-D repoint below is gated on it. */
    if (sites[SITE_DRAW_MENU].address != 0 &&
        detour_install(&scale_state.draw_menu_detour, sites[SITE_DRAW_MENU].address,
                       (const void *)hook_draw_menu, DRAW_MENU_PROLOGUE)) {
        log_info("the screens that rewrite their own rectangles, the pause family and the credits, "
                 "are corrected once per frame at xswift_drawMenu");
    } else {
        log_warning("xswift_drawMenu could not be hooked, so the pause screens and the credits "
                    "slide back to their authored places, and the 3-D widgets are left alone. "
                    "Every screen that lays itself out once is unaffected");
    }

    if (sites[SITE_QUERY_FONT].address != 0 &&
        detour_install(&scale_state.query_font_detour, sites[SITE_QUERY_FONT].address,
                       (const void *)hook_query_font, QUERY_FONT_PROLOGUE)) {
        log_info("font3d_queryFont now answers in drawn units, so centred menu text sits in the "
                 "middle of its box and list boxes derive their own row height");
    } else {
        log_warning("font3d_queryFont could not be hooked, so centred menu text sits high in its "
                    "box by about %d pixels and list box rows will be cramped",
                    (int)((ratio_y - 1.0f) * 8.0f + 0.5f));
    }

    if (sites[SITE_LISTBOX_FLOOR].address != 0) {
        uintptr_t site  = sites[SITE_LISTBOX_FLOOR].address;
        int32_t   floor = (int32_t)((float)LISTBOX_SHIPPED_FLOOR * ratio_y + 0.5f);

        if (floor > 127) {
            floor = 127;              /* the compare is a signed byte immediate */
        }
        if (patch_write_u8(site + LISTBOX_FLOOR_COMPARE, (uint8_t)floor) == PATCH_RESULT_OK &&
            patch_write_u32(site + LISTBOX_FLOOR_VALUE, (uint32_t)floor) == PATCH_RESULT_OK) {
            log_info("list box rows: the %d pixel minimum row height becomes %d, which is what the "
                     "engine then derives the row count, the box height and the row hit test from",
                     LISTBOX_SHIPPED_FLOOR, (int)floor);
        } else {
            log_warning("the list box row height floor could not be scaled, so list box rows will "
                        "be bunched together. Nothing else is affected");
        }
    } else {
        log_warning("the list box row height floor did not resolve, so list box rows will be "
                    "bunched together. Nothing else is affected");
    }

    if (sites[SITE_SET_WIDGET_IMAGE].address != 0 &&
        patch_write_u8(sites[SITE_SET_WIDGET_IMAGE].address + SET_WIDGET_IMAGE_COMPRESS,
                       COMPRESS_NEVER) == PATCH_RESULT_OK) {
        log_info("save game thumbnails are left uncompressed, which puts them on the surface copy "
                 "path and lets them scale with the canvas like the main menu previews");
    } else {
        log_warning("save game thumbnails could not be left uncompressed, so they stay at their "
                    "authored 160x120 inside a scaled frame. Nothing else is affected");
    }

    if (sites[SITE_DRAW_CURSOR].address != 0) {
        int32_t size = (int32_t)((float)DRAW_CURSOR_SHIPPED * ratio_y + 0.5f);
        bool    clamped = false;

        if (size > 127) {                     /* both are signed byte immediates */
            size = 127;
            clamped = true;
        }
        if (patch_write_u8(sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_WIDTH,  (uint8_t)size)
                == PATCH_RESULT_OK &&
            patch_write_u8(sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_HEIGHT, (uint8_t)size)
                == PATCH_RESULT_OK) {
            log_info("the drawn menu pointer is %d pixels instead of %d%s", (int)size,
                     DRAW_CURSOR_SHIPPED,
                     clamped ? ", which is the largest a signed byte immediate can hold; the "
                               "proportional size would have been larger" : "");
        } else {
            log_warning("the drawn menu pointer could not be resized, so it stays %d pixels and "
                        "looks small on a large screen. Nothing else is affected",
                        DRAW_CURSOR_SHIPPED);
        }
    }

    /* The list box insets. */
    if (sites[SITE_LISTBOX_DRAW].address != 0) {
        uintptr_t draw = sites[SITE_LISTBOX_DRAW].address;
        int32_t   inset_x = (int32_t)((float)LISTBOX_SHIPPED_X_INSET * ratio_x + 0.5f);
        int32_t   inset_y = (int32_t)((float)LISTBOX_TOP_INSET_BASE * ratio_y + 0.5f);

        if (inset_x > 127) { inset_x = 127; }      /* both are signed byte immediates */
        if (inset_y > 127) { inset_y = 127; }

        if (patch_write_u8(draw + LISTBOX_DRAW_X_INSET, (uint8_t)inset_x) == PATCH_RESULT_OK &&
            patch_write_u8(draw + LISTBOX_DRAW_Y_INSET, (uint8_t)inset_y) == PATCH_RESULT_OK) {
            log_info("list box text insets: %d -> %d across, %d -> %d down (the top one is "
                     "given more than its share, see the note by LISTBOX_TOP_INSET_BASE)",
                     LISTBOX_SHIPPED_X_INSET, (int)inset_x,
                     LISTBOX_SHIPPED_Y_INSET, (int)inset_y);
        } else {
            log_warning("the list box text insets could not be scaled, so rows sit a little "
                        "tight against the top and left of their box. Nothing else is affected");
        }
    } else {
        log_info("swlistbx_draw did not resolve, so the list box text insets stay at their "
                 "authored 6 and 3 and the rows sit a little tight. Nothing else is affected");
    }

    if (sites[SITE_SW3D_DRAW].address != 0 &&
        detour_install(&scale_state.sw3d_draw_detour, sites[SITE_SW3D_DRAW].address,
                       (const void *)hook_sw3d_draw, SW3D_DRAW_PROLOGUE) &&
        patch_write_pointer32(sites[SITE_SW3D_DRAW].address + SW3D_SCALE_OPERAND_X,
                              &menu_sw3d_model_scale) == PATCH_RESULT_OK &&
        patch_write_pointer32(sites[SITE_SW3D_DRAW].address + SW3D_SCALE_OPERAND_Y,
                              &menu_sw3d_model_scale) == PATCH_RESULT_OK &&
        patch_write_pointer32(sites[SITE_SW3D_DRAW].address + SW3D_SCALE_OPERAND_Z,
                              &menu_sw3d_model_scale) == PATCH_RESULT_OK) {
        log_info("3-D widget models are held at the size they have at the authored field of view, "
                 "so changing the field of view no longer grows or shrinks the hero and the "
                 "inventory");
    } else {
        log_warning("sw3d_draw could not be hooked, so the 3-D models on the pause screens grow as "
                    "the field of view narrows and shrink as it widens. Their positions are "
                    "unaffected");
    }

    if (sites[SITE_SW3D_PROJECT].address != 0 &&
        detour_install(&scale_state.sw3d_project_detour, sites[SITE_SW3D_PROJECT].address,
                       (const void *)hook_sw3d_project, SW3D_PROJECT_PROLOGUE)) {
        log_info("the 3-D widgets on the pause screens are placed from the camera's live focal "
                 "length, so the hero and the inventory models follow the canvas and hold still "
                 "while the field of view changes");
    } else {
        log_warning("sw3d_rectToViewOffset could not be hooked, so the 27 3-D widgets on the pause "
                    "screens keep projecting about the authored 320,240 and land in the wrong "
                    "place. Nothing else is affected");
    }

    if (sites[SITE_PIC_DRAW].address != 0 &&
        detour_install(&scale_state.pic_draw_detour, sites[SITE_PIC_DRAW].address,
                       (const void *)hook_pic_draw, PIC_DRAW_PROLOGUE)) {
        log_info("the four animated main menu previews are upscaled at draw time from their "
                 "authored 232x100; the engine's own surfaces are left untouched");
    } else {
        log_warning("swpic_draw could not be hooked, so the four animated buttons on the main "
                    "menu stay at their authored 232x100 inside their scaled places. Everything "
                    "else is scaled and they are still clickable, on the small picture");
    }

    scale_state.installed = true;
    log_info("menus scaled %.3f wide by %.3f high (%s): canvas %dx%d, origin and g_menuScale "
             "recentred at %08X and %08X, widget rectangles scaled on open at %08X",
             (double)ratio_x, (double)ratio_y,
             (configured_ratio > 0.0f) ? "MenuScale, set by hand" : "read from the artwork",
             (int)scale_state.canvas_width, (int)scale_state.canvas_height,
             (unsigned)origin_sites[0], (unsigned)origin_sites[1],
             (unsigned)sites[SITE_MENU_OPEN].address);
    log_info("  the artwork is not upscaled by this: the ratio above was read FROM it, so the two "
             "cannot disagree");
    return true;
}
