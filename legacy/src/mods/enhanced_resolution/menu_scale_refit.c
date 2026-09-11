/* menu_scale_refit.c: everything the ratio decides, written at install and written again when the
 * display changes size under an open menu.
 *
 * ==============================================================================================
 * Why a refit exists at all
 *
 * The menu canvas used to be welded to one resolution. Its ratio was read out of a converted set of
 * artwork, on the doctrine that a number taken from the pictures cannot disagree with the pictures,
 * and the artwork on disk cannot follow a reader who changes resolution in the options screen. So
 * the canvas could only be checked against the new mode and abandoned when it no longer fit, which
 * is menu_scale_stand_down and is a safety measure rather than an answer.
 *
 * menu_art_load.c removes the weld. Artwork is replicated to the canvas as the engine loads it, so
 * the canvas is free to be whatever the display is and the pictures are made to meet it. That is
 * what this file spends: when the display changes size, the ratio is recomputed from it and
 * everything written from the old ratio is written again from the new one.
 *
 * ==============================================================================================
 * What has to move, and what moves itself
 *
 * Written here, because each is a number this put somewhere:
 *
 *   the two canvas immediates in swrle_blit          the clip the menus are drawn inside
 *   the three cells the origin operands read         where the canvas is centred, and text size
 *   the menu origin the engine derived               see below
 *   the list box row floor, insets and cursor size   the trimmings
 *   every tracked menu's widget rectangles           from the authored rectangle, not the old one
 *   the cursor cage, the loading bar, the island     each sized from the canvas at install
 *
 * Follows on its own, and is listed so the next reader does not go looking for it:
 *
 *   the artwork          dropped here, reloaded by the engine, resampled by the load hook
 *   the previews         menu_preview.c reallocates a slot whose size no longer matches
 *   the 3-D widgets      menu_scale_3d.c reads the live projection every frame
 *   picture rectangles   swpic_draw writes the reloaded bitmap's size in on every draw
 *
 * The FOUR cells the engine derives are written here, and they are the part easiest to leave
 * out. The engine works out the menu origin on both axes, g_menuScale and g_menuTextScale in one
 * block, and that block runs at startup and on the mode-change message and nowhere else. On a live
 * change it runs BEFORE this has had a frame to move the canvas, so all four belong to the canvas
 * that has just gone, and nothing derives them again until the next mode change.
 *
 * Leaving them is not a cosmetic loss. The origin is where the canvas is centred, and g_menuScale
 * is glyph size, base font size and where the 3-D widgets are placed, so a canvas refitted without
 * them comes back laid out and lettered for the resolution the reader has just left. Dropping from
 * 4K to something small was unmistakable: text several times too large, everything off its
 * background. The arithmetic here is the engine's own, integer division and all.
 *
 * ==============================================================================================
 * The one thing that does not follow
 *
 * A list box works out its row height, its row count and its own height when the screen is opened,
 * from the font, and none of the three is derived again on a draw. So the screens the reader is
 * actually on when they change resolution, the ones with a resolution list on them, would keep a
 * row height belonging to the old canvas.
 *
 * The repair is the engine's own: swmenu_reset sends SWMSG_RESET to every widget when a screen is
 * opened, and a list box's arm of that message is exactly the three derivations. Sending it again
 * to the list boxes on the open screen re-runs them against the canvas that is now in force. It
 * allocates nothing and leaves the selected row alone, so it is not a second open, and it is sent
 * only to the screen that is open: every other tracked screen gets one from the engine when it is
 * next opened.
 */
#include "menu_scale.h"

#include "menu_art_load.h"
#include "menu_island_clip.h"
#include "menu_loading_bar.h"
#include "menu_scale_internal.h"
#include "menu_scale_sites.h"
#include "pointer_cage.h"

#include "common/logging.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A display cannot plausibly be larger than this on either axis, and the shutdown path writes
 * rubbish into the size cells on its way out. Reading one of those as a ratio would ask for a
 * canvas of some millions of pixels a row. */
#define PLAUSIBLE_SCREEN_EXTENT 16384

typedef void(__cdecl *free_bitmaps_fn_t)(void *menu);
typedef uint32_t(__cdecl *send_widget_fn_t)(void *widget, void *menu, uint32_t message,
                                            uint32_t param_a, uint32_t param_b);

