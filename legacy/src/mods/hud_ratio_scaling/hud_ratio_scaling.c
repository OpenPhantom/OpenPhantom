/* hud_ratio_scaling.c: the hooks. All the arithmetic is in hud_layout.c.
 *
 * Five detours. The middle three only act while the first one is running:
 *
 *     status_drawHud         marks "the HUD is being drawn now" and latches the display size
 *     texture_drawSprite     resizes a rectangle, but only inside that window and only when the
 *                            rectangle is one of the four the HUD builds
 *     font3d_draw            moves the health and ammo numbers the same way
 *     font3d_setGlyphScale   sizes the digits, by the HUD's rule inside the window, by the
 *                            general square-text rule outside it
 *     graphics_setMode       the display size changed: re-read the knob and log the new layout
 *
 * The gate matters: the sprite blitter draws a great deal more than the HUD, and the font layer
 * draws every subtitle and every menu string. Outside the window the three inner hooks are a
 * branch and a call to the original.
 *
 * Nothing is cached, by the engine or by us. status_drawHud rebuilds all four rectangles from the
 * live screen globals on every frame and the font layer re-reads the display size on every
 * string, so the layout follows a resolution change without being told. That is why the
 * mode-change hook is optional: it exists for the log line and for re-reading the knob, not for
 * correctness.
 *
 * SIZE NOTE. Over the 600 line mark, under the 900 hard limit. The remainder is the byte evidence
 * at the six engine sites, which has to stand at the site rather than only in a document.
 */
#include "hud_ratio_scaling.h"

#include "hud_layout.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HUD_SECTION "hud_ratio_scaling"

/* The engine passes every coordinate and every scale as a 32-bit float on the stack. A build
 * whose `float` were anything else would push the wrong number of bytes into these hooks. */
_Static_assert(sizeof(float) == 4, "the engine pushes 32-bit floats");
_Static_assert(sizeof(void *) == 4, "this DLL targets a 32-bit game");

/* --- 0x0046B293  font3d_setGlyphScale(sx, sy) ------------------------------------------------ *
 *   55 8B EC                    prologue
 *   83 3D 00626D00 00           cmp [g_pCurFont], 0
 *   75 02 / EB 17               if null, skip
 *   A1 00626D00                 mov eax,[g_pCurFont]
 *   8B 4D 08 / 89 48 28         pCurFont->glyphScaleX = sx
 *
 * The +0x28 field write is what separates this function from font3d_setPosScale, whose first
 * fourteen bytes are identical and whose field is +0x38, so the pattern has to run past it.
 *
 * The detour takes ten bytes, through the cmp, because three is too short for a jmp rel32. The
 * copied `cmp [imm32],0` is absolute rather than rip-relative, so a trampoline may hold it
 * verbatim, and the flags it sets are consumed by the jne the trampoline returns to. */
static const uint8_t SIG_SET_GLYPH_SCALE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x3D, 0x00, 0x62, 0x6D, 0x00, 0x00, 0x75, 0x02, 0xEB, 0x17,
    0xA1, 0x00, 0x62, 0x6D, 0x00, 0x8B, 0x4D, 0x08, 0x89, 0x48, 0x28
};
#define SET_GLYPH_SCALE_PROLOGUE 10u

/* --- 0x0049385A  the display size the font layer itself multiplies by ------------------------ *
 *   A1 2C278600 / 6B C0 54 / 8B 90 48278600   the raw-mode index, its stride, and the width
 *
 * Read through the engine's own accessor rather than from the screen globals, so the correction
 * cannot drift from what the renderer actually does. */
static const uint8_t SIG_CURRENT_MODE_SIZE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x13,
    0xA1, 0x2C, 0x27, 0x86, 0x00, 0x6B, 0xC0, 0x54, 0x8B, 0x4D, 0x08,
    0x8B, 0x90, 0x48, 0x27, 0x86, 0x00
};

/* --- 0x00459230  status_drawHud (function start) --------------------------------------------- *
 *   55 8B EC 81 EC C0 00 00 00     prologue, 9 bytes, clean boundary                            */
