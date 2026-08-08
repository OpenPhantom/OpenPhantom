/* fov_menu.c: the slider, its caption, and how it is driven without rewriting menu code.
 *
 * ==============================================================================================
 * Where the pointer comes from
 *
 * options_video 0x4410F2 passes the widget array as ONE `push imm32` (site + 0x1A,
 * `push 0x4AFB88`). common/menu_patcher.c copies the authored entries into our own array,
 * appends two and repoints that immediate. No engine code is rewritten.
 *
 * The eleven authored entries, read out of the retail .data at [0x4AFB88]:
 *   [0] SW_PIC 50 splashol.bmp   [1] SW_PIC 97 dialog.bmp   [2] SW_SLIDER 0x5E GAMMA
 *   [3] SW_TEXT 99 BACK          [4..6] SW_CHECKBOX 0,1,2   [7] SW_LISTBOX 0x5F MODE LIST
 *   [8] SW_TEXT 0x60 APPLY       [9] SW_TEXT 98 title       [10] SW_TEXT 93 "GAMMA" caption
 * We refuse to touch anything that does not have exactly that shape at 0x5E / 0x5F / 0x60.
 *
 * ==============================================================================================
 * How the widget is driven, without touching options_video's hand-written navigation switch
 *
 *   * swrle_windowProc 0x460A54 forwards WM_KEYDOWN to pMenu->pFocus as SWMSG_KEYDOWN, and
 *     swslider_input 0x461C90 turns VK_LEFT / VK_RIGHT into swslider_step. So Left/Right work on
 *     the focused slider by themselves; that is also how the gamma slider works, and the reason
 *     options_video only ever READS the gamma notch back.
 *   * The mouse hit-test focuses it and swslider_input tracks the drag, with the engine's
 *     32-pixel grab slop around the widget.
 *   * Tab -> SWNAV_TAB -> swmenu_focusNext 0x45EA94 -> swwidget_focusNext, which walks the ARRAY
 *     in index order and takes any entry with action != SW_ACTION_STATIC and visible == 1.
 *   * Up/Down do NOT reach it: options_video's arrow switch is a hand-written graph over the ids
 *     0,1,2,0x5E,0x5F,0x60,99 and everything else falls into `default: break`. Tab, the mouse and
 *     Escape all work. That is stated plainly rather than patched around, because moving that
 *     switch means rewriting a jump table for a convenience.
 *
 * The value is read straight out of our own array, the engine writes `state` into our memory,
 * so no engine call is needed to poll it, and nothing can go wrong when the screen is closed.
 *
 * ==============================================================================================
 * Why there is only one slider, and the two that were removed were removed on evidence
 *
 *   VIEW DISTANCE tore the picture apart. The log showed the slider pushing the range up and the
 *   cell watchdog taking it back in the same frame, 1.50 -> 1.35, 1.60 -> 1.45, 1.70 -> 1.55,
 *   frame after frame. Two controls on one number, working against each other, make the geometry
 *   pop in rhythm. That is NOT an argument against the range itself, which still works from the
 *   ini; it is an argument against a slider whose value a control loop immediately overwrites.
 *
 *   ASPECT MODE was simply not what was wanted: both settings change the HORIZONTAL too, because
 *   the engine only has one number. As a mode switch it belongs in the ini, not next
 *   to a slider it silently moves.
 */
#include "fov_menu.h"

#include "variable_fov.h"
#include "fov_math.h"
#include "fov_strings.h"

#include "common/detour.h"
#include "common/engine_types.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/menu_patcher.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* --- 0x004410F2  options_video: the video options screen and the pointer to its widget table, *
 *   55 8B EC 81 EC 88000000     prologue, 9 bytes, clean boundary -> the detour lands here
 *   C7 45 B0 FFFFFFFF           result = -1
 *   C7 45 A0 00000000
 *   6A 00                       push selInit = 0
 *   68 88FB4A00                 push pWidgets = 0x4AFB88     <- THE WIDGET TABLE, imm at +0x1A
 *   68 F0ED4A00                 push pFontNames = 0x4AEDF0
 *   6A 00                       push field18 = 0
 *   68 10EE4A00                 push pBmpNames = 0x4AEE10
 *   68 E4DF6C00                 push pMenu = 0x6CDFE4 (g_menuScreen[5])
 *   E8 <rel32>                  call swmenu_build 0x45E7A3                                      */
