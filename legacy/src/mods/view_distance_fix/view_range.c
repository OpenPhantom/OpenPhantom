/* view_range.c: the draw distance actually in force, frame by frame.
 *
 * THE SEAM. Lifted whole out of view_distance_fix.c, which was past the hard limit. Everything
 * that decides how far the world is drawn is here, together with the state it decides over: the
 * observed field of view, the radius cap, the cut edge, the bapmat_viewDistance detour and the
 * per frame tick. The tick came with it rather than staying with the install sequence because
 * almost all of it is this one number being argued over, and the two calls in it that are not,
 * the two-sided budget and the dither, have to happen once a frame at a fixed point in that
 * order.
 *
 * THE ORDER OF THE TICK IS ITS CORRECTNESS. The frame governor runs before the cell watchdog, the
 * two raises sit between them, and the fog reads the settled number afterwards. Each step says
 * next to itself what breaks in the other order.
 */
#include "view_range.h"

#include "view_distance_fix.h"
#include "view_settings.h"

#include "cell_watchdog.h"
#include "device_dither.h"
#include "fog_regime.h"
#include "frame_governor.h"
#include "two_sided_faces.h"

#include "common/cinematic_gate.h"
#include "common/detour.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

/* bapmat_viewDistance's prologue, `55 / 8B EC / 51 / 8B 45 08`, seven bytes on a clean boundary.
 * That site is not searched in the detour form, so this number is needed only here. */
#define VIEW_DISTANCE_PROLOGUE_SIZE 7u

/* The camera's own horizontal field of view in degrees, cam+0x38, as an index into the int32
 * view of the camera record. */
#define CAMERA_FOV_DEGREES_INDEX 14

/* The reference the RADIUS CAP is measured against. It is deliberately not the camera's own
 * default: bapview_newView builds the one camera in the image with 60 degrees (`push 0x42700000`
 * at 0x00417F79), and the world walk then widens that by three (`fsub [0x4A8064]` with
 * [0x4A8064] = -3.0f at 0x00404EF5) before it takes the tangent for the collect wedge. 63 is
 * therefore the angle the CELL COUNT is authored for, which is the quantity this cap protects.
 * The fog uses the camera's 60 instead, because what the fog has to hide is what the player sees.
 * The cap does not bite at either: `64*sqrt(63/hFOV)` is 64 for any hFOV at or below 63. */
#define AUTHORED_FOV_DEGREES 63.0f
#define MAX_DRAW_RANGE       64.0f
#define MIN_DRAW_RANGE        2.0f

typedef int32_t (__cdecl *view_distance_fn_t)(void *world, uint8_t *out_lod_mask);
typedef uint32_t (__cdecl *build_projection_fn_t)(int32_t *camera);

typedef struct view_range_state {
    /* The DLL's one configuration record, bound at install. Not a copy: the settings poll writes
     * through this same pointer, so a key changed on disk is seen here on the next frame. */
    view_distance_config_t *config;

    detour_t                view_distance_detour;
    detour_t                build_projection_detour;

    /* The EFFECTIVE range scale. It starts at the setting and is only ever lowered by the
     * watchdog. */
    float                   effective_view_scale;

    /* The horizontal field of view currently in force, observed rather than asked for. */
    float                   horizontal_fov_degrees;

    bool                    was_opening;
    bool                    was_cutscene;   /* so the window is logged once, not per frame */
} view_range_state_t;

static view_range_state_t range_state;

void view_range_configure(view_distance_config_t *config, float scale)
{
    range_state.config = config;
    range_state.effective_view_scale = scale;
}

void view_range_set_scale(float scale)
{
    range_state.effective_view_scale = scale;
}

/* ============================================================================================
 * The field-of-view observer. See the site comment for why this DLL reads it itself.
 * ============================================================================================ */
static uint32_t __cdecl hook_build_projection(int32_t *camera)
{
    build_projection_fn_t original =
        (build_projection_fn_t)range_state.build_projection_detour.original;
    uint32_t result = original(camera);

    if (camera != NULL) {
        float degrees = *(const float *)&camera[CAMERA_FOV_DEGREES_INDEX];
        if (degrees > 1.0f && degrees < 180.0f) {
            range_state.horizontal_fov_degrees = degrees;
            fog_regime_set_fov(degrees);
        }
    }
    return result;
}

/* ============================================================================================
 * The draw distance
 *
 * The detour calls the original and scales ONLY the return value. The second output `out_lod_mask`
 * (the interior mask from bapPoly+0x37) is left untouched. Clamping happens here, because two of
 * the three callers do not clamp at all.
 * ============================================================================================ */

