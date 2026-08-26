#include "frame_hook.h"

#include "detour.h"
#include "logging.h"
#include "signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0046C139  render_frameEnd (function start) -------------------------------------------- *
 *   55 8B EC 81 EC F8 03 00 00      prologue, 9 bytes, clean instruction boundary
 *   A1 14 87 86 00                  mov eax,[g_frameDelta]   -> its address at +0x0A            */
static const uint8_t SIG_RENDER_FRAME_END[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xF8, 0x03, 0x00, 0x00,
    0xA1, 0x14, 0x87, 0x86, 0x00, 0x50, 0x6A, 0x15, 0x6A, 0x00, 0xE8
};
#define RENDER_FRAME_END_PROLOGUE_SIZE 9u

/* Every DLL links its own copy of this library, so this is a per DLL ceiling and each slot costs
 * one pointer. It was 4, which the diagnostics DLL reached exactly: the frame summary, the present
 * timing, the projectile census and the world censuses. A fifth observer there would have been
 * refused, and the refusal is a log line rather than a crash, which is the kind of failure that
 * gets read as "the feature does nothing" instead of "the feature never installed". */
#define MAX_FRAME_CALLBACKS 8

typedef void (__cdecl *frame_end_fn_t)(void);

typedef struct frame_hook_state {
    detour_t              detour;
    uintptr_t             site;
    frame_hook_callback_t callbacks[MAX_FRAME_CALLBACKS];
    size_t                callback_count;
} frame_hook_state_t;

static frame_hook_state_t frame_state;

static void __cdecl hook_frame_end(void)
{
    frame_end_fn_t original = (frame_end_fn_t)frame_state.detour.original;
    size_t         index;

    original();

    for (index = 0; index < frame_state.callback_count; ++index) {
        frame_state.callbacks[index]();
    }
}

static bool install_detour(void)
{
    if (frame_state.detour.installed) {
        return true;
    }

    /* Four DLLs want this one function, so the first installer's `jmp rel32` is the NORMAL
     * state by the time the others look. signature_find_detour_target handles that. */
    frame_state.site = signature_find_detour_target(SIG_RENDER_FRAME_END, NULL,
                                                    sizeof(SIG_RENDER_FRAME_END),
                                                    RENDER_FRAME_END_PROLOGUE_SIZE);
    if (frame_state.site == 0) {
        log_warning("frame: render_frameEnd did not resolve, there is NO per-frame hook");
        return false;
    }

    if (!detour_install(&frame_state.detour, frame_state.site, (const void *)hook_frame_end,
                        RENDER_FRAME_END_PROLOGUE_SIZE)) {
        log_error("frame: the render_frameEnd detour at %08X failed", (unsigned)frame_state.site);
        return false;
    }

    log_info("frame: hooked render_frameEnd at %08X", (unsigned)frame_state.site);
    return true;
}

bool frame_hook_add(frame_hook_callback_t callback)
{
    size_t index;

    if (callback == NULL) {
        return false;
    }
    if (!install_detour()) {
        return false;
    }

    for (index = 0; index < frame_state.callback_count; ++index) {
        if (frame_state.callbacks[index] == callback) {
            return true;                           /* idempotent */
        }
    }
    if (frame_state.callback_count >= MAX_FRAME_CALLBACKS) {
        log_error("frame: more than %d callbacks in one DLL - the last one is dropped",
                  MAX_FRAME_CALLBACKS);
        return false;
    }

    frame_state.callbacks[frame_state.callback_count] = callback;
    ++frame_state.callback_count;
    return true;
}

bool frame_hook_is_installed(void)
{
    return frame_state.detour.installed;
}

uintptr_t frame_hook_site(void)
{
    return frame_state.site;
}
