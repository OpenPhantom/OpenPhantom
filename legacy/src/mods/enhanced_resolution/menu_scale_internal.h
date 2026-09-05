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

extern menu_scale_state_t scale_state;

/* Canvas units to scaled ones. Defined in menu_scale.c and used by the preview upscaler as well,
 * because a preview grows by exactly the ratio its widget did. */
int32_t scaled_coordinate(int32_t value, float ratio);

/* False once the canvas has been given up on, which it does on the spot. Defined in
 * menu_scale_install.c, beside the stand down it triggers. */
bool canvas_still_fits(void);

/* The three hooks menu_scale.c owns. */
int32_t __cdecl hook_menu_open(void *menu);
void __cdecl hook_draw_menu(void *menu);
uint32_t __cdecl hook_query_font(void);

#endif /* MENU_SCALE_INTERNAL_H */
