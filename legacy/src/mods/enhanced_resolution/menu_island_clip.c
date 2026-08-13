/* menu_island_clip.c: a menu may only draw where a menu can erase.
 *
 * ==============================================================================================
 * THE DEFECT, walked from the report to the bytes
 *
 * Reported from a 3840x2160 session with MenuKeepsResolution=1: hovering a button on the pause
 * menu's options screens leaves a blue smear at the left edge of the 640x480 menu island, one
 * smear per row ever hovered, permanent until the screen closes, and the front end never shows
 * it. The first theory was the widened cursor cage (pointer_cage.c) and it was wrong: the smears
 * survive WidenMenuCursorArea=0, and they track the hovered BUTTON, not the pointer.
 *
 * The mechanism needs three facts, each independently checkable:
 *
 *   1. The hovered button's glow reaches past the island. The buttons are authored at canvas
 *      x=7 (the controls screen's own table reads `7,175,199,53`), and the pulsing halo drawn
 *      behind a hovered button is wider than its plate, so its quad starts LEFT of canvas x=0.
 *
 *   2. The draw and the erase clip against different things. The glow is drawn through the
 *      textured-sprite blitter (texture_drawSprite, retail 0x0042963B), whose quad is clipped by
 *      the rasterizer against the SCREEN, 3840x2160 here. The menus repair themselves in canvas
 *      coordinates, and every blit in that toolkit clips against a hard-coded 640x480 (the same
 *      constant pair fov_menu.c and input_menu.c document for the widget blit). A pixel outside
 *      the island is therefore write-once: drawable, unrepairable.
 *
 *   3. Retail never had the mismatch because retail never had the gap: every menu bolted the
 *      display mode to 640x480 (swmenu_enterMenuMode, 0x45F7AC), where the screen edge IS the
 *      canvas edge and the rasterizer cropped the poked-out halo for free. MenuKeepsResolution=1
 *      removed the bolt, which is worth keeping (the bolt costs a Direct3D device rebuild per
 *      menu and has hung a wrapper mid-rebuild before), and inherited the stamp.
 *
 * The front end stays clean because its 3-D room repaints every pixel every frame; the pause
 * subpages are exactly the screens that do not.
 *
 * ==============================================================================================
 * THE REPAIR
 *
 * Clamp the sprite rectangle to the island before the blitter sees it, but ONLY while the menu
 * toolkit is the thing drawing. The gate is the engine's own bracket, not a guess of ours:
 * swmenu_render raises a flag around its widget pass and lowers it after,
 *
 *   0045DC6F  8B 0D 60 88 4B 00      mov  ecx,[g_tickCounter]          ; 0x004B8860
 *   0045DC75  83 C1 01               add  ecx,1
 *   0045DC78  89 0D 60 88 4B 00      mov  [g_tickCounter],ecx
 *   0045DC7E  C7 05 A0 FB 8B 00 01.. mov  [g_menuDrawFlag],1           ; <- the bracket opens
 *   0045DC88  83 3D 74 FD 6C 00 01   cmp  [g_menuHasParent],1
 *   ...        two swmenu_sendToWidgets(screen, 0x15) draw passes ...
 *   0045DCB0  C7 05 A0 FB 8B 00 00.. mov  [g_menuDrawFlag],0           ; <- and closes
 *
 * That counter is not a menu-private one. It is the same g_tickCounter the pose throttle reads,
 * and framerate_fix's own operand census over the whole image already lists this exact store,
 * 0x0045DC7A, as one of its three writers: the substep loop, a menu frame, and a reset. It is
 * named here the way it is named there, because a cell with two names in one tree is how a wrong
 * conclusion gets carried across a file boundary.
 *
 * The engine itself treats the flag cell as "the menu is what is drawing now": the menu's own
 * embedded 3-D preview raises and lowers it around its draws too, and the projection setup reads
 * it. Everything else that shares the blitter, the gameplay HUD above all, draws with the flag
 * down and passes through this hook untouched. The pause menu's TOP page draws the frozen world
 * behind its widgets with the flag down as well, which is exactly right: that backdrop is
 * full-screen by design.
 *
 * The pattern below is address-free: the four absolute operands are wildcarded and the flag's
 * address is READ out of the matched `C7 05` operand, then cross-checked three ways before it is
 * believed: the two counter operands must be the same cell, the flag cell must lie inside the
 * image, and the closing `mov [flag],0` must sit 0x41 bytes after the match with the SAME operand.
 * Being address-free is not a formality here: measured across every shipped image, the block sits
 * at 0x0045DC6F in retail and at 0x0045DC0F in the Edit Tool's own recompile, with the flag cell
 * moving from 0x008BFBA0 to 0x008BFB40 with it. The cross-check passes in both.
 *
 * The island origin is not read from the engine's origin cells: pointer_cage may have repointed
 * the one instruction block that names them. It is recomputed the way the engine computes it,
 * ((W-640)/2, (H-480)/2) from the mode accessor window_fit already resolved, which is the same
 * "one owner per question" rule the cage follows. It is recomputed per sprite rather than cached
 * per widget pass, and that is deliberate: caching it needs the pass's rising edge, this hook only
 * observes the flag on the sprites it happens to be handed, and a pass whose first sprite is
 * missed would then run on a stale origin. The accessor reads two engine globals, which is not
 * worth a correctness risk to avoid.
 *
 * ==============================================================================================
 * WHAT THIS IS NOT, stated honestly
 *
 * The retail rasterizer CROPPED a quad at the screen edge; clamping the rectangle SQUASHES the
 * texture into the remaining width instead, by the poked-out fraction. The reason is in the
 * blitter: it writes u = 0 at the left bound and u = fill at the right one, whatever those bounds
 * are, so moving a bound moves the picture rather than cutting it. For the halos that actually
 * poke out (a soft radial gradient some twenty canvas pixels past the border of a 250-pixel
 * sprite) the compression is invisible, and a sprite that fits the island, which is every
 * authored widget, is passed through bit-identical.
 *
 * A SPRITE DRAWN WITH fill < 1 IS LEFT ALONE ENTIRELY, and that is not caution but arithmetic.
 * The blitter's first act is a left-anchored wipe,
 *
 *     0042967C  ...                    xRight = (xRight - xLeft) * fill + xLeft
 *
 * so for any fill below 1 the rectangle handed in is not the rectangle drawn: the right bound is
 * recomputed FROM the left one. Clamping either bound would then move the drawn edge by a
 * fraction of the correction rather than to the border, and the outside test would be judging an
 * extent wider than the one on screen. Every sprite that has ever been seen to poke past the
 * island is a widget halo at fill 1, so nothing that needs repairing is skipped by this; it is
 * the partial-fill bars (the HUD's meters use the same call) that would be silently reshaped, and
 * they are not this repair's business.
 *
 * The cursor quad is drawn AFTER the bracket closes and is not covered here; it does not need to
 * be, because the engine's own cage keeps it inside the island, where its erase works.
 */
