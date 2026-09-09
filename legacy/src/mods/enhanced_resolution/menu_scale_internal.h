/* menu_scale_internal.h: the one state record the menu scale files share, and nothing else.
 *
 * The scale was a single file of over two thousand lines until it was split by responsibility.
 * The parts still share one record, because they share one install pass and one canvas: the
 * previews and the 3-D widgets are scaled by the same two ratios the rectangles are, and the
 * stand down switches all of them off by putting those ratios back to 1. Keeping one record is
 * what stops a second copy of the ratio existing to disagree with the first.
 *
 * The three hooks declared here are defined in menu_scale.c and installed from
 * menu_scale_install.c, which is the only reason they are not static.
 *
 * Internal to the menu scale files. menu_scale.h is the interface everything else uses.
 */
#ifndef MENU_SCALE_INTERNAL_H
#define MENU_SCALE_INTERNAL_H

#include "common/detour.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine reopens the same static menu structures over and over, so every one has to be scaled
 * exactly once. There are 22 screens in the shipped game; this is sized well past that and a menu
 * arriving when it is full is declined rather than scaled twice. */
#define SCALED_MENU_CAPACITY 64u

/* The shadow holds six ints per widget, and it does two jobs at once.
 *
 * The first four are the rectangle as the screen was AUTHORED, kept because every size the canvas
 * is ever drawn at is that rectangle times a ratio. Scaling from the authored numbers means a
 * canvas can be changed as often as the reader likes without the rounding of one change feeding
 * into the next, and it is what lets the stand down put a screen back exactly rather than by
 * dividing.
 *
 * The last two are the x and y this last WROTE, so a rectangle the GAME has written can be told
 * apart from one this left there. That comparison is the pause screen fix; the note by
 * hook_draw_menu carries the rest of it. */
#define SHADOW_STRIDE     6u
#define SHADOW_AUTHORED_X 0u
#define SHADOW_AUTHORED_Y 1u
#define SHADOW_AUTHORED_W 2u
#define SHADOW_AUTHORED_H 3u
#define SHADOW_WRITTEN_X  4u
#define SHADOW_WRITTEN_Y  5u

typedef struct scaled_menu {
    const void *menu;
    int32_t    *shadow;          /* SHADOW_STRIDE ints per widget, or NULL */
    size_t      widgets;
} scaled_menu_t;

typedef struct menu_scale_state {
    bool      installed;
    float     ratio_x;
    float     ratio_y;
    int32_t   canvas_width;
    int32_t   canvas_height;

    /* True when the ratio was taken from the display rather than from a converted set of artwork
     * or from MenuScale, and so may follow the display when it changes. See menu_scale_refit.c. */
    bool      follows_display;

    /* The display the canvas above was fitted to, so a refit is considered once per change rather
     * than once per frame. Zero until the engine has opened a mode. */
    int32_t   fitted_screen_width;
    int32_t   fitted_screen_height;

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

extern menu_scale_state_t scale_state;

/* Canvas units to scaled ones, and back. Both are defined in menu_scale.c; the first is used by the
 * preview upscaler as well, because a preview grows by exactly the ratio its widget did.
 *
 * Rounding means the two are not an exact round trip on every value, which is why the shadow keeps
 * the authored rectangle and unscaled_coordinate is only the fallback for a menu that has no
 * shadow to keep it in. */
int32_t scaled_coordinate(int32_t value, float ratio);
int32_t unscaled_coordinate(int32_t value, float ratio);

/* False once the canvas has been given up on, which it does on the spot. Defined in
 * menu_scale_install.c, beside the stand down it triggers. */
bool canvas_still_fits(void);

/* ---------------------------------------------------------------------------------------------
 * menu_scale_refit.c: the ratio in force, and what happens when the display stops matching it.
 */

/* The canvas a ratio gives, and the two writes that make the engine draw on it: the run length
 * blitter's clip and the three cells the origin operands read. Every one is absolute, so applying
 * the same ratio twice writes the same numbers.
 *
 * False when a clip immediate could not be written, and then the clip has been put back and
 * nothing else was touched. */
bool menu_scale_apply_canvas(float ratio_x, float ratio_y);

/* The rest of what the ratio in force decides: the list box row height floor and text insets, and
 * the drawn cursor's size. None of them can fail the scale, so each is attempted on its own and
 * a failure costs only itself. `verbose` is the install; a refit writes the same numbers quietly. */
void menu_scale_apply_trimmings(bool verbose);

/* Points the three origin operands at this file's cells. Done once, at install: after it, changing
 * the canvas is a matter of writing the cells rather than patching anything. */
bool menu_scale_repoint_origin(const uintptr_t *sites, size_t count);

/* Derives the four cells the engine derives from the canvas: the menu origin on both axes,
 * g_menuScale and g_menuTextScale. The engine's own block does this at startup and on the
 * mode-change message only, and on a live change it runs too early to see the new canvas, so
 * whatever changes the canvas afterwards has to do it instead. Either pointer may be NULL.
 *
 * Reads scale_state's canvas and the display, so call it after the canvas is in force. */
void menu_scale_derive_engine_cells(int32_t *out_origin_x, int32_t *out_origin_y);

/* Refits the canvas to the display if it has changed size, then checks the canvas still fits.
 * Called from both menu hooks in place of canvas_still_fits, which it ends with. */
void menu_scale_follow_display(void);

/* The three hooks menu_scale.c owns. */
int32_t __cdecl hook_menu_open(void *menu);
void __cdecl hook_draw_menu(void *menu);
uint32_t __cdecl hook_query_font(void);

#endif /* MENU_SCALE_INTERNAL_H */
