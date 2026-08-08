/* camera_watch.c: one line, at the instant the camera swings, naming which term moved.
 *
 * The decomposition this rests on is byte-read out of the camera update:
 *
 *     0x004185E5   interp = headPrevious + wrap180(headCurrent, headPrevious) * substepAlpha
 *     0x00418F86   yaw    = wrap360(interp + cameraYawOffset)
 *     0x00418FAA   view->euler.y = yaw
 *
 * Two terms, so a jump has two possible authors and printing both settles it. The line also carries
 * the raw inputs of the first term, because "the interpolated heading moved" has three quite
 * different causes and they are distinguishable only from headPrevious, headCurrent and the alpha:
 *
 *   * both headings moved together        -> the body really turned that far
 *   * the headings barely moved           -> the alpha did it, and it left (0,1]
 *   * headPrevious jumped to headCurrent  -> the shift register skipped, i.e. a frame ran several
 *                                            substeps or none
 */
#include "camera_watch.h"

#include "free_look_math.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_SECTION "enhanced_input"

/* Degrees the camera may turn in ONE rendered frame before the turn is worth a line. At 90 frames
 * per second 25 degrees is 2250 deg/s, which no ordinary frame of play reaches: the engine's own
 * turn ceiling is 120 deg/s and the mouse bolt cuts a hand off at 3000. It is deliberately well
 * below the reported symptom, "it whips completely round", so that the smaller swings leading
 * up to one are caught too. */
#define DEFAULT_JUMP_DEGREES 25.0f
#define MIN_JUMP_DEGREES      1.0f
#define MAX_JUMP_DEGREES    180.0f

/* Enough to see a pattern across a level, few enough that a camera flapping every frame cannot
 * fill a log. The count keeps rising after the last line so the summary can say how many there
 * really were. */
#define MAX_JUMP_LINES 60

typedef struct camera_watch_state {
    bool                 armed;
    const camera_sites_t *sites;
    float                jump_degrees;

    bool     have_previous;
    float    previous_yaw;
    uint32_t jumps;

    /* Sampled BEFORE the camera update, because it rewrites the offset at its own tail. */
    bool  inputs_valid;
    float in_head_previous;
    float in_head_current;
    float in_alpha;
    float in_offset;
} camera_watch_state_t;

static camera_watch_state_t watch_state;

void camera_watch_install(const camera_sites_t *sites)
{
    float threshold;

    if (sites == NULL || sites->view == NULL) {
        return;
    }

    threshold = ini_read_float(INPUT_SECTION, "CameraJumpWatchDeg", DEFAULT_JUMP_DEGREES);
    if (threshold <= 0.0f) {          /* also catches NaN: switched off by configuration */
        return;
    }
    if (threshold < MIN_JUMP_DEGREES) {
        threshold = MIN_JUMP_DEGREES;
    }
    if (threshold > MAX_JUMP_DEGREES) {
        threshold = MAX_JUMP_DEGREES;
    }

    watch_state.sites        = sites;
    watch_state.jump_degrees = threshold;
    watch_state.armed        = true;

    log_info("camera jump watch armed at %.0f degrees in one frame, it prints only when the "
             "camera really moves that far in a single frame, at most %d times, and names whether "
             "the interpolated heading or the yaw offset did it. CameraJumpWatchDeg=0 switches it "
             "off.", (double)threshold, MAX_JUMP_LINES);
}

void camera_watch_before_update(void)
{
    if (!watch_state.armed) {
        return;
    }
    watch_state.in_head_previous = *watch_state.sites->head_previous;
    watch_state.in_head_current  = *watch_state.sites->head_current;
    watch_state.in_alpha         = *watch_state.sites->substep_alpha;
    watch_state.in_offset        = *watch_state.sites->camera_yaw_offset;
    watch_state.inputs_valid     = true;
}

void camera_watch_sample(void)
{
    const uint8_t *view;
    float          yaw;
    float          delta;
    float          head_previous;
    float          head_current;
    float          alpha;
    float          interpolated;
    float          offset;

    if (!watch_state.armed) {
        return;
    }

    view = (const uint8_t *)*watch_state.sites->view;
    if (view == NULL || !memory_is_readable_range((uintptr_t)view, BAPVIEW_READ_SIZE)) {
        /* No camera object this frame, a load, or the front end. The previous yaw is dropped so
         * that the first frame of the next one is not measured against a stale number and reported
         * as a jump it never made. */
        watch_state.have_previous = false;
        return;
    }

    yaw = *(const float *)(view + BAPVIEW_EULER_YAW_OFFSET);
    if (!isfinite(yaw)) {
        watch_state.have_previous = false;
        return;
    }

    if (!watch_state.have_previous) {
        watch_state.have_previous = true;
        watch_state.previous_yaw  = yaw;
        return;
    }

    delta = free_look_wrap180(yaw - watch_state.previous_yaw);
    watch_state.previous_yaw = yaw;

    if (fabsf(delta) < watch_state.jump_degrees) {
        return;
    }

    ++watch_state.jumps;
    if (watch_state.jumps > MAX_JUMP_LINES) {
        return;
    }

    if (!watch_state.inputs_valid) {
        return;
    }
    head_previous = watch_state.in_head_previous;
    head_current  = watch_state.in_head_current;
    alpha         = watch_state.in_alpha;
    interpolated  = free_look_interpolated_heading(head_previous, head_current, alpha);
    offset        = watch_state.in_offset;

    /* The two terms are printed beside the result they compose, so the line can be checked against
     * itself: wrap360(interp + offset) must be the yaw. Where it is not, the reader is looking at a
     * third writer, and that is the finding. */
    log_warning("CAMERA JUMP %+.1f deg in one frame -> yaw %.1f. interp %.1f = prev %.1f + "
                "wrap180(cur %.1f, prev) * alpha %.3f, offset %.1f. Region %08X flags %02X, "
                "authored yaw %.1f. gOver %d, snap %d, camera state %d. Composed %.1f, offset "
                "AFTER %.1f. Jump %u.",
                (double)delta, (double)yaw, (double)interpolated, (double)head_previous,
                (double)head_current, (double)alpha, (double)offset,
                (unsigned)(uintptr_t)(watch_state.sites->current_region != NULL
                                      ? *watch_state.sites->current_region : NULL),
                (unsigned)((watch_state.sites->current_region != NULL &&
                            *watch_state.sites->current_region != NULL)
                           ? *(const uint32_t *)*watch_state.sites->current_region : 0u),
                (double)*watch_state.sites->region_yaw,
                (int)*watch_state.sites->camera_override,
                (int)*watch_state.sites->snap_countdown,
                (int)*(const int32_t *)view,
                (double)free_look_wrap360(interpolated + offset),
                (double)*watch_state.sites->camera_yaw_offset,
                watch_state.jumps);

    if (watch_state.jumps == MAX_JUMP_LINES) {
        log_warning("the camera jump watch has printed %d lines and prints no more this session. A "
                    "count that high is itself the finding: the camera is not jumping "
                    "occasionally, it is doing it constantly.", MAX_JUMP_LINES);
    }
}
