/* fog_regime.c: the fog the engine computes itself, and the band it computes it over.
 *
 * The geometry this file implements, why the cut edge is a circle of cell centres in world
 * units, why a wider picture needs the fog nearer, and why the WHOLE band has to move rather than
 * only its far end, is written out in fog_regime.h. Everything below is the byte evidence for
 * each site and the arithmetic itself.
 *
 * SIZE NOTE. Back under the 600 line mark, and the note stays because how it got there is the
 * part worth knowing. Most of what is left is byte evidence at each site rather than code, which
 * is where that evidence belongs.
 *
 * It was once past the hard limit, carrying four jobs its section banners named. Two have gone:
 * the arithmetic to fog_band.c, which needed no engine memory and is the half a unit test can
 * reach, and the install pass to fog_regime_install.c, which runs once and never again. What is
 * left is the live cut edge and camera, and the level record, which both run every frame and
 * share one state. Splitting those two would divide a machine rather than a responsibility.
 */
#include "fog_regime_internal.h"

#include "cell_watchdog.h"
#include "fog_band.h"
#include "fog_trace.h"
#include "view_distance_fix.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/* World record fields, all proven at the level loader that fills them from the B3D header:
 *   +0x210 <- hdr+0x88 render flags, bit 0 = has fog   (default 0    at 0x0041CC3A)
 *   +0x214 <- hdr+0x8C packed fog colour               (default 0x408020 at 0x0041CC61)
 *   +0x218 <- hdr+0x90 fog start   (0x0041D0A3)        (default 10.0f at 0x0041CC47)
 *   +0x21C <- hdr+0x94 fog end     (0x0041D0B5)        (default 22.0f at 0x0041CC54)
 *   +0x014 <- hdr+0x854 draw distance (0x0041CFDB)     (default 24    at 0x0041CFC8) */
#define WORLD_VIEW_DISTANCE 0x014
#define WORLD_RENDER_FLAGS  0x210
#define WORLD_FOG_START     0x218
#define WORLD_FOG_END       0x21C
#define WORLD_FOG_BIT       0x001u
#define WORLD_PROBE_SIZE    0x220   /* everything above must be readable before anything is read */


/* The band a level opens on: past anything the engine draws, so nothing is fogged at all.
 *
 * Why a level opens with no fog: a level load hands the engine the level's authored band, and the
 * coupling then recomputes the end from the cut edge the renderer reports on the first walked
 * frame. Those two are not the same number: Mos Espa authors 32 and its draw edge is 21.3, so the
 * fog visibly closes in by a third over the first seconds of the level. Easing it is what makes
 * that a slow slide rather than a jump, and on a level whose opening is an establishing camera a
 * long way from the player, which is most of them, the slide is the most conspicuous thing on
 * screen. Reported on the podrace, twice.
 *
 * So the band is held out here while that settles, and the real one arrives afterwards. It does
 * not arrive at full strength: see FOG_OPEN_FADE_FROM_AUTHORED.
 *
 * The same numbers the no-fog cheat uses, and for the same reason: far enough that the ramp finds
 * nothing to fog, near enough that dividing by the band width stays comfortably inside float32. */
#define FOG_OPEN_HIDDEN_START 4000.0f
#define FOG_OPEN_HIDDEN_END   5000.0f

/* Snapping straight to the real band was the first version and it arrives as a wall: one frame
 * with nothing fogged, the next with the far half of the world solid. There is no density to fade,
 * since the ramp always reaches full opacity at the band's end, so the fade is done on the band's
 * position instead, and the ordinary easing brings it in over FogSettleSeconds. Where it starts
 * from is the constant below. */