static const uint8_t SIG_OPTIONS_VIDEO[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
    0xC7, 0x45, 0xB0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC7, 0x45, 0xA0, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x00,
    0x68, 0x88, 0xFB, 0x4A, 0x00,
    0x68, 0xF0, 0xED, 0x4A, 0x00,
    0x6A, 0x00,
    0x68, 0x10, 0xEE, 0x4A, 0x00,
    0x68, 0xE4, 0xDF, 0x6C, 0x00,
    0xE8
};
#define OPTIONS_VIDEO_PROLOGUE_SIZE   9u
#define OPTIONS_VIDEO_PUSH_OFFSET  0x19u   /* the 0x68 opcode, verified before the operand */
#define OPTIONS_VIDEO_TABLE_OFFSET 0x1Au   /* the widget-table pointer */
/* The third `push imm32` of the same prologue: the screen's bitmap-name table. Every bitmap index
 * this file appends is bounds-checked against its length, because the engine will not. */
#define OPTIONS_VIDEO_BMP_PUSH_OFFSET  0x25u
#define OPTIONS_VIDEO_BMP_TABLE_OFFSET 0x26u
#define OPCODE_PUSH_IMM32          0x68u

/* Ids the shipped screen does not use. Authored: 0,1,2,50,93,94,95,96,97,98,99. */
#define WIDGET_ID_FOV_SLIDER 0x70
#define WIDGET_ID_FOV_LABEL  0x71

/* Ids the shipped screen MUST have, or this is not the video screen we think it is. */
#define WIDGET_ID_GAMMA      0x5E
#define WIDGET_ID_MODE_LIST  0x5F
#define WIDGET_ID_APPLY      0x60

/* Indices into the video screen's own bitmap-name table [0x4AEE10]:
 *   2 = slgauge.bmp (250x50, the track)   3 = slslide.bmp (25x50, the knob)
 * The gamma slider already carries exactly these two, so reusing them costs no new resource. */
#define BITMAP_GAUGE 2
#define BITMAP_KNOB  3
/* Font table [0x4AEDF0]: 0 = "indust", 1 = "sysfont", 2 = "courier". The gamma caption uses 1. */
#define FONT_CAPTION 1

/* ---- The layout, and the fact that decided it ------------------------------------------------
 *
 * There is no plate, and there cannot be one. Two previous versions of this file put the pair on
 * `popup.bmp` and reasoned about cropping it to size. Both were built on a false premise: the blit
 * every widget in this toolkit ends in takes canvas, bitmap, x and y and NO width and NO height,
 * and clips only against a hard-coded 640x480. A rectangle therefore cannot crop or scale a bitmap.
 * `popup.bmp` is 300x200 and draws 300x200 wherever it is put, and the only free space on this
 * screen is 108 pixels tall. No plate in the shared table fits it, popup is 200 tall and rusure
 * is 148, so the pair is drawn without one.
 *
 * The authored rectangles, read out of the retail table at [0x4AFB88]:
 *
 *     check box 0   50,155,300,50        gamma slider   325,250,250,50
 *     check box 1   50,205,300,50        gamma caption  325,300,250,50
 *     check box 2   50,255,300,50        mode list      300, 50,300,100
 *     title         50, 25,200,50        apply          300,150,300,50
 *     back         484,400,106, 67
 *
 * and the background bitmap `dialog.bmp` is opaque over columns 23..621, rows 10..371 (plus a
 * one-pixel hairline at column 299 that runs down to row 446), measured from the extracted artwork.
 * BACK's own artwork plate occupies columns 486..591, rows 399..465.
 *
 * The artwork and the rectangle disagree by two columns, and the hit test uses the rectangle.
 * BACK's plate starts at x=486 but its widget rectangle starts at x=484, and the widget hit test
 * walks the table from index 0 and stops at the FIRST widget containing the point, with the
 * authored widgets ahead of every appended one. A slider reaching x=484 would therefore hand its
 * two rightmost columns to BACK, so dragging to maximum could leave the screen instead of setting
 * the value. The strip this file may use is x 0..483, y 372..479.
 *
 * The pair is STACKED, slider over caption, both flush to the left edge of the canvas. Side by side
 * was tried first and rejected on the picture rather than on the numbers: the caption sat in the
 * middle of an otherwise empty strip with the gauge floating to its right, and the two read as two
 * unrelated things. Stacked and hard left they read as one control, and the eye finds the bar
 * before the words that name it, which is the order the authored screen uses for gamma.
 *
 * The strip is 108 rows and the pair needs 100, so the margin is four rows top and bottom. That is
 * tight by choice: the alternative was to keep a comfortable margin and put the caption somewhere
 * it does not belong.
 *
 * And why this screen gets no borrowed panel, unlike anything the controls screen might want.
 * The audio screen's panel art sits on the RIGHT, where this screen already has its mode list at
 * 300,50,300,100 and its apply button at 300,150,300,50. */
