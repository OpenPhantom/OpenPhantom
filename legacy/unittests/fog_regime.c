/* fog_regime.c: the fog band as a pure function of the picture.
 *
 * The eleven tables below are the SHIPPED numbers, read out of the level files at the header
 * offsets the loader uses (fog start hdr+0x90, fog end hdr+0x94, draw distance hdr+0x854). They
 * are what "per level, exact" has to be checked against: a coupling that is only correct on a
 * made-up band is not correct.
 *
 * The acceptance criterion is the first group: at the field of view the levels were authored at,
 * with the cut edge where the engine put it, the coupling must return the authored floats
 * BIT-EXACTLY, not close, exactly.
 */
#include "unittest.h"

#include "fog_regime.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* The authored numbers of the eleven shipped levels. */
typedef struct level_case {
    const char *name;
    int32_t     draw_distance;
    float       fog_start;
    float       fog_end;
} level_case_t;

static const level_case_t levels[] = {
    { "ASSAULT", 23, 12.0f, 38.0f },
    { "BIGCITY", 20, 20.0f, 50.0f },
    { "ESPA",    22,  8.0f, 32.0f },
    { "FEDSHIP", 18, 16.0f, 56.0f },
    { "FINAL",   24, 12.0f, 30.0f },
    { "GARDEN",  22,  4.0f, 26.0f },
    { "GUNGA",   16,  6.0f, 14.0f },
    { "MAUL",    28,  8.0f, 32.0f },
    { "QUEEN",   26, 12.0f, 38.0f },
    { "RACE",    22, 10.0f, 32.0f },
    { "SWAMP",   22,  6.0f, 30.0f }
};
#define LEVEL_COUNT (sizeof levels / sizeof levels[0])

#define AUTHORED_FOV 60.0f
#define WIDE_FOV     87.178f

static fog_regime_config_t default_config(void)
{
    fog_regime_config_t config;

    config.vertex_fog     = true;
    config.follow_fov     = true;
    config.inside_cut     = true;
    config.fog_scale      = 1.0f;
    config.settle_seconds = 1.5f;
    return config;
}

/* ============================================================================================
 * THE ACCEPTANCE CRITERION
 * ============================================================================================ */
static void test_identity_at_the_authored_field_of_view(void)
{
    fog_regime_config_t config = default_config();
    size_t index;

    /* The coupling on its own: the cap is a separate switch and is tested separately. */
    config.inside_cut = false;

    for (index = 0; index < LEVEL_COUNT; ++index) {
        const level_case_t *level = &levels[index];
        float cut = (float)level->draw_distance;
        fog_regime_band_t authored;
        fog_regime_band_t out;
        char label[96];

        authored.start = level->fog_start;
        authored.end   = level->fog_end;

        fog_regime_target_band(&config, &authored, AUTHORED_FOV, cut, cut, &out);

        _snprintf(label, sizeof label,
                  "%s keeps its authored band %.1f..%.1f bit-exactly at 60 degrees",
                  level->name, (double)authored.start, (double)authored.end);
        label[sizeof label - 1] = '\0';
        ut_check(out.start == authored.start && out.end == authored.end, label);
    }
}

static void test_the_follow_factor_is_exactly_one(void)
{
    /* Not "about one". The factor multiplies the authored end, so anything other than an exact
     * 1.0f would move the shipped numbers by a hair at the shipped settings. */
    ut_check(fog_regime_follow_factor(AUTHORED_FOV, 16.0f, 16.0f) == 1.0f,
          "60 degrees and an unmoved cut edge give exactly 1.0f");
    ut_check(fog_regime_follow_factor(AUTHORED_FOV, 64.0f, 64.0f) == 1.0f,
          "the same at the largest cut edge the engine allows");
    ut_check(fog_regime_follow_factor(30.0f, 22.0f, 22.0f) == 1.0f,
          "a NARROWER picture never pushes the fog further out than the level authored");

    /* The projection is rebuilt from the canvas, so the observed angle can land a fraction off the
     * authored one. Inside the dead band the answer has to stay exactly 1.0f. */
    ut_check(fog_regime_follow_factor(60.025f, 22.0f, 22.0f) == 1.0f,
          "60.025 degrees, what a 639x479 canvas produces, is still exactly the authored band");
    ut_check(fog_regime_follow_factor(61.0f, 22.0f, 22.0f) == 1.0f,
          "and so is the far edge of the dead band");
    ut_check(fog_regime_follow_factor(62.0f, 22.0f, 22.0f) < 1.0f,
          "one degree past it the fog starts moving");

    /* The dead band is on the ANGLE only. A cut edge that has been shortened still counts, even at
     * the authored field of view; that is the watchdog case. */
    ut_check(fog_regime_follow_factor(AUTHORED_FOV, 22.0f, 15.0f) < 1.0f,
          "a shortened cut edge moves the fog even at the authored field of view");
}

