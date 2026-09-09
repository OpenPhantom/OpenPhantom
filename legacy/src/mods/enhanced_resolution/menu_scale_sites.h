/* menu_scale_sites.h: the offsets, the shipped values and the engine cells the menu scale reads.
 *
 * The seam is between finding engine code and deciding what to write into it. The byte patterns
 * and the disassembly that proves each one are in menu_scale_sites.c; what is here is what the
 * rest of the feature needs in order to name a site, reach a field, or read an engine global.
 * Every number here is the engine's own, so none of it is adjustable and none of it is derived.
 *
 * Internal to the menu scale files. Nothing else includes it: the SITE_ names below have the same
 * shape as every other feature's in this DLL, and each of those keeps its own table.
 */
#ifndef MENU_SCALE_SITES_H
#define MENU_SCALE_SITES_H

#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* swrle_blit: the two canvas immediates, as offsets from the match. */
#define RLE_BLIT_WIDTH_IMMEDIATE  0x30u
#define RLE_BLIT_HEIGHT_IMMEDIATE 0x37u

/* The origin and scale block: the three operands repointed at cells of ours, and the number of
 * places the engine computes the menu origin in. */
#define ORIGIN_WIDTH_OPERAND  0x08u
#define ORIGIN_HEIGHT_OPERAND 0x24u
#define ORIGIN_SCALE_OPERAND  0x3Au
#define ORIGIN_SITE_COUNT     2u

/* swlistbx_draw: the two insets it holds its rows in by, and what the game ships them as. */
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

/* The detour prologues, one per hooked site: how many bytes of the function the jump overwrites.
 * Each is a whole number of instructions, and each pattern is proven against it. */
#define MENU_OPEN_PROLOGUE 8u
#define PIC_DRAW_PROLOGUE 7u
#define DRAW_MENU_PROLOGUE 8u
#define SW3D_PROJECT_PROLOGUE 6u
#define QUERY_FONT_PROLOGUE 10u
#define SW3D_DRAW_PROLOGUE 6u

/* The list box row height floor: the compare and the value, and the number the game ships. */
#define LISTBOX_FLOOR_COMPARE 0x07u   /* the imm8 of cmp eax,16  */
#define LISTBOX_FLOOR_VALUE   0x17u   /* the imm32 of mov ...,16 */
#define LISTBOX_SHIPPED_FLOOR 16

/* swpic_setWidgetImage: the compression flag it branches on. */
#define SET_WIDGET_IMAGE_COMPRESS 0x06u

/* bCompress is only ever 0 or 1, so any other value declines compression without changing what the
 * comparison means to a reader of the disassembly. */
#define COMPRESS_NEVER 0x7F

/* swpic_drawCursor: the two size immediates, and the size the game ships. */
#define DRAW_CURSOR_WIDTH   0x25u
#define DRAW_CURSOR_HEIGHT  0x3Cu
#define DRAW_CURSOR_SHIPPED 32

/* swmenu_freeBitmaps and swmenu_sendWidget, the two the refit borrows.
 *
 * Neither is patched. The refit calls them exactly as the engine calls them itself, which is the
 * whole reason it can drop a screen's artwork and put its list boxes back together while the screen
 * stays open: both of those are things the engine already does to a live menu.
 *
 * The widget message dispatcher indexes a table of handlers by the widget's own type, so the type
 * a list box has is the index whose handler is swlistbx_input. That was read out of the table
 * rather than guessed: entry 5's first field is 0x0045C940, and entry 0's second field is
 * swpic_draw, which is the picture widget and confirms the reading of the table. */
#define WIDGET_LISTBOX_TYPE 5

/* SWMSG_RESET. swmenu_reset sends it to every widget when a screen is opened, and a list box's arm
 * derives its row height, its row count and its own height from the font. Sending it again makes
 * those three follow a canvas that has changed under an open screen. It carries no
 * allocation and leaves the selected row alone, so a second one is not a second open. */
#define SWMSG_RESET 0

/* The three `mov reg,[g_menuScale]` operands, one per axis of the scale. */
#define SW3D_SCALE_OPERAND_X 0xAEu
#define SW3D_SCALE_OPERAND_Y 0xB6u
#define SW3D_SCALE_OPERAND_Z 0xBFu

/* g_menuScale and g_menuTextScale, the two the repointed numerator feeds, and the base size the
 * second is the first multiplied by.
 *
 * WRITTEN as well as read. Leaving them read-only is why a live resolution change used to leave
 * the menus in pieces. The engine derives both in exactly one block, which runs at startup and on
 * the mode-change message and nowhere else:
 *
 *     D9 05 90 88 4A 00   fld  [numerator]     <- repointed at a cell of ours
 *     D8 35 40 A4 86 00   fdiv [g_screenW]
 *     D9 15 2C 68 4B 00   fst  [g_menuScale]   non-popping, the next instruction consumes it
 *     D8 0D 3C 68 4B 00   fmul [base text size]
 *     D9 1D 30 68 4B 00   fstp [g_menuTextScale]
 *
 * On a live change that block runs BEFORE the canvas has been refitted, so what it leaves behind
 * belongs to the canvas that has just gone, and nothing derives it again until the next mode
 * change. g_menuScale is glyph size, base font size and where the 3-D widgets sit, so the menus
 * come back laid out for the old resolution. menu_scale_refit.c derives the two the same way the
 * block does rather than waiting for it.
 *
 * The base size is read, never written: swmenu_startup passes its address to a settings read, so
 * it is a live value rather than the 1.0 the image ships.
 *
 * The camera is whatever rdCamera_BuildProjection last produced, and +0x3C is the focal in pixels
 * it derived from the field of view. */
#define ENGINE_MENU_SCALE_CELL      0x004B682CU
#define ENGINE_MENU_TEXT_SCALE_CELL 0x004B6830U
#define ENGINE_MENU_BASE_TEXT_CELL  0x004B683CU

/* The display the engine settled on, as floats, and the menu origin it derived from them. Read to
 * check the canvas still fits, and the origin is WRITTEN when it does not. See
 * menu_scale_stand_down.
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

/* The widget record, from the engine's own layout. Stride and field offsets are byte proven. */
#define WIDGET_STRIDE        0x38u
#define WIDGET_TYPE          0x00u
#define WIDGET_RECT_X        0x20u
#define WIDGET_RECT_Y        0x24u
#define WIDGET_RECT_WIDTH    0x28u
#define WIDGET_RECT_HEIGHT   0x2Cu
#define WIDGET_TERMINATOR    (-1)
#define MENU_WIDGET_ARRAY    0x08u
#define WIDGET_LINK          0x30u
#define WIDGET_DATA          0x34u
#define WIDGET_FONT_INDEX    0x18u

/* A widget array that walks past this many entries without finding its terminator is not a widget
 * array, and scaling whatever it really is would corrupt memory rather than draw a menu. The
 * largest shipped screen holds well under a hundred. */
#define WIDGET_SANITY_LIMIT  512u

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
    SITE_FREE_BITMAPS,
    SITE_SEND_WIDGET,
    SITE_COUNT
};

/* Every site, resolved once by menu_scale_install. Unresolved entries hold 0 and each optional
 * patch tests its own before writing. */
extern signature_t menu_scale_sites[SITE_COUNT];

/* The origin block is matched by COUNT rather than by uniqueness, so the pattern and its mask stay
 * beside the disassembly that explains them and the caller asks for the addresses instead. The
 * return value is the true number of matches, and the install decides on it. */
size_t menu_scale_find_origin_sites(uintptr_t *addresses, size_t max_addresses);

#endif /* MENU_SCALE_SITES_H */