#define FREE_STRIP_X       0
#define FREE_STRIP_Y     372
/* Inclusive, and bounded by BACK's RECTANGLE (484) rather than by its artwork plate (486): the
 * hit test compares against the rectangle and the authored BACK is tested before anything this
 * file appends, so the last two columns of the plate-free gap are not ours to use. */
#define FREE_STRIP_RIGHT 483
#define FREE_STRIP_BOTTOM 479

/* Hard left: the gauge starts at the canvas edge, and the caption is left-aligned under it on the
 * same column so the two share an edge. `LABEL_WIDTH` matches the gauge so the caption's own centre
 * is the gauge's centre, an appended label carries link = 0, which is centred alignment, so the
 * width is what positions the text. */
#define SLIDER_X        0
#define SLIDER_Y      376
#define LABEL_X         0
#define LABEL_Y       426
#define LABEL_WIDTH   250

/* The gauge bitmap is 250x50 and the slider is redrawn to exactly that on every screen open, so
 * these two are what the engine will really occupy rather than merely what was asked for. */
#define GAUGE_WIDTH   250
#define GAUGE_HEIGHT   50
#define WIDGET_WIDTH  GAUGE_WIDTH
#define WIDGET_HEIGHT GAUGE_HEIGHT

/* These assert the drawn footprint, not the requested one. The previous version checked the pair
 * against a plate rectangle using a width the engine discards, which is an assertion about a number
 * that never reaches the screen, worse than no assertion, because it reads like a guarantee.
 * A slider really occupies exactly the gauge bitmap's 250x50 from its own (x,y). */
_Static_assert(SLIDER_X >= FREE_STRIP_X && SLIDER_X + GAUGE_WIDTH - 1 <= FREE_STRIP_RIGHT,
               "the field-of-view slider reaches the BACK button's own rectangle, which is hit-"
               "tested first and would swallow the slider's rightmost columns");
_Static_assert(SLIDER_Y >= FREE_STRIP_Y && SLIDER_Y + GAUGE_HEIGHT - 1 <= FREE_STRIP_BOTTOM,
               "the field-of-view slider leaves the free strip vertically");
_Static_assert(LABEL_X >= FREE_STRIP_X && LABEL_X + LABEL_WIDTH - 1 <= FREE_STRIP_RIGHT,
               "the field-of-view caption leaves the free strip horizontally");
/* Stacked, so the separation is vertical: the caption begins where the gauge ends, and the pair
 * must still finish inside the strip. */
_Static_assert(LABEL_Y >= SLIDER_Y + GAUGE_HEIGHT,
               "the field-of-view caption overlaps its own slider");
_Static_assert(LABEL_Y + WIDGET_HEIGHT - 1 <= FREE_STRIP_BOTTOM,
               "the field-of-view caption leaves the free strip vertically");

/* Notch n means an ABSOLUTE horizontal field of view of `min + n * step` degrees, so the number
 * on the slider is the number in the caption and the left end reads exactly SliderMinFovDegrees.
 *
 * It used to mean "+n * step on top of whatever the aspect mode computed", which had one real
 * flaw: on any frame wider than 4:3 the aspect correction already puts the horizontal well above
 * 60, and an offset that only ever ADDS cannot come back down to it. At 16:9 the leftmost notch
 * read about 75 deg and there was no way to ask for less. The offset is still what gets stored and
 * applied; it is simply computed from the angle the player picked. */
#define NOTCH_STEP_DEGREES 2.0f
#define MAX_NOTCHES       64
#define WIDGET_CAPACITY   80

typedef int32_t (__cdecl *options_video_fn_t)(void);

typedef struct fov_menu_state {
    bool                 armed;          /* everything resolved and written; only then do we poll */
    bool                 screen_open;
    bool                 live_preview;   /* the per-frame hook stands */

    menu_patch_context_t patch;
    sw_widget_t          widgets[WIDGET_CAPACITY];
    size_t               slider_index;
    int                  notch_count;
    int                  seed_notch;
    int                  last_notch;
    char                 caption[64];

    detour_t             options_video_detour;
} fov_menu_state_t;

