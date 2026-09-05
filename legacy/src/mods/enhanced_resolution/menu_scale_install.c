/* menu_scale_install.c: the ratio the artwork was made at, every write that follows from it, and
 * the stand down that takes them all back.
 *
 * The seam: this file is the only one that writes into the image or asks the detour layer for
 * anything, and it is the only one that reads a file off disk. Everything else in the feature
 * computes. Keeping the writes together is also what keeps the rollback honest, because the
 * function that undoes a patch sits next to the one that applied it and neither can be changed
 * without the other being read.
 *
 * The artwork witness stays here rather than in a file of its own: reading the converted
 * background is how the install decides its ratio, and nothing else asks that question.
 */
#include "menu_scale.h"

#include "menu_art_source.h"
#include "menu_preview.h"
#include "menu_scale_3d.h"
#include "menu_scale_internal.h"
#include "menu_scale_sites.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The one file whose size is read to learn what the artwork was converted to. It is the front
 * end's background, authored 640x480, always present in a converted set, and the first thing
 * drawn, so a mismatch between it and the layout is visible immediately rather than three screens
 * in. Reading the ratio out of the artwork rather than out of a setting is what stops the layout
 * and the artwork disagreeing. */
#define MENU_SCALE_WITNESS_BITMAP "splash3.BMP"

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

    if (menu_scale_sites[SITE_RLE_BLIT].address != 0) {
        restore_canvas_clip(menu_scale_sites[SITE_RLE_BLIT].address);
    }

    /* The cells the repointed operands read. The operands stay repointed; holding the authored
     * numbers makes them behave exactly as the constants they replaced. */
    menu_scaled_width         = (float)MENU_SCALE_CANVAS_WIDTH;
    menu_scaled_height        = (float)MENU_SCALE_CANVAS_HEIGHT;
    menu_text_scale_numerator = (float)MENU_SCALE_CANVAS_WIDTH;

    if (menu_scale_sites[SITE_LISTBOX_FLOOR].address != 0) {
        (void)patch_write_u8(menu_scale_sites[SITE_LISTBOX_FLOOR].address + LISTBOX_FLOOR_COMPARE,
                             (uint8_t)LISTBOX_SHIPPED_FLOOR);
        (void)patch_write_u32(menu_scale_sites[SITE_LISTBOX_FLOOR].address + LISTBOX_FLOOR_VALUE,
                              (uint32_t)LISTBOX_SHIPPED_FLOOR);
    }
    if (menu_scale_sites[SITE_SET_WIDGET_IMAGE].address != 0) {
        (void)patch_write_u8(menu_scale_sites[SITE_SET_WIDGET_IMAGE].address +
                                 SET_WIDGET_IMAGE_COMPRESS, 1);
    }
    if (menu_scale_sites[SITE_DRAW_CURSOR].address != 0) {
        (void)patch_write_u8(menu_scale_sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_WIDTH,
                             (uint8_t)DRAW_CURSOR_SHIPPED);
        (void)patch_write_u8(menu_scale_sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_HEIGHT,
                             (uint8_t)DRAW_CURSOR_SHIPPED);
    }

    /* Every menu already scaled is put back to its authored rectangles. Without this the fallback
     * is merely non-fatal rather than usable: a screen scaled for a 3840 canvas, drawn against a 640
     * clip, is a heap of widgets in the top left corner. The engine re-derives the parts it owns on
     * the next open anyway, a picture adopts its bitmap's size and a list box re-runs SWMSG_RESET,
     * so only the authored positions have to be restored here. */
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
bool canvas_still_fits(void)
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

    signature_resolve_table(menu_scale_sites, SITE_COUNT);
    if (menu_scale_sites[SITE_RLE_BLIT].address == 0 || menu_scale_sites[SITE_MENU_OPEN].address == 0) {
        log_warning("a menu scale of %.3f was asked for, but %s did not resolve, so the menus "
                    "are left at their authored size", (double)ratio_y,
                    (menu_scale_sites[SITE_RLE_BLIT].address == 0) ? "swrle_blit" : "swmenu_open");
        return false;
    }
    blit_site = menu_scale_sites[SITE_RLE_BLIT].address;

    /* Exactly two, because the engine computes the menu origin in exactly two places. */
    origin_hits = menu_scale_find_origin_sites(origin_sites, ORIGIN_SITE_COUNT);
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
    if (!detour_install(&scale_state.menu_open_detour, menu_scale_sites[SITE_MENU_OPEN].address,
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
    if (menu_scale_sites[SITE_DRAW_MENU].address != 0 &&
        detour_install(&scale_state.draw_menu_detour, menu_scale_sites[SITE_DRAW_MENU].address,
                       (const void *)hook_draw_menu, DRAW_MENU_PROLOGUE)) {
        log_info("the screens that rewrite their own rectangles, the pause family and the credits, "
                 "are corrected once per frame at xswift_drawMenu");
    } else {
        log_warning("xswift_drawMenu could not be hooked, so the pause screens and the credits "
                    "slide back to their authored places, and the 3-D widgets are left alone. "
                    "Every screen that lays itself out once is unaffected");
    }

    if (menu_scale_sites[SITE_QUERY_FONT].address != 0 &&
        detour_install(&scale_state.query_font_detour, menu_scale_sites[SITE_QUERY_FONT].address,
                       (const void *)hook_query_font, QUERY_FONT_PROLOGUE)) {
        log_info("font3d_queryFont now answers in drawn units, so centred menu text sits in the "
                 "middle of its box and list boxes derive their own row height");
    } else {
        log_warning("font3d_queryFont could not be hooked, so centred menu text sits high in its "
                    "box by about %d pixels and list box rows will be cramped",
                    (int)((ratio_y - 1.0f) * 8.0f + 0.5f));
    }

    if (menu_scale_sites[SITE_LISTBOX_FLOOR].address != 0) {
        uintptr_t site  = menu_scale_sites[SITE_LISTBOX_FLOOR].address;
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

    if (menu_scale_sites[SITE_SET_WIDGET_IMAGE].address != 0 &&
        patch_write_u8(menu_scale_sites[SITE_SET_WIDGET_IMAGE].address + SET_WIDGET_IMAGE_COMPRESS,
                       COMPRESS_NEVER) == PATCH_RESULT_OK) {
        log_info("save game thumbnails are left uncompressed, which puts them on the surface copy "
                 "path and lets them scale with the canvas like the main menu previews");
    } else {
        log_warning("save game thumbnails could not be left uncompressed, so they stay at their "
                    "authored 160x120 inside a scaled frame. Nothing else is affected");
    }

    if (menu_scale_sites[SITE_DRAW_CURSOR].address != 0) {
        int32_t size = (int32_t)((float)DRAW_CURSOR_SHIPPED * ratio_y + 0.5f);
        bool    clamped = false;

        if (size > 127) {                     /* both are signed byte immediates */
            size = 127;
            clamped = true;
        }
        if (patch_write_u8(menu_scale_sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_WIDTH,  (uint8_t)size)
                == PATCH_RESULT_OK &&
            patch_write_u8(menu_scale_sites[SITE_DRAW_CURSOR].address + DRAW_CURSOR_HEIGHT, (uint8_t)size)
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
    if (menu_scale_sites[SITE_LISTBOX_DRAW].address != 0) {
        uintptr_t draw = menu_scale_sites[SITE_LISTBOX_DRAW].address;
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

    if (menu_scale_sites[SITE_SW3D_DRAW].address != 0 &&
        detour_install(&scale_state.sw3d_draw_detour, menu_scale_sites[SITE_SW3D_DRAW].address,
                       (const void *)hook_sw3d_draw, SW3D_DRAW_PROLOGUE) &&
        patch_write_pointer32(menu_scale_sites[SITE_SW3D_DRAW].address + SW3D_SCALE_OPERAND_X,
                              &menu_sw3d_model_scale) == PATCH_RESULT_OK &&
        patch_write_pointer32(menu_scale_sites[SITE_SW3D_DRAW].address + SW3D_SCALE_OPERAND_Y,
                              &menu_sw3d_model_scale) == PATCH_RESULT_OK &&
        patch_write_pointer32(menu_scale_sites[SITE_SW3D_DRAW].address + SW3D_SCALE_OPERAND_Z,
                              &menu_sw3d_model_scale) == PATCH_RESULT_OK) {
        log_info("3-D widget models are held at the size they have at the authored field of view, "
                 "so changing the field of view no longer grows or shrinks the hero and the "
                 "inventory");
    } else {
        log_warning("sw3d_draw could not be hooked, so the 3-D models on the pause screens grow as "
                    "the field of view narrows and shrink as it widens. Their positions are "
                    "unaffected");
    }

    if (menu_scale_sites[SITE_SW3D_PROJECT].address != 0 &&
        detour_install(&scale_state.sw3d_project_detour, menu_scale_sites[SITE_SW3D_PROJECT].address,
                       (const void *)hook_sw3d_project, SW3D_PROJECT_PROLOGUE)) {
        log_info("the 3-D widgets on the pause screens are placed from the camera's live focal "
                 "length, so the hero and the inventory models follow the canvas and hold still "
                 "while the field of view changes");
    } else {
        log_warning("sw3d_rectToViewOffset could not be hooked, so the 27 3-D widgets on the pause "
                    "screens keep projecting about the authored 320,240 and land in the wrong "
                    "place. Nothing else is affected");
    }

    if (menu_scale_sites[SITE_PIC_DRAW].address != 0 &&
        detour_install(&scale_state.pic_draw_detour, menu_scale_sites[SITE_PIC_DRAW].address,
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
             (unsigned)menu_scale_sites[SITE_MENU_OPEN].address);
    log_info("  the artwork is not upscaled by this: the ratio above was read FROM it, so the two "
             "cannot disagree");
    return true;
}
