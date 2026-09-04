/* fog_regime.h: which fog the engine computes, and how far it reaches once the picture is wider
 *                 than the one the levels were authored for.
 *
 * ==============================================================================================
 * The cut edge is a circle of cell centres, in world units
 *
 * bapdraw_drawWorld builds an axis-aligned cell rectangle from the four frustum corners at
 * distance `range` and then rejects every cell inside it with one test, at 0x004054B0:
 *
 *     fld  [cellX + 0.5, camX]      dx        (0x00405456..0x004054AE)
 *     fld  [cellZ + 0.5, camZ]      dz
 *     dx*dx + dz*dz
 *     fcomp [esp+0x84]               <- range * range, formed at 0x00404FEB `imul edx, ecx`
 *     test ah,1 / je drop
 *
 * The grid step is ONE WORLD UNIT: every world-to-cell conversion in that function is
 * `floor(coordinate - (-0.5))` (`fsub [0x4A8054]` with [0x4A8054] = -0.5f, then the float-to-int
 * at 0x0049A44C), with no scale factor anywhere. The emitter cull at 0x004221FA settles it
 * independently; it takes the SAME number, converts it with `fild`, and compares it against a
 * real `sqrtf(dx*dx + dz*dz)` in world units (`call 0x0049A390` at 0x004222D7, `fcomp` at
 * 0x004222DF).
 *
 * So `range` is a HORIZONTAL RADIUS in world units, and `world+0x21C` (the fog end) is a depth in
 * world units. They are the same unit and can be compared directly.
 *
 * ==============================================================================================
 * Why a wider picture needs the fog nearer, the one line of geometry this module rests on
 *
 * The circle test is radial; the fog is not. The per-vertex ramp at 0x0040206C walks
 * `z = 1 / rhw`, the VIEW DEPTH along the camera axis, not the radius. A cell centre sitting
 * exactly on the cut circle, at radius R, has view depth
 *
 *     z = R * cos(theta)
 *
 * where theta is its horizontal angle off the view axis. Dead centre that is R; at the left or
 * right edge of the screen it is R * cos(hFOV/2). The nearest place a newly collected cell can
 * appear is therefore the SCREEN EDGE, and it gets nearer as the field of view opens:
 *
 *     no pop-in anywhere on screen   <=>   fogEnd <= R * cos(hFOV/2)
 *
 * At the 60 degrees the game ships with (`push 0x42700000` at 0x00417F79, the first argument of
 * the one and only rdCamera_new call) that factor is 0.866. At 87 degrees it is 0.724. That is
 * the whole of the "wider field of view, nearer fog" rule, and it is geometry rather than taste.
 *
 * And capping the fog that way costs nothing visible. graphics_clearFrame 0x0046C0F5 clears the
 * picture to the colour std3D_setFogColor last wrote ([0x6FC988]/[0x854E50]/[0x6FC980], packed by
 * 0x00487A90). Everything past the cut edge is already exactly fog-coloured. Pulling the fog
 * inside the edge does not hide anything that was visible; it replaces a hard silhouette with a
 * gradient into a colour that is on the screen either way.
 *
 * ==============================================================================================
 * The band is scaled, never just its far end, and that is a correctness rule, not a taste one
 *
 * bapdraw_setFrameState 0x00401E8B forms `end - start` and, when the result is NEGATIVE
 * (`fcomp [0x4A801C]` with [0x4A801C] = 0.0f, `test ah,1`), writes 0 into the "compute the fog
 * yourself" flag [0x4DD6D0] at 0x00401EAA. With that flag clear the emitter no longer copies the
 * ramp's alpha into the vertex: 0x00402459 writes a CONSTANT ZERO into vertex+0x14, and a
 * specular alpha of zero means FULLY FOGGED. The fog render state is still on; it was set nine
 * instructions earlier at 0x00401E28, before the flag was ever considered.
 *
 * So an end pulled below the level's own start does not remove the fog. It paints the entire
 * world in the fog colour. Two shipped levels sit inside that trap: BIGCITY authors start 20.0
 * with a draw distance of 20, and FEDSHIP authors start 16.0 with a draw distance of 18. Any cap
 * that moves the end alone crosses their start.
 *
 * Hence: the whole band is multiplied by one ratio. The authored profile survives exactly, an
 * object at depth d*k gets precisely the fog the level's author gave depth d, and `end - start`
 * keeps its sign for every positive ratio.
 */
