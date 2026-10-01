/* rate_independence.c: whether a carried character is drawn smoothly at ANY frame rate.
 *
 * The game caps nothing by default and a player may set any rate they like, so "it looked right
 * at 60" is not a property, it is one sample. What follows drives the same arithmetic the rider
 * blend uses over a whole run of frames at nine different rates and checks two claims that
 * together are what smooth means:
 *
 *   the drawn position is exactly one simulation step behind the simulation, on every frame;
 *   and consecutive frames advance it by equal amounts, so nothing on screen stutters.
 *
 * The first allows the second. The blend can only put an object between two
 * positions it really held, so the drawn moment has to trail the simulation by one step; what
 * matters is that the trail is CONSTANT, because a lag that varies from frame to frame is
 * precisely what judder is.
 *
 * The rates are not decoration. 32 is the simulation's own rate, where the alpha barely moves and
 * the first version of this counted no steps at all. 20 and 30 are below it, where one frame
 * spans several steps, which is the case the weight exists for. 64, 100, 144 and 240 are ordinary
 * modern rates, and 51.7 is there because none of the others is an awkward ratio, and a scheme
 * that only worked on neat divisors of 32 would pass every one of them.
 *
 * A real display does not deliver frames evenly either, so the last section jitters the frame
 * times and asks for the lag invariant alone. The per-frame advance genuinely does vary there,
 * because the frames do.
 */
#include "unittest.h"

#include "object_track.h"

#include <math.h>
#include <stdint.h>

#define KEY    0x30000000u
#define SIM_HZ 32.0

/* Measured in play: a platform carried the player this far in one simulation step. The real
 * number rather than a round one, so the tolerances below stay honest about float precision at
 * the magnitudes this actually runs at. */
#define STEP_UNITS 0.045

/* RiderTravelLimitPerStep as it ships. Nothing here should ever reach it, and a run that does has
 * found a blend refusing an ordinary walk. */
#define TRAVEL_LIMIT 2.0f

/* Long enough for the frame and step patterns to repeat many times over, and short enough that
 * the positions stay small: float carries about seven digits, and a test of differences this fine
 * has no business asking for them at the far end of the mantissa. */
#define RUN_STEPS 96.0

typedef struct run_result {
    int    frames_drawn;
    double worst_lag_error;      /* how far the drawn position is from one step behind */
    double worst_advance_error;  /* how uneven consecutive frames are */
} run_result_t;

/* One frame of the rider blend, exactly as the draw hook performs it: the step count from the
 * engine, the alpha from the frame, the weight from both, and the limit scaled by the same gap.
 * False while the tracker has nothing to answer with, which in the game is where the engine's own
 * pair is used instead. */
static bool draw_frame(double sim_steps, double *out_drawn)
{
    uint32_t stamp = (uint32_t)sim_steps;
    float    alpha = (float)(sim_steps - (double)stamp);
    float    position[3];
    float    previous[3];
    float    drawn[3];
    uint32_t gap = 1u;

    position[0] = (float)((double)stamp * STEP_UNITS);
    position[1] = 0.0f;
    position[2] = 0.0f;

    if (!object_track_sample(KEY, stamp, position, previous, &gap)) {
        return false;
    }
    object_track_blend(previous, position, object_track_weight(alpha, gap),
                       TRAVEL_LIMIT * (float)gap, drawn);
    *out_drawn = (double)drawn[0];
    return true;
}

static run_result_t run_at_rate(double fps)
{
    run_result_t result = { 0, 0.0, 0.0 };
    double       per_frame = STEP_UNITS * SIM_HZ / fps;
    double       previous_drawn = 0.0;
    bool         have_previous = false;
    int          frames = (int)(RUN_STEPS * fps / SIM_HZ);
    int          n;

    object_track_reset();

    for (n = 0; n < frames; ++n) {
        double sim_steps = (double)n * SIM_HZ / fps;
        double drawn;
        double error;

        object_track_frame();
        if (!draw_frame(sim_steps, &drawn)) {
            have_previous = false;     /* the run of consecutive drawn frames starts again */
            continue;
        }
        ++result.frames_drawn;

        error = fabs(drawn - (sim_steps - 1.0) * STEP_UNITS);
        if (error > result.worst_lag_error) {
            result.worst_lag_error = error;
        }

        if (have_previous) {
            error = fabs((drawn - previous_drawn) - per_frame);
            if (error > result.worst_advance_error) {
                result.worst_advance_error = error;
            }
        }
        previous_drawn = drawn;
        have_previous  = true;
    }
    return result;
}

