/* menu_loading_bar.c: the two sites and the ten numbers. See menu_loading_bar.h for what and why.
 *
 * ==============================================================================================
 * The sites
 *
 * menu_progressStep, retail 0x00446A4B, matched from 0x00446A54. Every number the bar is drawn
 * from is set up in one contiguous prologue block before the loop that draws it, which is what
 * makes this one pattern rather than eight:
 *
 *   00446A54  A1 5C FD 6C 00     mov  eax,[g_uiOriginY]
 *   00446A59  05 90 01 00 00     add  eax,400            <- y of the bar, at +0x06
 *   ...
 *   00446A6D  D8 05 7C 86 4A 00  fadd [32.0f]            <- its height, operand at +0x1B
 *   00446A76  8B 0D 58 FD 6C 00  mov  ecx,[g_uiOriginX]
 *   00446A7C  81 C1 BE 01 00 00  add  ecx,0x1be          <- x of the bar, at +0x2A
 *   ...
 *   00446A91  D8 05 80 86 4A 00  fadd [128.0f]           <- its width, operand at +0x3F
 *   00446AA0  81 C2 86 01 00 00  add  edx,0x186          <- x of the backdrop, at +0x4E
 *   00446AAE  05 5E 01 00 00     add  eax,0x15e          <- y of the backdrop, at +0x5B
 *   00446AB6  C7 45 F4 F0 ...    mov  [ebp-0xc],0xf0     <- its width,  at +0x65
 *   00446ABD  C7 45 F8 54 ...    mov  [ebp-0x8],0x54     <- its height, at +0x6C
 *
 * The pattern runs on to the colour constant 0xff00ffff, which costs nothing and makes it
 * unmistakable.
 *
 * ui_progress, retail 0x004467B9, matched from 0x0044688F for the two text origin offsets. The
 * call to graphics_getWidth between them is masked because its displacement is relative.
 *
 * ==============================================================================================
 * Why the two float constants are repointed and the eight integers are overwritten
 *
 * The integers are immediates inside instructions, so there is one reader each and writing them is
 * local by construction. The 32.0f and 128.0f are memory operands, and a constant that size is
 * exactly the kind the compiler pools: editing the constant would move every other reader of it in
 * the image. Repointing the operand moves one instruction, which is the same rule menu_scale.c
 * follows for the menu origin.
 */
#include "menu_loading_bar.h"

#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The canvas the offsets below are authored in. Kept here rather than included from menu_scale.h
 * so this file states the assumption it actually depends on. */
#define LOADING_CANVAS_WIDTH  640
#define LOADING_CANVAS_HEIGHT 480

/* ---------------------------------------------------------------------------------------------
 * menu_progressStep's geometry block
 */
static const uint8_t SIG_PROGRESS_GEOMETRY[] = {
    0xA1, 0x5C, 0xFD, 0x6C, 0x00, 0x05, 0x90, 0x01, 0x00, 0x00, 0x89, 0x85,
    0x6C, 0xFF, 0xFF, 0xFF, 0xDB, 0x85, 0x6C, 0xFF, 0xFF, 0xFF, 0xD9, 0x55,
    0xAC, 0xD8, 0x05, 0x7C, 0x86, 0x4A, 0x00, 0xD9, 0x5D, 0xB4, 0x8B, 0x0D,
    0x58, 0xFD, 0x6C, 0x00, 0x81, 0xC1, 0xBE, 0x01, 0x00, 0x00, 0x89, 0x8D,
    0x68, 0xFF, 0xFF, 0xFF, 0xDB, 0x85, 0x68, 0xFF, 0xFF, 0xFF, 0xD9, 0x55,
    0xA4, 0xD8, 0x05, 0x80, 0x86, 0x4A, 0x00, 0xD9, 0x5D, 0xFC, 0x8B, 0x15,
    0x58, 0xFD, 0x6C, 0x00, 0x81, 0xC2, 0x86, 0x01, 0x00, 0x00, 0x89, 0x55,
    0xEC, 0xA1, 0x5C, 0xFD, 0x6C, 0x00, 0x05, 0x5E, 0x01, 0x00, 0x00, 0x89,
    0x45, 0xF0, 0xC7, 0x45, 0xF4, 0xF0, 0x00, 0x00, 0x00, 0xC7, 0x45, 0xF8,
    0x54, 0x00, 0x00, 0x00, 0xC7, 0x45, 0xB0, 0xFF, 0xFF, 0x00, 0xFF
};

#define BAR_Y_OFFSET          0x06u   /* add eax,400        */
#define BAR_HEIGHT_OPERAND    0x1Bu   /* fadd [32.0f]       */
#define BAR_X_OFFSET          0x2Au   /* add ecx,0x1be      */
#define BAR_WIDTH_OPERAND     0x3Fu   /* fadd [128.0f]      */
#define BACKDROP_X_OFFSET     0x4Eu   /* add edx,0x186      */
#define BACKDROP_Y_OFFSET     0x5Bu   /* add eax,0x15e      */
#define BACKDROP_WIDTH        0x65u   /* mov [ebp-0xc],0xf0 */
#define BACKDROP_HEIGHT       0x6Cu   /* mov [ebp-0x8],0x54 */

/* ---------------------------------------------------------------------------------------------
 * ui_progress's text origin
 */
