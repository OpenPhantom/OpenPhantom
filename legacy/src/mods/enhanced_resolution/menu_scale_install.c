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
 * background is how the install decides its ratio, and no other file asks that question.
 */
#include "menu_scale.h"

#include "menu_art_source.h"
#include "menu_island_clip.h"
#include "menu_loading_bar.h"
#include "menu_preview.h"
#include "pointer_cage.h"
#include "menu_scale_3d.h"
#include "menu_art_census.h"
#include "menu_art_load.h"
#include "menu_scale_internal.h"
#include "menu_scale_sites.h"

#include "common/detour.h"
#include "common/memory.h"
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

/* Puts everything back and stops scaling, because the canvas no longer fits the screen.
 *
 * This is a MEMORY SAFETY measure, not a cosmetic one. swrle_blit clips against the canvas rather
 * than against the surface it is drawing into: it reads the destination's real width and height and
 * then throws both away for the two immediates this file writes. So a canvas wider or taller than
 * the back buffer does not merely draw off the edge, it writes PAST the end of the buffer, and
 * the game crashes. The report that led to this was exactly that: artwork converted for 3840x2160
 * and obi.ini left at something smaller.
 *
 * The origin is written too, and it has to be. Restoring the clip alone leaves g_menuOrigin holding
 * the value the engine derived for the old canvas, which is NEGATIVE when the canvas was wider than
 * the screen, and a 640 wide clip added to a negative origin writes before the START of the buffer.
 * The engine only recomputes the origin on a mode change, so waiting for one is not an option.
 *
 * Everything undone here is undone completely, because this runs before any widget rectangle has
 * been scaled: swmenu_open scales them, and this is the first thing swmenu_open's hook does. A
 * screen already scaled by an earlier open keeps its rectangles, wrong-looking and safe, which is
 * the right way round of the two.
 *
 * The three things sized from the canvas when they were installed are put back to the authored one
 * as well: the cursor cage, the sprite island and the loading bar. The cage used to be left where
 * it was, on the grounds that pointer_cage owns those immediates, which left the drawn pointer able
 * to travel outside the picture and stamp a copy of itself on every pixel nothing repaints. Each of
 * them now has a way to be told a new canvas, because the refit needed one. */