static const uint8_t SIG_DRAW_HUD[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00,
    /* The tail has to be long enough to stand on its own. The first version stopped one byte past
     * the prologue, so once another DLL had branched over that prologue the two-stage resolver was
     * left searching for a single 0xE8, which matches thousands of times, resolves to nothing,
     * and switched the whole patch off with a warning that read like a bad build. */
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x04,
    0x6A, 0x01,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x04,
    0x68, 0xCD, 0xCC, 0x4C, 0x3F
};
static const uint8_t MSK_DRAW_HUD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DRAW_HUD) == sizeof(MSK_DRAW_HUD),
               "the HUD-draw pattern and its mask are different lengths");
#define DRAW_HUD_PROLOGUE 9u

/* --- 0x0042963B  texture_drawSprite(texture, xL, xR, yT, yB, colour, fill) -------------------- *
 *   55 8B EC 81 EC 94 00 00 00     prologue, 9 bytes, which is what a detour overwrites
 *   8B 45 08 / 50 / E8 <rel32>     the texture argument goes straight into a lookup
 *   83 C4 04 / 89 45 F8            the page it returned
 *   D9 45 20 / D8 1D <abs32>       fill compared against 1.0
 *   DF E0 / F6 C4 41 / 75 09       and clamped to it
 *   C7 45 20 00 00 80 3F
 *
 * The argument order was recovered from the HUD's own call sites: the two x bounds come before
 * the two y bounds, the colour is ARGB with the alpha in the top byte, and `fill` is the
 * fraction of the sprite that is drawn. The blitter CROPS to that fraction rather than squashing
 * to it, so resizing the rectangle does not change what is drawn inside it.
 *
 * THE PATTERN REACHES WELL PAST THE PROLOGUE, AND THAT IS THE POINT. Another DLL in this tree
 * wants the same function, and whichever installs first replaces those nine bytes with a branch.
 * The second one then has to find the site by the bytes AFTER the prologue, which is what
 * SIGNATURE_ENTRY_DETOUR falls back to. A short tail cannot carry that: `8B 45 08 50 E8` alone
 * occurs 275 times in the image, and it only separated because exactly one of those had the
 * authored prologue in front of it. Once other DLLs have detoured other functions, their branches
 * make further candidates acceptable, the tail stops being unique and the fallback refuses. This
 * tail is unique on its own in all six shipped images, so the load order stops mattering. */
static const uint8_t SIG_DRAW_SPRITE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00,   /* the overwritten prologue     */
    0x8B, 0x45, 0x08, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00,   /* call, operand wildcarded     */
    0x83, 0xC4, 0x04, 0x89, 0x45, 0xF8,
    0xD9, 0x45, 0x20, 0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00,   /* fcomp, operand wildcarded    */
    0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x75, 0x09,
    0xC7, 0x45, 0x20, 0x00, 0x00, 0x80, 0x3F
};
static const uint8_t MSK_DRAW_SPRITE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DRAW_SPRITE) == sizeof(MSK_DRAW_SPRITE),
               "the sprite pattern and its mask are different lengths");
#define DRAW_SPRITE_PROLOGUE 9u

/* --- 0x0046B3C0  font3d_draw(text, x, y) ------------------------------------------------------ *
 *   55 8B EC 81 EC 0C 08 00 00     prologue, 9 bytes: it reserves a 2 KB local for the formatted
 *                                  string, and the next byte is `push edi`, so 9 is an exact
 *                                  instruction boundary
 *   57                             push edi
 *   C6 85 f4 f7 ff ff 00           mov byte [ebp-0x80c], 0
 *
 * THREE ARGUMENTS, __cdecl. All seventeen call sites clean up with `add esp, 0x0c`, and the body
 * reads [ebp+8], [ebp+0x0c] and [ebp+0x10] and nothing above.
 *
 * X and y arrive here in framebuffer pixels, the same space the sprite rectangles are in. This
 * function multiplies them by the font's position scale, which the HUD sets to (1/W, 1/H), and
 * only then calls the string renderer one level down. A hook placed on THAT renderer sees
 * canvas fractions in [0,1] instead, which is a different space and needs different arithmetic.
 *
 * The pattern is deliberately address-free: the `cmp [g_pCurFont],0` that follows would pin an
 * absolute address into it and stop it resolving on a recompiled build, where the same function
 * still exists at a different data address. */