static fov_menu_state_t menu_state;

/* ============================================================================================ */
static int clamped_slider_notch(void)
{
    int notch = (int)menu_state.widgets[menu_state.slider_index].state;

    if (notch < 0) {
        notch = 0;
    }
    if (notch > menu_state.notch_count - 1) {
        notch = menu_state.notch_count - 1;
    }
    return notch;
}

/* The absolute angle a notch stands for, and the offset that reaches it from what the aspect mode
 * computed on its own. The base is asked for fresh every time because it changes with the frame:
 * the same notch is the same ANGLE at every resolution, which is the whole point of making the
 * slider absolute, and the offset behind it differs. */
static float degrees_for_notch(int notch)
{
    return (float)variable_fov_slider_min_fov_degrees() + (float)notch * NOTCH_STEP_DEGREES;
}

static float offset_for_notch(int notch)
{
    float base = variable_fov_base_horizontal_degrees();

    /* No base yet, or ASPECT_MODE_STRETCH, where there is no computed field to offset from. Fall
     * back to treating the notch as the old pure offset rather than writing a wild number. */
    if (!(base > 0.0f)) {
        return (float)notch * NOTCH_STEP_DEGREES;
    }
    return degrees_for_notch(notch) - base;
}

static int notch_for_current_view(void)
{
    float base = variable_fov_base_horizontal_degrees();
    float absolute;
    int   notch;

    absolute = (base > 0.0f) ? base + variable_fov_extra_degrees()
                             : (float)variable_fov_slider_min_fov_degrees();

    notch = (int)((absolute - (float)variable_fov_slider_min_fov_degrees()) / NOTCH_STEP_DEGREES + 0.5f);
    if (notch < 0) {
        notch = 0;
    }
    if (notch > menu_state.notch_count - 1) {
        notch = menu_state.notch_count - 1;
    }
    return notch;
}

/* The caption names the RESULT, not the offset; that is the number a player can judge, and it
 * is the number variable_fov really wrote into rdCamera+0x38. And it names BOTH axes: the vertical
 * falls out of the horizontal and the canvas height, so it belongs displayed rather
 * than controlled. */
static void update_caption(void)
{
    float horizontal = variable_fov_horizontal_degrees();
    float vertical   = variable_fov_vertical_degrees();

    if (horizontal >= FOV_MIN_DEGREES && vertical >= 1.0f) {
        _snprintf(menu_state.caption, sizeof(menu_state.caption),
                  fov_string(FOV_STRING_HORIZONTAL_AND_VERTICAL),
                  (double)horizontal, (double)vertical);
    } else if (horizontal >= FOV_MIN_DEGREES) {
        _snprintf(menu_state.caption, sizeof(menu_state.caption),
                  fov_string(FOV_STRING_HORIZONTAL_ONLY), (double)horizontal);
    } else {
        _snprintf(menu_state.caption, sizeof(menu_state.caption),
                  "%s", fov_string(FOV_STRING_NO_PROJECTION));
    }
    menu_state.caption[sizeof(menu_state.caption) - 1] = '\0';
}

/* The single apply path, used both for the live update and for the menu-close update, so the two
 * can never disagree. `force` skips the "did the notch actually move" test. */
static bool apply_notch(bool force)
{
    int notch = clamped_slider_notch();

    if (!force && notch == menu_state.last_notch) {
        return false;
    }
    menu_state.last_notch = notch;

    /* Save immediately rather than only on leaving the screen. A crash or a hung graphics wrapper
     * must not swallow a setting the user has just made, which is exactly what happened once.
     * WritePrivateProfileString buffers; with a 31-notch slider that is not measurable work. */
    variable_fov_set_extra_degrees(offset_for_notch(notch));

    /* AFTER the refresh, never before: the caption quotes numbers the rebuild has just set. */
    update_caption();
    return true;
}

/* Runs inside options_video's own `while (result < 0) { sys_frame(); ... }` loop, because
 * render_frameEnd is part of sys_frame. */
static void on_frame(void)
{
    if (!menu_state.screen_open || !menu_state.armed) {
        return;
    }
    apply_notch(false);
}

