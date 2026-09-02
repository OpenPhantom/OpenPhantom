/* frame_governor.c: the frame-time half of the view distance's two watchdogs. See the header for
 * the field measurement this exists because of, and for why frame time is the only instrument that
 * can see the cost it guards against. */
#include "frame_governor.h"

#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#include <stdlib.h>
#include <string.h>

/* One step, the same coarse step the cell watchdog moves in. Matching it is deliberate: two
 * governors on one number that moved in different increments would make the log much harder to
 * read than it needs to be, and neither has any claim to a finer one. */
#define GOVERNOR_SCALE_STEP 0.15f
#define GOVERNOR_SCALE_FLOOR 1.00f

/* The margin between "too slow" and "fast enough to try again", as a multiple of the backoff rate.
 * The band between the two is the dead zone the governor settles into. 15 % is wide enough that
 * the ordinary variation within one scene does not cross it and narrow enough that a real recovery
 * is still recognised. */
#define GOVERNOR_RECOVERY_MARGIN 1.15f

/* Impatient about pain, slow about recovery: one bad second is enough to take a step, and it takes
 * thirty good ones in a row to give the FIRST one back. Thirty consecutive good seconds is a much
 * stronger claim than one, and it is what distinguishes "the heavy scene is over" from a lull in
 * the middle of it.
 *
 * Once that first step back has been earned, the rest come every ten seconds. The expensive claim
 * is the first one; after it has been made, holding the remaining steps at half a minute each only
 * means a player who has walked out of a bad scene spends four minutes climbing back to the
 * setting they asked for, which the first field test did. */
#define GOVERNOR_HEALTHY_SECONDS       30u
#define GOVERNOR_HEALTHY_SECONDS_AGAIN 10u

/* How often it decides, and it is not once a second any more.
 *
 * The cutscene this was built for is about thirteen seconds long and the first version took
 * fourteen to walk 2.50 down to 1.00, so it arrived after the thing it was reacting to had
 * finished. Half a second doubles the rate without making the measurement much noisier: at 66 fps
 * a window still holds better than thirty frames, which is plenty to take a median of.
 *
 * The healthy counts above are in SECONDS and are converted here, so changing this interval does
 * not silently halve how long a recovery takes. */
#define GOVERNOR_DECISION_MS 500u
#define GOVERNOR_DECISIONS_PER(seconds)     (((seconds) * 1000u) / GOVERNOR_DECISION_MS)

/* The shortfall at which a whole step is the right answer: 10 % past the frame time the scale is
 * allowed to cost. Below that the step shrinks towards a third of one, above it grows to four
 * times, so the governor eases into the target from a near miss and moves properly when a scene
 * turns sharply heavier.
 *
 * These were both twice as timid to begin with, and the field runs said so. A 10 % miss - 14.7 ms
 * against 13.3 - took a step of 0.077, and at that size crossing the range this setting actually
 * has takes longer than the scene being reacted to lasts. The measured gain is real and large:
 * the same cutscene runs at about 15 ms at 2.50 and a flat 10.00 ms at 1.00, so a governor being
 * delicate about a 10 % miss is being delicate about the wrong thing. */
#define GOVERNOR_FULL_STEP_SHORTFALL 0.10f
#define GOVERNOR_STEP_MIN_FRACTION   0.333333f
#define GOVERNOR_STEP_MAX_FRACTION   4.0f

/* When there is no frame cap to miss, this is what counts as too slow. Only used when
 * framerate_fix is uncapped, where there is no target to measure against. */
#define GOVERNOR_UNCAPPED_BACKOFF_FPS 50.0f

/* The fraction of a frame cap that counts as missing it. At TargetFps 100 this is 75 fps: not a
 * frame rate anybody would call broken, which is the point. The report this was built for was a
 * drop from 100 to 66, and a governor that only woke at 30 fps would have slept through it. */
#define GOVERNOR_CAP_FRACTION 0.75f