/* The opening frames of a run are a first sighting or a step the tracker has not been shown twice
 * yet, so a run that drew almost nothing would pass both checks by never testing them. */
#define MIN_DRAWN_FRACTION 0.9

/* Float, and the positions run out to four units, so the last digit is worth about half a
 * millionth. Asking for better than this would be a test of the compiler's rounding. */
#define TOLERANCE 2.0e-5

int main(void)
{
    static const double RATES[] = { 20.0, 30.0, 32.0, 51.7, 60.0, 64.0, 100.0, 144.0, 240.0 };
    size_t              index;
    uint32_t            seed = 0x13579BDFu;
    double              worst_jittered_lag = 0.0;
    double              sim_steps = 0.0;
    int                 n;

    ut_section("the drawn moment trails the simulation by one step, whatever the rate");

    for (index = 0; index < sizeof RATES / sizeof RATES[0]; ++index) {
        double       fps = RATES[index];
        run_result_t run = run_at_rate(fps);
        int          expected_frames = (int)(RUN_STEPS * fps / SIM_HZ);

        ut_checkf(run.frames_drawn > (int)(MIN_DRAWN_FRACTION * (double)expected_frames),
                  "%.1f fps: %d of %d frames were drawn from the remembered position",
                  fps, run.frames_drawn, expected_frames);
        ut_checkf(run.worst_lag_error < TOLERANCE,
                  "%.1f fps: the drawn position is one step behind on every frame, worst error "
                  "%.2e units", fps, run.worst_lag_error);
        ut_checkf(run.worst_advance_error < TOLERANCE,
                  "%.1f fps: consecutive frames advance it equally, worst error %.2e units",
                  fps, run.worst_advance_error);
    }

    ut_section("a display that does not deliver frames evenly");

    /* Vsync at 60 was measured swinging between roughly 15.5 and 17.6 ms on the test rig, so the
     * even lattice above is the friendly case. Under jitter the per-frame advance is SUPPOSED to
     * vary, because the frames do; the lag is what must not. */
    object_track_reset();
    for (n = 0; n < 2000; ++n) {
        double drawn;
        double error;
        double delta;

        seed = seed * 1664525u + 1013904223u;              /* a plain LCG, so the run repeats */
        delta = (1.0 / 60.0) * (0.85 + 0.30 * (double)(seed >> 16) / 65535.0);

        object_track_frame();
        if (draw_frame(sim_steps, &drawn)) {
            error = fabs(drawn - (sim_steps - 1.0) * STEP_UNITS);
            if (error > worst_jittered_lag) {
                worst_jittered_lag = error;
            }
        }
        sim_steps += delta * SIM_HZ;
        if (sim_steps > RUN_STEPS) {
            sim_steps = 0.0;
            object_track_reset();      /* keeps the positions inside the precision asked for */
        }
    }
    ut_checkf(worst_jittered_lag < TOLERANCE,
              "an uneven 60 fps still draws one step behind, worst error %.2e units",
              worst_jittered_lag);

    ut_section("a rate the simulation outruns");

    /* Below 32 frames a second nothing can be genuinely smooth, because the simulation steps
     * faster than the display can show the result and no amount of blending invents a frame that
     * was never drawn. What this owes at those rates is to do no harm, and the 20 and
     * 30 fps rows above checked. Worth stating, because the first version of the weight covered
     * several steps of travel in one step's worth of alpha and drew the object running ahead of
     * itself and dropping back, once every frame. */
    ut_near(object_track_weight(0.0f, 2u), 0.5f, 0.0001,
            "two steps in one frame asks for the moment halfway between the samples, not the "
            "start of the later one");

    return ut_summary("rate independence");
}
