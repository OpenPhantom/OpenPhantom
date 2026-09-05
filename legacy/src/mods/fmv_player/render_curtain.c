/* render_curtain.c: see render_curtain.h.
 *
 * Three sites, all byte-identical to ones already proven elsewhere in this tree, resolved a
 * second time rather than shared, the same way sfx_mute.c, spawn_census.c and diag_flow.c each
 * carry their own copy of Plr_RunPhases' signature:
 *
 *   - the call that closes the scene, identical to dev_overlay.c's own SIG_SCENE_END. Redirected
 *     the same way it is there: read whatever is CURRENTLY at this call site as `original` (the
 *     true engine function, or another DLL's own hook if it loaded first), point the call at this
 *     file's own hook instead. Both hooks run; whichever installed later becomes the outer one, and
 *     nothing here assumes it is the only DLL that wants this instant.
 *   - the engine's own filled-shape drawer (0x00419660), identical to
 *     dev_overlay/overlay_sites.c's own SIG_DRAW_QUAD, what the game draws its own letterbox bars
 *     and screen tint with, four screen coordinates plus a packed ARGB.
 *   - the screen size cells, identical to overlay_sites.c's own SIG_SCREEN_SIZE.
 *
 * Drawn every real frame while armed, right before the scene closes and the page is shown, so the
 * same instant dev_overlay's own panel paints into, which is why a panel opened on top of this
 * still shows on top of it: this file's hook runs as the outer wrapper (loading after
 * "dev_overlay" alphabetically), draws its own quad, THEN calls original, which is what reaches
 * dev_overlay's own hook and its own, later paint.
 */
#include "render_curtain.h"

#include "sfx_mute.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stdint.h>

/* --- the call that closes the scene, byte-identical to dev_overlay.c's own SIG_SCENE_END. */
static const uint8_t SIG_SCENE_END[] = {
    0x6A, 0x00,                                                   /* push 0          */
    0xE8, 0x00, 0x00, 0x00, 0x00,                                 /* a call          */
    0x83, 0xC4, 0x04,                                             /* add esp,4       */
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* a flag, cleared */
    0xE8, 0x00, 0x00, 0x00, 0x00,                                 /* close the scene */
    0xE8, 0x00, 0x00, 0x00, 0x00                                  /* show the page   */
};
static const uint8_t MSK_SCENE_END[] = {
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_SCENE_END_CALL 20u

/* --- 0x00419660, the engine's own filled shape, byte-identical to overlay_sites.c's own
 * SIG_DRAW_QUAD. */
static const uint8_t SIG_DRAW_QUAD[] = {
    0x81, 0xEC, 0x84, 0x00, 0x00, 0x00,              /* sub esp,0x84             */
    0xD9, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00,        /* fld dword [esp+0x90]     */
    0xD8, 0x25, 0x00, 0x00, 0x00, 0x00               /* fsub the pixel snap      */
};
static const uint8_t MSK_DRAW_QUAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};