/* ============================================================================================
 * THE GEOMETRY
 * ============================================================================================ */
static void test_the_edge_limit_is_the_screen_corner(void)
{
    /* (cut - half a cell diagonal) * cos(hFOV/2): the view depth of a cell centre sitting on the
     * cut circle at the left or right edge of the screen. */
    const float margin = 0.70710678f;

    ut_near(fog_regime_edge_limit(60.0f, 20.0f), (20.0f - margin) * 0.8660254f, 0.001f,
               "60 degrees, cut 20");
    ut_near(fog_regime_edge_limit(90.0f, 20.0f), (20.0f - margin) * 0.70710678f, 0.001f,
               "90 degrees, cut 20");
    ut_near(fog_regime_edge_limit(WIDE_FOV, 20.0f), (20.0f - margin) * 0.72434f, 0.001f,
               "87.178 degrees, cut 20");

    ut_check(fog_regime_edge_limit(60.0f, 0.5f) == 0.0f,
          "a cut edge inside the cell margin has no meaningful limit and reports zero");
    ut_check(fog_regime_edge_limit(60.0f, -5.0f) == 0.0f,
          "a negative cut edge reports zero rather than a negative distance");
}

static void test_wider_always_means_nearer(void)
{
    float previous = fog_regime_edge_limit(20.0f, 24.0f);
    float degrees;

    for (degrees = 21.0f; degrees < 179.0f; degrees += 1.0f) {
        float now = fog_regime_edge_limit(degrees, 24.0f);

        if (now > previous) {
            ut_checkf(0, "the limit grew between %.0f and %.0f degrees",
                   (double)(degrees - 1.0f), (double)degrees);
            return;
        }
        previous = now;
    }
    ut_check(1, "the limit never grows as the field of view opens, 20..179 degrees");
}

static void test_every_level_moves_when_the_picture_widens(void)
{
    fog_regime_config_t config = default_config();
    size_t index;

    for (index = 0; index < LEVEL_COUNT; ++index) {
        const level_case_t *level = &levels[index];
        float cut = (float)level->draw_distance;
        fog_regime_band_t authored;
        fog_regime_band_t narrow;
        fog_regime_band_t wide;
        char label[96];

        authored.start = level->fog_start;
        authored.end   = level->fog_end;

        fog_regime_target_band(&config, &authored, AUTHORED_FOV, cut, cut, &narrow);
        fog_regime_target_band(&config, &authored, WIDE_FOV, cut, cut, &wide);

        _snprintf(label, sizeof label, "%s: %.1f at 60 degrees -> %.1f at 87",
                  level->name, (double)narrow.end, (double)wide.end);
        label[sizeof label - 1] = '\0';
        ut_check(wide.end < narrow.end && wide.start < narrow.start, label);
    }
}

/* ============================================================================================
 * THE TRAP: a band whose end falls below the level's own start disarms the engine's ramp, and a
 * disarmed ramp paints the world in the fog colour. BIGCITY (start 20, draw distance 20) and
 * FEDSHIP (start 16, draw distance 18) are the two shipped levels that reach it.
 * ============================================================================================ */
static void test_the_span_never_inverts(void)
{
    fog_regime_config_t config = default_config();
    size_t index;
    float  degrees;

    for (index = 0; index < LEVEL_COUNT; ++index) {
        const level_case_t *level = &levels[index];
        fog_regime_band_t authored;
        bool ok = true;

        authored.start = level->fog_start;
        authored.end   = level->fog_end;

        for (degrees = 30.0f; degrees < 170.0f; degrees += 0.5f) {
            float cut;

            for (cut = 2.0f; cut <= 64.0f; cut += 1.0f) {
                fog_regime_band_t out;

                fog_regime_target_band(&config, &authored, degrees,
                                       (float)level->draw_distance, cut, &out);
                if (!(out.end > out.start)) {
                    ok = false;
                }
            }
        }
        ut_checkf(ok, "%s keeps a positive span over 30..170 degrees and every cut edge 2..64",
                  level->name);
    }
}