/* The fade starts from the LEVEL'S OWN band, not from the one the coupling is heading for, and
 * that choice is what makes this independent of when anything else happens.
 *
 * Three attempts read the target at the moment the window closed, and all three read it against
 * the RAISED draw distance, because the scale is applied at the end of a frame and governs the
 * next one, so the cut the renderer reports lags every decision about it. Ending the two together,
 * deferring by a frame, and giving the raise back a tenth of a second early all produced the same
 * wrong number: Mos Espa faded in to a band ending at 27.1 against a world stopping at 22.
 *
 * The band is recomputed every frame anyway, so the close does not need a correct target at all.
 * It needs a starting point that is visually clear and belongs to this level, and the authored
 * band doubled is exactly that: on Mos Espa it is 20..64 against a draw edge of 22, about five per
 * cent fogged where the geometry stops. The ordinary easing then walks it to whatever the target
 * is by the time each frame asks, which is the right one as soon as the restored scale has been
 * through the renderer, whenever that happens to be. Nothing here has to know when. */
#define FOG_OPEN_FADE_FROM_AUTHORED 2.0f



fog_regime_state_t fog_state;   /* declared in fog_regime_internal.h */

/* ==============================================================================================
 * B, the live cut edge and the live camera
 * ============================================================================================ */
static float clamp_cut(int32_t range)
{
    /* The world walk clamps the draw distance to [2,64] itself at 0x00404F33..0x00404F48, after
     * bapmat_viewDistance has returned. Both numbers are put through the same clamp here so the
     * ratio between them is the ratio the renderer actually sees. */
    if (range < 2)  { range = 2; }
    if (range > 64) { range = 64; }
    return (float)range;
}

void *fog_regime_device(void)
{
    if (fog_state.device_ptr_addr == 0) {
        return NULL;
    }
    return *(void **)fog_state.device_ptr_addr;
}

void fog_regime_set_fov(float horizontal_fov_degrees)
{
    if (horizontal_fov_degrees > 1.0f && horizontal_fov_degrees < 180.0f) {
        fog_state.horizontal_fov_degrees = horizontal_fov_degrees;
    }
}

void fog_regime_note_cut(int32_t reference_range, int32_t effective_range)
{
    fog_state.reference_cut = clamp_cut(reference_range);
    fog_state.live_cut      = clamp_cut(effective_range);
    if (!fog_state.cut_observed) {
        /* The level snapped its band from an ASSUMED cut, because nothing had walked the new world
         * yet. This is the first real one, so there is nothing to ease from and the settled value
         * starts here rather than crawling up to it from a guess. */
        fog_state.settled_live_cut = fog_state.live_cut;
        fog_state.cut_first_seen   = true;
    }
    fog_state.cut_observed  = true;
}

/* The cut edge to work from. Without an observation, the draw-distance detour declined, or the
 * world walk has not run yet, the level's own draw distance is the honest stand-in, and it makes
 * the follow factor exactly 1.0 rather than an invented number. */
/* Before the first frame of a level there is no reported cut, so both sides are predicted from
 * the level's own authored view distance: the reference is that distance, and the live side is
 * where this DLL's own scale and radius cap put it, asked of the one function that owns that
 * arithmetic. The prediction is exact wherever the level has no per-cell override, which is the
 * ordinary case, and it is what lets a level open on the band it is going to keep.
 *
 * It used to use the authored distance for BOTH sides, a ratio of one, which is only the same
 * answer at ViewRangeScale=1. RACE authors 22 and the real cut at 2.5 is 39, and a band computed
 * from 22 sits far nearer the camera than it belongs; that was reported, and predicting the live
 * side rather than assuming it is what stops it coming back. */
static void current_cut(float *reference, float *live)
{
    if (fog_state.cut_observed) {
        *reference = fog_state.reference_cut;
        *live      = fog_state.settled_live_cut;
        return;
    }
    *reference = clamp_cut(fog_state.level_view_distance);
    *live      = clamp_cut(view_distance_fix_cut_for(fog_state.level_view_distance));
}

static void target_now(fog_regime_band_t *out)
{
    float reference;
    float live;

    /* A level that has just loaded has not been walked, so there is no reported cut. This used to
     * return the authored band and wait, which is the honest answer only if the two are close, and
     * at a wide field of view they are not: the edge limit takes a band ending at 32 down to 21.3
     * on a level that draws to 22, so the level opened on almost no fog and the ease then drove
     * the fog wall in at 19 units a second, in a world 22 units deep. Measured over 442 frames
     * with everything else on that path constant, which is what the reports of flashing at the
     * start of a level turned out to be.
     *
     * current_cut predicts both sides instead, so the band a level opens on is the band it keeps
     * and there is nothing to travel. Whatever the first real cut then reports is a correction,
     * not a journey. */
    current_cut(&reference, &live);
    fog_regime_target_band(&fog_state.config, &fog_state.authored,
                           fog_state.horizontal_fov_degrees, reference, live, out);
}