/* The cells the three origin operands are repointed at. Written at install and on every refit, and
 * read by the engine on every mode change afterwards. They are floats because the instructions
 * reading them are fsub and fld on dword operands.
 *
 * They live here rather than beside the install because writing them IS the refit: once the
 * operands point at them, changing the canvas is a matter of changing these three numbers and
 * nothing has to be patched a second time. */
static float menu_scaled_width  = (float)MENU_SCALE_CANVAS_WIDTH;
static float menu_scaled_height = (float)MENU_SCALE_CANVAS_HEIGHT;

/* g_menuScale gets a cell of its OWN, and this is not a tidiness choice.
 *
 * It is computed as `cell / g_screenW` and drives glyph size, the base font size and the 3-D
 * widgets. Glyphs are scaled UNIFORMLY by that one number, so it has to be the ratio the layout
 * advances vertically by: text that is scaled horizontally but laid out vertically comes out too
 * tall for the row it sits in, and the lines pile into each other.
 *
 * So this holds 640 * the VERTICAL ratio, while the two cells above hold the real canvas. When the
 * canvas is scaled uniformly the two are equal and this changes nothing; they differ only when a
 * 4:3 canvas has been stretched onto a wider display. */
static float menu_text_scale_numerator = (float)MENU_SCALE_CANVAS_WIDTH;

/* ============================================================================================ */
bool menu_scale_apply_canvas(float ratio_x, float ratio_y)
{
    uintptr_t blit = menu_scale_sites[SITE_RLE_BLIT].address;
    int32_t   previous_width;
    int32_t   previous_height;
    int32_t   width;
    int32_t   height;

    if (blit == 0) {
        return false;
    }

    /* Clamped here rather than only at the install, because a refit takes its ratio from whatever
     * mode the engine has opened and the ceiling is a property of the run length format rather
     * than of the setting. Above it the encoder writes a literal run as `run & 0xfff` while
     * advancing by the whole run, which desynchronises the stream instead of failing. */
    if (ratio_x < MENU_SCALE_MIN_RATIO) { ratio_x = MENU_SCALE_MIN_RATIO; }
    if (ratio_y < MENU_SCALE_MIN_RATIO) { ratio_y = MENU_SCALE_MIN_RATIO; }
    if (ratio_x > MENU_SCALE_MAX_RATIO) { ratio_x = MENU_SCALE_MAX_RATIO; }
    if (ratio_y > MENU_SCALE_MAX_RATIO) { ratio_y = MENU_SCALE_MAX_RATIO; }

    previous_width  = (scale_state.canvas_width  > 0) ? scale_state.canvas_width
                                                      : (int32_t)MENU_SCALE_CANVAS_WIDTH;
    previous_height = (scale_state.canvas_height > 0) ? scale_state.canvas_height
                                                      : (int32_t)MENU_SCALE_CANVAS_HEIGHT;

    width  = (int32_t)((float)MENU_SCALE_CANVAS_WIDTH  * ratio_x + 0.5f);
    height = (int32_t)((float)MENU_SCALE_CANVAS_HEIGHT * ratio_y + 0.5f);

    /* The clip goes first, and a failure puts back the canvas that was in force rather than the
     * authored one: this is reached from a refit as well as from an install, and half a canvas is
     * the one state neither caller can draw on. */
    if (patch_write_u32(blit + RLE_BLIT_WIDTH_IMMEDIATE,  (uint32_t)width) != PATCH_RESULT_OK ||
        patch_write_u32(blit + RLE_BLIT_HEIGHT_IMMEDIATE, (uint32_t)height) != PATCH_RESULT_OK) {
        (void)patch_write_u32(blit + RLE_BLIT_WIDTH_IMMEDIATE,  (uint32_t)previous_width);
        (void)patch_write_u32(blit + RLE_BLIT_HEIGHT_IMMEDIATE, (uint32_t)previous_height);
        return false;
    }

    scale_state.ratio_x       = ratio_x;
    scale_state.ratio_y       = ratio_y;
    scale_state.canvas_width  = width;
    scale_state.canvas_height = height;

    /* Rounded to the same integers as the clip, so the origin centres exactly the canvas that is
     * clipped rather than one half a pixel wider. */
    menu_scaled_width         = (float)width;
    menu_scaled_height        = (float)height;
    menu_text_scale_numerator = (float)MENU_SCALE_CANVAS_WIDTH * ratio_y;
    return true;
}