#ifndef FOG_REGIME_H
#define FOG_REGIME_H

#include <stdbool.h>
#include <stdint.h>

/* The three answers `inside_cut` selects between. Named because a bare 0/1/2 in an ini and in a
 * comparison is the kind of thing that gets misread as a boolean by whoever comes next. */
#define FOG_END_UNBOUNDED      0
#define FOG_END_NO_POP_IN      1
#define FOG_END_NO_SATURATION  2

typedef struct fog_regime_config {
    /* Capture what the band was computed from, frame by frame, for the first seconds of a level
     * and write the run out in one block. A diagnostic for the reported flashing at level start;
     * off unless somebody is looking. */
    bool  log_band;

    /* The band the opening window holds, in world units, owed to nothing the level says. An end
     * of 0 pushes the band past everything the renderer still has in view, which is the original
     * behaviour and reads as no fog at all. Anything else is laid at exactly these distances from
     * the camera and held there for the whole window, which is the point: it does not follow the
     * cut, the field of view or the level's own band, so it cannot move while the window runs. */
    float open_fog_start;
    float open_fog_end;
    bool  vertex_fog;       /* run the fog on the engine's own per-vertex ramp */
    bool  follow_fov;       /* scale the authored band as the picture widens or the cut moves in */
    /* WHERE THE BAND ENDS RELATIVE TO THE CUT. Three answers to one question, so it is a choice
     * and not a stack: see fog_regime_depth_limit for why two of them can never both hold.
     *   0  neither. The authored band, followed by the field of view if FogFollowFov is on.
     *   1  the no-pop-in cap: end at (cut - margin) * cos(hFOV/2), so a cell arriving at the
     *      frustum CORNER is already fully fogged. Everything nearer is fogged solid too.
     *   2  the no-saturation end: end at (cut + margin), so nothing DRAWN is ever fully fogged.
     */
    int   inside_cut;
    float fog_scale;        /* >1 pushes the authored fog out; 1 leaves it alone */
    float settle_seconds;   /* how long a change takes; 0 means step immediately */
    bool  pixel_fog;        /* hand fog to the device per pixel, where it can measure eye-space w */
    bool  authored_band;    /* use each level's own band untouched, ignoring every term below */
    float min_end_fraction; /* least the fog end may be, as a share of the cut edge; 0 disables */
    float band_scale;       /* the whole band, nearer, after every term above; 1 leaves it */
    float open_seconds;     /* how long a level opens with the fog switched off; 0 disables */
} fog_regime_config_t;

typedef struct fog_regime_band {
    float start;            /* world+0x218 */
    float end;              /* world+0x21C */
} fog_regime_band_t;

/* ==============================================================================================
 * Pure arithmetic. No engine memory, no state; this is what the unit test exercises.
 * ============================================================================================ */

/* The nearest view depth at which a cell on the cut circle can appear, for a cut edge of
 * `cut_units` and a horizontal field of view of `horizontal_fov_degrees`. Zero when the cut edge
 * is too short to mean anything. */
float fog_regime_edge_limit(float horizontal_fov_degrees, float cut_units);

