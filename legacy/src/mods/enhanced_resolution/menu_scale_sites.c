/* menu_scale_sites.c: where every piece of engine code the menu scale patches is, and the
 * evidence for each.
 *
 * The seam: this file answers where the code is and nothing in it decides what to do with the
 * code. It is all pattern bytes and disassembly, which is what made it the natural half to lift
 * out of a file that had grown to more than twice what this project allows. The offsets and cells
 * the rest of the feature needs are in menu_scale_sites.h.
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
#include "menu_scale_sites.h"

#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

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
    0xA1, 0x70, 0xD3, 0x86, 0x00,                            /* mov eax,[g_swMac.pCurrMenu]    */
    0x3B, 0x45, 0x08,                                        /* cmp eax,[ebp+8]                */
    0x75, 0x0A                                               /* jne                            */
};

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
 * itself, in game/dialog.c, that text_emitRow is "the part that was not reconstructed", the very
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
_Static_assert(sizeof SIG_DRAW_CURSOR == sizeof MSK_DRAW_CURSOR,
               "the drawn cursor pattern and its mask are different lengths");


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

signature_t menu_scale_sites[SITE_COUNT] = {
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

/* Exactly two matches are expected here; see the note above the pattern for why one or three is
 * worth declining over. */
size_t menu_scale_find_origin_sites(uintptr_t *addresses, size_t max_addresses)
{
    return signature_count_matches(SIG_MENU_ORIGIN, MSK_MENU_ORIGIN, sizeof SIG_MENU_ORIGIN,
                                   addresses, max_addresses);
}