#include "menu_island_clip.h"

#include "window_fit.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 4,
               "menu_island_clip reads 32-bit engine pointers out of operands");

/* --- 0x0045DC6F  swmenu_render: the widget-pass bracket --------------------------------------- *
 * Six instructions, all four absolute operands wildcarded; the shape "load counter, add 1, store
 * counter, store 1 into a second cell, compare a third against 1" is what identifies it. */
static const uint8_t SIG_MENU_DRAW_FLAG[] = {
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,                         /* mov ecx,[counter]      */
    0x83, 0xC1, 0x01,                                           /* add ecx,1              */
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,                         /* mov [counter],ecx      */
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, /* mov dword [flag],1     */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01                    /* cmp dword [parent],1   */
};
static const uint8_t MSK_MENU_DRAW_FLAG[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
_Static_assert(sizeof(SIG_MENU_DRAW_FLAG) == sizeof(MSK_MENU_DRAW_FLAG),
               "the bracket pattern and its mask are different lengths");

#define OFFSET_COUNTER_LOAD  0x02u
#define OFFSET_COUNTER_STORE 0x0Bu
#define OFFSET_FLAG_SET      0x11u
/* `mov dword [flag],0`, the close of the bracket, measured from the match base. The two draw
 * passes between the stores are position-independent code of fixed length in every shipped
 * WMAIN image, which is what makes the distance a constant worth checking. */
#define OFFSET_FLAG_CLEAR    0x41u
static const uint8_t FLAG_CLEAR_HEAD[] = { 0xC7, 0x05 };
static const uint8_t FLAG_CLEAR_TAIL[] = { 0x00, 0x00, 0x00, 0x00 };
#define FLAG_CLEAR_OPERAND   0x02u
#define FLAG_CLEAR_TAIL_AT   0x06u

/* --- 0x0042963B  texture_drawSprite(texture, xL, xR, yT, yB, colour, fill) -------------------- *
 * The same pattern hud_ratio_scaling anchors on, and the two DLLs both detour it. The detour
 * layer chains, so that part is fine whichever installs first; what has to hold is that the
 * SECOND one can still FIND the site after the first replaced the nine prologue bytes with a
 * branch. That is what SIGNATURE_ENTRY_DETOUR's fallback to the tail is for, and it needs a tail
 * that carries the site on its own: `8B 45 08 50 E8` occurs 275 times in the image and only
 * separated because one of them had the authored prologue in front of it, which stops being true
 * as soon as other DLLs have branched away from other functions. So the pattern reaches to the
 * fill clamp, and that tail is unique by itself in all six shipped images.
 *
 * Keep this pattern and hud_ratio_scaling's identical. They describe the same bytes, and a
 * difference between them would show up as one DLL resolving and the other not, in a load order
 * that nobody chose. */
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

enum {
    CLIP_SITE_DRAW_FLAG,
    CLIP_SITE_DRAW_SPRITE,
    CLIP_SITE_COUNT
};

static signature_t sites[CLIP_SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("swmenu_widget_pass_bracket", SIG_MENU_DRAW_FLAG, MSK_MENU_DRAW_FLAG),
    SIGNATURE_ENTRY_DETOUR_MASKED("texture_draw_sprite", SIG_DRAW_SPRITE, MSK_DRAW_SPRITE,
                                  DRAW_SPRITE_PROLOGUE)
};

typedef void (__cdecl *draw_sprite_fn_t)(void *texture, float left, float right, float top,
                                         float bottom, uint32_t colour, float fill);

typedef struct menu_island_clip_state {
    bool                     installed;
    bool                     active;

    const volatile uint32_t *menu_draw_flag;   /* the engine's own bracket cell */

    detour_t                 draw_sprite_detour;

    bool                     logged_clamp;     /* one line each, ever: the first sprite that */
    bool                     logged_skip;      /* needed clamping, and the first dropped whole */
} menu_island_clip_state_t;

static menu_island_clip_state_t clip_state;

/* ============================================================================================ */
bool menu_island_clip_fill_is_whole(float fill)
{
    /* Not a tolerance: the blitter clamps fill to [0,1] and then multiplies with it, so 1.0f is
     * the one value for which the rectangle handed in survives to the screen unchanged. Anything
     * else, including a hair under 1, moves the right bound. */
    return fill >= 1.0f;
}

bool menu_island_clip_rect(float *left, float *right, float *top, float *bottom,
                           float island_left, float island_top)
{
    float island_right  = island_left + (float)MENU_ISLAND_WIDTH;
    float island_bottom = island_top  + (float)MENU_ISLAND_HEIGHT;

    /* Reversed or unordered bounds (NaN lands here too, every comparison with it is false) are
     * not this repair's business: pass them to the engine exactly as they arrived. */
    if (!(*left <= *right) || !(*top <= *bottom)) {
        return true;
    }

    /* Entirely outside, edge-touching included: a zero-width remainder is not a draw. */
    if (*right <= island_left || *left >= island_right ||
        *bottom <= island_top || *top >= island_bottom) {
        return false;
    }

    if (*left < island_left) {
        *left = island_left;
    }
    if (*right > island_right) {
        *right = island_right;
    }
    if (*top < island_top) {
        *top = island_top;
    }
    if (*bottom > island_bottom) {
        *bottom = island_bottom;
    }
    return true;
}

/* ============================================================================================ */
static bool island_origin(float *out_left, float *out_top)
{
    int width  = 0;
    int height = 0;
    int left;
    int top;

    if (!window_fit_current_mode_size(&width, &height)) {
        return false;
    }

    /* The engine's own formula, g_menuOrigin = ((W-640)/2, (H-480)/2), integer division and all.
     * The 640x480 floor in graphics_buildModeList makes a negative origin unreachable, and the
     * clamp below is for the shutdown path that writes garbage into the size cells. */
    left = (width  - MENU_ISLAND_WIDTH)  / 2;
    top  = (height - MENU_ISLAND_HEIGHT) / 2;
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }

    *out_left = (float)left;
    *out_top  = (float)top;
    return true;
}