static void menu_scale_stand_down(int32_t screen_width, int32_t screen_height)
{
    int32_t origin_x   = 0;
    int32_t origin_y   = 0;
    float   previous_x = scale_state.ratio_x;
    float   previous_y = scale_state.ratio_y;

    if (scale_state.stood_down) {
        return;
    }
    scale_state.stood_down = true;

    log_warning("the menu artwork is %dx%d but the game is running at %dx%d, so the menus are NOT "
                "scaled. Drawing a canvas larger than the screen writes past the end of the frame "
                "buffer and crashes. Delete the converted artwork and the patch will size the "
                "menus from the display itself, or set the game back to the size that artwork was "
                "made for",
                (int)scale_state.canvas_width, (int)scale_state.canvas_height,
                (int)screen_width, (int)screen_height);

    /* Ratio 1 IS the authored canvas, so the two calls that put a ratio into force put all of it
     * back: the clip, the three cells the repointed operands read, the list box row floor
     * and insets and the drawn cursor's size. The operands stay repointed, and cells holding the
     * authored numbers behave exactly as the constants they replaced. */
    if (!menu_scale_apply_canvas(1.0f, 1.0f)) {
        log_error("the menu canvas clip could NOT be put back to 640x480. The menus are still "
                  "clipped to %dx%d against a smaller frame buffer, which is the write past the "
                  "end of it that this whole function exists to prevent",
                  (int)scale_state.canvas_width, (int)scale_state.canvas_height);
    }
    menu_scale_apply_trimmings(false);

    if (menu_scale_sites[SITE_SET_WIDGET_IMAGE].address != 0) {
        (void)patch_write_u8(menu_scale_sites[SITE_SET_WIDGET_IMAGE].address +
                                 SET_WIDGET_IMAGE_COMPRESS, 1);
    }

    pointer_cage_resize(MENU_SCALE_CANVAS_WIDTH, MENU_SCALE_CANVAS_HEIGHT);
    menu_island_clip_resize(MENU_SCALE_CANVAS_WIDTH, MENU_SCALE_CANVAS_HEIGHT);
    (void)menu_loading_bar_resize(MENU_SCALE_CANVAS_WIDTH, MENU_SCALE_CANVAS_HEIGHT);

    /* Every menu already scaled is put back to its authored rectangles. Without this the fallback
     * is merely non-fatal rather than usable: a screen scaled for a 3840 canvas, drawn against a
     * 640 clip, is a heap of widgets in the top left corner. The engine re-derives the parts it
     * owns on the next open anyway, a picture adopts its bitmap's size and a list box re-runs
     * SWMSG_RESET, so only the authored positions have to be restored here. */
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
                const int32_t *kept =
                    (tracked->shadow != NULL) ? &tracked->shadow[widget * SHADOW_STRIDE] : NULL;

                /* From the rectangle the screen was authored with where there is one, which is
                 * exact; by dividing where the shadow could not be allocated, which is not. */
                if (kept != NULL) {
                    rect[0] = kept[SHADOW_AUTHORED_X];
                    rect[1] = kept[SHADOW_AUTHORED_Y];
                    rect[2] = kept[SHADOW_AUTHORED_W];
                    rect[3] = kept[SHADOW_AUTHORED_H];
                } else {
                    rect[0] = unscaled_coordinate(rect[0], previous_x);
                    rect[1] = unscaled_coordinate(rect[1], previous_y);
                    rect[2] = unscaled_coordinate(rect[2], previous_x);
                    rect[3] = unscaled_coordinate(rect[3], previous_y);
                }
            }
            free(tracked->shadow);
            tracked->shadow = NULL;
            tracked->menu   = NULL;
        }
        scale_state.scaled_menu_count = 0;
    }

    /* The origin, g_menuScale and g_menuTextScale, all four derived the way the engine derives
     * them. Waiting for the engine to do it is not an option: its own block runs on a mode change
     * and this is reached without one. g_menuScale is the piece that used to be missed here, and
     * left behind it keeps the scaled canvas's glyph size against a 640x480 layout. */
    if (!menu_scale_derive_engine_cells(&origin_x, &origin_y)) {
        log_info("  the engine has no screen size yet, so the menu origin and the glyph scale are "
                 "left for its own block to derive on the next mode change");
        return;
    }

    log_info("  menu origin put back to %d,%d, and the glyph scale with it", (int)origin_x,
             (int)origin_y);
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

/* The three scale operands are 0xAC and more past the matched prologue, so before any of them is
 * repointed the instruction in front of each is checked and the three are checked to read one
 * global. A build that matched the prologue and laid the body out differently would otherwise be
 * handed a pointer into an arbitrary instruction. */
static bool sw3d_scale_operands_are_expected(uintptr_t site)
{
    static const uint8_t MOV_EDX_ABS[2] = { 0x8B, 0x15 };
    static const uint8_t MOV_EAX_ABS[1] = { 0xA1 };
    static const uint8_t MOV_ECX_ABS[2] = { 0x8B, 0x0D };
    uint32_t x;
    uint32_t y;
    uint32_t z;

    if (!patch_validate_bytes(site + SW3D_SCALE_OPERAND_X - 2u, MOV_EDX_ABS, sizeof MOV_EDX_ABS) ||
        !patch_validate_bytes(site + SW3D_SCALE_OPERAND_Y - 1u, MOV_EAX_ABS, sizeof MOV_EAX_ABS) ||
        !patch_validate_bytes(site + SW3D_SCALE_OPERAND_Z - 2u, MOV_ECX_ABS, sizeof MOV_ECX_ABS) ||
        !memory_read_u32(site + SW3D_SCALE_OPERAND_X, &x) ||
        !memory_read_u32(site + SW3D_SCALE_OPERAND_Y, &y) ||
        !memory_read_u32(site + SW3D_SCALE_OPERAND_Z, &z) || x != y || y != z ||
        !memory_is_inside_image(x, sizeof(float))) {
        log_warning("sw3d_draw at %08X does not carry the three reads of g_menuScale where the "
                    "retail build has them, so the 3-D widget models keep following the lens",
                    (unsigned)site);
        return false;
    }
    return true;
}