/* The frame times of the window being decided on. Emptied after every decision, deliberately: a
 * ring that is never emptied answers with a median of the last 256 frames however long that is,
 * which at 100 fps is two and a half seconds of history and lags the thing it is measuring by
 * over a second. That lag was half of why the first field runs reacted so late. It is only ever
 * read as a whole and sorted, so where the cursor sits does not matter; the size is a ceiling for
 * a very fast machine, not a window length. */
#define GOVERNOR_RING 256u

typedef struct {
    bool     enabled;
    float    lower_above_ms;
    float    raise_below_ms;
    float    ceiling;              /* the scale THIS governor allows; never below 1.0 */
    uint32_t healthy_seconds;
    uint32_t healthy_needed;       /* 30 for the first step back, 10 for the ones after it */

    float    samples[GOVERNOR_RING];
    uint32_t sample_count;         /* how many of the ring are valid, saturates at GOVERNOR_RING */
    uint32_t cursor;

    LARGE_INTEGER frequency;
    LONGLONG      last_frame_at;
    LONGLONG      second_began_at;
} governor_state_t;

static governor_state_t governor;

/* ============================================================================================ */
frame_governor_action_t frame_governor_decide(float median_ms,
                                              float lower_above_ms,
                                              float raise_below_ms,
                                              uint32_t healthy_seconds,
                                              uint32_t healthy_seconds_needed)
{
    /* A measurement that cannot be believed, or a pair of thresholds that are not a dead zone at
     * all, both mean the same thing here: this governor does not know enough to act. */
    if (!(median_ms > 0.0f) || !(lower_above_ms > 0.0f) || !(raise_below_ms > 0.0f) ||
        !(raise_below_ms < lower_above_ms)) {
        return FRAME_GOVERNOR_HOLD;
    }
    if (median_ms > lower_above_ms) {
        return FRAME_GOVERNOR_LOWER;
    }
    if (median_ms < raise_below_ms && healthy_seconds >= healthy_seconds_needed) {
        return FRAME_GOVERNOR_RAISE;
    }
    return FRAME_GOVERNOR_HOLD;
}

float frame_governor_step_size(float median_ms, float lower_above_ms, float base_step,
                               float full_step_shortfall)
{
    float shortfall;
    float fraction;

    if (!(median_ms > lower_above_ms) || !(lower_above_ms > 0.0f) || !(base_step > 0.0f) ||
        !(full_step_shortfall > 0.0f)) {
        return 0.0f;
    }
    shortfall = (median_ms / lower_above_ms) - 1.0f;
    fraction = shortfall / full_step_shortfall;

    if (fraction < GOVERNOR_STEP_MIN_FRACTION) {
        fraction = GOVERNOR_STEP_MIN_FRACTION;
    } else if (fraction > GOVERNOR_STEP_MAX_FRACTION) {
        fraction = GOVERNOR_STEP_MAX_FRACTION;
    }
    return base_step * fraction;
}

/* ============================================================================================ */
static int compare_float(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;

    return (a < b) ? -1 : ((a > b) ? 1 : 0);
}

/* The middle sample of the ring. A median rather than a mean specifically so that one enormous
 * frame, and a level load is 250 ms of one, cannot move the decision. A mean over a second
 * containing a single 250 ms frame reads as 12 ms when every other frame was 10. */
static float median_frame_ms(void)
{
    float    sorted[GOVERNOR_RING];
    uint32_t count = governor.sample_count;

    if (count == 0) {
        return 0.0f;
    }
    memcpy(sorted, governor.samples, count * sizeof sorted[0]);
    qsort(sorted, count, sizeof sorted[0], compare_float);
    return sorted[count / 2u];
}

