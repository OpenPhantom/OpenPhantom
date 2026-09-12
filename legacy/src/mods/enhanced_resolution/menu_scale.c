/* menu_scale.c: the canvas scale itself, and the widget rectangles that move with it.
 *
 * What is here is the state every part of the feature reads, the two ratios and the canvas they
 * describe, the rounding the rectangles go through, and the three hooks that own a menu's
 * rectangles: swmenu_open scales a menu's widgets once, xswift_drawMenu corrects the screens that
 * rewrite their own rectangles every frame, and font3d_queryFont answers the line height in the
 * units the glyphs are actually drawn at.
 *
 * The rest was split off by responsibility, one whole job per file:
 *
 *   menu_scale_sites.c    where the engine code is, and the disassembly that proves each site
 *   menu_scale_install.c  the ratio read from the artwork, every write, and the stand down
 *   menu_preview.c        the four animated previews on the main menu, and the resampler
 *   menu_scale_3d.c       where a menu's 3-D models go, and how big they are drawn
 *
 * See menu_scale.h for what the feature is for and for the things that have to move together.
 */
#include "menu_scale.h"

#include "menu_scale_internal.h"
#include "menu_scale_sites.h"

#include "common/detour.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef int32_t(__cdecl *menu_open_fn_t)(void *menu);
typedef void(__cdecl *draw_menu_fn_t)(void *menu);
typedef uint32_t(__cdecl *query_font_fn_t)(void);

/* The one record every part of the feature reads. See menu_scale_internal.h. */
menu_scale_state_t scale_state;

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
int32_t scaled_coordinate(int32_t value, float ratio)
{
    float scaled = (float)value * ratio;

    return (scaled >= 0.0f) ? (int32_t)(scaled + 0.5f) : -(int32_t)(-scaled + 0.5f);
}

/* The inverse, for a menu whose shadow could not be allocated and so has no authored rectangle
 * kept for it. Rounding makes it approximate, so it is the fallback rather than the
 * method: a screen put back this way can sit a pixel off, and a screen refitted this way twice can
 * sit two. Every menu that has a shadow is scaled from the authored numbers instead. */
int32_t unscaled_coordinate(int32_t value, float ratio)
{
    float restored;

    if (!(ratio > 0.0f)) {
        return value;
    }
    restored = (float)value / ratio;
    return (restored >= 0.0f) ? (int32_t)(restored + 0.5f) : -(int32_t)(-restored + 0.5f);
}

/* See the long note by SIG_QUERY_FONT in menu_scale_sites.c. The glyphs are drawn ratio_y taller
 * than this used to say they were, so this says so. */
uint32_t __cdecl hook_query_font(void)
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

    /* The subtitle layout is the one text drawn while a menu is open that is not the menu's, and
     * the open-menu gate below cannot tell it apart: with the pause screen up every subtitle row
     * took the menu's line height and dropped out of its box. It is not told apart here either.
     * The subtitle scale points the layout's one line height call at a function of its own, which
     * asks menu_scale_query_font_beneath for the answer under this hook, so this hook never sees
     * that call at all. An earlier version recognised it by the address it returned to, which
     * holds only while nothing else detours this function after this DLL. */
    if (*menu_cells.current_menu == NULL) {
        /* Not a menu. Reported once, because it is the evidence that a caller exists which the
         * decompilation does not contain, and the next person to widen this needs to know. */
        if (!scale_state.warned_outside_menu) {
            scale_state.warned_outside_menu = true;
            log_info("font3d_queryFont was called with no menu open, so something outside the "
                     "menus uses it: it is answered unscaled there");
        }
        return raw;
    }
    return (uint32_t)((float)raw * scale_state.ratio_y + 0.5f);
}

uint32_t menu_scale_query_font_beneath(bool *out_hooked)
{
    query_font_fn_t original = (query_font_fn_t)scale_state.query_font_detour.original;

    *out_hooked = (original != NULL);
    return (original != NULL) ? original() : 0u;
}

uintptr_t menu_scale_query_font_site(void)
{
    return menu_scale_sites[SITE_QUERY_FONT].address;
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
 * into them have grown with g_menuScale. The rows pile into each other. The resolution list, the
 * sound providers and the keyboard controls all look like that.
 *
 * The repair is not here. It is in menu_scale_install.c, which moves the engine's own row-height
 * floor and hooks font3d_queryFont, so the engine derives the row count and the box height from a
 * row height that matches the text actually being drawn. Leaving the arithmetic to the engine is
 * the point: nothing here invents a row count.
 *
 * This function walks the rectangles only. */
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

    /* The shadow is optional. Without it the menu is still scaled, and what is lost is the two
     * things it is kept for: the screens that rewrite their own rectangles go uncorrected, which is
     * where this file stood before, and a refit has to divide its way back to the authored
     * rectangle instead of reading it. */
    shadow = (int32_t *)malloc((size_t)count * SHADOW_STRIDE * sizeof(int32_t));
    if (shadow == NULL && !scale_state.warned_shadow) {
        scale_state.warned_shadow = true;
        log_warning("a menu could not be given its %u entry rectangle shadow, so the pause and "
                    "credits screens will slide back to their authored places. Every other screen "
                    "is unaffected", (unsigned)count);
    }

    for (index = 0; index < count; ++index) {
        int32_t *rect = (int32_t *)(widgets + (size_t)index * WIDGET_STRIDE + WIDGET_RECT_X);

        /* The authored rectangle is taken BEFORE anything is scaled, which is the only moment it
         * is on offer: swmenu_open has not run yet, so nothing has re-derived a list box's height
         * and no picture has written its own size in. */
        if (shadow != NULL) {
            int32_t *kept = &shadow[(size_t)index * SHADOW_STRIDE];

            kept[SHADOW_AUTHORED_X] = rect[0];
            kept[SHADOW_AUTHORED_Y] = rect[1];
            kept[SHADOW_AUTHORED_W] = rect[2];
            kept[SHADOW_AUTHORED_H] = rect[3];
        }

        rect[0] = scaled_coordinate(rect[0], scale_state.ratio_x);   /* x      */
        rect[1] = scaled_coordinate(rect[1], scale_state.ratio_y);   /* y      */
        rect[2] = scaled_coordinate(rect[2], scale_state.ratio_x);   /* width  */
        rect[3] = scaled_coordinate(rect[3], scale_state.ratio_y);   /* height */

        if (shadow != NULL) {
            shadow[(size_t)index * SHADOW_STRIDE + SHADOW_WRITTEN_X] = rect[0];
            shadow[(size_t)index * SHADOW_STRIDE + SHADOW_WRITTEN_Y] = rect[1];
        }
    }

    scale_state.scaled_menus[scale_state.scaled_menu_count].menu    = menu;
    scale_state.scaled_menus[scale_state.scaled_menu_count].shadow  = shadow;
    scale_state.scaled_menus[scale_state.scaled_menu_count].widgets = count;
    scale_state.scaled_menu_count++;
}