bool menu_scale_repoint_origin(const uintptr_t *sites, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        uintptr_t site = sites[index];

        if (patch_write_pointer32(site + ORIGIN_WIDTH_OPERAND,  &menu_scaled_width)
                != PATCH_RESULT_OK ||
            patch_write_pointer32(site + ORIGIN_HEIGHT_OPERAND, &menu_scaled_height)
                != PATCH_RESULT_OK ||
            patch_write_pointer32(site + ORIGIN_SCALE_OPERAND,  &menu_text_scale_numerator)
                != PATCH_RESULT_OK) {
            log_warning("a menu origin operand at %08X could not be repointed, so nothing is "
                        "scaled", (unsigned)site);
            return false;
        }
    }
    return true;
}

void menu_scale_apply_trimmings(bool verbose)
{
    float   ratio_x = scale_state.ratio_x;
    float   ratio_y = scale_state.ratio_y;
    int32_t floor   = (int32_t)((float)LISTBOX_SHIPPED_FLOOR * ratio_y + 0.5f);
    int32_t size    = (int32_t)((float)DRAW_CURSOR_SHIPPED * ratio_y + 0.5f);
    int32_t inset_x = (int32_t)((float)LISTBOX_SHIPPED_X_INSET * ratio_x + 0.5f);

    /* The top inset is given more than its share while the canvas is scaled, see the note by
     * LISTBOX_TOP_INSET_BASE, but at ratio 1 the authored 3 is what belongs there. Multiplying the
     * larger base by 1 would leave a canvas that is not scaled holding a number the game never
     * shipped, which is the one thing a refit down to the authored size must not do. */
    int32_t inset_y = (ratio_y > 1.0f)
                          ? (int32_t)((float)LISTBOX_TOP_INSET_BASE * ratio_y + 0.5f)
                          : (int32_t)LISTBOX_SHIPPED_Y_INSET;

    /* Every one of these is a signed byte immediate. */
    bool clamped = (size > 127);

    if (floor   > 127) { floor   = 127; }
    if (size    > 127) { size    = 127; }
    if (inset_x > 127) { inset_x = 127; }
    if (inset_y > 127) { inset_y = 127; }

    if (menu_scale_sites[SITE_LISTBOX_FLOOR].address != 0) {
        uintptr_t site = menu_scale_sites[SITE_LISTBOX_FLOOR].address;

        if (patch_write_u8(site + LISTBOX_FLOOR_COMPARE, (uint8_t)floor) == PATCH_RESULT_OK &&
            patch_write_u32(site + LISTBOX_FLOOR_VALUE, (uint32_t)floor) == PATCH_RESULT_OK) {
            if (verbose) {
                log_info("list box rows: the %d pixel minimum row height becomes %d. The engine "
                         "derives the row count, the box height and the row hit test from it",
                         LISTBOX_SHIPPED_FLOOR, (int)floor);
            }
        } else if (verbose) {
            log_warning("the list box row height floor could not be scaled, so list box rows will "
                        "be bunched together. Nothing else is affected");
        }
    } else if (verbose) {
        log_warning("the list box row height floor did not resolve, so list box rows will be "
                    "bunched together. Nothing else is affected");
    }

    if (menu_scale_sites[SITE_DRAW_CURSOR].address != 0) {
        uintptr_t site = menu_scale_sites[SITE_DRAW_CURSOR].address;

        if (patch_write_u8(site + DRAW_CURSOR_WIDTH,  (uint8_t)size) == PATCH_RESULT_OK &&
            patch_write_u8(site + DRAW_CURSOR_HEIGHT, (uint8_t)size) == PATCH_RESULT_OK) {
            if (verbose) {
                log_info("the drawn menu pointer is %d pixels instead of %d%s", (int)size,
                         DRAW_CURSOR_SHIPPED,
                         clamped ? ", which is the largest a signed byte immediate can hold; the "
                                   "proportional size would have been larger" : "");
            }
        } else if (verbose) {
            log_warning("the drawn menu pointer could not be resized, so it stays %d pixels and "
                        "looks small on a large screen. Nothing else is affected",
                        DRAW_CURSOR_SHIPPED);
        }
    }

    if (menu_scale_sites[SITE_LISTBOX_DRAW].address != 0) {
        uintptr_t draw = menu_scale_sites[SITE_LISTBOX_DRAW].address;

        static const uint8_t ADD_ECX_IMM8[2] = { 0x83, 0xC1 };
        static const uint8_t ADD_EAX_IMM8[2] = { 0x83, 0xC0 };

        /* Both insets sit far past the matched pattern, so the instruction each is the immediate
         * of is checked first; a body laid out differently is left alone rather than written. */
        if (patch_validate_bytes(draw + LISTBOX_DRAW_X_INSET - 2u, ADD_ECX_IMM8, 2u) &&
            patch_validate_bytes(draw + LISTBOX_DRAW_Y_INSET - 2u, ADD_EAX_IMM8, 2u) &&
            patch_write_u8(draw + LISTBOX_DRAW_X_INSET, (uint8_t)inset_x) == PATCH_RESULT_OK &&
            patch_write_u8(draw + LISTBOX_DRAW_Y_INSET, (uint8_t)inset_y) == PATCH_RESULT_OK) {
            if (verbose) {
                log_info("list box text insets: %d -> %d across, %d -> %d down (the top one is "
                         "given more than its share, see the note by LISTBOX_TOP_INSET_BASE)",
                         LISTBOX_SHIPPED_X_INSET, (int)inset_x,
                         LISTBOX_SHIPPED_Y_INSET, (int)inset_y);
            }
        } else if (verbose) {
            log_warning("the list box text insets could not be scaled, so rows sit a little tight "
                        "against the top and left of their box. Nothing else is affected");
        }
    } else if (verbose) {
        log_info("swlistbx_draw did not resolve, so the list box text insets stay at their "
                 "authored 6 and 3 and the rows sit a little tight. Nothing else is affected");
    }
}