static void __cdecl hook_draw_sprite(void *texture, float left, float right, float top,
                                     float bottom, uint32_t colour, float fill)
{
    draw_sprite_fn_t original = (draw_sprite_fn_t)clip_state.draw_sprite_detour.original;
    float            island_left;
    float            island_top;

    if (original == NULL) {
        return;                         /* the un-armed instant between write and state */
    }

    /* The gate is the engine's own bracket. Down = not the menu drawing = not our business. */
    if (!clip_state.active || clip_state.menu_draw_flag == NULL ||
        *clip_state.menu_draw_flag == 0) {
        original(texture, left, right, top, bottom, colour, fill);
        return;
    }

    /* A partial fill means the blitter recomputes the right bound from the left one, so the
     * rectangle here is not the one that reaches the screen. See the header. */
    if (!menu_island_clip_fill_is_whole(fill)) {
        original(texture, left, right, top, bottom, colour, fill);
        return;
    }

    /* No readable mode means no island to clamp against; the engine's behaviour is the correct
     * fallback rather than a guess of ours. */
    if (!island_origin(&island_left, &island_top)) {
        original(texture, left, right, top, bottom, colour, fill);
        return;
    }

    {
        /* Kept only to notice the FIRST sprite a bound really moved on, so the log carries one
         * line of proof that the repair is live rather than merely installed. A sprite that
         * legitimately starts at the border (the backdrop sits at canvas x=0 every frame) moves
         * no bound and is not that proof. */
        float original_left   = left;
        float original_right  = right;
        float original_top    = top;
        float original_bottom = bottom;

        if (!menu_island_clip_rect(&left, &right, &top, &bottom, island_left, island_top)) {
            if (!clip_state.logged_skip) {
                clip_state.logged_skip = true;
                log_info("a menu sprite lay entirely outside the 640x480 island and was dropped, "
                         "which is what the engine's own canvas clip does with it at real "
                         "640x480. Reported once.");
            }
            return;
        }

        if (!clip_state.logged_clamp &&
            (left != original_left || right != original_right ||
             top != original_top || bottom != original_bottom)) {
            clip_state.logged_clamp = true;
            log_info("a menu sprite reached past the island's border and was clamped to it "
                     "(%.1f,%.1f)-(%.1f,%.1f) -> (%.1f,%.1f)-(%.1f,%.1f); this is the class of "
                     "draw that used to stamp the border blue. Reported once.",
                     (double)original_left, (double)original_top,
                     (double)original_right, (double)original_bottom,
                     (double)left, (double)top, (double)right, (double)bottom);
        }
    }

    original(texture, left, right, top, bottom, colour, fill);
}

