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

#include "common/detour.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The bitmap asked how big the converted artwork is. The front end background: it is authored
 * 640x480, it is always present in a converted set, and it is the first thing drawn, so a
 * mismatch between it and the layout is visible immediately rather than three screens in. */
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
 * swmenu_open
 */
static const uint8_t SIG_MENU_OPEN[] = {
    0x55, 0x8B, 0xEC,                                        /* push ebp / mov ebp,esp         */
    0xA1, 0x70, 0xD3, 0x86, 0x00,                            /* mov eax,[g_swMac.pCurrMenu]    */
    0x3B, 0x45, 0x08,                                        /* cmp eax,[ebp+8]                */
    0x75, 0x0A                                               /* jne                            */
};
#define MENU_OPEN_PROLOGUE 8u

enum {
    SITE_RLE_BLIT,
    SITE_MENU_OPEN,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("swrle_blit", SIG_RLE_BLIT),
    SIGNATURE_ENTRY_DETOUR("swmenu_open", SIG_MENU_OPEN, MENU_OPEN_PROLOGUE)
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

/* A widget array that walks past this many entries without finding its terminator is not a widget
 * array, and scaling whatever it really is would corrupt memory rather than draw a menu. The
 * largest shipped screen holds well under a hundred. */
#define WIDGET_SANITY_LIMIT  512u

/* The engine reopens the same static menu structures over and over, so every one has to be scaled
 * exactly once. There are 22 screens in the shipped game; this is sized well past that and a menu
 * arriving when it is full is declined rather than scaled twice. */
#define SCALED_MENU_CAPACITY 64u

typedef int32_t(__cdecl *menu_open_fn_t)(void *menu);

typedef struct menu_scale_state {
    bool      installed;
    float     ratio_x;
    float     ratio_y;
    int32_t   canvas_width;
    int32_t   canvas_height;

    detour_t  menu_open_detour;

    const void *scaled_menus[SCALED_MENU_CAPACITY];
    size_t      scaled_menu_count;
    bool        warned_capacity;
    bool        warned_sanity;
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

/* Has this menu already been scaled? The engine hands back the same pointers for the life of the
 * process, so pointer identity is the whole test. */
static bool menu_already_scaled(const void *menu)
{
    size_t index;

    for (index = 0; index < scale_state.scaled_menu_count; ++index) {
        if (scale_state.scaled_menus[index] == menu) {
            return true;
        }
    }
    return false;
}

static bool remember_scaled_menu(const void *menu)
{
    if (scale_state.scaled_menu_count >= SCALED_MENU_CAPACITY) {
        if (!scale_state.warned_capacity) {
            scale_state.warned_capacity = true;
            log_warning("more than %u distinct menus have been opened, so this one is left at its "
                        "authored size rather than risk scaling another one twice",
                        (unsigned)SCALED_MENU_CAPACITY);
        }
        return false;
    }
    scale_state.scaled_menus[scale_state.scaled_menu_count++] = menu;
    return true;
}

/* Multiplies one menu's widget rectangles by the scale, once. Everything downstream reads these
 * same numbers: the blitter, the focus outline, the hit test, the slider and the list box. */
static void scale_widgets(void *menu)
{
    char    *widgets;
    uint32_t index;

    widgets = *(char *const *)((char *)menu + MENU_WIDGET_ARRAY);
    if (widgets == NULL) {
        return;
    }

    for (index = 0; index < WIDGET_SANITY_LIMIT; ++index) {
        char *widget = widgets + (size_t)index * WIDGET_STRIDE;

        if (*(const int32_t *)(widget + WIDGET_TYPE) == WIDGET_TERMINATOR) {
            return;                                  /* the array ended where it should have    */
        }

        int32_t *rect = (int32_t *)(widget + WIDGET_RECT_X);

        rect[0] = scaled_coordinate(rect[0], scale_state.ratio_x);   /* x      */
        rect[1] = scaled_coordinate(rect[1], scale_state.ratio_y);   /* y      */
        rect[2] = scaled_coordinate(rect[2], scale_state.ratio_x);   /* width  */
        rect[3] = scaled_coordinate(rect[3], scale_state.ratio_y);   /* height */
    }

    /* Ran off the end without a terminator. The widgets already touched keep their new size, which
     * is the correct outcome: they are a real menu whatever follows them is. */
    if (!scale_state.warned_sanity) {
        scale_state.warned_sanity = true;
        log_warning("a menu's widget array ran past %u entries with no terminator, so the rest of "
                    "it was left alone", (unsigned)WIDGET_SANITY_LIMIT);
    }
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
    FILE         *file = fopen(MENU_SCALE_WITNESS_BITMAP, "rb");

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

static int32_t __cdecl hook_menu_open(void *menu)
{
    menu_open_fn_t original = (menu_open_fn_t)scale_state.menu_open_detour.original;

    if (original == NULL) {
        return 0;                                    /* the un-armed instant between write and
                                                      * state, the same guard every detour here
                                                      * carries */
    }

    /* BEFORE the original, because the original is what makes the menu current and starts drawing
     * from it. Scaling afterwards would leave one frame at the authored size. */
    if (menu != NULL && !menu_already_scaled(menu) && remember_scaled_menu(menu)) {
        scale_widgets(menu);
    }

    return original(menu);
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

    scale_state.installed = true;
    log_info("menus scaled %.3f wide by %.3f high (%s): canvas %dx%d, origin and g_menuScale "
             "recentred at %08X and %08X, widget rectangles scaled on open at %08X",
             (double)ratio_x, (double)ratio_y,
             (configured_ratio > 0.0f) ? "MenuScale, set by hand" : "read from the artwork",
             (int)scale_state.canvas_width, (int)scale_state.canvas_height,
             (unsigned)origin_sites[0], (unsigned)origin_sites[1],
             (unsigned)sites[SITE_MENU_OPEN].address);
    log_info("  the pause panel rewrites its own rectangles every frame and is NOT scaled by this; "
             "the artwork is not upscaled either, so bitmaps keep their authored size");
    return true;
}