/* ==============================================================================================
 * C, the level record
 * ============================================================================================ */
/* With the per-vertex ramp the engine re-reads the band from the world record every frame, so
 * writing those two floats was enough. The device path does not: FOGSTART and FOGEND only move when
 * applyLevelFog runs. That function is already detoured here, so its original is called directly,
 * which reprograms the device from the fields just written and re-enters nothing. */
void push_band_to_device(void)
{
    apply_fog_fn_t original = (apply_fog_fn_t)fog_state.apply_detour.original;

    /* Not while the detour below is running. That function ends by calling the original itself, so
     * pushing here as well would reprogram the device twice for one engine call. */
    if (fog_state.inside_apply) {
        return;
    }
    if (fog_state.pixel_fog_active && original != NULL && fog_state.level != NULL) {
        const float *live = (const float *)((const char *)fog_state.level + WORLD_FOG_START);

        original(fog_state.level);
        fog_state.device_band.start = live[0];
        fog_state.device_band.end   = live[1];
    }
}

static void write_band(const fog_regime_band_t *band)
{
    float *fields;

    if (fog_state.level == NULL) {
        return;
    }

    /* The one invariant the renderer cannot survive being wrong about: bapdraw_setFrameState
     * disarms the ramp on a negative span and the emitter then writes a zero specular, which is
     * FULLY FOGGED, not "no fog". Refusing here leaves the level exactly as authored. */
    if (!(band->end > band->start)) {
        if (!fog_state.span_refused) {
            fog_state.span_refused = true;
            log_warning("refused a fog band of %.2f..%.2f, the engine disarms its own ramp on a "
                        "non-positive span and then paints every world vertex in the fog colour. "
                        "The level keeps the band it was authored with.",
                        (double)band->start, (double)band->end);
        }
        return;
    }

    fields = (float *)((char *)fog_state.level + WORLD_FOG_START);
    fields[0] = band->start;               /* +0x218 */
    fields[1] = band->end;                 /* +0x21C */
    fog_state.written = *band;
    push_band_to_device();
}

/* Is the band in the record still the one we put there? Its own function because two of the
 * three tests below need it and the per-frame tick needs it on its own. */
static bool the_band_is_still_ours(const void *level)
{
    const float *fog = (const float *)((const char *)level + WORLD_FOG_START);

    return fog[0] == fog_state.written.start && fog[1] == fog_state.written.end;
}

/* Is this the world we were last told about? The pointer ALONE is not an identity: the record is
 * reallocated on every level load and an allocator hands out the same address more often than not,
 * so a new level could inherit the previous level's remembered band. The second and third
 * criteria are what close that: if the record no longer holds exactly what WE last wrote, or
 * carries another draw distance, the loader has been through it.
 *
 * The reads need no range check of their own. They only happen once the pointer matches one that
 * remember_level already validated, and the function this hook sits on reads the very same fields
 * three instructions later, so a record these would fault on is one the engine faults on too. */
static bool is_the_same_level(const void *level)
{
    const int32_t *view_distance =
        (const int32_t *)((const char *)level + WORLD_VIEW_DISTANCE);

    return fog_state.level == level &&
           the_band_is_still_ours(level) &&
           *view_distance == fog_state.level_view_distance;
}

/* The same record and the same authored draw distance, whatever the band now says.
 *
 * This is the weaker question, and it exists because the band is not ours alone: the no-fog cheat
 * writes it every frame and is entitled to. Answering the FULL identity above with "no" while that
 * is happening is correct as far as it goes, but the caller then took the record for a freshly
 * loaded level and read the cheat's 4000..5000 as the level's AUTHORED band. The level's real fog
 * was gone until it was reloaded, and the opening window, which arms from the same place, switched
 * the fog off and raised the draw distance in the middle of play.
 *
 * So a record that fails only on its band is neither the same state nor a new level. It is this. */