static void apply(float *effective_view_scale, float configured_scale, float cell_ceiling)
{
    float allowed = configured_scale;

    if (governor.ceiling < allowed) {
        allowed = governor.ceiling;
    }
    /* Correctness outranks comfort. The cell watchdog lowers to keep the draw table from
     * overflowing into the bucket list heads, and this must never hand back a scale it refused. */
    if (cell_ceiling < allowed) {
        allowed = cell_ceiling;
    }
    if (allowed < GOVERNOR_SCALE_FLOOR) {
        allowed = GOVERNOR_SCALE_FLOOR;
    }
    *effective_view_scale = allowed;
}

void frame_governor_on_frame(float *effective_view_scale, float configured_scale,
                             float cell_ceiling)
{
    LARGE_INTEGER now;
    float         elapsed_ms;

    if (effective_view_scale == NULL || !governor.enabled) {
        return;
    }
    if (governor.frequency.QuadPart <= 0 || !QueryPerformanceCounter(&now)) {
        return;
    }

    if (governor.last_frame_at != 0) {
        elapsed_ms = (float)(((double)(now.QuadPart - governor.last_frame_at) * 1000.0) /
                             (double)governor.frequency.QuadPart);
        if (elapsed_ms > 0.0f) {
            governor.samples[governor.cursor] = elapsed_ms;
            governor.cursor = (governor.cursor + 1u) % GOVERNOR_RING;
            if (governor.sample_count < GOVERNOR_RING) {
                governor.sample_count++;
            }
        }
    }
    governor.last_frame_at = now.QuadPart;

    /* Decisions are once a second, on the whole window, not per frame. Per-frame steering on a
     * per-frame measurement is how a governor becomes an oscillator. */
    if (governor.second_began_at == 0) {
        governor.second_began_at = now.QuadPart;
    }
    if ((now.QuadPart - governor.second_began_at) >=
        ((governor.frequency.QuadPart * (LONGLONG)GOVERNOR_DECISION_MS) / 1000)) {
        const float median = median_frame_ms();

        governor.second_began_at = now.QuadPart;
        /* The window is spent. Everything below decides on THIS window, and the next one starts
         * empty so that it describes the next half second rather than the last few. */
        governor.sample_count = 0;
        governor.cursor = 0;

        switch (frame_governor_decide(median, governor.lower_above_ms, governor.raise_below_ms,
                                      governor.healthy_seconds, governor.healthy_needed)) {
        case FRAME_GOVERNOR_LOWER:
            governor.healthy_seconds = 0;
            /* Back to the patient count: whatever this scene is, it has just proved it is not over,
             * and the first step back out of it has to be earned the hard way again. */
            governor.healthy_needed = GOVERNOR_DECISIONS_PER(GOVERNOR_HEALTHY_SECONDS);

            if (governor.ceiling > GOVERNOR_SCALE_FLOOR) {
                const float previous = governor.ceiling;
                const float step = frame_governor_step_size(median, governor.lower_above_ms,
                                                            GOVERNOR_SCALE_STEP,
                                                            GOVERNOR_FULL_STEP_SHORTFALL);

                governor.ceiling -= step;
                if (governor.ceiling < GOVERNOR_SCALE_FLOOR) {
                    governor.ceiling = GOVERNOR_SCALE_FLOOR;
                }
                log_info("frame governor: %.1f ms a frame (%.0f fps), %.0f%% past the %.1f ms this "
                         "is allowed to cost. View scale %.2f -> %.2f (step %.3f).",
                         (double)median, (double)(1000.0f / median),
                         (double)(((median / governor.lower_above_ms) - 1.0f) * 100.0f),
                         (double)governor.lower_above_ms,
                         (double)previous, (double)governor.ceiling, (double)step);
            }
            break;

        case FRAME_GOVERNOR_RAISE:
            governor.healthy_seconds = 0;
            if (governor.ceiling < configured_scale) {
                const float previous = governor.ceiling;

                governor.ceiling += GOVERNOR_SCALE_STEP;
                if (governor.ceiling > configured_scale) {
                    governor.ceiling = configured_scale;
                }
                log_info("frame governor: %u s at %.1f ms a frame (%.0f fps), so the view distance "
                         "is given a step back. View scale %.2f -> %.2f, ceiling %.2f.",
                         governor.healthy_needed * GOVERNOR_DECISION_MS / 1000u, (double)median, (double)(1000.0f / median),
                         (double)previous, (double)governor.ceiling, (double)configured_scale);
                governor.healthy_needed = GOVERNOR_DECISIONS_PER(GOVERNOR_HEALTHY_SECONDS_AGAIN);
            }
            break;

        case FRAME_GOVERNOR_HOLD:
        default:
            /* Healthy seconds only accumulate while there is something to give back. Counting
             * them at the configured scale would mean the first slow second after a long quiet
             * stretch was answered by an immediate raise. */
            if (median < governor.raise_below_ms && governor.ceiling < configured_scale) {
                governor.healthy_seconds++;
            } else if (median >= governor.raise_below_ms) {
                governor.healthy_seconds = 0;
            }
            break;
        }
    }

    apply(effective_view_scale, configured_scale, cell_ceiling);
}