static int32_t __cdecl hook_options_video(void)
{
    options_video_fn_t original = (options_video_fn_t)menu_state.options_video_detour.original;
    int32_t            result;
    int                notch;

    if (!menu_state.armed) {
        return original();
    }

    menu_state.seed_notch = notch_for_current_view();
    menu_state.last_notch = menu_state.seed_notch;
    menu_state.widgets[menu_state.slider_index].state = menu_state.seed_notch;
    update_caption();

    menu_state.screen_open = true;
    log_info("video options opened, slider seeded at notch %d (%.0f deg horizontal, which is "
             "%+.1f deg on top of the %.1f this aspect mode computes by itself)",
             menu_state.seed_notch, (double)degrees_for_notch(menu_state.seed_notch),
             (double)offset_for_notch(menu_state.seed_notch),
             (double)variable_fov_base_horizontal_degrees());

    result = original();

    menu_state.screen_open = false;

    /* The fallback that must really exist: without the per-frame hook nothing polled the slider
     * while it moved, so the final value is read and applied here. With the hook this is still
     * correct, the screen may have closed on a frame that never polled. */
    notch = clamped_slider_notch();
    if (notch != menu_state.seed_notch) {
        apply_notch(true);
        log_info("video options closed - offset %.1f deg (notch %d), hFOV now %.3f",
                 (double)variable_fov_extra_degrees(), notch, (double)variable_fov_horizontal_degrees());
    } else {
        log_info("video options closed, the slider was not touched, the ini is left alone");
    }

    return result;
}

/* ============================================================================================ */
static bool table_has_shipped_shape(const menu_patch_context_t *context)
{
    bool has_gamma = false;
    bool has_list  = false;
    bool has_apply = false;
    size_t index;

    for (index = 0; index < context->original_count; ++index) {
        const sw_widget_t *widget = &context->widgets[index];

        if (widget->id == WIDGET_ID_GAMMA     && widget->type == SW_TYPE_SLIDER)  { has_gamma = true; }
        if (widget->id == WIDGET_ID_MODE_LIST && widget->type == SW_TYPE_LISTBOX) { has_list  = true; }
        if (widget->id == WIDGET_ID_APPLY     && widget->type == SW_TYPE_TEXT)    { has_apply = true; }
    }

    if (has_gamma && has_list && has_apply) {
        return true;
    }

    log_error("the table at the video screen does not have the shipped shape "
              "(gamma slider 0x5E=%d, mode list 0x5F=%d, apply 0x60=%d), refused",
              has_gamma ? 1 : 0, has_list ? 1 : 0, has_apply ? 1 : 0);
    return false;
}

static bool resolve_widget_table(uintptr_t site, uintptr_t *out_pointer_address,
                                 uintptr_t *out_table_address)
{
    uint8_t  opcode = 0;
    uint32_t table  = 0;

    if (!memory_read_u8(site + OPTIONS_VIDEO_PUSH_OFFSET, &opcode) ||
        opcode != OPCODE_PUSH_IMM32) {
        log_error("expected `push imm32` (68) at %08X, found %02X - refused",
                  (unsigned)(site + OPTIONS_VIDEO_PUSH_OFFSET), opcode);
        return false;
    }
    if (!memory_read_u32(site + OPTIONS_VIDEO_TABLE_OFFSET, &table)) {
        return false;
    }

    *out_pointer_address = site + OPTIONS_VIDEO_TABLE_OFFSET;
    *out_table_address   = table;
    return true;
}

/* The screen's own bitmap-name table, from the third `push imm32` of the same prologue. It is what
 * bounds every bitmap index appended below: the engine's own lookup rejects a negative index and
 * checks nothing else, so without this table an index one past the end is an unchecked read. */
static bool resolve_bitmap_name_table(uintptr_t site, uintptr_t *out_table_address)
{
    uint8_t  opcode = 0;
    uint32_t table  = 0;

    if (!memory_read_u8(site + OPTIONS_VIDEO_BMP_PUSH_OFFSET, &opcode) ||
        opcode != OPCODE_PUSH_IMM32) {
        log_error("expected `push imm32` (68) for the bitmap-name table at %08X, found %02X - "
                  "refused", (unsigned)(site + OPTIONS_VIDEO_BMP_PUSH_OFFSET), opcode);
        return false;
    }
    if (!memory_read_u32(site + OPTIONS_VIDEO_BMP_TABLE_OFFSET, &table) || table == 0) {
        return false;
    }

    *out_table_address = (uintptr_t)table;
    return true;
}