static const uint8_t SIG_FONT3D_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x0C, 0x08, 0x00, 0x00,
    0x57, 0xC6, 0x85, 0xF4, 0xF7, 0xFF, 0xFF, 0x00
};
#define FONT3D_DRAW_PROLOGUE 9u

/* --- 0x0046BC85  graphics_setMode(rawModeIndex) ----------------------------------------------- *
 *   55 8B EC 83 EC 10              prologue, 6 bytes, exactly enough for a jmp rel32
 *   83 7D 08 00 / 7D 1A            if (rawModeIndex < 0) -> the assertion below
 *   68 9A 01 00 00                 push 410, the assertion's line number
 *
 * The bare prologue is not a signature. Those six bytes are the standard MSVC frame setup with a
 * 16-byte local area and they occur 74 times in the code section; a pattern that short resolves
 * to "74 matches" and switches the patch off. The argument test and the assertion line number
 * behind it are what make the anchor unique, and the tail from byte six is unique on its own, so
 * the site still resolves after another DLL has branched over the prologue.
 *
 * One argument, __cdecl, returns non-zero on success: all eight call sites clean up with
 * `add esp, 4` and test eax. The display size is written from the raw-mode table shortly before
 * the only success return, so an epilogue hook always sees the new size.
 *
 * It is not the only writer of the display size. The shutdown path stores -1.0f into the same
 * globals and is reachable from four call chains that never enter this function, so this hook
 * sees every VALID size change and not every write. Everything downstream therefore validates the
 * size it is handed instead of trusting this hook to have latched it. */
static const uint8_t SIG_GRAPHICS_SET_MODE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x83, 0x7D, 0x08, 0x00, 0x7D, 0x1A,
    0x68, 0x9A, 0x01, 0x00, 0x00
};
#define GRAPHICS_SET_MODE_PROLOGUE 6u