/* The screens that move their own widgets.
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
void __cdecl hook_draw_menu(void *menu)
{
    draw_menu_fn_t original = (draw_menu_fn_t)scale_state.draw_menu_detour.original;
    scaled_menu_t *tracked;
    char          *widgets;

    if (original == NULL) {
        return;
    }

    /* HERE, and not only at swmenu_open, because the resolution is changed FROM a menu. The
     * options screen is open the whole time: the mode changes, the engine recomputes the origin
     * from the cells this file owns, and the very next frame blits a canvas wider than the new
     * back buffer. Nothing reopens, so a check on open never runs, and the first thing that
     * happens is the write past the end of the buffer. Two float reads and two compares, before
     * anything is drawn.
     *
     * The canvas is refitted to the new mode here rather than merely checked against it, which is
     * the difference between the menus following a resolution change and giving up on one. */
    if (!scale_state.stood_down) {
        menu_scale_follow_display();
    }
    tracked = (menu != NULL) ? find_scaled_menu(menu) : NULL;
    widgets = (menu != NULL) ? *(char *const *)((char *)menu + MENU_WIDGET_ARRAY) : NULL;

    if (tracked != NULL && tracked->shadow != NULL && widgets != NULL) {
        size_t index;

        for (index = 0; index < tracked->widgets; ++index) {
            int32_t *rect = (int32_t *)(widgets + index * WIDGET_STRIDE + WIDGET_RECT_X);
            int32_t *kept = &tracked->shadow[index * SHADOW_STRIDE];

            /* A value that is not the one this wrote is one the game has just authored, so the
             * authored copy moves on to it as well. Without that, a refit would put the sliding
             * panel back to wherever it happened to be when the screen was opened. */
            if (rect[0] != kept[SHADOW_WRITTEN_X]) {
                kept[SHADOW_AUTHORED_X] = rect[0];
                rect[0] = scaled_coordinate(rect[0], scale_state.ratio_x);
                kept[SHADOW_WRITTEN_X]  = rect[0];
            }
            if (rect[1] != kept[SHADOW_WRITTEN_Y]) {
                kept[SHADOW_AUTHORED_Y] = rect[1];
                rect[1] = scaled_coordinate(rect[1], scale_state.ratio_y);
                kept[SHADOW_WRITTEN_Y]  = rect[1];
            }
        }
    }
    original(menu);
}

/* The list boxes, AFTER the engine has opened the menu.
 *
 * They cannot be done in the same walk as the rectangles. A list box keeps its row height and row
 * count in a record hung off the widget, and that record does not exist yet when swmenu_open is
 * entered: swmenu_open is what sends SWMSG_RESET, and the reset is what allocates it, measures the
 * font and derives both numbers. Running earlier finds a null pointer. That is what an earlier
 * version did.
 *
 * Worse than nothing, in fact: the reset derives `numLines` from `(rect.height - 3) / lineHeight`,
 * so once the box has been scaled it computes how many SIXTEEN pixel rows fit in a box two or three
 * times taller, then draws that many rows of text that is no longer sixteen pixels tall. That is
 * the crammed list, and it is caused by the scale rather than merely left unfixed by it.
 *
 * The row height itself is not touched here. menu_scale_install.c raises the engine's own floor
 * and hooks font3d_queryFont, so by the time the reset runs its arithmetic it is already dividing
 * by a row height that matches the text being drawn, and this hook only has to make sure the
 * rectangles it reads are the scaled ones. */
int32_t __cdecl hook_menu_open(void *menu)
{
    menu_open_fn_t original = (menu_open_fn_t)scale_state.menu_open_detour.original;
    int32_t        result;

    if (original == NULL) {
        return 0;                                    /* the un-armed instant between write and
                                                      * state, the same guard every detour here
                                                      * carries */
    }

    /* The rectangles BEFORE the original, because the original makes the menu current and starts
     * drawing from it, and because its own reset pass reads the box heights this writes.
     * Scaling afterwards would leave one frame at the authored size. */
    if (!scale_state.stood_down) {
        menu_scale_follow_display();
    }

    if (menu != NULL && !scale_state.stood_down &&
        find_scaled_menu(menu) == NULL && scaled_menu_room_left()) {
        scale_widgets(menu);
    }

    result = original(menu);

    return result;
}