/* ============================================================================================ */
static bool resolve_flag_cell(uintptr_t site)
{
    uint32_t counter_load = 0;
    uint32_t counter_store = 0;
    uint32_t flag_cell = 0;
    uint32_t clear_cell = 0;
    uint8_t  head[sizeof(FLAG_CLEAR_HEAD)];
    uint8_t  tail[sizeof(FLAG_CLEAR_TAIL)];
    uintptr_t clear_at = site + OFFSET_FLAG_CLEAR;

    if (!memory_read_u32(site + OFFSET_COUNTER_LOAD, &counter_load) ||
        !memory_read_u32(site + OFFSET_COUNTER_STORE, &counter_store) ||
        !memory_read_u32(site + OFFSET_FLAG_SET, &flag_cell)) {
        return false;
    }
    if (counter_load != counter_store) {
        log_warning("the bracket's two counter operands read different cells (%08X vs %08X), "
                    "this is not swmenu_render, refused",
                    (unsigned)counter_load, (unsigned)counter_store);
        return false;
    }
    if (flag_cell == counter_load || !memory_is_inside_image(flag_cell, sizeof(uint32_t))) {
        log_warning("the widget-pass flag would be at %08X, refused", (unsigned)flag_cell);
        return false;
    }

    /* The close of the bracket: same opcode, same operand, immediate zero, 0x41 bytes on. */
    if (!memory_read(clear_at, head, sizeof(head)) ||
        !memory_read_u32(clear_at + FLAG_CLEAR_OPERAND, &clear_cell) ||
        !memory_read(clear_at + FLAG_CLEAR_TAIL_AT, tail, sizeof(tail)) ||
        head[0] != FLAG_CLEAR_HEAD[0] || head[1] != FLAG_CLEAR_HEAD[1] ||
        clear_cell != flag_cell ||
        tail[0] != FLAG_CLEAR_TAIL[0] || tail[1] != FLAG_CLEAR_TAIL[1] ||
        tail[2] != FLAG_CLEAR_TAIL[2] || tail[3] != FLAG_CLEAR_TAIL[3]) {
        log_warning("no matching `mov [%08X],0` closes the bracket 0x%X bytes after %08X, this "
                    "is not the widget pass, refused",
                    (unsigned)flag_cell, (unsigned)OFFSET_FLAG_CLEAR, (unsigned)site);
        return false;
    }

    clip_state.menu_draw_flag = (const volatile uint32_t *)(uintptr_t)flag_cell;
    return true;
}