/* The cap on the scale does not protect, and this is why.
 *
 * bapmat_viewDistance lets bapCell+0x03 override the level default. 132 authored cells ALREADY
 * run range = 64 in retail (BIGCITY 101, RACE 18, FEDSHIP 12, SWAMP 1) and 4757 carry >= 32. For
 * those it makes no difference whether we scale by 1.0 or by 2.25: `64 * anything` is clamped to
 * 64 anyway. That is how a measurement reached 8189 cells at ViewRangeScale=1.25 while the wedge
 * model never exceeds 6914.
 *
 * The cell count goes roughly like (hFOV/360)*pi*r^2, so a wider picture costs draw distance,
 * and with the SQUARE ROOT. That is the honest trade:
 *
 *     rMax = 64 * sqrt(63 * cellBudget / hFOV)
 *
 * At the authored 63 degrees that is exactly 64, i.e. RETAIL-IDENTICAL. Only when WE widen the
 * picture does the picture pay for it, rather than the renderer paying with an overflow. */
static float maximum_range(void)
{
    float hfov = range_state.horizontal_fov_degrees;
    float quotient;
    float root;

    if (!(hfov > AUTHORED_FOV_DEGREES) || hfov >= 180.0f) {
        return MAX_DRAW_RANGE;
    }

    quotient = (AUTHORED_FOV_DEGREES * cell_watchdog_budget()) / hfov;

    /* Square root without a libm call on the hot path: two Newton steps are better than 1e-4 for
     * a quotient in [0.35, 1.0]. */
    root = 0.5f + 0.5f * quotient;
    root = 0.5f * (root + quotient / root);
    root = 0.5f * (root + quotient / root);

    return (MAX_DRAW_RANGE * root > MAX_DRAW_RANGE) ? MAX_DRAW_RANGE : MAX_DRAW_RANGE * root;
}

/* Where this DLL puts the cut edge for an engine range of `engine_range`, with the scale and the
 * radius cap that are in force at the moment of asking.
 *
 * Its own function because the fog needs the answer BEFORE the first frame of a level. The world
 * has not been walked at that point, so the hook below has not run and there is no reported cut,
 * and the fog used to decline to have an opinion and hold the authored band until one arrived.
 * That is a whole frame of authored fog followed by a walk to the real band, and at a wide field
 * of view the two are far apart: 32 against 21.3 on a level that draws to 22. Predicting it from
 * the level's own authored view distance costs nothing and is exact wherever the level has no
 * per-cell override and the scale is 1, which is what ships. */
int32_t view_distance_fix_cut_for(int32_t engine_range)
{
    int32_t range = engine_range;
    float   limit = maximum_range();
    float   scaled;

    if (range_state.effective_view_scale <= 1.0f) {
        /* Even without a scale: the field of view alone already costs cells. */
        if ((float)range > limit) {
            range = (int32_t)limit;
        }
    } else {
        scaled = (float)range * range_state.effective_view_scale + 0.5f;
        if (scaled > limit) {
            scaled = limit;
        }
        range = (int32_t)scaled;

        if ((float)range < MIN_DRAW_RANGE) { range = (int32_t)MIN_DRAW_RANGE; }
        if ((float)range > MAX_DRAW_RANGE) { range = (int32_t)MAX_DRAW_RANGE; }
    }
    return range;
}

static int32_t __cdecl hook_view_distance(void *world, uint8_t *out_lod_mask)
{
    view_distance_fn_t original = (view_distance_fn_t)range_state.view_distance_detour.original;
    int32_t            engine_range = original(world, out_lod_mask);
    int32_t            range = view_distance_fix_cut_for(engine_range);

    /* This is the only place both numbers exist at once, which is why the fog is told from here
     * rather than recomputing the cut edge from the configuration. `engine_range` is where the cut
     * edge would have been; it already carries the level's default AND any per-cell override
     * from bapCell+0x03, and `range` is where we have actually put it. The fog needs the ratio,
     * not either number on its own. */
    /* But NOT while the level's opening window has the scale raised above what the player asked
     * for. The frame governor and the cell watchdog only ever LOWER it, so a scale above the
     * configured one can only be that override, and the cut edge it produces is one the player is
     * about to lose. Letting it into the fog's memory is what made the band fade in against an
     * edge two and a half times the real one, on every level whose window ran: Mos Espa settled
     * towards 27.1 in a world that stops at 22.
     *
     * A comparison rather than a timer, deliberately. Three attempts at timing this failed,
     * because the scale is applied at the end of a frame and governs the next one, so every
     * decision about it is a frame ahead of the cut that reports it. This test is made in the one
     * place that holds the scale which actually produced the number in hand. */
    if (range_state.effective_view_scale <= range_state.config->view_range_scale ||
        range_state.was_cutscene) {
        /* The cutscene raise is the one case where the fog SHOULD follow the raised edge. The
         * level opening it was written for hid the fog entirely, so reporting a raised cut there
         * only poisoned the band the level settled to afterwards. A scripted camera keeps its fog,
         * and a band left at the unraised edge would fog solid at 21 while the engine draws to 55,
         * which is the raise bought and then thrown away. */
        fog_regime_note_cut(engine_range, range);
    }
    return range;
}

