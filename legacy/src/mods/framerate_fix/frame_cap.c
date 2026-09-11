/* frame_cap.c: see frame_cap.h. */
#include "frame_cap.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

/* --- 0x00475B75  sys_waitForFrame (function start) ------------------------------------------- *
 *   +0x10 : the 1/30 immediate (0x3D088889)
 *   +0x16 : `mov dword ptr [ebp-4],0x3C888889`, the "60fps" cheat arm, whose immediate is +0x19
 *   +0x39 : `cmp [g_frameLimiterOn], 0`, its absolute operand at +0x3B
 *
 * The site itself is resolved by framerate_fix.c, which owns the signature table for this
 * function: the render cap, the frame delta repair and the Sleep push all live on it, and one
 * owner for the pattern keeps them from each other's throats. */
#define OFFSET_CAP_30_IMMEDIATE 0x10u
#define OFFSET_CAP_60_MOV       0x16u
#define OFFSET_CAP_60_IMMEDIATE 0x19u
#define OFFSET_LIMITER_CMP      0x39u
#define OFFSET_LIMITER_ADDRESS  0x3Bu

/* The whole seven byte instruction rather than its immediate alone. Checking four bytes would
 * accept the right immediate in the wrong instruction, which on a build that diverges after the
 * matched prologue is a write into the middle of something else. */
static const uint8_t EXPECTED_CAP_60_MOV[7] = { 0xC7, 0x45, 0xFC, 0x89, 0x88, 0x88, 0x3C };

/* The limiter flag ships as 1. Clearing it is how uncapped is expressed, and putting it back is
 * how a live change returns from uncapped to capped. */
#define LIMITER_ON  1u
#define LIMITER_OFF 0u

/* VREFRESH, from wingdi.h. Named here rather than included for, because this is the only thing
 * this file wants out of it. */
#define DEVICE_CAP_VREFRESH 116

/* A rate outside this is a driver reporting something other than a rate. 0 and 1 both mean "the
 * hardware default" and neither is a number to cap to. */
#define REFRESH_MINIMUM 20
#define REFRESH_MAXIMUM 1000

static uintptr_t cap_site;
static uintptr_t cap_limiter_address;
static bool      cap_usable;
static int       cap_applied = -1;      /* -1 means nothing has been applied yet */

int frame_cap_effective(int configured, bool match_refresh, int refresh_hz)
{
    if (configured < 0) {
        configured = 0;
    }
    if (configured > REFRESH_MAXIMUM) {
        configured = REFRESH_MAXIMUM;
    }
    if (!match_refresh) {
        return configured;
    }
    if (refresh_hz < REFRESH_MINIMUM || refresh_hz > REFRESH_MAXIMUM) {
        /* Asked to match and cannot. The configured number stands, because the alternatives are
         * to uncap the game or to cap it at nothing, and both are worse than the answer the
         * player already gave. */
        return configured;
    }
    return refresh_hz;
}

int frame_cap_refresh_hz(void)
{
    HDC dc;
    int hz;

    dc = GetDC(NULL);
    if (dc == NULL) {
        return 0;
    }
    hz = GetDeviceCaps(dc, DEVICE_CAP_VREFRESH);
    (void)ReleaseDC(NULL, dc);

    if (hz < REFRESH_MINIMUM || hz > REFRESH_MAXIMUM) {
        return 0;
    }
    return hz;
}

bool frame_cap_install(uintptr_t wait_site)
{
    uint8_t  cmp_opcode[2];
    uint32_t limiter_address;

    cap_usable = false;
    if (wait_site == 0) {
        return false;
    }

    /* Everything a later write will touch is checked now, so a change made from the dev panel a
     * quarter of an hour into a session cannot be the thing that discovers a bad byte. */
    if (!patch_validate_bytes(wait_site + OFFSET_CAP_60_MOV, EXPECTED_CAP_60_MOV,
                              sizeof EXPECTED_CAP_60_MOV)) {
        log_warning("the 60fps cheat arm is not the expected mov [ebp-4],1/60 at %08X, so the "
                    "render cap is left alone entirely rather than half applied",
                    (unsigned)(wait_site + OFFSET_CAP_60_MOV));
        return false;
    }
    if (!memory_read(wait_site + OFFSET_LIMITER_CMP, cmp_opcode, sizeof cmp_opcode) ||
        cmp_opcode[0] != 0x83 || cmp_opcode[1] != 0x3D) {
        log_warning("the limiter cmp [imm32],0 shape is not at %08X, so the render cap is left "
                    "alone", (unsigned)(wait_site + OFFSET_LIMITER_CMP));
        return false;
    }
    if (!memory_read_u32(wait_site + OFFSET_LIMITER_ADDRESS, &limiter_address) ||
        !memory_is_inside_image(limiter_address, sizeof(uint32_t))) {
        log_warning("the limiter address %08X is outside the image, so the render cap is left "
                    "alone", (unsigned)limiter_address);
        return false;
    }

    cap_site            = wait_site;
    cap_limiter_address = (uintptr_t)limiter_address;
    cap_usable          = true;
    return true;
}

void frame_cap_apply(int fps)
{
    float cap;

    if (!cap_usable || fps == cap_applied) {
        return;
    }
    if (fps < 0) {
        fps = 0;
    }

    if (fps > 0) {
        cap = 1.0f / (float)fps;
        patch_write_f32(cap_site + OFFSET_CAP_30_IMMEDIATE, cap);
        patch_write_f32(cap_site + OFFSET_CAP_60_IMMEDIATE, cap);
        patch_write_u32(cap_limiter_address, LIMITER_ON);
        log_info("render cap %d fps (%.8f s) at %08X and %08X, limiter [%08X] on",
                 fps, (double)cap,
                 (unsigned)(cap_site + OFFSET_CAP_30_IMMEDIATE),
                 (unsigned)(cap_site + OFFSET_CAP_60_IMMEDIATE),
                 (unsigned)cap_limiter_address);
    } else {
        patch_write_u32(cap_limiter_address, LIMITER_OFF);
        log_info("render cap removed, g_frameLimiterOn [%08X] cleared. The game will draw as fast "
                 "as the machine manages, which is smooth because the display always has a recent "
                 "frame rather than because anything is paced, and it loads one core fully",
                 (unsigned)cap_limiter_address);
    }
    cap_applied = fps;
}