/* The DEEPEST any drawn geometry can be, in view depth, for a radial cut of `cut_units`.
 *
 * Same geometry as the edge limit, z = r * cos(theta), evaluated at the other end of both of its
 * variables. The edge limit asks how SHALLOW newly gathered geometry can arrive, so it takes the
 * screen edge, theta = hFOV/2, and subtracts the cell margin because a dropped cell centre can
 * still hold geometry nearer than itself. This asks how DEEP drawn geometry can reach, so it takes
 * the view axis, theta = 0 where cos is 1, and ADDS the margin because a kept centre can hold
 * geometry further than itself. Hence no field of view argument: the axis is always in the picture.
 *
 * Ending the band here means nothing on screen is ever fully fogged, which matters because a flat
 * region of exactly one colour is what the engine's own level fade bands against on a 16-bit frame
 * buffer, and that was measured as a flashing line where the flat region met lit geometry.
 *
 * The two limits are MUTUALLY EXCLUSIVE: (cut - margin) * cos(hFOV/2) < cut + margin for every
 * legal field of view and every cut, so a rule that capped and then floored would make the cap dead
 * at every field of view on every level. That is why inside_cut selects rather than accumulates. */
float fog_regime_depth_limit(float cut_units);

/* How much of the authored band survives, as a fraction in (0, 1]. EXACTLY 1.0f when the field of
 * view is the one the levels were authored at and the cut edge is where the engine put it; both
 * sides are then the same expression over the same inputs. */
float fog_regime_follow_factor(float horizontal_fov_degrees, float reference_cut, float live_cut);

/* The band the level should be showing right now. `authored` is the level's own band as loaded;
 * this function never reads what is currently in the level record, which is what keeps a repeated
 * call from squaring its own effect. */
void fog_regime_target_band(const fog_regime_config_t *config,
                            const fog_regime_band_t *authored,
                            float horizontal_fov_degrees,
                            float reference_cut,
                            float live_cut,
                            fog_regime_band_t *out);

/* One eased step towards `target`. `seconds` is the engine's own frame delta, so the result after
 * a given amount of REAL time is the same whatever the frame rate. */
float fog_regime_ease(float current, float target, float seconds, float settle_seconds);

/* ==============================================================================================
 * The engine side.
 * ============================================================================================ */

/* Resolves the sites, switches the engine onto its own per-vertex ramp and detours the level-fog
 * apply. Idempotent: a second call does nothing. */
void fog_regime_install(const fog_regime_config_t *config);

/* The horizontal field of view now in force, observed from the camera the engine rebuilt. */
void fog_regime_set_fov(float horizontal_fov_degrees);

/* The Direct3D device this module already resolved, or NULL. Exposed because it is the one place
 * in the project that finds it, and it is read out of an instruction operand rather than written
 * down, so nobody else should be resolving it a second time. */
void *fog_regime_device(void);

/* Switches the authored-band mode while the game runs, which is how the developer overlay's own
 * row reaches it. The next frame's target is computed the new way and eased to like any other
 * change, so the two can be compared without a restart. */
void fog_regime_set_authored_band(bool authored);

/* How near the band sits, as a share of where every other term put it, while the game runs. Below
 * 1 the fog arrives sooner and is denser at a given distance, and it is the last term applied in
 * both modes, so whether the band follows the draw distance decides what this is a share OF and
 * never whether it applies. Ignored when not positive, so a missing or nonsense value leaves the
 * band where it was rather than collapsing it onto the camera.
 *
 * There is deliberately no live switch for the fog DELIVERY beside it. See consider_pixel_fog():
 * going from the device back to the engine's ramp needs the device reprogrammed, and this engine
 * only programs it from inside applyLevelFog, which runs at a level load. That is chosen once,
 * from FogImplementation. */
void fog_regime_set_band_scale(float scale);

/* True while a level is still opening: the window the fog spends switched off, so that the band
 * settling to its real value is not watched happening. Public because the draw distance is raised
 * over the same window and that lives in another file, so the two have to begin and end together.
 */
bool fog_regime_level_opening(void);

/* The cut edge, both as the engine would have had it and as it actually is after the radius cap
 * and the watchdog. Called from the draw-distance detour, which is the only place both numbers
 * exist at once. */
void fog_regime_note_cut(int32_t reference_range, int32_t effective_range);

/* One eased step and one write into the level record. Cheap, and silent unless something moved. */
void fog_regime_on_frame(void);

#endif /* FOG_REGIME_H */