void view_range_on_frame(void)
{

    two_sided_faces_begin_frame();
    view_settings_poll(range_state.config, &range_state.effective_view_scale);

    /* STRICT MODE, and it is deliberately the first thing after the poll rather than a branch
     * wrapped around everything below.
     *
     * Every term further down either lowers the scale or raises it, and each has a reason. Strict
     * mode says none of those reasons outrank the number the reader typed, so the honest way to
     * express it is to assign that number here and then decline each term in turn, rather than to
     * skip a block and leave whatever the last frame settled on. A scale lowered by the watchdog
     * before strict was switched on is therefore released on the next frame, which is what a
     * reader turning it on is asking for. */
    if (range_state.config->strict_view_range) {
        range_state.effective_view_scale = range_state.config->view_range_scale;
    }


    /* BEFORE the cell watchdog, and handed that watchdog's own ceiling so it can never give back a
     * scale the watchdog refused. The two lower the same number for different reasons: the cell
     * watchdog to stop the draw table overflowing into the bucket list heads, which is a
     * correctness guard, and the governor because the scale is costing more frame time than it is
     * worth. Correctness outranks comfort, so the governor is the one that has to yield. */
    if (!range_state.config->strict_view_range) {
        frame_governor_on_frame(&range_state.effective_view_scale,
                                range_state.config->view_range_scale, cell_watchdog_ceiling());
    }

    /* The opening window raises the draw distance, and it has to be done HERE, after the governor
     * and before the watchdog.
     *
     * A level's opening is usually an establishing camera set well back from the player, and at
     * the shipped scale the geometry is cut off close enough that the shot is mostly empty ground.
     * The fog is switched off over the same window by fog_regime, so the two are one effect: for
     * these few seconds the game shows as much of the world as it can and hides none of it.
     *
     * Handing the raised number to the governor as a request was the first attempt and it did
     * nothing at all. The governor keeps a ceiling of its own, pinned to the configured scale at
     * install and only ever lowered from there, and it takes the smallest of that, the request and
     * the watchdog's own limit. A request above its ceiling is therefore not a request, and the
     * log said so: "never above the configured 1.00".
     *
     * It stays inside the governor's sampling rather than skipping the call, so the frames these
     * seconds cost are still measured and the counter it times from is not left five seconds
     * stale. What is overridden is only the value, and only upward, so a player already asking for
     * more keeps what they asked for. The watchdog runs after and can still refuse the whole
     * thing, which is the order everything else here uses: correctness outranks comfort. */
    if (fog_regime_level_opening() && !range_state.config->strict_view_range) {
        if (range_state.config->level_open_range > range_state.effective_view_scale) {
            if (!range_state.was_opening) {
                log_info("level opening: the draw distance is held at x%.2f while the fog is off, "
                         "over the x%.2f otherwise in force",
                         (double)range_state.config->level_open_range,
                         (double)range_state.effective_view_scale);
            }
            range_state.effective_view_scale = range_state.config->level_open_range;
        }
        range_state.was_opening = true;
    } else if (range_state.was_opening) {
        range_state.was_opening = false;
        log_info("level opening: the draw distance is back at x%.2f",
                 (double)range_state.effective_view_scale);
    }

    /* A SCRIPTED CAMERA GETS A RADIUS AUTHORED FOR SOMEWHERE ELSE, and this is the correction.
     *
     * bapdraw_drawWorld collects cells in a circle centred on the CAMERA's eye, and takes the
     * radius from bapmat_viewDistance, which reads the override through level+0xA30. That field is
     * the PLAYER's ground-contact block, copied in from player+0x2CC, and it is the only writer in
     * the image. While the player drives, the two agree and nobody notices. A cutscene detaches
     * them: the race opening puts the camera on a clifftop while the player walks the town below,
     * so the camera is handed the town's 22 and everything beyond 22 units of the CLIFF is never
     * gathered at all. Cells cross that edge as the shot moves and the geometry blinks.
     *
     * Measured rather than reasoned: raising this scale by hand pushed the boundary far out and
     * took the blinking with it, while the fog, the frame rate, the field of view, the camera
     * compensation and every buffer in the draw path were each ruled out on their own. The draw
     * path's own ceilings peak between 1 and 33 per cent during the shot, so nothing is failing;
     * the cells are simply never asked for.
     *
     * Only ever upward, after the governor and before the watchdog, exactly like the opening
     * window above: a player already asking for more keeps it, and the watchdog can still refuse
     * the whole thing when the table or the cache is near its limit. */
    if (range_state.config->cutscene_range > 0.0f && !range_state.config->strict_view_range &&
        cinematic_gate_script_owns_camera()) {
        if (range_state.config->cutscene_range > range_state.effective_view_scale) {
            if (!range_state.was_cutscene) {
                log_info("scripted camera: the draw distance is held at x%.2f over the x%.2f "
                         "otherwise in force, because the radius belongs to the player and the "
                         "circle to the camera",
                         (double)range_state.config->cutscene_range,
                         (double)range_state.effective_view_scale);
            }
            range_state.effective_view_scale = range_state.config->cutscene_range;
        }
        range_state.was_cutscene = true;
    } else if (range_state.was_cutscene) {
        range_state.was_cutscene = false;
        log_info("scripted camera: the draw distance is back at x%.2f",
                 (double)range_state.effective_view_scale);
    }

    device_dither_on_frame();
    if (range_state.config->strict_view_range) {
        /* The watchdog still runs, on a copy. It keeps measuring both walls, keeps its own ceiling
         * current for the moment strict mode is switched off again, and keeps warning in the log;
         * what it cannot do is move the number the reader asked for. That is the whole of the
         * setting, and it is the dangerous half: see the key's comment in engine_fixes.ini. */
        float measured_only = range_state.effective_view_scale;

        cell_watchdog_on_frame(&measured_only);
    } else {
        cell_watchdog_on_frame(&range_state.effective_view_scale);
    }
    view_settings_publish_effective_scale(range_state.effective_view_scale);

    /* AFTER the watchdog, deliberately. When the watchdog lowers the scale it moves the cut edge,
     * and the fog eases towards a target computed from the number that is in force, reading it
     * before the watchdog would hand the fog a value one frame out of date at exactly the moment
     * it changes. */
    fog_regime_on_frame();
}

