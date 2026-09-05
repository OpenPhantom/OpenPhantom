/* fog_band.c: the fog band arithmetic, and nothing that touches the engine.
 *
 * Split out of fog_regime.c, which had grown past the hard limit while carrying four jobs its own
 * section banners already named: this arithmetic, the live cut edge and camera, the level record,
 * and the install pass. This is the first of those, and it is the cut worth taking first because
 * it is the only one that can be reasoned about on its own: no engine memory is touched anywhere
 * below, every function here is a value in and a value out, and the unit test covers exactly this
 * and nothing else.
 *
 * The seam is therefore not a line count. It is the boundary between what can be tested without a
 * game and what cannot.
 */
#include "fog_regime.h"

#include "fog_band.h"

#include <math.h>
#include <stdbool.h>

float fog_regime_depth_limit(float cut_units)
{
    /* No field of view, no clamp, no guard beyond the one that keeps the band positive: the view
     * axis is always on screen and cos(0) is 1, so this is the radial cut plus the margin and
     * nothing else. See the header for why the margin's sign is opposite to the edge limit's. */
    float reach = cut_units + FOG_CELL_MARGIN_UNITS;

    return (reach > 0.0f) ? reach : 0.0f;
}

float fog_regime_edge_limit(float horizontal_fov_degrees, float cut_units)
{
    float reach = cut_units - FOG_CELL_MARGIN_UNITS;
    float half_angle;

    if (!(reach > 0.0f)) {
        return 0.0f;
    }

    half_angle = 0.5f * horizontal_fov_degrees;
    if (!(half_angle > 0.0f)) {
        /* An unobservable or nonsensical camera falls back to the authored picture rather than to
         * an arbitrary number, so the limit stays the one the levels were built for. */
        half_angle = 0.5f * FOG_REFERENCE_FOV_DEGREES;
    }
    if (half_angle > FOG_MAX_HALF_ANGLE) {
        half_angle = FOG_MAX_HALF_ANGLE;
    }

    return reach * cosf(half_angle * FOG_DEGREES_TO_RADIANS);
}

float fog_regime_follow_factor(float horizontal_fov_degrees, float reference_cut, float live_cut)
{
    float now;
    float reference;
    float factor;

    /* A picture no wider than the authored one, with a cut edge no shorter, gets the level's band
     * untouched, and it gets it EXACTLY, tested on the angle rather than on the quotient. The
     * dead band is there because the field of view arrives as a measured number: rebuilding the
     * projection from a canvas of 639x479 pixels rather than 640x480 lands on 60.025 rather than
     * 60.000, and a coupling that answered 0.9998 to that would move every shipped number by a
     * hair for no reason anybody could see. */
    if (horizontal_fov_degrees <= FOG_REFERENCE_FOV_DEGREES + FOG_FOV_DEAD_BAND_DEGREES &&
        live_cut >= reference_cut) {
        return 1.0f;
    }

    /* Both sides go through the SAME function, so identical arguments give identical bits. */
    now = fog_regime_edge_limit(horizontal_fov_degrees, live_cut);
    reference = fog_regime_edge_limit(FOG_REFERENCE_FOV_DEGREES, reference_cut);

    if (!(reference > 0.0f) || !(now > 0.0f)) {
        return 1.0f;
    }

    factor = now / reference;
    if (factor > 1.0f) {
        return 1.0f;                       /* a NARROWER picture never pushes the fog out */
    }
    if (factor < FOG_FOLLOW_FLOOR) {
        return FOG_FOLLOW_FLOOR;
    }
    return factor;
}

/* The taste multiplier, applied to a band that is otherwise finished.
 *
 * Kept out of the terms below because it is the only one of them that is not about hiding the edge
 * the world stops at, and it applies whether the band was scaled against the draw distance or left
 * exactly as the level authored it: somebody who wants thicker fog than the level shipped wants it
 * either way, and having the setting quietly do nothing in one of the two would be worse than not
 * offering it.
 *
 * Both ends move by the same factor, so the authored proportions survive and the span cannot
 * invert. The one refusal is a band whose end would land inside the engine's own floor, where
 * clamping would put one end past the other and have the ramp paint the world in the fog colour
 * rather than showing less of it. */
static void bring_band_in(fog_regime_band_t *band, float scale)
{
    if (band == NULL || !(scale > 0.0f) || scale == 1.0f) {
        return;
    }
    if (!(band->end * scale > FOG_MIN_END)) {
        return;
    }
    band->start *= scale;
    band->end   *= scale;
}