enum {
    SITE_SET_GLYPH_SCALE,
    SITE_CURRENT_MODE_SIZE,
    SITE_DRAW_HUD,
    SITE_DRAW_SPRITE,
    SITE_FONT3D_DRAW,
    SITE_GRAPHICS_SET_MODE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("font3d_set_glyph_scale", SIG_SET_GLYPH_SCALE, SET_GLYPH_SCALE_PROLOGUE),
    SIGNATURE_ENTRY       ("current_mode_size",      SIG_CURRENT_MODE_SIZE),
    SIGNATURE_ENTRY_DETOUR_MASKED("status_draw_hud", SIG_DRAW_HUD, MSK_DRAW_HUD,
                                  DRAW_HUD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("texture_draw_sprite", SIG_DRAW_SPRITE, MSK_DRAW_SPRITE,
                                  DRAW_SPRITE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("font3d_draw",            SIG_FONT3D_DRAW,  FONT3D_DRAW_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("graphics_set_mode",      SIG_GRAPHICS_SET_MODE,
                           GRAPHICS_SET_MODE_PROLOGUE)
};

typedef void (__cdecl *set_glyph_scale_fn_t)(float horizontal, float vertical);
typedef void (__cdecl *current_mode_size_fn_t)(int32_t *out_width, int32_t *out_height);
typedef void (__cdecl *draw_hud_fn_t)(void);
typedef void (__cdecl *draw_sprite_fn_t)(void *texture, float left, float right, float top,
                                         float bottom, uint32_t colour, float fill);
typedef void (__cdecl *font3d_draw_fn_t)(const char *text, float x, float y);
typedef int32_t (__cdecl *graphics_set_mode_fn_t)(int32_t raw_mode_index);

typedef struct hud_config {
    bool  enabled;
    bool  square_text;
    bool  square_hud;
    float hud_scale;
} hud_config_t;

typedef struct hud_state {
    bool         installed;
    hud_config_t config;

    detour_t     glyph_scale_detour;
    detour_t     draw_hud_detour;
    detour_t     draw_sprite_detour;
    detour_t     font3d_draw_detour;
    detour_t     set_mode_detour;

    current_mode_size_fn_t engine_mode_size;

    bool  inside_hud;      /* set only while status_drawHud is on the stack */
    float screen_width;    /* the size that call was laid out with */
    float screen_height;

    /* One-shot log flags. A display-mode change clears all of them, so the next HUD frame
     * reports the new layout instead of staying silent about it. */
    bool     logged_glyph;
    bool     logged_hud_glyph;
    uint32_t logged_blocks;

    /* The size the mode hook last acted on, so a request for the mode already in use, which the
     * engine answers with success without changing anything, is not announced as a change. */
    int last_mode_width;
    int last_mode_height;
} hud_state_t;

static hud_state_t hud_state;

/* Plausibility bounds: before a mode is configured the accessor reports zeroes, and the shutdown
 * path leaves the screen globals negative, so a garbage pair would move every glyph and every bar
 * somewhere absurd. */
#define MODE_SIZE_MIN 64
#define MODE_SIZE_MAX 16384

#define HUD_SCALE_MIN 0.25f
#define HUD_SCALE_MAX 4.00f

static bool current_mode_size(int *out_width, int *out_height)
{
    int32_t width = 0;
    int32_t height = 0;

    if (hud_state.engine_mode_size == NULL) {
        return false;
    }

    hud_state.engine_mode_size(&width, &height);
    if (width  < MODE_SIZE_MIN || width  > MODE_SIZE_MAX ||
        height < MODE_SIZE_MIN || height > MODE_SIZE_MAX) {
        return false;
    }

    *out_width  = (int)width;
    *out_height = (int)height;
    return true;
}

static float clamp_float(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static float read_hud_scale(void)
{
    return clamp_float(ini_read_float(HUD_SECTION, "HudScale", 1.0f),
                       HUD_SCALE_MIN, HUD_SCALE_MAX);
}

static void load_config(void)
{
    hud_state.config.enabled     = ini_read_bool(HUD_SECTION, "Enabled", true);
    hud_state.config.square_text = ini_read_bool(HUD_SECTION, "SquareText", true);
    hud_state.config.square_hud  = ini_read_bool(HUD_SECTION, "SquareHud", true);
    hud_state.config.hud_scale   = read_hud_scale();
}

/* True when the configuration asks for the HUD itself to be moved at all. With both knobs at
 * their authentic values every formula collapses to the identity, so nothing is hooked and the
 * HUD is drawn exactly as the engine built it. */
static bool hud_transform_configured(void)
{
    return hud_state.config.hud_scale != 1.0f || hud_state.config.square_hud;
}

static bool hud_transform_wanted(void)
{
    return hud_state.inside_hud && hud_transform_configured();
}

static const char *block_name(hud_block_t block)
{
    switch (block) {
    case HUD_BLOCK_HEALTH:
        return "health bar";
    case HUD_BLOCK_FORCE:
        return "force bar";
    case HUD_BLOCK_WEAPON:
        return "weapon icon";
    case HUD_BLOCK_ESCORT:
        return "escort bar";
    case HUD_BLOCK_NONE:
    default:
        return "unclassified";
    }
}

/* ============================================================================================ */
static void __cdecl hook_set_glyph_scale(float horizontal, float vertical)
{
    set_glyph_scale_fn_t original = (set_glyph_scale_fn_t)hud_state.glyph_scale_detour.original;
    int width;
    int height;

    if (hud_transform_wanted()) {
        /* Inside the HUD the digits have to grow by the same factor the bars grow by, which is
         * the height ratio and not the width ratio the renderer would otherwise apply. Both axes
         * therefore take the multipliers the rectangles use, and they are read from the size
         * THIS HUD frame was laid out with, because asking the engine again in the middle of a
         * frame could answer with a different mode than the rectangles were built from. */
        float corrected_horizontal = horizontal;
        float corrected_vertical   = vertical;

        hud_glyph_scale(&corrected_horizontal, &corrected_vertical,
                        hud_state.screen_width, hud_state.screen_height,
                        hud_state.config.hud_scale, hud_state.config.square_hud);

        if (!hud_state.logged_hud_glyph) {
            hud_state.logged_hud_glyph = true;
            log_info("HUD glyph scale at %.0fx%.0f: (%.4f, %.4f) becomes (%.4f, %.4f), which is "
                     "the same growth the bars get",
                     (double)hud_state.screen_width, (double)hud_state.screen_height,
                     (double)horizontal, (double)vertical,
                     (double)corrected_horizontal, (double)corrected_vertical);
        }

        original(corrected_horizontal, corrected_vertical);
        return;
    }

    /* Everywhere else: the general rule, unchanged. It raises the vertical instead of lowering
     * the horizontal because the menus already pass a hand-corrected horizontal. */
    if (hud_state.config.square_text && current_mode_size(&width, &height)) {
        float corrected = hud_square_glyph_scale(horizontal, width, height);

        if (!hud_state.logged_glyph) {
            hud_state.logged_glyph = true;
            log_info("glyph aspect at %dx%d: the renderer scales by (W/640, H/480), so a uniform "
                     "(%.4f, %.4f) draws %.3f:1 too wide. The vertical becomes %.4f.",
                     width, height, (double)horizontal, (double)vertical,
                     (double)(((float)width / 640.0f) / ((float)height / 480.0f)),
                     (double)corrected);
        }
        vertical = corrected;
    }

    original(horizontal, vertical);
}

static void __cdecl hook_draw_hud(void)
{
    draw_hud_fn_t original = (draw_hud_fn_t)hud_state.draw_hud_detour.original;
    int width = 0;
    int height = 0;

    /* Latched BEFORE the original runs, and every hook below reads this latch rather than asking
     * the engine again, so two blits in one frame can never be transformed against two different
     * mode reads. An implausible size leaves the gate shut and the HUD untouched. */
    if (current_mode_size(&width, &height)) {
        hud_state.screen_width  = (float)width;
        hud_state.screen_height = (float)height;
        hud_state.inside_hud    = true;
    }

    original();

    hud_state.inside_hud = false;
}

static void __cdecl hook_draw_sprite(void *texture, float left, float right, float top,
                                     float bottom, uint32_t colour, float fill)
{
    draw_sprite_fn_t original = (draw_sprite_fn_t)hud_state.draw_sprite_detour.original;
    hud_rect_t       rect;
    hud_block_t      block;

    if (!hud_transform_wanted()) {
        original(texture, left, right, top, bottom, colour, fill);
        return;
    }

    rect.left   = left;
    rect.right  = right;
    rect.top    = top;
    rect.bottom = bottom;

    /* A rectangle that matches none of the four formulas is forwarded exactly as it arrived. That
     * is not only the engine's own non-HUD sprites: another DLL chained onto the HUD-draw function
     * blits inside this window too, and its rectangles must pass through untouched. */
    block = hud_classify(&rect, hud_state.screen_width, hud_state.screen_height);
    if (block != HUD_BLOCK_NONE) {
        uint32_t bit = 1u << (unsigned int)block;

        rect = hud_transform(&rect, block, hud_state.screen_width, hud_state.screen_height,
                             hud_state.config.hud_scale, hud_state.config.square_hud);

        if ((hud_state.logged_blocks & bit) == 0u) {
            hud_state.logged_blocks |= bit;
            log_info("HUD at %.0fx%.0f: the %s was %.1f,%.1f-%.1f,%.1f and is now "
                     "%.1f,%.1f-%.1f,%.1f (scale %.2f, square %d)",
                     (double)hud_state.screen_width, (double)hud_state.screen_height,
                     block_name(block),
                     (double)left, (double)top, (double)right, (double)bottom,
                     (double)rect.left, (double)rect.top, (double)rect.right, (double)rect.bottom,
                     (double)hud_state.config.hud_scale, hud_state.config.square_hud ? 1 : 0);
        }
    }

    original(texture, rect.left, rect.right, rect.top, rect.bottom, colour, fill);
}

/* Only two numbers are drawn inside the HUD: the health value, centred on the health bar, and the
 * ammo count, centred on the weapon icon. Both are on the bottom row of the screen, and each is
 * drawn twice, once as a one-pixel drop shadow and once in colour, so four calls reach this
 * hook per HUD frame. */
static void __cdecl hook_font3d_draw(const char *text, float x, float y)
{
    font3d_draw_fn_t original = (font3d_draw_fn_t)hud_state.font3d_draw_detour.original;
    hud_block_t      block;

    if (!hud_transform_wanted()) {
        original(text, x, y);
        return;
    }

    /* Both HUD numbers sit on the bottom row by construction. Refusing the upper half of the
     * screen costs nothing here and keeps text another DLL might draw inside the same window out
     * of the transform. */
    if (!(y > hud_state.screen_height * 0.5f)) {
        original(text, x, y);
        return;
    }

    block = hud_block_for_number(x, hud_state.screen_width);

    hud_transform_point(&x, &y, block, hud_state.screen_width, hud_state.screen_height,
                        hud_state.config.hud_scale, hud_state.config.square_hud);

    original(text, x, y);
}

/* The knob and the log line are the whole content of the mode-change path. The layout itself does
 * not need it: every rectangle is rebuilt from the live display size on every HUD frame. */
static void on_mode_changed(int width, int height)
{
    float previous;

    hud_state.logged_glyph     = false;
    hud_state.logged_hud_glyph = false;
    hud_state.logged_blocks    = 0u;

    if (!hud_state.draw_sprite_detour.installed) {
        log_info("display mode is now %dx%d, the glyph correction follows it on the next string. "
                 "The HUD transform is not installed, so HudScale is not re-read.",
                 width, height);
        return;
    }

    previous = hud_state.config.hud_scale;
    hud_state.config.hud_scale = read_hud_scale();

    log_info("display mode is now %dx%d, the HUD follows it on the next frame. "
             "HudScale re-read: %.2f -> %.2f",
             width, height, (double)previous, (double)hud_state.config.hud_scale);
}

static int32_t __cdecl hook_set_mode(int32_t raw_mode_index)
{
    graphics_set_mode_fn_t original = (graphics_set_mode_fn_t)hud_state.set_mode_detour.original;
    int32_t result;
    int width = 0;
    int height = 0;

    result = original(raw_mode_index);
    if (result == 0) {
        return result;
    }

    if (!current_mode_size(&width, &height)) {
        return result;
    }

    /* The engine also returns success for a request that names the mode already in use, and then
     * nothing has changed. Comparing the size we can now read against the one this hook last
     * acted on is what keeps it from announcing a change that never happened, and it is what
     * makes the hook idempotent when a caller retries, which the resolution path does. */
    if (width == hud_state.last_mode_width && height == hud_state.last_mode_height) {
        return result;
    }

    hud_state.last_mode_width  = width;
    hud_state.last_mode_height = height;

    on_mode_changed(width, height);
    return result;
}

/* ============================================================================================ */
static void install_glyph_hook(void)
{
    /* The in-HUD rule lives in this same detour, so gating its installation on SquareText alone
     * would leave the digits at the wrong size whenever SquareHud or HudScale is in use and
     * SquareText is not. The two settings are independent everywhere else and stay so here. */
    if (!hud_state.config.square_text && !hud_transform_configured()) {
        return;
    }

    if (sites[SITE_SET_GLYPH_SCALE].address != 0 &&
        detour_install(&hud_state.glyph_scale_detour, sites[SITE_SET_GLYPH_SCALE].address,
                       (const void *)hook_set_glyph_scale, SET_GLYPH_SCALE_PROLOGUE)) {
        log_info("glyph scale hooked: %s outside the HUD, %s inside it",
                 hud_state.config.square_text ? "sy = sx * 3W/4H, identity at 4:3"
                                              : "left exactly as the engine sets it",
                 hud_transform_configured() ? "the multipliers the bars use"
                                            : "left exactly as the engine sets it");
        return;
    }

    log_warning("font3d_set_glyph_scale could not be hooked, text keeps the engine's "
                "aspect-dependent glyph scale, and the HUD numbers keep the wrong size while the "
                "bars move");
}

static void install_hud_hooks(void)
{
    if (sites[SITE_DRAW_HUD].address == 0 ||
        !detour_install(&hud_state.draw_hud_detour, sites[SITE_DRAW_HUD].address,
                        (const void *)hook_draw_hud, DRAW_HUD_PROLOGUE)) {
        log_warning("status_drawHud could not be hooked. Without it there is no way to tell a HUD "
                    "blit from any other, so the HUD is NOT resized.");
        return;
    }

    if (sites[SITE_DRAW_SPRITE].address == 0 ||
        !detour_install(&hud_state.draw_sprite_detour, sites[SITE_DRAW_SPRITE].address,
                        (const void *)hook_draw_sprite, DRAW_SPRITE_PROLOGUE)) {
        log_warning("texture_drawSprite could not be hooked, the HUD is NOT resized");
        return;
    }

    if (sites[SITE_FONT3D_DRAW].address == 0 ||
        !detour_install(&hud_state.font3d_draw_detour, sites[SITE_FONT3D_DRAW].address,
                        (const void *)hook_font3d_draw, FONT3D_DRAW_PROLOGUE)) {
        log_warning("font3d_draw could not be hooked. The four rectangles move, but the health and "
                    "ammo numbers stay where they were, they will not line up.");
    }

    /* The summary names only what actually stands, because a success line that overstates the
     * installation is the one thing a reader of the log cannot check. */
    log_info("HUD scale %.2f, square %d, the four rectangles move about the edge each block is "
             "anchored to, and the two numbers %s",
             (double)hud_state.config.hud_scale, hud_state.config.square_hud ? 1 : 0,
             hud_state.font3d_draw_detour.installed ? "move with them" : "do NOT move");
}

static void install_mode_change_hook(void)
{
    if (!hud_state.glyph_scale_detour.installed && !hud_state.draw_sprite_detour.installed) {
        return;   /* nothing that a mode change could refresh */
    }

    if (sites[SITE_GRAPHICS_SET_MODE].address != 0 &&
        detour_install(&hud_state.set_mode_detour, sites[SITE_GRAPHICS_SET_MODE].address,
                       (const void *)hook_set_mode, GRAPHICS_SET_MODE_PROLOGUE)) {
        log_info("a display-mode change re-reads HudScale and logs the new layout once per block");
        return;
    }

    log_warning("graphics_setMode could not be hooked. The layout itself still follows the "
                "resolution frame by frame, because every rectangle is rebuilt from the live "
                "display size on every HUD frame. What is lost is the fresh log line after a mode "
                "change and the live re-read of HudScale, which then needs a restart.");
}

void hud_ratio_scaling_install(void)
{
    log_init("hud_ratio_scaling", false);

    if (hud_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the HUD is left alone");
        return;
    }

    load_config();
    if (!hud_state.config.enabled) {
        log_info("Enabled=0, the HUD and the glyph scale stay as the engine sets them");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_CURRENT_MODE_SIZE].address == 0) {
        log_warning("current_mode_size did not resolve. Neither the glyph correction nor the HUD "
                    "scale can know the display size, so both stay off.");
        return;
    }
    hud_state.engine_mode_size = (current_mode_size_fn_t)sites[SITE_CURRENT_MODE_SIZE].address;

    hud_state.installed = true;

    install_glyph_hook();

    if (hud_transform_configured()) {
        install_hud_hooks();
    } else {
        log_info("HudScale=1.0 and SquareHud=0, the HUD itself is left exactly as authored, so "
                 "none of its three hooks is installed and HudScale cannot be changed live");
    }

    install_mode_change_hook();
}
