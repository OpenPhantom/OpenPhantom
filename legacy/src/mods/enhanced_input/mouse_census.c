/* mouse_census.c: see mouse_census.h. */
#include "mouse_census.h"

#include "raw_mouse.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

#define MILLISECONDS_PER_SECOND 1000.0f

void mouse_census_frame(mouse_census_t *census, float seconds)
{
    if (!census->open || census->reported) {
        return;
    }
    census->seconds += (seconds > 0.0f) ? seconds : 0.0f;
    ++census->frames;
}

static void census_report(mouse_census_t *census, bool raw_source, float smoothing_seconds)
{
    double         speed;
    double         roughness = 0.0;
    double         median = 0.0;
    unsigned       packets;
    float          report_hz = 0.0f;

    census->reported = true;

    speed = census->speed_sum / (double)census->steps;
    if (census->rough_steps > 0u && speed > 0.0) {
        roughness = 100.0 * (census->rough_sum / (double)census->rough_steps) / speed;
    }
    if (census->rough_kept > 0u && speed > 0.0) {
        median = 100.0 * (double)census->rough[census->rough_kept / 2u] / speed;
    }

    packets = raw_mouse_packet_count() - census->packet_baseline;
    if (raw_source && census->seconds > 0.0f) {
        report_hz = (float)packets / census->seconds;
    }

    /* The roughness first, because it is the answer. Everything after it is the input to that
     * answer, and it is printed on the same line so that a field log needs no second run. */
    log_info("view turn measured over %u simulation steps in %.1f s of play: roughness MEDIAN %.1f "
             "percent, mean %.1f, over %u slope changes. Mean speed %.0f deg/s, %.0f to %.0f. The "
             "camera draws each step as a straight ramp, so what the eye reads as a kink is the "
             "change of slope between two of them, and the median divided by four is the wobble "
             "that implies. Read the MEDIAN: a mean far above it means a few large events "
             "dominate, such as a hard reversal of the hand.",
             census->steps, (double)census->seconds, median, roughness, census->rough_steps,
             speed, (double)census->rate_min, (double)census->rate_max);

    if (report_hz > 0.0f) {
        log_info("the mouse reported at about %.0f Hz during that window (%u packets over %u "
                 "frames, %.1f per simulation step), and the reconstruction smoothed over %.0f ms. "
                 "A faster device is given less smoothing and a slower one more, up to the "
                 "MouseSmoothMaxMs ceiling.",
                 (double)report_hz, packets, census->frames, (double)(report_hz / 32.0f),
                 (double)(smoothing_seconds * MILLISECONDS_PER_SECOND));
    } else if (raw_source) {
        log_info("no raw packet arrived during the measurement window, so the report rate is not "
                 "known and the numbers above came from the engine's own reader");
    }
}

void mouse_census_step(mouse_census_t *census, float degrees, float dt_seconds, bool raw_source,
                       float smoothing_seconds)
{
    float rate;

    if (census->reported || !(dt_seconds > 0.0f)) {
        return;
    }
    if (!census->open) {
        census->open            = true;
        census->rate_min        = 0.0f;
        census->rate_max        = 0.0f;
        census->packet_baseline = raw_mouse_packet_count();
    }

    ++census->substep_index;
    if (degrees == 0.0f) {
        census->chain = 0u;             /* a still hand measures the still hand */
        return;
    }

    rate = degrees / dt_seconds;
    if (census->steps == 0 || rate < census->rate_min) {
        census->rate_min = rate;
    }
    if (census->steps == 0 || rate > census->rate_max) {
        census->rate_max = rate;
    }
    ++census->steps;
    census->speed_sum += (rate < 0.0f) ? -(double)rate : (double)rate;
    census->degrees   += (degrees < 0.0f) ? -(double)degrees : (double)degrees;

    if (census->chain > 0u && census->last_index + 1u != census->substep_index) {
        census->chain = 0u;             /* a gap: the two are not neighbours */
    }
    if (census->chain >= 2u) {
        float second = rate - 2.0f * census->prev_rate + census->prev2_rate;

        if (second < 0.0f) {
            second = -second;
        }
        census->rough_sum += (double)second;
        ++census->rough_steps;

        /* Inserted in order rather than sorted at the end, which costs a few dozen moves per sample
         * and nothing at all where it would be noticed. Once the array is full the SMALLEST value
         * is dropped, which biases the median upward and therefore cannot flatter the result. */
        if (census->rough_kept < CENSUS_MAX_SAMPLES) {
            uint32_t slot = census->rough_kept;

            ++census->rough_kept;
            while (slot > 0u && census->rough[slot - 1u] > second) {
                census->rough[slot] = census->rough[slot - 1u];
                --slot;
            }
            census->rough[slot] = second;
        } else if (second > census->rough[0]) {
            uint32_t slot = 0u;

            while (slot + 1u < CENSUS_MAX_SAMPLES && census->rough[slot + 1u] < second) {
                census->rough[slot] = census->rough[slot + 1u];
                ++slot;
            }
            census->rough[slot] = second;
        }
    }
    census->prev2_rate = census->prev_rate;
    census->prev_rate  = rate;
    census->last_index = census->substep_index;
    if (census->chain < 2u) {
        ++census->chain;
    }

    if ((census->steps >= CENSUS_MIN_STEPS && census->degrees >= (double)CENSUS_MIN_DEGREES) ||
        census->seconds >= CENSUS_MAX_SECONDS) {
        census_report(census, raw_source, smoothing_seconds);
    }
}