bool menu_scale_derive_engine_cells(int32_t *out_origin_x, int32_t *out_origin_y)
{
    float   screen_width  = *menu_cells.screen_width;
    float   screen_height = *menu_cells.screen_height;
    int32_t origin_x;
    int32_t origin_y;
    float   scale;

    /* Written before the one refusal below, so a caller that logs them cannot print whatever was
     * on its stack. One did: it announced an origin it had never been given and said the glyph
     * scale had been put back, on the one path where neither had happened. */
    if (out_origin_x != NULL) { *out_origin_x = 0; }
    if (out_origin_y != NULL) { *out_origin_y = 0; }

    if (!(screen_width > 0.0f) || !(screen_height > 0.0f)) {
        return false;                 /* no mode yet: the engine's own block has not run either */
    }

    origin_x = ((int32_t)screen_width  - scale_state.canvas_width)  / 2;
    origin_y = ((int32_t)screen_height - scale_state.canvas_height) / 2;

    /* The engine does not clamp, and at the authored size it never needs to: the 640x480 floor in
     * graphics_buildModeList puts a negative origin out of reach. A canvas taken from the display
     * cannot exceed it either. The clamp is for the shutdown path, which writes rubbish into the
     * size cells on its way out, and a negative origin makes the blitter write before the start of
     * the frame buffer. */
    if (origin_x < 0) { origin_x = 0; }
    if (origin_y < 0) { origin_y = 0; }
    *menu_cells.origin_x = origin_x;
    *menu_cells.origin_y = origin_y;

    /* The same three instructions the engine's block ends with. The numerator is the cell its own
     * fld was repointed at, so this is its arithmetic on its own operands and not a second opinion
     * about what the scale should be. */
    scale = menu_text_scale_numerator / screen_width;
    *menu_cells.menu_scale      = scale;
    *menu_cells.menu_text_scale = scale * *menu_cells.base_text;

    if (out_origin_x != NULL) { *out_origin_x = origin_x; }
    if (out_origin_y != NULL) { *out_origin_y = origin_y; }
    return true;
}

/* ============================================================================================ */
/* The engine's own bitmap cache, dropped so the pictures are loaded again at the new canvas. Every
 * slot it clears is refilled by name on the next draw, and the load hook resamples each one on the
 * way through. */
