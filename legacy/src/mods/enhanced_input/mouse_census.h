/* mouse_census.h: the one number that decides whether the mouse look worked.
 *
 * Taken out of mouse_look.c along the seam its SIZE NOTE named: the census touches the
 * engine at no point and shares nothing with the rest but the two calls below.
 *
 * The camera draws the player heading by interpolating between the previous substep's value and the
 * current one, sweeping the whole way across exactly one substep of game time. So the rendered
 * angular velocity inside a substep is the increment times 32, and it is constant. A constant
 * increment therefore draws a straight line and a varying one draws a polyline whose slope changes
 * 32 times a second. That is the entire difference between the mouse and a held direction
 * key, whose axis is pinned at exactly 1.0 and whose increment cannot vary.
 *
 * Smoothness here is therefore not a feeling; it is the spread of the per-substep increment, and
 * nothing in this tree had ever measured it. It is measured over gameplay rather than from
 * installation, and that gate was paid for: the first version of this instrument opened its window
 * at process start and printed fields that a reset clears, and the reset runs once per rendered
 * frame in every state where nobody consumes, a menu being exactly such a state. It reported a
 * measured rate of zero over a consumed interval of 0.00 ms from a state that is parked by
 * construction, and a whole handoff was written on the strength of reading that as a broken chain.
 *
 * What is measured: the second difference of the delivered rate, along a chain of consecutive
 * substeps, normalised by the mean speed, reported as a MEDIAN. It is the change of slope between
 * two consecutive ramps that the eye reads as a kink, so a steady sweep measures near zero and so
 * does a smooth reversal, while an alternation of plus and minus p about the true rate measures 4p
 * and that number divided by four is the size of the wobble it implies.
 *
 * Two metrics before this one measured the wrong thing, and both were caught in the field on the
 * same evening.
 *
 * The first was the spread about the mean, the coefficient of variation of the delivered rate. The
 * rate is signed, and a player asked to sweep steadily sweeps one way and then the other, so the
 * mean runs through zero and the statistic ends up measuring how symmetric the sweep was. The first
 * field run came back at 327.1 per cent, mean -49 deg/s, -532 to 327, from a session the player
 * called fine. Nothing about the data was wrong; the question was.
 *
 * The second was the MEAN of the second difference. The quantity is right and the average is not: a
 * hard reversal of the hand is a genuine and enormous change of slope, and one of them among a
 * hundred samples carries several percentage points. Two field runs then produced 22.1 per cent at
 * a mean speed of 111 deg/s and 32.6 per cent at 33 deg/s, and the second was the one the player
 * preferred. They are not comparable: they differ in hand speed by a factor of three and in how
 * often the hand turned around.
 *
 * Hence the median, with the mean printed beside it, because the gap between the two is itself the
 * measurement of how much a few large events dominate.
 *
 * And what the normalisation means for reading the number. Roughness is divided by the mean speed,
 * so it is dimensionless, and the residual it is measuring is a fixed number of COUNTS per substep,
 * so the same residual reads larger at a slower sweep. At 0.100 degrees per count a sweep at
 * 33 deg/s carries about ten counts into each substep and one at 111 deg/s carries thirty-five. Two
 * runs are therefore only comparable at the same hand speed, so the comparison that settles
 * anything is the same sweep with one ini key changed rather than two sessions side by
 * side. MouseAccumulate=0 is that key: the census runs on the degraded path too, and that path is
 * the engine's own behaviour, one live read per substep with nothing banked. Same hand, two
 * settings, two numbers. */
#ifndef MOUSE_CENSUS_H
#define MOUSE_CENSUS_H

#include <stdbool.h>
#include <stdint.h>

#define CENSUS_MIN_STEPS       128      /* four seconds of substeps */
#define CENSUS_MIN_DEGREES     60.0f    /* enough turn that the roughness means something */
#define CENSUS_MAX_SECONDS     30.0f    /* give up and report what there is */
#define CENSUS_MAX_SAMPLES     256      /* kept sorted, so the median is free at the end */

typedef struct mouse_census {
    bool     open;
    bool     reported;
    uint32_t steps;                 /* substeps that carried movement */
    double   speed_sum;             /* the delivered rate, unsigned, summed */
    double   rough_sum;             /* the second difference, unsigned, summed */
    uint32_t rough_steps;
    float    rough[CENSUS_MAX_SAMPLES];  /* the same values, kept in ascending order */
    uint32_t rough_kept;
    float    rate_min;
    float    rate_max;
    double   degrees;               /* total turn delivered, for the gate */
    float    seconds;
    uint32_t frames;
    unsigned packet_baseline;

    /* The chain the second difference is taken along. A substep that delivered nothing, or a gap in
     * the substep numbering, breaks it: a second difference across a hole is not a slope change, it
     * is two unrelated movements subtracted from each other. */
    uint32_t substep_index;
    uint32_t last_index;
    uint32_t chain;
    float    prev_rate;
    float    prev2_rate;
} mouse_census_t;

/* One rendered frame: time passes for an open census. */
void mouse_census_frame(mouse_census_t *census, float seconds);

/* One simulation step and the degrees delivered into it. `raw_source` says whether the samples
 * come from raw input, so the report rate means something; `smoothing_seconds` is the filter's
 * time constant at that moment, printed with the report. Opens the census on the first call,
 * reports once when there is enough movement in it to mean something, and is inert after that. */
void mouse_census_step(mouse_census_t *census, float degrees, float dt_seconds, bool raw_source,
                       float smoothing_seconds);

#endif /* MOUSE_CENSUS_H */
