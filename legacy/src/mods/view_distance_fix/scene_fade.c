/* scene_fade.c: see scene_fade.h. */
#include "scene_fade.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x0043ED48  the level's fade-in, armed right after the post-load broadcast --------------- *
 *   6A 00  6A 00  6A 00  6A 00     blue, green, red, hold-at-end: black, do not hold
 *   68 0000A040 (4.0f)             the duration in seconds
 *   6A 01                          mode 1, fade IN from the colour
 *   E8 <rel32>                     call the fade setup
 *
 * The call displacement is wildcarded so the pattern carries no address of its own. It matches
 * once; the image holds many `push 4.0f` sites and this is the only one followed by `push 1` and a
 * call. */
static const uint8_t SIG_LEVEL_FADE_IN[] = {
    0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00,
    0x68, 0x00, 0x00, 0x80, 0x40, 0x6A, 0x01, 0xE8,
    0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_LEVEL_FADE_IN[] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0
};
_Static_assert(sizeof SIG_LEVEL_FADE_IN == sizeof MSK_LEVEL_FADE_IN,
               "the fade pattern and its mask are different lengths");

#define OFFSET_DURATION 9u
#define DURATION_AS_SHIPPED  4.0f
#define DURATION_ONE_FRAME   0.001f    /* done before the first frame is drawn */

void scene_fade_install(float seconds)
{
    uintptr_t site;
    float     held = 0.0f;
    float     wanted;

    if (seconds == DURATION_AS_SHIPPED) {
        return;                                /* the engine's own value, nothing to write */
    }
    /* Never zero. The animator divides the elapsed time by this, and the shortest fade the engine
     * can be asked for without that division is one that finishes inside the first frame. */
    wanted = (seconds < DURATION_ONE_FRAME) ? DURATION_ONE_FRAME : seconds;

    site = signature_find_unique(SIG_LEVEL_FADE_IN, MSK_LEVEL_FADE_IN, sizeof SIG_LEVEL_FADE_IN);
    if (site == 0) {
        log_warning("LevelFadeSeconds=%.3f was asked for but the fade site did not resolve, so the "
                    "level keeps its four second fade", (double)seconds);
        return;
    }
    if (!memory_try_read(site + OFFSET_DURATION, &held, sizeof(held))) {
        return;
    }
    if (held == wanted) {
        return;                                /* idempotent */
    }
    if (held != DURATION_AS_SHIPPED) {
        log_warning("the fade duration at %08X reads %.3f rather than the expected %.3f, refused",
                    (unsigned)(site + OFFSET_DURATION), (double)held, (double)DURATION_AS_SHIPPED);
        return;
    }
    if (patch_write_f32(site + OFFSET_DURATION, wanted) != PATCH_RESULT_OK) {
        log_error("the fade duration at %08X is not writable",
                  (unsigned)(site + OFFSET_DURATION));
        return;
    }

    log_info("LevelFadeSeconds=%.3f: the level's fade from black at %08X now takes %.3f s instead "
             "of %.1f. The fade is a full-screen black quad multiplying the whole picture, and "
             "against a 16-bit buffer its slide crosses 5-bit boundaries in visible jumps: a flat "
             "region steps as a block, lit geometry does not, and the edge between them reads as a "
             "flashing line. The steps belong to the buffer and cannot be removed here; how long "
             "they stay on screen belongs to the fade and can be.",
             (double)seconds, (unsigned)site, (double)wanted, (double)DURATION_AS_SHIPPED);
}