bool menu_scale_install(float configured_ratio, bool cursor_cage_widens)
{
    uintptr_t origin_sites[ORIGIN_SITE_COUNT];
    size_t    origin_hits;
    float     ratio_x;
    float     ratio_y;
    bool      follows_display = false;

    if (scale_state.installed) {
        return true;
    }

    /* Before the artwork test below, and deliberately: the census exists to measure whether that
     * artwork could stop being needed, so it has to run on an installation that has none. */
    (void)menu_art_census_install();

    if (configured_ratio > 0.0f) {
        ratio_x = ratio_y = configured_ratio;  /* an explicit setting, which exists for testing */
    } else if (!ratio_from_artwork(&ratio_x, &ratio_y)) {
        /* No converted set, so the display decides instead and the artwork is replicated to meet
         * it as it loads. That inverts this file's older doctrine, which was that the artwork is
         * the one source of truth because a number read from the pictures cannot disagree with the
         * pictures. It could not survive contact with a resolution that changes: the artwork
         * cannot follow one and the display always can.
         *
         * This is also the only arrangement in which the canvas may be refitted later, and it is
         * recorded as such rather than worked out again: a converted set is a fixed size on disk,
         * so a canvas read from it can only be checked against the display and abandoned. */
        follows_display = true;
        if (!menu_art_load_display_ratio(&ratio_x, &ratio_y)) {
            /* 640x480, or a settings file that could not be read. The authored canvas is the right
             * answer for both, and the scale is installed at it rather than declined, so that a
             * reader who then makes the window larger gets a canvas that grows with it. Every
             * write below is absolute, so at ratio 1 all of them write the shipped numbers. */
            ratio_x = ratio_y = 1.0f;
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

    if (ratio_x <= 1.0f && ratio_y <= 1.0f && !follows_display) {
        log_info("the menu scale is 1, so the menus stay at their authored 640x480 canvas");
        return true;
    }

    /* Declined rather than installed, and this is a correctness gate and not a preference: with the
     * cage shut, every widget the scale moves outside the old 607x447 box becomes unreachable and
     * nothing reports it. See the header. */
    if (!cursor_cage_widens) {
        log_warning("the menus would be drawn on a canvas of %.3f, but WidenMenuCursorArea=0, so "
                    "the drawn cursor would keep the engine's 607x447 clamp while the widgets "
                    "move outside it and become unreachable. NOT scaling, and NOT following the "
                    "display either. Set WidenMenuCursorArea=1 to use this", (double)ratio_y);
        return false;
    }

    signature_resolve_table(menu_scale_sites, SITE_COUNT);
    if (menu_scale_sites[SITE_RLE_BLIT].address == 0 ||
        menu_scale_sites[SITE_MENU_OPEN].address == 0) {
        log_warning("a menu scale of %.3f was asked for, but %s did not resolve, so the menus "
                    "are left at their authored size", (double)ratio_y,
                    (menu_scale_sites[SITE_RLE_BLIT].address == 0) ? "swrle_blit" : "swmenu_open");
        return false;
    }
    /* Exactly two, because the engine computes the menu origin in exactly two places. */
    origin_hits = menu_scale_find_origin_sites(origin_sites, ORIGIN_SITE_COUNT);
    if (origin_hits != ORIGIN_SITE_COUNT) {
        log_warning("the menu origin block matched %u times rather than %u, so the menus are left "
                    "at their authored size", (unsigned)origin_hits, (unsigned)ORIGIN_SITE_COUNT);
        return false;
    }

    scale_state.follows_display = follows_display;

    /* The canvas clip first. On its own it changes nothing visible, because nothing yet draws past
     * canvas 640, which makes it the safest of the writes to have applied on its own. */
    if (!menu_scale_apply_canvas(ratio_x, ratio_y)) {
        log_warning("the canvas clip could not be widened, so the menus are left at their authored "
                    "size");
        return false;
    }

    /* Then the three operands at each of the two origin sites. A failure here puts the canvas back
     * to the authored one, cells and clip together: a widened canvas with a 1x origin draws the
     * menu off centre, which is a worse state than either end of the change. */
    if (!menu_scale_repoint_origin(origin_sites, ORIGIN_SITE_COUNT)) {
        (void)menu_scale_apply_canvas(1.0f, 1.0f);
        return false;
    }

    /* Last of the things that can fail, because it is the only piece that changes what is in
     * memory rather than what the image reads, and because the two above are what make its
     * arithmetic correct. */
    if (!detour_install(&scale_state.menu_open_detour, menu_scale_sites[SITE_MENU_OPEN].address,
                        (const void *)hook_menu_open, MENU_OPEN_PROLOGUE)) {
        (void)menu_scale_apply_canvas(1.0f, 1.0f);
        log_warning("swmenu_open could not be hooked, so nothing is scaled. The origin operands "
                    "stay repointed and are harmless on their own: they read cells that hold the "
                    "authored canvas again");
        return false;
    }

    /* HERE, and not before the gates above, because the redirect is only correct alongside a canvas
     * that was really widened. Armed early, a scale that then declined would leave the engine
     * loading pictures three times the size of a canvas still clipped at 640, which draws the top
     * left corner of every one of them.
     *
     * Armed whatever the ratio came from, because a picture that is already the right size declines
     * itself. That is what lets a converted set, a half converted one and none at all all work: the
     * converted pictures pass straight through and only the ones still at their authored size are
     * replicated. */
    (void)menu_art_load_install(scale_state.ratio_x, scale_state.ratio_y);

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

    /* The row height floor, the drawn cursor's size and the list box text insets. All three are
     * written from the ratio in force and are rewritten from it on every refit, so they live
     * together in menu_scale_refit.c rather than one block each here. */
    menu_scale_apply_trimmings(true);

    if (menu_scale_sites[SITE_SET_WIDGET_IMAGE].address != 0 &&
        patch_write_u8(menu_scale_sites[SITE_SET_WIDGET_IMAGE].address + SET_WIDGET_IMAGE_COMPRESS,
                       COMPRESS_NEVER) == PATCH_RESULT_OK) {
        log_info("save game thumbnails are left uncompressed, which puts them on the surface copy "
                 "path and lets them scale with the canvas like the main menu previews");
    } else {
        log_warning("save game thumbnails could not be left uncompressed, so they stay at their "
                    "authored 160x120 inside a scaled frame. Nothing else is affected");
    }

    if (menu_scale_sites[SITE_SW3D_DRAW].address != 0 &&
        sw3d_scale_operands_are_expected(menu_scale_sites[SITE_SW3D_DRAW].address) &&
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
        detour_install(&scale_state.sw3d_project_detour,
                       menu_scale_sites[SITE_SW3D_PROJECT].address,
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
             (configured_ratio > 0.0f) ? "MenuScale, set by hand"
                                       : (follows_display ? "the display's own size"
                                                          : "read from the artwork"),
             (int)scale_state.canvas_width, (int)scale_state.canvas_height,
             (unsigned)origin_sites[0], (unsigned)origin_sites[1],
             (unsigned)menu_scale_sites[SITE_MENU_OPEN].address);
    if (follows_display) {
        log_info("  and the canvas follows the display: change resolution and the menus are laid "
                 "out again at the new size, with their artwork reloaded to match. That is what "
                 "there being no converted artwork buys, because a converted set is a fixed size "
                 "on disk and a canvas read from it cannot follow anything");
    } else {
        log_info("  the artwork is not upscaled by this: the ratio above was read FROM it, so the "
                 "two cannot disagree, and the canvas is welded to the one resolution that "
                 "artwork was made for");
    }
    return true;
}