static void drop_bitmaps(const void *menu)
{
    free_bitmaps_fn_t free_bitmaps =
        (free_bitmaps_fn_t)menu_scale_sites[SITE_FREE_BITMAPS].address;

    if (free_bitmaps != NULL && menu != NULL) {
        free_bitmaps((void *)(uintptr_t)menu);
    }
}

/* SWMSG_RESET to the list boxes on the screen that is open, so they derive their row height, row
 * count and own height from the canvas now in force. See the file header. */
static void reset_list_boxes(const void *menu, char *widgets, size_t count)
{
    send_widget_fn_t send = (send_widget_fn_t)menu_scale_sites[SITE_SEND_WIDGET].address;
    size_t           index;

    if (send == NULL || menu != *menu_cells.current_menu) {
        return;
    }
    for (index = 0; index < count; ++index) {
        char *widget = widgets + index * WIDGET_STRIDE;

        /* The link is the record the reset writes its row height and row count into, and
         * swlistbx_input reads it without checking. The engine can afford that because it only
         * ever sends the message from swmenu_reset, where the menu has just been built; this is
         * sent later and from outside, so it checks. */
        if (*(const int32_t *)(widget + WIDGET_TYPE) == WIDGET_LISTBOX_TYPE &&
            *(void *const *)(widget + WIDGET_LINK) != NULL) {
            (void)send(widget, (void *)(uintptr_t)menu, SWMSG_RESET, 0, 0);
        }
    }
}

/* One tracked screen, from the rectangles it was authored with to the canvas now in force.
 *
 * `previous_x` and `previous_y` are only for a screen with no shadow, which is a screen whose
 * rectangle allocation failed. Those are put back by dividing, which is approximate; every other
 * screen is scaled from the numbers it was authored with and so cannot drift however many times
 * the reader changes resolution. */
static void refit_menu(scaled_menu_t *tracked, float previous_x, float previous_y)
{
    char  *widgets;
    size_t index;

    if (tracked->menu == NULL) {
        return;
    }
    widgets = *(char *const *)((const char *)tracked->menu + MENU_WIDGET_ARRAY);
    if (widgets == NULL) {
        return;
    }

    for (index = 0; index < tracked->widgets; ++index) {
        int32_t *rect = (int32_t *)(widgets + index * WIDGET_STRIDE + WIDGET_RECT_X);
        int32_t *kept = (tracked->shadow != NULL) ? &tracked->shadow[index * SHADOW_STRIDE] : NULL;
        int32_t  authored[4];

        if (kept != NULL) {
            authored[0] = kept[SHADOW_AUTHORED_X];
            authored[1] = kept[SHADOW_AUTHORED_Y];
            authored[2] = kept[SHADOW_AUTHORED_W];
            authored[3] = kept[SHADOW_AUTHORED_H];
        } else {
            authored[0] = unscaled_coordinate(rect[0], previous_x);
            authored[1] = unscaled_coordinate(rect[1], previous_y);
            authored[2] = unscaled_coordinate(rect[2], previous_x);
            authored[3] = unscaled_coordinate(rect[3], previous_y);
        }

        rect[0] = scaled_coordinate(authored[0], scale_state.ratio_x);
        rect[1] = scaled_coordinate(authored[1], scale_state.ratio_y);
        rect[2] = scaled_coordinate(authored[2], scale_state.ratio_x);
        rect[3] = scaled_coordinate(authored[3], scale_state.ratio_y);

        if (kept != NULL) {
            kept[SHADOW_WRITTEN_X] = rect[0];
            kept[SHADOW_WRITTEN_Y] = rect[1];
        }
    }

    reset_list_boxes(tracked->menu, widgets, tracked->widgets);
    drop_bitmaps(tracked->menu);
}