void fog_regime_target_band(const fog_regime_config_t *config,
                            const fog_regime_band_t *authored,
                            float horizontal_fov_degrees,
                            float reference_cut,
                            float live_cut,
                            fog_regime_band_t *out)
{
    float end;
    float ratio;
    float edge;
    /* The cap is fed the PESSIMISTIC cut. current_cut hands this function the eased live cut, which
     * lags the real gather radius by up to FOG_CUT_SETTLE_SECONDS whenever the cut RISES: a
     * governor recovery, or the scripted-camera raise. For those seconds the eased value is lower
     * than the truth, and a cap computed from it sits inside the real cut, which is the flicker
     * condition arrived at by arithmetic rather than by accident. Taking the smaller of the two
     * here means an eased input can only ever make the band safer. The follow factor and the floor
     * keep the eased value, because they are about how the band LOOKS rather than about what it has
     * to cover. */
    float cap_cut = (live_cut < reference_cut) ? live_cut : reference_cut;
    /* And its mirror, for the same reason in the opposite direction. current_cut hands this the
     * EASED live cut, which lags low while the cut is rising. A pop-in cap computed from a low cut
     * is merely conservative, so that one takes the smaller. A no-saturation end computed from a
     * low cut sits INSIDE the drawn world, which is exactly the flat region the rule exists to
     * remove, so this one takes the larger. Residual, stated rather than hidden: above
     * ViewRangeScale 1 the true live cut can still exceed both for up to FOG_CUT_SETTLE_SECONDS
     * after a governor step, leaving a shallow saturated shell until the ease catches up. */
    float sat_cut = (live_cut > reference_cut) ? live_cut : reference_cut;

    if (config == NULL || authored == NULL || out == NULL) {
        return;
    }

    /* A level that authored no fog must not gain any. The engine treats a band it cannot use by
     * disarming the ramp, and a disarmed ramp means a fog-coloured world, so the safe answer for
     * an unusable band is to hand it back untouched and let the level keep whatever it had. */
    if (!(authored->end > 0.0f) || !(authored->end > authored->start)) {
        *out = *authored;
        return;
    }

    /* The level's own band, untouched, when that is what was asked for.
     *
     * Everything below scales the authored band against the draw distance and the field of view,
     * to guarantee the fog has gone solid before the geometry stops. Measuring the eleven shipped
     * levels showed nine already do that on their own: at the draw distance their own header asks
     * for, their authored fog is between 42 and 100 per cent opaque where the geometry ends, so
     * there was nothing to fix and the scaling only brought the fog nearer than the level wanted.
     * Two do not, Coruscant with no cover at all at its edge and the Federation ship with five per
     * cent, and those two are what the scaling below exists for.
     *
     * Which is right depends on whether covering those two matters more than leaving the other
     * nine as authored, and that is taste rather than correctness. */
    if (config->authored_band) {
        *out = *authored;
        bring_band_in(out, config->band_scale);
        return;
    }

    end = authored->end;
    if (config->fog_scale > 1.0f) {
        end *= config->fog_scale;
    }
    if (config->follow_fov) {
        end *= fog_regime_follow_factor(horizontal_fov_degrees, reference_cut, live_cut);
    }
    /* WHERE THE BAND ENDS. Rule 2 assigns rather than caps, and skips the floor entirely: the
     * floor is (cut - margin) * fraction, which is below (cut + margin) for every fraction at or
     * under one, so it could never bind here anyway.
     *
     * fog_scale and follow_fov are bypassed by that assignment, and neither is lost. FogScale's job
     * is to make the fog follow the range, and this reads the live cut directly instead of
     * inferring it from a configured scale. FogFollowFov's premise, that a wider picture needs the
     * band nearer, is true of the corner and false of the axis, and the axis is what this bound is
     * about. Both are still honoured under rules 0 and 1. */
    edge = 0.0f;
    if (config->inside_cut == FOG_END_NO_SATURATION) {
        end = fog_regime_depth_limit(sat_cut);
    } else if (config->inside_cut == FOG_END_NO_POP_IN) {
        /* The no-pop-in limit, computed here and APPLIED AFTER THE FLOOR. See the floor's own
         * comment for why the order is the whole point. */
        edge = fog_regime_edge_limit(horizontal_fov_degrees, cap_cut);
        if (edge > 0.0f && end > edge) {
            end = edge;
        }
    }
    /* HOW FAR IN THE COUPLING IS ALLOWED TO PULL THE END, and this is the one bound the original
     * had none of.
     *
     * Both terms above are cos(half angle) in disguise, which converts a radial reach into a
     * forward one: a cell at the frustum CORNER at radial distance `cut` is only cut*cos(theta)
     * ahead of the eye, and fog is measured on the forward distance, so ending the band there is
     * what guarantees the corner is hidden before it pops. The trouble is that it applies a
     * correction computed for the corner to the WHOLE screen, and straight ahead the forward
     * distance is the radial distance:
     *
     *   hFOV  60 (authored)   end 18.2 of a 22 cut    83 %
     *   hFOV  94              end 14.3                65 %
     *   hFOV 120              end 10.5                48 %
     *
     * So a wide picture is fogged solid at half the distance the engine is still drawing geometry,
     * and everything between is painted flat. Field reported as a level looking washed out and as
     * characters below a cliff being drawn but invisible.
     *
     * The floor keeps the corner correction while refusing to spend more than a set share of the
     * visible world on it. Past that the extreme corners may pop in, which costs a few pixels at
     * the edge of the picture rather than everything beyond halfway.
     *
     * BUT THE FLOOR MAY NOT RAISE THE END PAST THE LIMIT, and until now it always did. The limit is
     * (cut - margin) * cos(hFOV/2) and the floor is (cut - margin) * min_end_fraction, so at the
     * shipped min_end_fraction of 1.0 the floor is at or above the limit for EVERY legal field of
     * view, and the limit above was overwritten on every level, always. FogInsideCut has been dead
     * at the shipped defaults, and the end has been pinned to the cut edge itself.
     *
     * That is not a missed optimisation, it is the flicker. The cut is a RADIAL radius and the fog
     * ramp walks VIEW DEPTH, so a cell arriving at the boundary near the screen edge is only
     * (cut - margin) * cos(hFOV/2) deep: about half the band end at 120 degrees. Boundary cells
     * therefore appear and vanish at a fog factor near 0.5 rather than buried at 1.0, which is a
     * visible blink at exactly the fog end, with a static band and no net change in the cell count
     * because the churn is in the membership rather than the size. Every measurement taken of this
     * agreed with that and with nothing else.
     *
     * So the floor is clamped to the limit. The floor keeps its purpose, which is to stop the
     * corner correction washing out the middle of a wide picture, because that is served by RAISING
     * a band the cosine pulled too near; it was never served by pushing one past the cut. */
    if (config->inside_cut != FOG_END_NO_SATURATION && config->min_end_fraction > 0.0f) {
        float floor_end = (live_cut - FOG_CELL_MARGIN_UNITS) * config->min_end_fraction;

        if (edge > 0.0f && floor_end > edge) {
            floor_end = edge;
        }
        if (floor_end > 0.0f && end < floor_end) {
            end = floor_end;
        }
    }

    if (end < FOG_MIN_END) {
        end = FOG_MIN_END;
    }

    /* ONE ratio for the whole band. See the header: moving the end alone crosses the authored
     * start in two shipped levels, and a negative span makes the engine paint the world in the fog
     * colour instead of showing less of it. */
    ratio = end / authored->end;
    out->end   = end;
    out->start = authored->start * ratio;

    /* Last, after every term that had a reason. See bring_band_in(). */
    bring_band_in(out, config->band_scale);
}

float fog_regime_ease(float current, float target, float seconds, float settle_seconds)
{
    float keep;
    float gap;

    if (!(settle_seconds > 0.0f)) {
        return target;                     /* easing switched off: the old instant step */
    }
    if (!(seconds > 0.0f)) {
        return current;                    /* no time passed, or a delta that cannot be believed */
    }
    if (seconds > FOG_MAX_TRUSTED_SECONDS) {
        seconds = FOG_MAX_TRUSTED_SECONDS; /* a loading hitch must not teleport the fog */
    }

    gap = target - current;
    if (gap > -FOG_DEAD_BAND_UNITS && gap < FOG_DEAD_BAND_UNITS) {
        return target;
    }

    /* The fraction of the gap left after `settle_seconds` is FOG_DAMP_REMAINDER however the
     * interval was cut into frames; that is the whole point of taking the exponent from the
     * engine's live delta instead of assuming a frame rate. */
    keep = powf(FOG_DAMP_REMAINDER, seconds / settle_seconds);
    return (1.0f - keep) * target + keep * current;
}