static void test_the_authored_profile_is_preserved(void)
{
    /* The whole band is multiplied by one ratio, so start/end is the level's own number whatever
     * the picture does. BIGCITY has the most extreme ratio of the eleven: 20/50. */
    fog_regime_config_t config = default_config();
    fog_regime_band_t authored;
    fog_regime_band_t out;

    authored.start = 20.0f;
    authored.end   = 50.0f;

    fog_regime_target_band(&config, &authored, WIDE_FOV, 20.0f, 20.0f, &out);
    ut_near(out.start / out.end, 0.4f, 0.0001f,
               "BIGCITY keeps its authored 20:50 shape after the band is pulled in");

    fog_regime_target_band(&config, &authored, 120.0f, 20.0f, 8.0f, &out);
    ut_near(out.start / out.end, 0.4f, 0.0001f,
               "and it still keeps it at 120 degrees with the cut edge cut to 8");
}

static void test_a_level_without_fog_is_left_alone(void)
{
    fog_regime_config_t config = default_config();
    fog_regime_band_t authored;
    fog_regime_band_t out;

    authored.start = 0.0f;
    authored.end   = 0.0f;
    fog_regime_target_band(&config, &authored, WIDE_FOV, 24.0f, 12.0f, &out);
    ut_check(out.start == 0.0f && out.end == 0.0f,
          "an empty band stays empty, a level with no fog must not gain any");

    /* An already inverted band is the engine's own "no vertex fog" state. Repairing it here would
     * turn a level the author switched off into a fogged one. */
    authored.start = 30.0f;
    authored.end   = 10.0f;
    fog_regime_target_band(&config, &authored, WIDE_FOV, 24.0f, 12.0f, &out);
    ut_check(out.start == 30.0f && out.end == 10.0f,
          "an inverted band is handed back untouched rather than repaired");
}

/* ============================================================================================
 * IDEMPOTENCY, the coupling is a pure function of the REMEMBERED authored band
 * ============================================================================================ */
static void test_repeating_the_call_changes_nothing(void)
{
    fog_regime_config_t config = default_config();
    fog_regime_band_t authored;
    fog_regime_band_t first;
    fog_regime_band_t again;
    int repeat;

    authored.start = 12.0f;
    authored.end   = 38.0f;

    fog_regime_target_band(&config, &authored, WIDE_FOV, 23.0f, 23.0f, &first);
    for (repeat = 0; repeat < 8; ++repeat) {
        fog_regime_target_band(&config, &authored, WIDE_FOV, 23.0f, 23.0f, &again);
    }
    ut_check(again.start == first.start && again.end == first.end,
          "eight repeats of the same inputs give bit-identical output");

    /* The failure this guards against: feeding the previous OUTPUT back in as the authored band,
     * which is what a coupling that reads the live field would do on the second apply. */
    fog_regime_target_band(&config, &first, WIDE_FOV, 23.0f, 23.0f, &again);
    ut_check(again.end < first.end,
          "feeding the output back in DOES shrink it, which is why the module keeps the "
          "authored band and never re-reads the field");
}

/* ============================================================================================
 * THE EASING
 * ============================================================================================ */
static void test_easing_is_frame_rate_independent(void)
{
    float slow = 30.0f;
    float fast = 30.0f;
    int   step;

    /* One second of real time, cut two ways. */
    for (step = 0; step < 30; ++step) {
        slow = fog_regime_ease(slow, 15.0f, 1.0f / 30.0f, 1.0f);
    }
    for (step = 0; step < 120; ++step) {
        fast = fog_regime_ease(fast, 15.0f, 1.0f / 120.0f, 1.0f);
    }

    ut_near(slow, fast, 0.02f, "30 fps and 120 fps agree after one second");
    ut_near(slow, 15.0f + 0.1f * 15.0f, 0.05f,
               "one settle time leaves a tenth of the gap");
}