static void refit(float ratio_x, float ratio_y, int32_t screen_width, int32_t screen_height)
{
    float   previous_x = scale_state.ratio_x;
    float   previous_y = scale_state.ratio_y;
    int32_t origin_x;
    int32_t origin_y;
    size_t  index;

    if (!menu_scale_apply_canvas(ratio_x, ratio_y)) {
        log_warning("the menu canvas could not be refitted to %dx%d, so it stays the %dx%d it was "
                    "and the menus keep the size they had. Nothing has been half done",
                    (int)screen_width, (int)screen_height,
                    (int)scale_state.canvas_width, (int)scale_state.canvas_height);
        return;
    }
    menu_scale_apply_trimmings(false);
    /* The screen cells were read at the top of this function, so the derive cannot decline here
     * and the result is nothing this path can act on. */
    (void)menu_scale_derive_engine_cells(&origin_x, &origin_y);

    for (index = 0; index < scale_state.scaled_menu_count; ++index) {
        refit_menu(&scale_state.scaled_menus[index], previous_x, previous_y);
    }

    /* The artwork is told the new ratio before any of it is asked for again, which the drop above
     * has just guaranteed. */
    menu_art_load_set_ratio(scale_state.ratio_x, scale_state.ratio_y);

    /* The three that were sized from the canvas when they were installed. The cage matters most:
     * it is the reason the scale refuses to install without it, because a canvas larger than the
     * cage leaves every widget outside the cage unreachable. */
    pointer_cage_resize(scale_state.canvas_width, scale_state.canvas_height);
    menu_island_clip_resize(scale_state.canvas_width, scale_state.canvas_height);
    (void)menu_loading_bar_resize(scale_state.canvas_width, scale_state.canvas_height);

    log_info("the display is now %dx%d, so the menu canvas is refitted to %dx%d (%.3f by %.3f) and "
             "centred at %d,%d. %u screen%s already laid out %s put back to the rectangles they "
             "were authored with and scaled again, and their artwork is dropped so the engine "
             "loads it at the new size",
             (int)screen_width, (int)screen_height,
             (int)scale_state.canvas_width, (int)scale_state.canvas_height,
             (double)scale_state.ratio_x, (double)scale_state.ratio_y,
             (int)origin_x, (int)origin_y,
             (unsigned)scale_state.scaled_menu_count,
             (scale_state.scaled_menu_count == 1u) ? "" : "s",
             (scale_state.scaled_menu_count == 1u) ? "was" : "were");
}

void menu_scale_follow_display(void)
{
    float   screen_width;
    float   screen_height;
    int32_t width;
    int32_t height;

    if (!scale_state.installed || scale_state.stood_down || !scale_state.follows_display) {
        (void)canvas_still_fits();
        return;
    }

    screen_width  = *menu_cells.screen_width;
    screen_height = *menu_cells.screen_height;
    if (!(screen_width >= (float)MENU_SCALE_CANVAS_WIDTH) ||
        !(screen_height >= (float)MENU_SCALE_CANVAS_HEIGHT) ||
        screen_width > (float)PLAUSIBLE_SCREEN_EXTENT ||
        screen_height > (float)PLAUSIBLE_SCREEN_EXTENT) {
        (void)canvas_still_fits();      /* no mode yet, or a size nothing could have opened */
        return;
    }

    width  = (int32_t)screen_width;
    height = (int32_t)screen_height;
    if (width != scale_state.fitted_screen_width || height != scale_state.fitted_screen_height) {
        int32_t wanted_width;
        int32_t wanted_height;
        float   ratio_x = screen_width  / (float)MENU_SCALE_CANVAS_WIDTH;
        float   ratio_y = screen_height / (float)MENU_SCALE_CANVAS_HEIGHT;

        /* Recorded before the refit rather than after it, so a refit that declines is not tried
         * again on every frame for as long as the reader stays at that resolution. */
        scale_state.fitted_screen_width  = width;
        scale_state.fitted_screen_height = height;

        /* The same ceiling menu_scale_apply_canvas would impose, applied here as well so that a
         * display past it is compared against the canvas it will actually get rather than against
         * one the run length format cannot carry. */
        if (ratio_x > MENU_SCALE_MAX_RATIO) { ratio_x = MENU_SCALE_MAX_RATIO; }
        if (ratio_y > MENU_SCALE_MAX_RATIO) { ratio_y = MENU_SCALE_MAX_RATIO; }

        wanted_width  = (int32_t)((float)MENU_SCALE_CANVAS_WIDTH  * ratio_x + 0.5f);
        wanted_height = (int32_t)((float)MENU_SCALE_CANVAS_HEIGHT * ratio_y + 0.5f);
        if (wanted_width != scale_state.canvas_width ||
            wanted_height != scale_state.canvas_height) {
            refit(ratio_x, ratio_y, width, height);
        }
    }

    (void)canvas_still_fits();
}