static bool is_the_same_record(const void *level)
{
    const int32_t *view_distance =
        (const int32_t *)((const char *)level + WORLD_VIEW_DISTANCE);

    return fog_state.level == level && *view_distance == fog_state.level_view_distance;
}

static bool remember_level(void *level)
{
    const float   *fog;
    const int32_t *view_distance;
    const uint32_t *flags;

    /* A level with no fog reaches this function again on every effects fog restore, so both the
     * page walk below and its log line have to happen once rather than once per restore. */
    if (level == fog_state.level_without_fog) {
        return false;
    }

    if (!memory_is_readable_range((uintptr_t)level, WORLD_PROBE_SIZE)) {
        /* Forget the previous level too: the per-frame tick must not keep writing through a
         * pointer we have just decided we do not trust. */
        fog_state.level = NULL;
        log_warning("the world record at %08X is not readable to +0x220, the fog is left alone",
                    (unsigned)(uintptr_t)level);
        return false;
    }

    flags = (const uint32_t *)((const char *)level + WORLD_RENDER_FLAGS);
    if ((*flags & WORLD_FOG_BIT) == 0) {
        /* Fog switched off for this level. Its band is never read, and writing one would be the
         * one way to give a level fog it does not want. */
        fog_state.level = NULL;
        fog_state.level_without_fog = level;
        log_info("this level has fog disabled (world+0x210 bit 0 clear), nothing is written");
        return false;
    }

    fog           = (const float *)((const char *)level + WORLD_FOG_START);
    view_distance = (const int32_t *)((const char *)level + WORLD_VIEW_DISTANCE);

    fog_state.level_without_fog   = NULL;
    fog_state.level               = level;
    fog_state.authored.start      = fog[0];
    fog_state.authored.end        = fog[1];
    fog_state.level_view_distance = *view_distance;
    fog_state.span_refused        = false;

    /* The cut belongs to the level being left, not this one. Nothing has walked the new world yet,
     * so the draw-distance hook has not run and reference_cut and live_cut still describe the
     * previous level. Clearing this makes the snap below fall back to this level's own authored
     * view distance for both, a ratio of one, which is the honest answer until the first frame of
     * the new world reports a real one. Left set, the snap is computed from the level just left
     * and the band then walks to its true value over the settle time. */
    fog_state.cut_observed   = false;
    fog_state.cut_first_seen = false;

    /* AND WHAT WE BELIEVE THE DEVICE IS SHOWING, which is nothing, because this is a new level.
     *
     * This is a record of what we last pushed, and it was carried across a level load. The tick's
     * settled test asks whether the device already shows the current band, so on a second load of
     * the same level, where the numbers come out identical, it compared our new band against the
     * previous level's push, decided there was nothing to do and never pushed at all. The device
     * then kept whatever the engine's own apply had put there: the AUTHORED band.
     *
     * Measured across two passes at the same level in one session. Both logged
     * "level fog 10.0..32.0 -> 6.7..21.3", and the device held 6.65..21.29 on the first and
     * 10.00..32.00 on the second, for 1063 frames, having never once been written. So this fog
     * reached the hardware on the first level of a session and no other. */
    fog_state.device_band.start = -1.0f;
    fog_state.device_band.end   = -1.0f;

    /* And the opening window starts here, which is the one place that knows a level is new. */
    fog_state.open_left = fog_state.config.open_seconds;
    fog_trace_begin();
    return true;
}