void view_range_install_draw_distance(uintptr_t site)
{
    /* The hook is always installed, even at ViewRangeScale = 1.0. It used to hang off `> 1.0`,
     * which made the conservative setting the UNPROTECTED one: the radius cap
     * 64*sqrt(63/hFOV) lives in the hook body and is the only guard against the 132 authored
     * cells that already run range = 64 in retail. Widening the field of view with a scale of 1.0
     * used to get the full cell count with no brake at all. */
    if (site == 0) {
        log_warning("view_distance did not resolve, the range stays as authored, and the radius "
                    "cap that pays for a wider field of view is NOT active");
        return;
    }

    if (detour_install(&range_state.view_distance_detour, site,
                       (const void *)hook_view_distance, VIEW_DISTANCE_PROLOGUE_SIZE)) {
        log_info("draw distance x%.2f active (%08X); radius cap 64*sqrt(63/hFOV) for ALL three "
                 "callers including the emitter cull, it bites at scale 1.0 too, because a wider "
                 "picture already costs cells on its own",
                 (double)range_state.config->view_range_scale, (unsigned)site);
    } else {
        log_error("the bapmat_viewDistance detour at %08X failed", (unsigned)site);
    }
}

void view_range_install_fov_observer(uintptr_t site)
{
    range_state.horizontal_fov_degrees = AUTHORED_FOV_DEGREES;

    if (site == 0) {
        log_warning("rdcamera_build_projection did not resolve, the field of view cannot be "
                    "observed. The radius cap assumes the authored %.0f degrees and does not "
                    "shorten the range on a widened picture, and the fog therefore does not "
                    "follow a widened picture either, it keeps each level's authored band.",
                    (double)AUTHORED_FOV_DEGREES);
        return;
    }
    if (!detour_install(&range_state.build_projection_detour, site,
                        (const void *)hook_build_projection, BUILD_PROJECTION_PROLOGUE_SIZE)) {
        log_warning("could not observe rdCamera_BuildProjection, the radius cap assumes the "
                    "authored %.0f degrees and the fog keeps each level's authored band",
                    (double)AUTHORED_FOV_DEGREES);
    }
}