static bool build_widgets(uintptr_t site)
{
    uintptr_t pointer_address = 0;
    uintptr_t table_address   = 0;
    uintptr_t bitmap_names    = 0;

    if (!resolve_widget_table(site, &pointer_address, &table_address) ||
        !resolve_bitmap_name_table(site, &bitmap_names)) {
        return false;
    }
    if (!menu_patcher_begin(&menu_state.patch, table_address, pointer_address, bitmap_names,
                            menu_state.widgets, WIDGET_CAPACITY)) {
        return false;
    }
    if (!table_has_shipped_shape(&menu_state.patch)) {
        return false;
    }

    /* NO PLATE. See the layout above: this toolkit cannot crop or scale a bitmap, and no plate in
     * the shared table is short enough for the 108-pixel strip that is free. The caption is drawn
     * over the screen's own background, which is opaque there. */
    if (!menu_patcher_append_slider(&menu_state.patch, WIDGET_ID_FOV_SLIDER,
                                    menu_state.notch_count, SLIDER_X, SLIDER_Y,
                                    WIDGET_WIDTH, WIDGET_HEIGHT, BITMAP_KNOB, BITMAP_GAUGE,
                                    &menu_state.slider_index)) {
        return false;
    }
    if (!menu_patcher_append_label(&menu_state.patch, WIDGET_ID_FOV_LABEL, LABEL_X, LABEL_Y,
                                   LABEL_WIDTH, WIDGET_HEIGHT, FONT_CAPTION,
                                   menu_state.caption, NULL)) {
        return false;
    }
    return true;
}

void fov_menu_install(void)
{
    uintptr_t site;

    if (menu_state.armed) {
        return;
    }
    if (!variable_fov_is_active()) {
        log_info("the camera hook is not active, the slider would drive nothing, so it is NOT "
                 "added");
        return;
    }

    menu_state.notch_count = (variable_fov_slider_max_fov_degrees() - variable_fov_slider_min_fov_degrees())
                           / (int)NOTCH_STEP_DEGREES + 1;
    if (menu_state.notch_count < 2) {
        menu_state.notch_count = 2;                /* swslider divides by start - 1 */
    }
    if (menu_state.notch_count > MAX_NOTCHES) {
        menu_state.notch_count = MAX_NOTCHES;
    }

    site = signature_find_unique(SIG_OPTIONS_VIDEO, NULL, sizeof(SIG_OPTIONS_VIDEO));
    if (site == 0) {
        log_warning("options_video did not resolve, no field-of-view slider");
        return;
    }

    update_caption();
    if (!build_widgets(site)) {
        return;
    }

    /* The detour FIRST: without it the slider would move and mean nothing, so a failed detour
     * must leave the screen exactly as it shipped, and menu_patcher_commit() has not run yet,
     * so at this point the engine still owns its own array. */
    if (!detour_install(&menu_state.options_video_detour, site,
                        (const void *)hook_options_video, OPTIONS_VIDEO_PROLOGUE_SIZE)) {
        log_error("the options_video detour at %08X failed, the screen is left untouched",
                  (unsigned)site);
        return;
    }
    if (!menu_patcher_commit(&menu_state.patch)) {
        log_error("the widget table could not be repointed, the detour is inert");
        return;
    }

    menu_state.armed = true;
    log_info("options_video hooked at %08X; slider id %d with %d notches x %.1f deg, selecting an "
             "ABSOLUTE horizontal field of view of %d..%d deg (this aspect mode computes %.1f on "
             "its own, so the range spans %+.1f..%+.1f of offset); caption id %d",
             (unsigned)site, WIDGET_ID_FOV_SLIDER, menu_state.notch_count,
             (double)NOTCH_STEP_DEGREES,
             variable_fov_slider_min_fov_degrees(),
             (int)degrees_for_notch(menu_state.notch_count - 1),
             (double)variable_fov_base_horizontal_degrees(),
             (double)offset_for_notch(0),
             (double)offset_for_notch(menu_state.notch_count - 1),
             WIDGET_ID_FOV_LABEL);

    menu_state.live_preview = frame_hook_add(on_frame);
    if (!menu_state.live_preview) {
        log_warning("the per-frame hook is unavailable. Live preview is disabled, but the value "
                    "is still read, applied and saved when the screen closes.");
    }
}