bool menu_island_clip_install(bool enabled)
{
    if (clip_state.installed) {
        return clip_state.active;
    }
    clip_state.installed = true;

    if (!enabled) {
        log_info("ClampMenuSpritesToIsland=0, menu sprites may write outside the 640x480 island, "
                 "where nothing can erase them: with the menus keeping a large resolution, a "
                 "hovered button's halo stamps the island's border and the stamp stays until the "
                 "screen closes");
        return false;
    }

    signature_resolve_table(sites, CLIP_SITE_COUNT);

    if (sites[CLIP_SITE_DRAW_FLAG].address == 0 ||
        !resolve_flag_cell(sites[CLIP_SITE_DRAW_FLAG].address)) {
        log_warning("the widget-pass bracket did not resolve, so there is no safe gate and menu "
                    "sprites are NOT clamped to the island. On a large mode with "
                    "MenuKeepsResolution=1 the hovered button's halo will stamp the island's "
                    "border; MenuKeepsResolution=0 avoids it at the cost of a device rebuild per "
                    "menu.");
        return false;
    }
    if (sites[CLIP_SITE_DRAW_SPRITE].address == 0) {
        log_warning("texture_drawSprite did not resolve, menu sprites are NOT clamped to the "
                    "island (see the previous warning for what that shows on screen)");
        return false;
    }

    if (!detour_install(&clip_state.draw_sprite_detour, sites[CLIP_SITE_DRAW_SPRITE].address,
                        (const void *)hook_draw_sprite, DRAW_SPRITE_PROLOGUE)) {
        log_warning("texture_drawSprite at %08X could not be detoured, menu sprites are NOT "
                    "clamped to the island",
                    (unsigned)sites[CLIP_SITE_DRAW_SPRITE].address);
        return false;
    }

    clip_state.active = true;
    log_info("menu sprites are clamped to the 640x480 island while the engine's own widget-pass "
             "flag [%08X] is up (blitter hooked at %08X). A sprite fully inside the island "
             "passes through bit-identical, and so does one drawn with a partial fill; the "
             "hovered button's halo, which pokes past the island's left border and used to stamp "
             "it blue for the life of the screen, is now cut at the border the way real 640x480 "
             "cut it.",
             (unsigned)(uintptr_t)clip_state.menu_draw_flag,
             (unsigned)sites[CLIP_SITE_DRAW_SPRITE].address);
    return true;
}