static const uint8_t SIG_TEXT_ORIGIN[] = {
    0x8B, 0x0D, 0x58, 0xFD, 0x6C, 0x00,              /* mov ecx,[g_uiOriginX]              */
    0x81, 0xC1, 0xFE, 0x01, 0x00, 0x00,              /* add ecx,0x1fe        <- at +0x08   */
    0x89, 0x4D, 0xF4, 0xDB, 0x45, 0xF4, 0xD9, 0x5D, 0xF0,
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call graphics_getWidth, masked     */
    0x89, 0x45, 0xE8, 0xC7, 0x45, 0xEC, 0x00, 0x00, 0x00, 0x00,
    0xDF, 0x6D, 0xE8, 0xD8, 0x7D, 0xF0,
    0xD9, 0x1D, 0x04, 0xE5, 0x6C, 0x00,              /* fstp [g_loadTextX]                 */
    0x8B, 0x15, 0x5C, 0xFD, 0x6C, 0x00,              /* mov edx,[g_uiOriginY]              */
    0x81, 0xC2, 0x7C, 0x01, 0x00, 0x00               /* add edx,0x17c        <- at +0x38   */
};
static const uint8_t MSK_TEXT_ORIGIN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_TEXT_ORIGIN == sizeof MSK_TEXT_ORIGIN,
               "the loading bar text origin pattern and its mask are different lengths");

#define TEXT_X_OFFSET 0x08u
#define TEXT_Y_OFFSET 0x38u

enum {
    SITE_PROGRESS_GEOMETRY,
    SITE_TEXT_ORIGIN,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("menu_progressStep geometry", SIG_PROGRESS_GEOMETRY),
    SIGNATURE_ENTRY_MASKED("ui_progress text origin", SIG_TEXT_ORIGIN, MSK_TEXT_ORIGIN)
};

/* The two cells the float operands are repointed at. Their shipped values are the defaults, so a
 * half-done install leaves the engine's own numbers. */
static float loading_bar_width  = 128.0f;
static float loading_bar_height = 32.0f;

static bool installed;

static int32_t scaled(int32_t value, float ratio)
{
    return (int32_t)((float)value * ratio + 0.5f);
}

bool menu_loading_bar_install(int32_t canvas_width, int32_t canvas_height)
{
    float ratio_x;
    float ratio_y;

    if (installed) {
        return true;
    }
    if (canvas_width <= 0 || canvas_height <= 0) {
        return false;
    }

    ratio_x = (float)canvas_width  / (float)LOADING_CANVAS_WIDTH;
    ratio_y = (float)canvas_height / (float)LOADING_CANVAS_HEIGHT;
    if (ratio_x <= 1.0f && ratio_y <= 1.0f) {
        return false;                 /* the canvas is authored size: the shipped numbers are right */
    }

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_PROGRESS_GEOMETRY].address == 0 || sites[SITE_TEXT_ORIGIN].address == 0) {
        log_warning("the loading bar's geometry did not resolve, so the bar and its percentage "
                    "stay at their authored size in the top left of the loading screen. Nothing "
                    "else is affected");
        return false;
    }

    loading_bar_width  = 128.0f * ratio_x;
    loading_bar_height =  32.0f * ratio_y;

    {
        uintptr_t bar  = sites[SITE_PROGRESS_GEOMETRY].address;
        uintptr_t text = sites[SITE_TEXT_ORIGIN].address;

        if (patch_write_u32(bar + BAR_Y_OFFSET,      (uint32_t)scaled(400,   ratio_y))
                == PATCH_RESULT_OK &&
            patch_write_u32(bar + BAR_X_OFFSET,      (uint32_t)scaled(0x1be, ratio_x))
                == PATCH_RESULT_OK &&
            patch_write_u32(bar + BACKDROP_X_OFFSET, (uint32_t)scaled(0x186, ratio_x))
                == PATCH_RESULT_OK &&
            patch_write_u32(bar + BACKDROP_Y_OFFSET, (uint32_t)scaled(0x15e, ratio_y))
                == PATCH_RESULT_OK &&
            patch_write_u32(bar + BACKDROP_WIDTH,    (uint32_t)scaled(0xf0,  ratio_x))
                == PATCH_RESULT_OK &&
            patch_write_u32(bar + BACKDROP_HEIGHT,   (uint32_t)scaled(0x54,  ratio_y))
                == PATCH_RESULT_OK &&
            patch_write_pointer32(bar + BAR_WIDTH_OPERAND,  &loading_bar_width)
                == PATCH_RESULT_OK &&
            patch_write_pointer32(bar + BAR_HEIGHT_OPERAND, &loading_bar_height)
                == PATCH_RESULT_OK &&
            patch_write_u32(text + TEXT_X_OFFSET,    (uint32_t)scaled(0x1fe, ratio_x))
                == PATCH_RESULT_OK &&
            patch_write_u32(text + TEXT_Y_OFFSET,    (uint32_t)scaled(0x17c, ratio_y))
                == PATCH_RESULT_OK) {
            installed = true;
            log_info("loading bar scaled with the canvas: %dx%d at %d,%d on a %dx%d backdrop, "
                     "percentage at %d,%d",
                     (int)loading_bar_width, (int)loading_bar_height,
                     (int)scaled(0x1be, ratio_x), (int)scaled(400, ratio_y),
                     (int)scaled(0xf0, ratio_x), (int)scaled(0x54, ratio_y),
                     (int)scaled(0x1fe, ratio_x), (int)scaled(0x17c, ratio_y));
            return true;
        }
    }

    log_warning("the loading bar's geometry could not be written, so it may be part scaled on the "
                "loading screen. Nothing else is affected");
    return false;
}