static void test_easing_never_overshoots_and_always_arrives(void)
{
    float value = 40.0f;
    int   step;

    for (step = 0; step < 600; ++step) {
        value = fog_regime_ease(value, 12.0f, 1.0f / 60.0f, 1.5f);
        if (value < 12.0f) {
            ut_checkf(0, "the ease undershot its target at step %d (%.4f)", step, (double)value);
            return;
        }
    }
    ut_check(value == 12.0f, "ten seconds of easing lands exactly on the target and stops there");

    value = 5.0f;
    for (step = 0; step < 600; ++step) {
        value = fog_regime_ease(value, 20.0f, 1.0f / 60.0f, 1.5f);
        if (value > 20.0f) {
            ut_checkf(0, "the ease overshot upwards at step %d (%.4f)", step, (double)value);
            return;
        }
    }
    ut_check(value == 20.0f, "and the same going the other way");
}

static void test_easing_degenerate_input(void)
{
    ut_check(fog_regime_ease(10.0f, 20.0f, 0.0f, 1.5f) == 10.0f,
          "a zero frame delta moves nothing");
    ut_check(fog_regime_ease(10.0f, 20.0f, -1.0f, 1.5f) == 10.0f,
          "a negative frame delta moves nothing");
    ut_check(fog_regime_ease(10.0f, 20.0f, 1.0f / 60.0f, 0.0f) == 20.0f,
          "settle 0 is the instant step, which is what the setting promises");

    /* A three-second hitch must not sweep the fog across the level in one frame: the delta is
     * clamped, so the step is the one an eighth of a second would have made. */
    ut_near(fog_regime_ease(10.0f, 20.0f, 3.0f, 1.5f),
               fog_regime_ease(10.0f, 20.0f, 0.125f, 1.5f), 0.0001f,
               "a loading hitch is clamped to the largest delta worth believing");
}

/* ============================================================================================
 * The cut edge moving on its own, the watchdog lowering the range, not the field of view
 * ============================================================================================ */
static void test_the_fog_follows_a_shortened_cut_edge(void)
{
    fog_regime_config_t config = default_config();
    fog_regime_band_t authored;
    fog_regime_band_t full;
    fog_regime_band_t braked;

    config.inside_cut = false;                 /* the follow factor alone */

    authored.start = 6.0f;
    authored.end   = 14.0f;                    /* GUNGA, the one level whose fog fits its cut */

    fog_regime_target_band(&config, &authored, AUTHORED_FOV, 16.0f, 16.0f, &full);
    fog_regime_target_band(&config, &authored, AUTHORED_FOV, 16.0f, 11.0f, &braked);

    ut_check(full.end == 14.0f, "at the full cut edge the authored end survives exactly");
    ut_check(braked.end < full.end,
          "a cut edge the watchdog has pulled in to 11 brings the fog in with it");
    ut_near(braked.end / full.end, (11.0f - 0.70710678f) / (16.0f - 0.70710678f), 0.001f,
               "and it does so in the ratio of the two cut edges");
}

static void test_the_floor(void)
{
    fog_regime_config_t config = default_config();
    fog_regime_band_t authored;
    fog_regime_band_t out;

    config.inside_cut = false;

    authored.start = 6.0f;
    authored.end   = 14.0f;

    /* An absurd cut edge must not put the fog on the camera lens. */
    fog_regime_target_band(&config, &authored, 175.0f, 64.0f, 2.0f, &out);
    ut_check(out.end >= 14.0f * 0.25f - 0.001f,
          "the follow factor bottoms out rather than collapsing the band to nothing");
}

int main(void)
{
    test_identity_at_the_authored_field_of_view();
    test_the_follow_factor_is_exactly_one();
    test_the_edge_limit_is_the_screen_corner();
    test_wider_always_means_nearer();
    test_every_level_moves_when_the_picture_widens();
    test_the_span_never_inverts();
    test_the_authored_profile_is_preserved();
    test_a_level_without_fog_is_left_alone();
    test_repeating_the_call_changes_nothing();
    test_easing_is_frame_rate_independent();
    test_easing_never_overshoots_and_always_arrives();
    test_easing_degenerate_input();
    test_the_fog_follows_a_shortened_cut_edge();
    test_the_floor();

    return ut_summary("fog_regime");
}