/* --- 0x00439476, the screen size, byte-identical to overlay_sites.c's own SIG_SCREEN_SIZE. */
static const uint8_t SIG_SCREEN_SIZE[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SCREEN_SIZE[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_SCREEN_HEIGHT 1u
#define OFFSET_SCREEN_WIDTH  8u

#define OVERLAY_DRAW_NOW 1
#define CALL_REL32_OPCODE 0xE8u
#define CALL_REL32_LENGTH  5u

/* patch_read_call_target() refuses a target outside WMAIN.EXE's own image, which is exactly right
 * for validating an UNTOUCHED site, and exactly wrong here. dev_overlay.dll already redirects this
 * same call site to its own hook, in its own module, well outside WMAIN.EXE's image, whenever it
 * loads first (alphabetically, "dev_overlay" sorts before "fmv_player"). Reading THAT as garbage
 * and refusing is what "no usable call" actually meant: not a bad site, a target that happened to
 * be a legitimate cross-DLL trampoline. This is patch_read_call_target's own opcode/displacement
 * arithmetic without the image-boundary check, used only to discover what to chain to. */
static bool resolve_call_target_anywhere(uintptr_t call_address, uintptr_t *out_target)
{
    uint8_t  opcode = 0;
    uint32_t displacement = 0;

    if (!memory_read_u8(call_address, &opcode) || opcode != CALL_REL32_OPCODE) {
        return false;
    }
    if (!memory_read_u32(call_address + 1, &displacement)) {
        return false;
    }
    *out_target = call_address + CALL_REL32_LENGTH + displacement;
    return true;
}

typedef void (__cdecl *scene_end_fn_t)(void);
typedef void (__cdecl *draw_quad_fn_t)(float x0, float y0, float x1, float y1, uint32_t argb,
                                       int32_t layer);

typedef struct render_curtain_state {
    bool                   resolved;
    scene_end_fn_t         scene_end_original;
    draw_quad_fn_t         quad;
    const volatile float  *screen_w;
    const volatile float  *screen_h;

    bool                   active;
    bool                   fading;
    DWORD                  hold_until;
    DWORD                  fade_until;
} render_curtain_state_t;

static render_curtain_state_t curtain_state;
static DWORD hold_ms = 2500;
static DWORD fade_ms = 300;
static bool  mute_enabled = true;

void render_curtain_set_hold_ms(unsigned milliseconds)
{
    hold_ms = (DWORD)milliseconds;
}

void render_curtain_set_fade_ms(unsigned milliseconds)
{
    fade_ms = (DWORD)milliseconds;
}

void render_curtain_set_mute_enabled(bool enabled)
{
    mute_enabled = enabled;
}

static uint32_t fade_argb(DWORD now)
{
    DWORD remaining = curtain_state.fade_until - now;
    DWORD elapsed = (remaining < fade_ms) ? (fade_ms - remaining) : 0;
    uint8_t alpha = (uint8_t)(255u - (255u * elapsed) / fade_ms);

    return ((uint32_t)alpha) << 24;
}

static void __cdecl hook_scene_end(void)
{
    if (curtain_state.active) {
        DWORD now = timeGetTime();
        bool  finished = false;

        if (!curtain_state.fading && (int32_t)(now - curtain_state.hold_until) >= 0) {
            if (fade_ms == 0) {
                finished = true;
            } else {
                curtain_state.fading = true;
                curtain_state.fade_until = now + fade_ms;
            }
        }
        if (!finished && curtain_state.fading && (int32_t)(now - curtain_state.fade_until) >= 0) {
            finished = true;
        }

        if (finished) {
            curtain_state.active = false;
            curtain_state.fading = false;
            if (mute_enabled) {
                sfx_mute_end();
            }
        } else if (curtain_state.quad != NULL && curtain_state.screen_w != NULL &&
                  curtain_state.screen_h != NULL && *curtain_state.screen_w > 0.0f &&
                  *curtain_state.screen_h > 0.0f) {
            uint32_t argb = curtain_state.fading ? fade_argb(now) : 0xFF000000u;

            curtain_state.quad(0.0f, 0.0f, *curtain_state.screen_w, *curtain_state.screen_h, argb,
                               OVERLAY_DRAW_NOW);
        }
    }
    curtain_state.scene_end_original();
}

void render_curtain_begin(void)
{
    if (!curtain_state.resolved || curtain_state.active || hold_ms == 0) {
        return;
    }
    curtain_state.active = true;
    curtain_state.fading = false;
    curtain_state.hold_until = timeGetTime() + hold_ms;
    if (mute_enabled) {
        sfx_mute_begin();
    }
}

void render_curtain_install(void)
{
    uintptr_t scene_end_site;
    uintptr_t quad_site;
    uintptr_t screen_site;
    uintptr_t call;
    uintptr_t original = 0;
    uint32_t  width_cell = 0;
    uint32_t  height_cell = 0;

    /* sfx_mute.c has nothing of its own to be installed FOR outside of this curtain, so this is
     * the one place that brings it up rather than making every caller remember both. */
    sfx_mute_install();

    scene_end_site = signature_find_unique(SIG_SCENE_END, MSK_SCENE_END, sizeof SIG_SCENE_END);
    if (scene_end_site == 0) {
        log_warning("render_curtain: the end of the scene did not resolve, the post-movie curtain "
                    "cannot be drawn into the picture");
        return;
    }
    quad_site = signature_find_unique(SIG_DRAW_QUAD, MSK_DRAW_QUAD, sizeof SIG_DRAW_QUAD);
    if (quad_site == 0) {
        log_warning("render_curtain: the engine's own filled-shape drawer did not resolve, the "
                    "post-movie curtain cannot be drawn");
        return;
    }
    screen_site = signature_find_unique(SIG_SCREEN_SIZE, MSK_SCREEN_SIZE, sizeof SIG_SCREEN_SIZE);
    if (screen_site == 0 ||
        !memory_read_u32(screen_site + OFFSET_SCREEN_WIDTH, &width_cell) ||
        !memory_read_u32(screen_site + OFFSET_SCREEN_HEIGHT, &height_cell) ||
        !memory_is_inside_image(width_cell, sizeof(float)) ||
        !memory_is_inside_image(height_cell, sizeof(float))) {
        log_warning("render_curtain: the screen size did not resolve, the post-movie curtain "
                    "cannot be sized");
        return;
    }

    call = scene_end_site + OFFSET_SCENE_END_CALL;
    if (!resolve_call_target_anywhere(call, &original)) {
        log_warning("render_curtain: no usable call at %08X", (unsigned)call);
        return;
    }
    curtain_state.scene_end_original = (scene_end_fn_t)original;
    if (patch_redirect_call(call, (const void *)&hook_scene_end) != PATCH_RESULT_OK) {
        log_error("render_curtain: redirecting the scene-end call at %08X failed", (unsigned)call);
        return;
    }

    curtain_state.quad = (draw_quad_fn_t)quad_site;
    curtain_state.screen_w = (const volatile float *)(uintptr_t)width_cell;
    curtain_state.screen_h = (const volatile float *)(uintptr_t)height_cell;
    curtain_state.resolved = true;

    log_info("render_curtain: drawn through the engine's own shape drawer at %08X, right before "
             "the scene closes at %08X, part of the real rendered frame, so any capture of the "
             "game shows it the same way the player's own screen does",
             (unsigned)quad_site, (unsigned)call);
}