void __cdecl hook_apply_fog(void *level)
{
    apply_fog_fn_t original = (apply_fog_fn_t)fog_state.apply_detour.original;

    if (level == NULL) {
        original(level);
        return;
    }

    fog_state.inside_apply = true;

    if (is_the_same_level(level)) {
        /* The effects fog restore came through, or the level was re-applied: this function
         * re-programs the device from these two fields, so they have to hold ours rather than the
         * authored pair. The eased value is kept; nothing about the level changed.
         *
         * Not while somebody else is holding the band. The no-fog cheat writes it every frame and
         * is entitled to; overwriting it here would take the fog back off them for one apply. */
        write_band(&fog_state.current);
    } else if (is_the_same_record(level)) {
        /* The same level with somebody else's band in it. Nothing to do: they are holding it on
         * purpose and the tick below pushes their value to the device on their behalf. What
         * matters is that this is NOT taken for a new level, which would read their band as this
         * level's authored one and arm the opening window in the middle of play. */
    } else if (remember_level(level)) {
        /* A fresh level SNAPS. There is nothing on screen to ease away from, and easing here
         * would show the authored band for a moment and then walk it in. */
        target_now(&fog_state.current);
        write_band(&fog_state.current);

        log_info("level fog %.1f..%.1f -> %.1f..%.1f (draw distance %d, %.1f degrees)",
                 (double)fog_state.authored.start, (double)fog_state.authored.end,
                 (double)fog_state.current.start, (double)fog_state.current.end,
                 (int)fog_state.level_view_distance,
                 (double)fog_state.horizontal_fov_degrees);
    }

    fog_state.inside_apply = false;
    original(level);
}

/* report_device_fog_caps and consider_pixel_fog are defined in fog_regime_install.c and declared
 * in fog_regime_internal.h. The tick below is where they are first called. */