/* ============================================================================================ */
void frame_governor_reset(float configured_scale)
{
    governor.ceiling = configured_scale;
    governor.healthy_seconds = 0;
    governor.healthy_needed = GOVERNOR_DECISIONS_PER(GOVERNOR_HEALTHY_SECONDS);
    governor.sample_count = 0;
    governor.cursor = 0;
    governor.last_frame_at = 0;
    governor.second_began_at = 0;
}

void frame_governor_configure(bool enabled, float backoff_fps, float configured_scale)
{
    float target_fps = backoff_fps;

    governor.enabled = enabled;
    if (!QueryPerformanceFrequency(&governor.frequency)) {
        governor.frequency.QuadPart = 0;
    }
    frame_governor_reset(configured_scale);

    if (!enabled) {
        log_info("frame governor: OFF (FrameBackoff=0). A view distance that costs more than the "
                 "frame rate can afford will stay where it is set.");
        return;
    }
    if (governor.frequency.QuadPart <= 0) {
        governor.enabled = false;
        log_warning("frame governor: no performance counter, so frame time cannot be measured and "
                    "the governor is off. The view distance stays where it is set.");
        return;
    }

    if (!(target_fps > 0.0f)) {
        /* framerate_fix owns the cap, and its own section is where the number lives. Reading
         * across sections is not new here: [diagnostics] Spawns is read by this same DLL, for the
         * same reason: the setting belongs where its subject is, not where its reader is. */
        const float cap = ini_read_float("framerate_fix", "TargetFps", 0.0f);

        target_fps = (cap > 0.0f) ? (cap * GOVERNOR_CAP_FRACTION) : GOVERNOR_UNCAPPED_BACKOFF_FPS;
        log_info("frame governor: BackoffFps is automatic -> %.0f fps (%s).", (double)target_fps,
                 (cap > 0.0f) ? "three quarters of framerate_fix's TargetFps"
                              : "no frame cap set, so the uncapped default");
    }

    governor.lower_above_ms = 1000.0f / target_fps;
    governor.raise_below_ms = 1000.0f / (target_fps * GOVERNOR_RECOVERY_MARGIN);

    log_info("frame governor active: the view distance backs off below %.0f fps (%.1f ms a frame) "
             "and is given a step back after %u s above %.0f fps (%.1f ms), then every %u s. Steps "
             "of about %.2f, sized by how far off target the second was, never below %.2f and "
             "never above the configured %.2f. It measures the MEDIAN frame of each second, so a "
             "level load cannot move it.",
             (double)target_fps, (double)governor.lower_above_ms,
             GOVERNOR_HEALTHY_SECONDS, (double)(target_fps * GOVERNOR_RECOVERY_MARGIN),
             (double)governor.raise_below_ms, GOVERNOR_HEALTHY_SECONDS_AGAIN,
             (double)GOVERNOR_SCALE_STEP, (double)GOVERNOR_SCALE_FLOOR, (double)configured_scale);
}