void fog_regime_on_frame(void)
{
    fog_regime_band_t target;
    float             seconds;
    bool              settled;

    if (!fog_state.tick_active || fog_state.level == NULL) {
        return;
    }
    /* The world is freed and replaced by the level load. Until the fog detour hands us the new
     * one, the remembered pointer is not ours to write through. */
    if (*fog_state.level_pointer != fog_state.level) {
        fog_trace_aside('p', 0.0f, 0.0f);
        return;
    }
    /* And the record still has to hold what we last put there. Two things ride on this. A level
     * loaded into the address the previous one had would otherwise be ticked with the previous
     * level's remembered band for however many frames pass before the fog apply reaches us. And
     * anything that deliberately writes this band; nothing in the shipped image does, but a
     * future patch might, gets to keep its value instead of being overwritten sixty times a
     * second by ours. */
    if (!the_band_is_still_ours(fog_state.level)) {
        {
            const float *seen = (const float *)((const char *)fog_state.level + WORLD_FOG_START);

            fog_trace_aside('f', seen[0], seen[1]);
        }
        /* Somebody else is holding the band, and they are entitled to. The no-fog cheat pushes it
         * past everything the renderer still has in view, every frame, and this tick stands aside
         * for exactly that.
         *
         * On the per-vertex path standing aside was enough, because the engine re-read these two
         * fields every frame and whatever was in them took effect. The device path does not: the
         * band only reaches FOGSTART and FOGEND through applyLevelFog. So a writer who is not us
         * would be writing into a record nobody reads, and their fog would never change. Pushing
         * their value, not ours, is what keeps that promise. */
        if (fog_state.pixel_fog_active) {
            const float *live = (const float *)((const char *)fog_state.level + WORLD_FOG_START);

            if (live[0] != fog_state.device_band.start ||
                live[1] != fog_state.device_band.end) {
                apply_fog_fn_t original = (apply_fog_fn_t)fog_state.apply_detour.original;

                if (original != NULL) {
                    original(fog_state.level);
                    fog_state.device_band.start = live[0];
                    fog_state.device_band.end   = live[1];
                }
            }
        }
        return;
    }

    report_device_fog_caps();
    consider_pixel_fog(fog_state.device_caps);

    seconds = (fog_state.frame_delta != NULL) ? *fog_state.frame_delta : 0.0f;

    /* The opening window. See FOG_OPEN_HIDDEN_START. */
    if (fog_state.open_left > 0.0f) {
        /* A loading hitch must not spend the whole window in one frame, which is the same reason
         * the easing clamps its own delta. A window measured in seconds and a frame that claims to
         * have taken one are not compatible claims. */
        if (seconds > 0.0f) {
            fog_state.open_left -= (seconds > FOG_MAX_TRUSTED_SECONDS)
                                 ? FOG_MAX_TRUSTED_SECONDS : seconds;
        }
        if (fog_state.open_left > 0.0f) {
            fog_regime_band_t open_band;

            if (fog_state.config.open_fog_end > fog_state.config.open_fog_start) {
                /* Our own band, at the distances asked for, derived from nothing. The draw
                 * distance is raised for this window and the engine culls whole cells at that
                 * edge, so a cell arriving there takes its near end with it and no band placed at
                 * the edge can cover it. A band laid well inside the raised cut can, and laying it
                 * absolutely is what stops it moving while the window runs. */
                open_band.start = fog_state.config.open_fog_start;
                open_band.end   = fog_state.config.open_fog_end;
            } else {
                open_band.start = FOG_OPEN_HIDDEN_START;
                open_band.end   = FOG_OPEN_HIDDEN_END;
            }
            if (open_band.start != fog_state.written.start ||
                open_band.end != fog_state.written.end) {
                fog_state.current = open_band;
                write_band(&open_band);
            }
            /* 'o' for the opening window. This branch returns before the sample at the bottom, so
               without this the capture has a hole exactly where the window runs. */
            fog_trace_aside('o', fog_state.written.start, fog_state.written.end);
            return;
        }
        /* See FOG_OPEN_FADE_FROM_AUTHORED. The cut is marked unobserved so the first report at
         * the restored scale is snapped to rather than eased towards: the settled cut spent the
         * window climbing to the raised value, and easing back down from it would be the same slow
         * slide this window exists to prevent. */
        fog_state.open_left     = 0.0f;
        fog_state.cut_observed  = false;
        fog_state.current.start = fog_state.authored.start * FOG_OPEN_FADE_FROM_AUTHORED;
        fog_state.current.end   = fog_state.authored.end * FOG_OPEN_FADE_FROM_AUTHORED;
        write_band(&fog_state.current);
        log_info("the opening window is over, fog fades in from %.1f..%.1f towards this level's",
                 (double)fog_state.current.start, (double)fog_state.current.end);
        return;
    }

    /* The input, eased, before anything reads it. */
    if (fog_state.cut_observed) {
        fog_state.settled_live_cut = fog_regime_ease(fog_state.settled_live_cut,
                                                     fog_state.live_cut, seconds,
                                                     FOG_CUT_SETTLE_SECONDS);
    }

    /* The level opened on its own authored band, which is a real band and not a guess, so the move
     * to ours is EASED rather than snapped. It used to snap, and correctly: the starting point was
     * then a value computed from an assumed cut, and sliding away from a wrong number only draws
     * attention to it. Now that the starting point is the level's own, a snap is a visible jump on
     * the first frame of every level, which is what a couple of them were reported flashing on. */
    fog_state.cut_first_seen = false;

    target_now(&target);

    fog_state.current.start = fog_regime_ease(fog_state.current.start, target.start, seconds,
                                              fog_state.config.settle_seconds);
    fog_state.current.end   = fog_regime_ease(fog_state.current.end, target.end, seconds,
                                              fog_state.config.settle_seconds);

    /* Settled means OUR band has not moved AND the device is showing it. The second half matters
     * only on the device path, and it is what lets fog come back after another feature has held
     * the band: the no-fog cheat restores exactly the value we last wrote, so our own bookkeeping
     * sees nothing to do while FOGSTART and FOGEND still hold the cheat's band. Without this the
     * fog can be switched off and never on again. */
    settled = fog_state.current.start == fog_state.written.start &&
              fog_state.current.end == fog_state.written.end &&
              (!fog_state.pixel_fog_active ||
               (fog_state.device_band.start == fog_state.current.start &&
                fog_state.device_band.end == fog_state.current.end));

    fog_trace_sample(fog_state.horizontal_fov_degrees, fog_state.reference_cut,
                     fog_state.live_cut, fog_state.settled_live_cut, fog_state.cut_observed,
                     &target, &fog_state.current, !settled);
    {
        uint32_t cells = 0u, vertices = 0u;

        cell_watchdog_counts(&cells, &vertices);
        fog_trace_counts(cells, vertices, seconds);
    }
    if (settled) {
        return;                            /* settled: no write, no cache line touched */
    }
    write_band(&fog_state.current);
}
