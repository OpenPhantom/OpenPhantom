/* view_distance_fix.c: how far the world is drawn, how far the fog reaches, and when NPCs come
 * into being.
 *
 * ==============================================================================================
 * What has to be understood before any number here is turned
 *
 * Draw distance and fog are two numbers no engine code connects.
 *     draw distance: world+0x014 (B3D hdr+0x854), overridable per cell by bapCell+0x03
 *     fog:           world+0x218 / +0x21C (B3D hdr+0x90 / +0x94)
 * And graphics_clearFrame 0x46C0F5 clears the picture to the FOG COLOUR. Geometry beyond fogEnd
 * is therefore exactly background-coloured, i.e. invisible. Raising the draw distance alone costs
 * fill rate without a single additional pixel. The two have to move together, and connecting them
 * is what fog_regime.c does.
 *
 * Authored per level (draw distance / fog end):
 *   GUNGA 16/14, GARDEN 22/26, MAUL 28/32, FINAL 24/30, SWAMP 22/30, ESPA 22/32 ,
 *   RACE 22/32, QUEEN 26/38, ASSAULT 23/38, BIGCITY 20/50, FEDSHIP 18/56
 * Only GUNGA is fully fog-covered. In BIGCITY and FEDSHIP the fog ends far BEHIND the geometry,
 * there the cut edge is a visible wall even in the shipped state.
 *
 * Field of view and range multiply. bapdraw_drawWorld collects over a rectangle that grows with
 * tan(fovH/2) and caps it with a circle test at `range`. The cell count goes like
 * (hFOV/360)*pi*r^2, from 63 to 106 degrees that is x1.68 from the field of view alone.
 *
 * And the fog has to reach the screen at all, and it has to follow the cut edge when the cut edge
 * moves. Both of those live in fog_regime.c now: which of the engine's two fog regimes is in
 * force, and how far the band reaches once the radius cap below has shortened the picture. This
 * file feeds it the two numbers only it has, the field of view it observes and the cut edge it
 * computes, and never reaches into the fog itself.
 *
 * NPCs ARE CREATED, not merely drawn. 1127 of 1315 scannable placements (85.7 %) have an
 * activation radius SMALLER than their level's draw distance, the enemy only comes into being
 * once it is well inside the visible picture. That is the pop-in, and it exists at 60 degrees
 * too. This is the ONE change here that touches GAME BEHAVIOUR: an actor created earlier thinks
 * earlier. It therefore ships at 1.0, the engine's own value, and installs nothing.
 *
 * Creating them earlier is not free, and that is why the default came back down. The engine draws
 * actors from two pools fixed at start-up, 128 actors and 255 things, and the activation test is
 * a SPHERE, so a radius multiplied by k multiplies the activated volume by k cubed: 1.25 was very
 * nearly twice as many actors alive at once. A full pool makes the spawn return zero silently,
 * the placement is skipped, and an enemy that should be standing in front of the player is not
 * there at all. spawn_census.c counts exactly that.
 *
 * SIZE NOTE. Past the hard limit, and this change is what took it there. What is long is the byte
 * evidence beside each site rather than the code, which is well inside the normal band, and that
 * evidence is the reason the file reads the way it does.
 *
 * THE SEAM. Reading the settings and polling the handful of them that can change while the game
 * runs is a responsibility of its own: it touches no engine memory, resolves no signature, and is
 * the only part of this file a reader looking for a default cares about. It would take the config
 * struct with it and leave the hooks behind. It is not taken here because this change adds a
 * setting and moving the reader at the same time would mix a refactor into a feature.
 */
#include "view_distance_fix.h"

#include "cell_watchdog.h"
#include "frame_governor.h"
#include "draw_table.h"
#include "fog_regime.h"

#include "poly_bias.h"
#include "device_dither.h"
#include "scene_fade.h"
#include "translucent_fog.h"

#include "common/cinematic_gate.h"
#include "fog_trace.h"
#include "spawn_census.h"
#include "vertex_table.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIEW_DISTANCE_SECTION "view_distance_fix"

/* The measurement switch below lives in the diagnostics section, where every other
 * measurement in this ini lives. Declaring the name here rather than including another
 * feature's header keeps this DLL standing on its own. */
#define DIAGNOSTICS_SECTION   "diagnostics"

/* --- 0x0040E42A  bapmat_viewDistance: THE DRAW DISTANCE --------------------------------------- *
 *   55 / 8B EC / 51 / 8B 45 08        prologue, 7 bytes, clean boundary
 *   8B 48 14                          range = world->viewRange (+0x14, from B3D hdr+0x854)
 *
 * The [2,64] clamp is NOT here; it lives only in the world-walk caller 0x404F33. The other two
 * callers (0x4048F3 an oldcode cheat, 0x4221FA the emitter cull radius) do NOT clamp, which is
 * why our detour clamps itself. */
static const uint8_t SIG_VIEW_DISTANCE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x14, 0x89, 0x4D, 0xFC, 0x8B, 0x55, 0x0C
};
#define VIEW_DISTANCE_PROLOGUE_SIZE 7u

/* --- 0x004371E4  enemy_activationScan: THE ACTIVATION RADIUS ---------------------------------- *
 *   8B 55 F8 / 8B 42 28 / 50          push rec+0x28 = ACTIVE RANGE  -> arg3
 *   8B 4D FC / 83 C1 18 / 51          push playerBody+0x18
 *   8B 55 F8 / 81 C2 AC000000 / 52    push rec+0xAC
 *   E8 <rel32>                        call within_range 0x428EB3    <- at +0x18
 *
 * Only THIS call site is redirected. within_range has a second caller (0x4332F2, an AI query)
 * that must stay untouched. radius == 0 means "always active" and must not become finite. */
static const uint8_t SIG_ACTIVATION_SCAN[] = {
    0x8B, 0x55, 0xF8, 0x8B, 0x42, 0x28, 0x50,
    0x8B, 0x4D, 0xFC, 0x83, 0xC1, 0x18, 0x51,
    0x8B, 0x55, 0xF8, 0x81, 0xC2, 0xAC, 0x00, 0x00, 0x00, 0x52
};
#define OFFSET_ACTIVATION_SCAN_CALL 0x18u

/* --- 0x0040F3F7  rdMesh_draw: THE BACKFACE CULL ---------------------------------------------- *
 *   (before) 8A 1D A0868600   mov bl,[0x8686A0]      <- the cull address, at anchor-4
 *   B8 01000000              mov eax,1
 *   84 D8                    test al,bl
 *   75 32                    jne  -> face DROPPED
 *
 * The DEVICE does not cull at all: 0x489D02 sets D3DRENDERSTATE_CULLMODE to D3DCULL_NONE. The
 * engine culls in SOFTWARE, per face, at this one place. The anchor is DELIBERATELY address-free;
 * the cull address is read from anchor-4 and checked against the image. */
static const uint8_t SIG_MESH_CULL_WORD[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x84, 0xD8, 0x75, 0x32 };
#define OFFSET_CULL_WORD_ADDRESS (-4)

/* --- 0x0040FE70  rdThing_Draw --------------------------------------------------------------- *
 *   83 EC 48 / B9 0C000000            prologue, 8 bytes, clean boundary
 * NO frame pointer: the two cdecl arguments are at [esp+4] / [esp+8] on entry.
 * rdMesh_draw has two callers (0x4100E5 from here, 0x456E17 from shot_drawAll), which is why
 * the detour must RESET the cull word at the end, not merely set it at the start.
 *
 * The pattern reaches eight bytes past the prologue on purpose. The prologue itself is what another
 * DLL's detour overwrites, and dev_overlay does exactly that here, so the site is declared in the
 * DETOUR form and the tail is what identifies it. */
static const uint8_t SIG_THING_DRAW[] = {
    0x83, 0xEC, 0x48, 0xB9, 0x0C, 0x00, 0x00, 0x00, 0x55, 0x8B, 0x6C, 0x24, 0x50, 0x56, 0x8B, 0x74
};
#define THING_DRAW_PROLOGUE_SIZE 8u

/* --- 0x00475FFA  rdCamera_BuildProjection --------------------------------------------------- *
 * We hook it ONLY to observe cam+0x38 after the engine has rebuilt the projection. The radius cap
 * below couples range to the field of view, and asking another DLL for that number would make
 * this DLL depend on it. common/detour.c chains us with whoever else is on this function, so the
 * value we read is always the one that is in force. */
static const uint8_t SIG_RDCAMERA_BUILD_PROJECTION[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x04,
    0x89, 0x4D, 0xFC, 0x83, 0x7D, 0xFC, 0x00
};
#define BUILD_PROJECTION_PROLOGUE_SIZE 6u
#define CAMERA_FOV_DEGREES_INDEX 14   /* +0x38 */

enum {
    SITE_VIEW_DISTANCE,
    SITE_ACTIVATION_SCAN,
    SITE_MESH_CULL_WORD,
    SITE_THING_DRAW,
    SITE_RDCAMERA_BUILD_PROJECTION,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("view_distance",             SIG_VIEW_DISTANCE),
    SIGNATURE_ENTRY("activation_scan",           SIG_ACTIVATION_SCAN),
    SIGNATURE_ENTRY("mesh_cull_word",            SIG_MESH_CULL_WORD),
    /* DETOUR form, because another DLL gets here first. dev_overlay hooks this same function
       for giant and tiny player, and it loads before this one, so by the time this pattern is
       searched the first eight bytes are a jump into its thunk. The plain form found zero
       matches and switched two-sided drawing off in every session the overlay was installed,
       on both platforms, reported only as a warning line nobody was reading. */
    SIGNATURE_ENTRY_DETOUR("thing_draw",         SIG_THING_DRAW, THING_DRAW_PROLOGUE_SIZE),
    SIGNATURE_ENTRY_DETOUR("rdcamera_build_projection", SIG_RDCAMERA_BUILD_PROJECTION,
                           BUILD_PROJECTION_PROLOGUE_SIZE)
};

/* Engine field offsets. */
#define THING_MODEL3        0x004
#define THING_NODE_HIDDEN   0x028   /* int32 per node */
#define THING_MESH_HIDDEN   0x02C   /* int32 per MESH (0x414C1C) */
#define MODEL3_NODE_COUNT   0x054

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
#define MAX_PLAUSIBLE_NODES  1024u

typedef int32_t (__cdecl *view_distance_fn_t)(void *world, uint8_t *out_lod_mask);
typedef int32_t (__cdecl *within_range_fn_t)(const float *a, const float *b, float radius);
/* rdThing_Draw RETURNS A VALUE and this typedef said `void` until 2026-08-07. Three return paths
 * in the retail image: `xor eax,eax` at 0x0040FFD0, `mov eax,1` at 0x00410094 and
 * `mov eax,[ebx+0x54]` at 0x00410126. bapthing_dispatch 0x00417930 is a seven-way jump table whose
 * case 3 is `call 0x40fe70` followed straight by the epilogue, no `mov eax` in between, while
 * every other case does an explicit `xor eax,eax`. So EAX is the contract, and it reaches FOUR
 * callers: 0x004116F1 (bapobj_drawAll, where it becomes `visCode`), 0x00414A2A, 0x00458904 (shot.c)
 * and 0x0045C3B9.
 *
 * With the void typedef the compiler emitted `mov eax,[cull_word]` immediately after the call, so
 * every caller read a POINTER as the visibility code. MEASURED consequence: at the shadow gate that
 * is merely always-non-zero and harmless, but through shot.c the blaster scorch decal was never
 * stamped at all, 0 of them with the hook active, 5 in the same test without it. The direction of
 * the damage at one call site says nothing about the others. */
typedef int32_t (__cdecl *thing_draw_fn_t)(void *thing, void *matrix);
typedef uint32_t (__cdecl *build_projection_fn_t)(int32_t *camera);

typedef struct view_distance_config {
    bool  enabled;
    float view_range_scale;
    bool  frame_backoff;
    bool  strict_view_range;
    float backoff_fps;
    int   fog_inside_cut;
    bool  fog_follow_fov;
    int   fog_implementation;   /* 0 engine untouched, 1 the per-vertex ramp, 2 per pixel */
    bool  authored_fog;
    float fog_min_end;
    float fog_band_scale;
    float level_open_seconds;
    float level_open_range;
    float fog_scale;
    float fog_settle_seconds;
    float npc_range_scale;
    float cutscene_range;
    bool  poly_depth_bias;
    bool  translucent_fog;
    bool  dither;
    float level_fade_seconds;
    bool  log_fog_band;
    float level_open_fog_start;
    float level_open_fog_end;
    bool  two_sided_severed;
    int   two_sided_max;
    bool  relocate_draw_table;
    bool  lower_cell_limit;
    bool  relocate_vertex_table;
    bool  spawn_census;
    bool  log_player_position;
} view_distance_config_t;

typedef struct view_distance_state {
    bool                   installed;
    view_distance_config_t config;

    detour_t               view_distance_detour;
    detour_t               thing_draw_detour;
    detour_t               build_projection_detour;
    within_range_fn_t      engine_within_range;

    /* The EFFECTIVE range scale. It starts at the setting and is only ever lowered by the
     * watchdog. */
    float                  effective_view_scale;

    /* The horizontal field of view currently in force, observed rather than asked for. */
    float                  horizontal_fov_degrees;

    uint8_t               *cull_word;
    int                    two_sided_this_frame;
    bool                   was_opening;
    bool                   was_cutscene;   /* so the window is logged once, not per frame */
} view_distance_state_t;

static view_distance_state_t view_state;

/* ============================================================================================ */
static float clamp_float(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void load_config(void)
{
    view_distance_config_t *config = &view_state.config;

    config->enabled             = ini_read_bool (VIEW_DISTANCE_SECTION, "Enabled", true);
    config->view_range_scale    = ini_read_float(VIEW_DISTANCE_SECTION, "ViewRangeScale", 1.0f);
    config->frame_backoff       = ini_read_bool (VIEW_DISTANCE_SECTION, "FrameBackoff", true);
    config->strict_view_range   = ini_read_bool (VIEW_DISTANCE_SECTION, "StrictViewRange", false);
    config->backoff_fps         = ini_read_float(VIEW_DISTANCE_SECTION, "BackoffFps", 0.0f);
    /* Read as a NUMBER, and an old ini carrying 1 keeps exactly the behaviour its owner has been
     * running. 2 is the shipped answer: see fog_regime_depth_limit. */
    config->fog_inside_cut      = ini_read_int  (VIEW_DISTANCE_SECTION, "FogInsideCut",
                                                 FOG_END_NO_SATURATION);
    if (config->fog_inside_cut < FOG_END_UNBOUNDED ||
        config->fog_inside_cut > FOG_END_NO_SATURATION) {
        log_warning("FogInsideCut=%d is not one of 0, 1 or 2, so the band ends just beyond the cut "
                    "as it does by default", config->fog_inside_cut);
        config->fog_inside_cut = FOG_END_NO_SATURATION;
    }
    config->fog_follow_fov      = ini_read_bool (VIEW_DISTANCE_SECTION, "FogFollowFov", true);
    config->fog_scale           = ini_read_float(VIEW_DISTANCE_SECTION, "FogScale", 0.0f);
    config->fog_settle_seconds  = ini_read_float(VIEW_DISTANCE_SECTION, "FogSettleSeconds", 1.5f);
    /* Which half of the engine draws the fog, and the one fog setting that is read here and never
     * again. The two implementations differ in device state that this engine only programs from
     * inside applyLevelFog, which runs at a level load and nowhere else, so a switch made while a
     * level is up leaves the device and the engine disagreeing and nothing is fogged. Two attempts
     * at making that safe both failed in the game, so it is not offered live: the panel carries
     * the two band switches, which are pure arithmetic, and this one waits for a restart. */
    config->fog_implementation  = ini_read_int  (VIEW_DISTANCE_SECTION, "FogImplementation", 2);
    if (config->fog_implementation < 0 || config->fog_implementation > 2) {
        log_warning("FogImplementation=%d is not one of 0, 1 or 2, so 2 is used",
                    config->fog_implementation);
        config->fog_implementation = 2;
    }
    config->authored_fog        = ini_read_bool (VIEW_DISTANCE_SECTION, "AuthoredFogBand", false);
    config->fog_min_end         = ini_read_float(VIEW_DISTANCE_SECTION, "FogMinEndFraction", 1.0f);
    config->fog_band_scale      = ini_read_float(VIEW_DISTANCE_SECTION, "FogBandScale", 1.0f);
    config->level_open_seconds  = clamp_float(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelOpenSeconds", 0.0f), 0.0f, 30.0f);
    config->level_open_range    = clamp_float(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelOpenViewRange", 2.5f), 1.0f, 2.5f);
    /* 1.0, which is the engine's own activation distance and installs no patch at all.
     *
     * The test this would scale is a plain squared distance in three dimensions (0x00428EB3:
     * three subtractions, three multiplies, one compare against radius*radius). There is no view
     * direction in it. A scale above 1 therefore does not open the picture sideways, it creates
     * every actor earlier in EVERY direction, straight ahead included, which is a change to how
     * the game plays.
     *
     * What made the pop-in visible is that this project widens the field of view: 60 degrees
     * horizontal at 4:3 becomes 75.2 at 16:9, so the picture reaches sideways into ground the
     * original could never show, and placements there are in view while their authored activation
     * distance has not been reached. The engine is not behaving differently, it is being watched
     * from further round the corner. Hiding that by creating actors early trades a cosmetic
     * problem for a behavioural one, and the original rule wins. */
    config->npc_range_scale     = ini_read_float(VIEW_DISTANCE_SECTION, "NpcRangeScale", 1.0f);
    config->cutscene_range      = clamp_float(
        ini_read_float(VIEW_DISTANCE_SECTION, "CutsceneViewRange", 0.0f), 0.0f, 2.5f);
    config->poly_depth_bias     = ini_read_bool (VIEW_DISTANCE_SECTION, "PolyDepthBias", true);
    config->translucent_fog     = ini_read_bool (VIEW_DISTANCE_SECTION, "TranslucentFog", false);
    config->dither              = ini_read_bool (VIEW_DISTANCE_SECTION, "Dither", false);
    config->level_fade_seconds  = clamp_float(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelFadeSeconds", 0.4f), 0.0f, 10.0f);
    config->log_fog_band        = ini_read_bool (VIEW_DISTANCE_SECTION, "LogFogBand", false);
    config->level_open_fog_start = clamp_float(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelOpenFogStart", 0.0f), 0.0f, 4000.0f);
    config->level_open_fog_end   = clamp_float(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelOpenFogEnd", 0.0f), 0.0f, 4000.0f);
    config->two_sided_severed   = ini_read_bool (VIEW_DISTANCE_SECTION, "TwoSidedSevered", false);
    config->two_sided_max       = ini_read_int  (VIEW_DISTANCE_SECTION, "TwoSidedMax", 8);
    config->relocate_draw_table = ini_read_bool (VIEW_DISTANCE_SECTION, "RelocateDrawTable", true);
    config->lower_cell_limit    = ini_read_bool (VIEW_DISTANCE_SECTION, "LowerCellLimit", true);
    /* WALL 2 in cell_watchdog.h: the vertex cache. Doubling it the same ratio draw_table.c already
     * field-proved for the cell table (16384 -> 32768 slots, 1 MiB -> 2 MiB) so the three gates
     * that abort cleanly today have real headroom before ANY of the 132 authored range=64 cells or
     * a wider field of view can trip them. */
    config->relocate_vertex_table = ini_read_bool (VIEW_DISTANCE_SECTION, "RelocateVertexCache",
                                                    true);
    /* Read out of the diagnostics section rather than this one, because that is where every
     * measurement switch in this file lives and a reader looking for one should find them
     * together. It is only an ini key: this DLL still has no run-time dependency on the
     * diagnostics DLL, and it works whether or not that DLL is installed at all. */
    config->spawn_census        = ini_read_bool (DIAGNOSTICS_SECTION, "Spawns", false);

    config->log_player_position =
        ini_read_bool (VIEW_DISTANCE_SECTION, "LogPlayerPosition", false);

    /* THIS WAS RAISED TO 4.0 ONCE, ON REASONING THAT FIELD TESTING THEN DISPROVED. Kept here
     * rather than quietly reverted, because the reasoning was wrong in a way worth remembering.
     *
     * The argument was: RelocateDrawTable makes the 16384-entry cell table safe with a proven
     * 1.93x reserve, cell_watchdog_budget() already folds that into the radius cap, and the
     * remaining wall - the 16384-slot vertex cache - is watched in real time with an alarm at 75%,
     * earlier than the cells' 90%. All of that is true and none of it was enough: at 3.0 and 4.0
     * the game showed exactly the failure cell_watchdog.c's own comments already named -
     * "torn geometry until the level reloads" - and it did not self-correct.
     *
     * What the argument missed: the counters do not climb, they JUMP. cell_watchdog.c documents
     * this for cells - "the counter jumped from under 7680 to 8189 in ONE frame, the gentle
     * back-off never got its turn, only the emergency brake" - and the same is true of the vertex
     * cache, turning a corner into open geometry. The watchdog's backoff helps the NEXT frame; it
     * cannot undo the frame that already overshot, and a vertex-cache overshoot does not clear
     * itself the way a cell-table one does. A larger ViewRangeScale does not make that jump safer,
     * it makes the jump BIGGER, which is the opposite of what real-time coverage alone could fix.
     *
     * SECOND ATTEMPT, and the difference from the first is not more reasoning about the existing
     * wall, it is that the wall itself moved. RelocateVertexCache=1 (default, vertex_table.c) is
     * no longer a real-time watch on a fixed 16384-slot ceiling; it is a relocated 32768-slot
     * buffer, and a field session confirmed the relocation itself: engine_fixes.log shows all
     * 15/15 operands written and the watchdog's alarm rescaled to 24576, then a played session
     * with a widened FOV, thousands of decals and nearly 4800 mover poses produced not one
     * VERTEX CACHE FULL line. That is evidence the relocation WORKS, not evidence 2.5 is safe -
     * the counter still jumps rather than climbs, and this ceiling has been wrong once already on
     * an argument that sounded just as sound. So: one step, to 2.5, not back to 4.0, and it stays
     * here pending its own field test rather than being trusted on the strength of this one. */
    config->view_range_scale = clamp_float(config->view_range_scale, 1.0f, 2.5f);
    config->npc_range_scale  = clamp_float(config->npc_range_scale, 1.0f, 2.0f);
    if (config->fog_scale <= 0.0f) {
        config->fog_scale = config->view_range_scale;
    }
    config->fog_scale = clamp_float(config->fog_scale, 1.0f, 4.0f);
    /* Zero is a legal setting and means "step immediately", so the lower bound is 0 and not the
     * usual minimum. Ten seconds is long enough that anything above it is a typing mistake. */
    if (!(config->fog_settle_seconds >= 0.0f)) { config->fog_settle_seconds = 0.0f; }
    config->fog_settle_seconds = clamp_float(config->fog_settle_seconds, 0.0f, 10.0f);
    if (config->two_sided_max < 1)  { config->two_sided_max = 1; }
    if (config->two_sided_max > 64) { config->two_sided_max = 64; }
}

/* ============================================================================================
 * The field-of-view observer. See the site comment for why this DLL reads it itself.
 * ============================================================================================ */
static uint32_t __cdecl hook_build_projection(int32_t *camera)
{
    build_projection_fn_t original =
        (build_projection_fn_t)view_state.build_projection_detour.original;
    uint32_t result = original(camera);

    if (camera != NULL) {
        float degrees = *(const float *)&camera[CAMERA_FOV_DEGREES_INDEX];
        if (degrees > 1.0f && degrees < 180.0f) {
            view_state.horizontal_fov_degrees = degrees;
            fog_regime_set_fov(degrees);
        }
    }
    return result;
}

/* ============================================================================================
 * A, the draw distance
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
    float hfov = view_state.horizontal_fov_degrees;
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

    if (view_state.effective_view_scale <= 1.0f) {
        /* Even without a scale: the field of view alone already costs cells. */
        if ((float)range > limit) {
            range = (int32_t)limit;
        }
    } else {
        scaled = (float)range * view_state.effective_view_scale + 0.5f;
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
    view_distance_fn_t original = (view_distance_fn_t)view_state.view_distance_detour.original;
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
    if (view_state.effective_view_scale <= view_state.config.view_range_scale ||
        view_state.was_cutscene) {
        /* The cutscene raise is the one case where the fog SHOULD follow the raised edge. The
         * level opening it was written for hid the fog entirely, so reporting a raised cut there
         * only poisoned the band the level settled to afterwards. A scripted camera keeps its fog,
         * and a band left at the unraised edge would fog solid at 21 while the engine draws to 55,
         * which is the raise bought and then thrown away. */
        fog_regime_note_cut(engine_range, range);
    }
    return range;
}

/* ============================================================================================
 * B, the NPC activation radius
 * ============================================================================================ */
static int32_t __cdecl hook_within_range(const float *a, const float *b, float radius)
{
    /* radius == 0 means "always active" in the engine (0x4371D7 compares against 0.0f), the
     * scaling must not turn that into a finite radius. */
    if (radius > 0.0f) {
        radius *= view_state.config.npc_range_scale;
    }
    return view_state.engine_within_range(a, b, radius);
}

/* ============================================================================================
 * C, two-sided faces, but ONLY on dismembered bodies
 *
 * Drawing two-sided globally would be the simpler patch (two bytes) but it is the wrong default:
 * the frame pools g_queuePoly (4096 records) and g_queueVert (8192 vertices) are GLOBAL, not per
 * asset. The backface pass throws away roughly half of everything today, which is exactly what
 * keeps the shipped game with its ~36 simultaneous actors of ~165 faces below the limit. If the
 * vertex buffer overflows, bapdraw_reserveVerts returns NULL and rdMesh_draw aborts silently: a
 * WHOLE MODEL disappears.
 *
 * So per object, and SHIPPED OFF. The marking needs no bookkeeping of its own, a thing with a set
 * entry in pMeshHidden has a hole, and that is the severed piece. It is off by default because
 * this feature never once ran in a released build: dev_overlay hooks rdThing_Draw for giant and
 * tiny player and loads first, so the plain signature form below found nothing and the warning
 * that said so went unread for months. The first session in which it did run drew a beam across
 * the level, and one evening of testing is not enough to put it back on by default.
 *
 * WARNING: the word has to be RESET at the end. rdMesh_draw has a second caller (0x456E17 in
 * shot_drawAll) which would otherwise see the value of the last drawn thing.
 * HONEST: this does not close the hole, it softens it. A severed limb is not a cut mesh,
 * bapobj_detachNode only hides a node, there is no cap and no cut mesh. Two-sided means you see
 * the inside of the far side, lit with the front normal, i.e. flat.
 * ============================================================================================ */

/* A thing has a hole when an entry in pMeshHidden is set, and NOT when one in pNodeHidden is.
 *
 * It tested both until the beam. pNodeHidden is ordinary engine bookkeeping with nothing to do
 * with dismemberment: Plr_RebindWeaponModel calls bapobj_hideNodeChildren on the weapon mount,
 * node name id 7, which is the HAND, once on spawn and again on every weapon change, and menu.c
 * does the same to the inventory preview. Every armed actor in the game therefore carries a set
 * entry in +0x28 from the moment it spawns, this predicate called all of them severed, and the
 * player was drawn with backface culling off for the whole session. On screen that is a long
 * bright sliver out of Obi-Wan's hand that follows him when he walks.
 *
 * pMeshHidden is reached only through bapobj_hideMeshesBelow, which only bapobj_detachNode and
 * the reattach beside it call, so a set entry there does mean a cut. The narrowing costs the
 * corpse: detachNode marks the PIECE through +0x2C and the body it came off through +0x28, so
 * the piece keeps its two sides and the body loses them. That is the half worth having, and the
 * half that cannot be mistaken for a holstered blaster.
 * Both fields are allocated by rdThing_SetModel with numNodes*4 and zeroed; maxMeshIdx < numNodes
 * holds in 265/265 measured models, so numNodes bounds the mesh array too. */
static bool thing_has_hole(const void *thing)
{
    const char *record = (const char *)thing;
    const char *model;
    const char *hidden;
    uint32_t    node_count;
    uint32_t    index;

    /* Every read below is the faulting form rather than the asking one, and that is a performance
     * decision with a measurable size. This function runs for every thing the engine draws, and it
     * makes four of these reads each time; at three dozen actors that is a couple of hundred system
     * calls per frame for nothing but permission to look. The guarantee is unchanged: a bad pointer
     * still refuses rather than killing the process. */
    if (record == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)(record + THING_MODEL3), &model, sizeof(model)) ||
        model == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)(model + MODEL3_NODE_COUNT), &node_count, sizeof(node_count)) ||
        node_count > MAX_PLAUSIBLE_NODES) {
        return false;                              /* plausibility, never read blind */
    }

    if (memory_try_read((uintptr_t)(record + THING_MESH_HIDDEN), &hidden, sizeof(hidden)) &&
        hidden != NULL &&
        memory_try_readable((uintptr_t)hidden, node_count * sizeof(uint32_t))) {
        for (index = 0; index < node_count; ++index) {
            if (((const uint32_t *)hidden)[index] != 0) {
                return true;
            }
        }
    }

    return false;
}

static int32_t __cdecl hook_thing_draw(void *thing, void *matrix)
{
    thing_draw_fn_t original = (thing_draw_fn_t)view_state.thing_draw_detour.original;
    uint8_t         saved;
    int32_t         result;

    if (!view_state.config.two_sided_severed || view_state.cull_word == NULL) {
        return original(thing, matrix);
    }

    saved = *view_state.cull_word;
    if (view_state.two_sided_this_frame < view_state.config.two_sided_max &&
        thing_has_hole(thing)) {
        ++view_state.two_sided_this_frame;
        *view_state.cull_word = (uint8_t)(saved & ~1u);   /* clear bit 0 = draw backfaces */
    }

    result = original(thing, matrix);              /* KEEP IT: it is the caller's visibility code */

    *view_state.cull_word = saved;                 /* ALWAYS back, see shot_drawAll */
    return result;
}

/* ============================================================================================ */
/* How often the ViewRangeScale key is re-read, in frames. The developer overlay writes that key
 * when its draw distance row is committed, and this is how the change reaches a running game.
 *
 * WHY A POLL AND NOT A CALL. The overlay lives in its own DLL, and feature DLLs in this project
 * never depend on each other at run time: any one of them can be deleted from mods\ without
 * breaking the others. The ini is a channel both already have and neither owns.
 *
 * WHAT IT COSTS. One profile read a second. That is a file the operating system has cached and is
 * measured in tens of microseconds, so amortised across sixty frames it is well under a microsecond
 * each. Worth stating rather than assuming, since this project has already been caught once by a
 * cheap looking call inside a per-frame path, but a once-a-second read is a different order of
 * thing from a per-object syscall. */
#define SCALE_POLL_FRAMES 60u

/* Re-reads the setting and adopts it when it has changed. Assigning the config value is not enough
 * on its own: effective_view_scale is what the range hook actually multiplies by, and the watchdog
 * only ever lowers it, so a raise has to reset it. Lowering the setting resets it too, which hands
 * the watchdog a fresh start rather than leaving it braked from a scale that is no longer set. */
static void poll_view_range_scale(void)
{
    static uint32_t frames;
    float           requested;

    if (++frames < SCALE_POLL_FRAMES) {
        return;
    }
    frames = 0;

    /* The automation's own switch, read on the same schedule and for the same reason: the overlay
     * writes it to the ini and this is where a running game notices. */
    {
        bool wanted = ini_read_bool(VIEW_DISTANCE_SECTION, "FrameBackoff",
                                    view_state.config.frame_backoff);

        if (wanted != view_state.config.frame_backoff) {
            view_state.config.frame_backoff = wanted;
            frame_governor_set_enabled(wanted, view_state.config.view_range_scale);
        }
    }

    /* Strict mode, on the same schedule, so the overlay's row takes effect within the second like
     * every other row rather than at the next level. Nothing is configured when it changes: the
     * frame tick reads it directly, and the governor and the watchdog are left installed and
     * measuring so that turning it back off restores their judgement rather than a stale one. */
    {
        bool wanted = ini_read_bool(VIEW_DISTANCE_SECTION, "StrictViewRange",
                                    view_state.config.strict_view_range);

        if (wanted != view_state.config.strict_view_range) {
            view_state.config.strict_view_range = wanted;
            if (wanted) {
                log_warning("StrictViewRange=1: the draw distance is now held at exactly "
                            "ViewRangeScale=%.2f and NOTHING will lower it. The cell watchdog "
                            "still measures and still warns in this file, but it can no longer "
                            "act, and the draw table overflowing writes over the bucket list "
                            "heads rather than stopping. See the key's own comment in "
                            "engine_fixes.ini before leaving this on.",
                            (double)view_state.config.view_range_scale);
            } else {
                log_info("StrictViewRange=0: the frame governor and the cell watchdog have their "
                         "say over the draw distance again. Any ceiling the watchdog had imposed "
                         "before strict mode was switched on still stands, because a brake applied "
                         "to avoid an overflow holds for the rest of the level.");
            }
        }
    }

    /* And which fog band to compute, on the same schedule and through the same channel. */
    {
        bool authored = ini_read_bool(VIEW_DISTANCE_SECTION, "AuthoredFogBand",
                                      view_state.config.authored_fog);

        if (authored != view_state.config.authored_fog) {
            view_state.config.authored_fog = authored;
            fog_regime_set_authored_band(authored);
        }
    }

    /* How near the band sits, read on the same schedule so it can be tuned with the game up.
     * That is the whole point of polling this one: the right number is a matter of looking at it,
     * and a restart between each try makes that a long evening. */
    {
        float scale = clamp_float(ini_read_float(VIEW_DISTANCE_SECTION, "FogBandScale",
                                                 view_state.config.fog_band_scale), 0.25f, 1.0f);

        if (scale != view_state.config.fog_band_scale) {
            view_state.config.fog_band_scale = scale;
            fog_regime_set_band_scale(scale);
        }
    }

    requested = clamp_float(ini_read_float(VIEW_DISTANCE_SECTION, "ViewRangeScale",
                                           view_state.config.view_range_scale), 1.0f, 2.5f);
    if (requested == view_state.config.view_range_scale) {
        return;
    }

    log_info("ViewRangeScale changed on disk, %.2f -> %.2f, adopting it",
             (double)view_state.config.view_range_scale, (double)requested);
    view_state.config.view_range_scale = requested;
    view_state.effective_view_scale = requested;
    /* Both watchdogs start again from here. The reader has just said what they want, and either of
     * them still braked from a setting nobody is asking for any more would quietly ignore it. */
    cell_watchdog_reset_ceiling();
    frame_governor_reset(requested);
}

/* The draw distance actually in force, published for the panel to show.
 *
 * The panel writes ViewRangeScale and cannot see what happens to it afterwards. Two guards lower
 * it: the frame governor when a scene costs too much, and the cell watchdog when the draw table or
 * the vertex cache is close to overflowing. On Coruscant the watchdog can pin it at 1.00 for the
 * whole level, and until this the panel went on showing the number that had been typed while the
 * game ran something else, with nothing anywhere saying so.
 *
 * Through the ini because that is the channel these two DLLs already share and neither owns. It is
 * written only when the value actually changes, which is a step of the governor every ten seconds
 * at worst and an alarm from the watchdog, so this is not a file write per frame. The key is
 * output only: nothing reads it back into the engine. */
static void publish_effective_scale(float scale)
{
    static float published = -1.0f;

    if (published >= 0.0f && scale > published - 0.005f && scale < published + 0.005f) {
        return;
    }
    published = scale;
    (void)ini_write_float(VIEW_DISTANCE_SECTION, "EffectiveViewRange", scale, 2);
}

static void on_frame(void)
{

    view_state.two_sided_this_frame = 0;
    poll_view_range_scale();

    /* STRICT MODE, and it is deliberately the first thing after the poll rather than a branch
     * wrapped around everything below.
     *
     * Every term further down either lowers the scale or raises it, and each has a reason. Strict
     * mode says none of those reasons outrank the number the reader typed, so the honest way to
     * express it is to assign that number here and then decline each term in turn, rather than to
     * skip a block and leave whatever the last frame settled on. A scale lowered by the watchdog
     * before strict was switched on is therefore released on the next frame, which is what a
     * reader turning it on is asking for. */
    if (view_state.config.strict_view_range) {
        view_state.effective_view_scale = view_state.config.view_range_scale;
    }


    /* BEFORE the cell watchdog, and handed that watchdog's own ceiling so it can never give back a
     * scale the watchdog refused. The two lower the same number for different reasons: the cell
     * watchdog to stop the draw table overflowing into the bucket list heads, which is a
     * correctness guard, and the governor because the scale is costing more frame time than it is
     * worth. Correctness outranks comfort, so the governor is the one that has to yield. */
    if (!view_state.config.strict_view_range) {
        frame_governor_on_frame(&view_state.effective_view_scale,
                                view_state.config.view_range_scale, cell_watchdog_ceiling());
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
    if (fog_regime_level_opening() && !view_state.config.strict_view_range) {
        if (view_state.config.level_open_range > view_state.effective_view_scale) {
            if (!view_state.was_opening) {
                log_info("level opening: the draw distance is held at x%.2f while the fog is off, "
                         "over the x%.2f otherwise in force",
                         (double)view_state.config.level_open_range,
                         (double)view_state.effective_view_scale);
            }
            view_state.effective_view_scale = view_state.config.level_open_range;
        }
        view_state.was_opening = true;
    } else if (view_state.was_opening) {
        view_state.was_opening = false;
        log_info("level opening: the draw distance is back at x%.2f",
                 (double)view_state.effective_view_scale);
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
    if (view_state.config.cutscene_range > 0.0f && !view_state.config.strict_view_range &&
        cinematic_gate_script_owns_camera()) {
        if (view_state.config.cutscene_range > view_state.effective_view_scale) {
            if (!view_state.was_cutscene) {
                log_info("scripted camera: the draw distance is held at x%.2f over the x%.2f "
                         "otherwise in force, because the radius belongs to the player and the "
                         "circle to the camera",
                         (double)view_state.config.cutscene_range,
                         (double)view_state.effective_view_scale);
            }
            view_state.effective_view_scale = view_state.config.cutscene_range;
        }
        view_state.was_cutscene = true;
    } else if (view_state.was_cutscene) {
        view_state.was_cutscene = false;
        log_info("scripted camera: the draw distance is back at x%.2f",
                 (double)view_state.effective_view_scale);
    }

    device_dither_on_frame();
    if (view_state.config.strict_view_range) {
        /* The watchdog still runs, on a copy. It keeps measuring both walls, keeps its own ceiling
         * current for the moment strict mode is switched off again, and keeps warning in the log;
         * what it cannot do is move the number the reader asked for. That is the whole of the
         * setting, and it is the dangerous half: see the key's comment in engine_fixes.ini. */
        float measured_only = view_state.effective_view_scale;

        cell_watchdog_on_frame(&measured_only);
    } else {
        cell_watchdog_on_frame(&view_state.effective_view_scale);
    }
    publish_effective_scale(view_state.effective_view_scale);

    /* AFTER the watchdog, deliberately. When the watchdog lowers the scale it moves the cut edge,
     * and the fog eases towards a target computed from the number that is in force, reading it
     * before the watchdog would hand the fog a value one frame out of date at exactly the moment
     * it changes. */
    fog_regime_on_frame();
}

/* ============================================================================================ */
static void install_view_distance(void)
{
    uintptr_t site = sites[SITE_VIEW_DISTANCE].address;

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

    if (detour_install(&view_state.view_distance_detour, site,
                       (const void *)hook_view_distance, VIEW_DISTANCE_PROLOGUE_SIZE)) {
        log_info("draw distance x%.2f active (%08X); radius cap 64*sqrt(63/hFOV) for ALL three "
                 "callers including the emitter cull, it bites at scale 1.0 too, because a wider "
                 "picture already costs cells on its own",
                 (double)view_state.config.view_range_scale, (unsigned)site);
    } else {
        log_error("the bapmat_viewDistance detour at %08X failed", (unsigned)site);
    }
}

static void install_npc_range(void)
{
    uintptr_t site = sites[SITE_ACTIVATION_SCAN].address;
    uintptr_t call_site;
    uintptr_t target;

    if (view_state.config.npc_range_scale <= 1.0f) {
        log_info("NpcRangeScale=1 - NPCs appear as in the original");
        return;
    }
    if (site == 0) {
        log_warning("activation_scan did not resolve - NPCs keep appearing inside the clear "
                    "picture");
        return;
    }

    call_site = site + OFFSET_ACTIVATION_SCAN_CALL;
    if (!patch_read_call_target(call_site, &target)) {
        log_warning("no usable E8 at %08X - refused", (unsigned)call_site);
        return;
    }
    view_state.engine_within_range = (within_range_fn_t)target;

    if (patch_redirect_call(call_site, (const void *)hook_within_range) == PATCH_RESULT_OK) {
        log_info("NPC activation radius x%.2f (only the call site %08X; the second caller of "
                 "%08X is untouched). This is the only change here that touches GAME BEHAVIOUR.",
                 (double)view_state.config.npc_range_scale, (unsigned)call_site,
                 (unsigned)target);
    } else {
        log_error("the call site %08X could not be written", (unsigned)call_site);
        view_state.engine_within_range = NULL;
    }
}

static void install_two_sided(void)
{
    uintptr_t cull_site = sites[SITE_MESH_CULL_WORD].address;
    uintptr_t draw_site = sites[SITE_THING_DRAW].address;
    uint32_t  cull_address;

    if (!view_state.config.two_sided_severed) {
        log_info("TwoSidedSevered=0");
        return;
    }
    if (cull_site == 0) {
        log_warning("mesh_cull_word did not resolve, dismembered bodies stay see-through");
        return;
    }
    if (!memory_read_u32((uintptr_t)((intptr_t)cull_site + OFFSET_CULL_WORD_ADDRESS),
                         &cull_address) ||
        !memory_is_inside_image(cull_address, sizeof(uint8_t))) {
        log_warning("the cull word %08X is outside the image, refused", (unsigned)cull_address);
        return;
    }
    if (draw_site == 0) {
        log_warning("thing_draw did not resolve - two-sided is OFF (a cull word with no writer "
                    "would be worse than none)");
        return;
    }

    view_state.cull_word = (uint8_t *)(uintptr_t)cull_address;
    if (detour_install(&view_state.thing_draw_detour, draw_site,
                       (const void *)hook_thing_draw, THING_DRAW_PROLOGUE_SIZE)) {
        log_info("dismembered bodies are drawn two-sided (cull word %08X, hook %08X), at most %d "
                 "per frame. The marking uses pNodeHidden and pMeshHidden, no extra state.",
                 (unsigned)cull_address, (unsigned)draw_site, view_state.config.two_sided_max);
    } else {
        view_state.cull_word = NULL;
        log_error("the rdThing_Draw detour at %08X failed - two-sided is OFF", (unsigned)draw_site);
    }
}

static void install_fov_observer(void)
{
    uintptr_t site = sites[SITE_RDCAMERA_BUILD_PROJECTION].address;

    view_state.horizontal_fov_degrees = AUTHORED_FOV_DEGREES;

    if (site == 0) {
        log_warning("rdcamera_build_projection did not resolve, the field of view cannot be "
                    "observed. The radius cap assumes the authored %.0f degrees and does not "
                    "shorten the range on a widened picture, and the fog therefore does not "
                    "follow a widened picture either, it keeps each level's authored band.",
                    (double)AUTHORED_FOV_DEGREES);
        return;
    }
    if (!detour_install(&view_state.build_projection_detour, site,
                        (const void *)hook_build_projection, BUILD_PROJECTION_PROLOGUE_SIZE)) {
        log_warning("could not observe rdCamera_BuildProjection, the radius cap assumes the "
                    "authored %.0f degrees and the fog keeps each level's authored band",
                    (double)AUTHORED_FOV_DEGREES);
    }
}

/* The fog is a responsibility of its own and lives in fog_regime.c. All this does is hand it the
 * settings; the two live numbers reach it from the hooks above. */
static void install_fog_regime(void)
{
    fog_regime_config_t fog_config;

    /* 0 leaves the engine as the device asks for it, which on modern hardware is no fog at all.
     * 1 and 2 both arm the ramp first, so a device that turns out not to support per-pixel fog
     * degrades to the ramp rather than to nothing. */
    fog_config.vertex_fog     = view_state.config.fog_implementation >= 1;
    fog_config.pixel_fog      = view_state.config.fog_implementation == 2;
    fog_config.authored_band  = view_state.config.authored_fog;
    fog_config.min_end_fraction = clamp_float(view_state.config.fog_min_end, 0.0f, 1.0f);
    fog_config.band_scale       = clamp_float(view_state.config.fog_band_scale, 0.25f, 1.0f);
    fog_config.open_seconds     = view_state.config.level_open_seconds;
    fog_config.follow_fov     = view_state.config.fog_follow_fov;
    fog_config.inside_cut     = view_state.config.fog_inside_cut;
    fog_config.fog_scale      = view_state.config.fog_scale;
    fog_config.settle_seconds = view_state.config.fog_settle_seconds;
    fog_config.log_band       = view_state.config.log_fog_band;
    fog_config.open_fog_start = view_state.config.level_open_fog_start;
    fog_config.open_fog_end   = view_state.config.level_open_fog_end;

    fog_regime_install(&fog_config);
}

/* ============================================================================================ */
void view_distance_fix_install(void)
{
    bool watchdog_ok;

    log_init("view_distance_fix", false);

    if (view_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }

    load_config();
    if (!view_state.config.enabled) {
        log_info("Enabled=0, draw distance, fog and NPC activation stay as they shipped");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    view_state.installed = true;
    view_state.effective_view_scale = view_state.config.view_range_scale;

    /* The frame hook belongs here, not only in the frame-rate DLL. on_frame carries the cell
     * watchdog AND the two-sided reset; without it the watchdog is mute and two-sidedness is off
     * for the whole process after eight things, while the log a few lines further down would
     * cheerfully say "watchdog active". Exactly the kind of silent failure this project has paid
     * for three times. */
    if (!frame_hook_add(on_frame)) {
        log_warning("no per-frame hook - NO cell watchdog. The draw distance is therefore held at "
                    "1.0, because an overflow would silently overwrite the bucket list heads.");
        view_state.config.view_range_scale = 1.0f;
        view_state.effective_view_scale = 1.0f;
    }

    /* AFTER the frame hook is settled, so that the scale it is told about is the one that survived
     * the branch above: with no hook the range is pinned to 1.0 and there is nothing for a
     * governor to give back. */
    frame_governor_configure(view_state.config.frame_backoff, view_state.config.backoff_fps,
                             view_state.config.view_range_scale);

    install_fov_observer();

    /* ORDER: the watchdog resolves its counters against the very operands the relocation
     * rewrites, so it must run FIRST. See draw_table.h. */
    watchdog_ok = cell_watchdog_install(view_state.config.lower_cell_limit,
                                        view_state.config.relocate_draw_table);
    if (!watchdog_ok) {
        view_state.config.view_range_scale = 1.0f;
        view_state.effective_view_scale = 1.0f;
    }

    if (view_state.config.relocate_draw_table) {
        draw_table_relocate();
        if (draw_table_is_active()) {
            cell_watchdog_set_limit(draw_table_limit());
        }
    }

    /* Same ordering rule as the draw table just above, and for the identical reason: the watchdog
     * has already resolved the vertex counter by this point, and this relocation touches none of
     * the operands that resolution reads. */
    if (view_state.config.relocate_vertex_table) {
        vertex_table_relocate();
        if (vertex_table_is_active()) {
            cell_watchdog_set_vertex_limit(vertex_table_limit());
        }
    }

    /* ORDER: the fog reads the cut edge the draw-distance detour reports, so that detour has to be
     * standing before the first frame the fog ticks on. */
    install_view_distance();
    install_fog_regime();
    install_npc_range();
    if (view_state.config.cutscene_range > 0.0f) {
        (void)cinematic_gate_install();
    }
    poly_bias_install(view_state.config.poly_depth_bias);
    translucent_fog_install(view_state.config.translucent_fog);
    device_dither_configure(view_state.config.dither);
    scene_fade_install(view_state.config.level_fade_seconds);
    install_two_sided();

    /* Last, and on the same resolved site the range test uses. It is the only observer of a
     * failure the engine reports nowhere: a spawn the pools were too full to satisfy. */
    (void)spawn_census_install(sites[SITE_ACTIVATION_SCAN].address,
                               view_state.config.spawn_census);

    /* The destroy side of the same investigation. Self-resolves its own site, independent of
     * activation_scan. Nothing else in this project detours that function any more; see
     * spawn_census.h's own comment. */
    (void)spawn_census_install_destroy_observer(view_state.config.spawn_census);

    spawn_census_log_player_position(view_state.config.log_player_position);
}

void view_distance_fix_shutdown(void)
{
    /* The capture is a rolling window now, so this is the only place it can be written
     * out: whatever the player was doing last is what it holds. */
    fog_trace_flush("the game is closing");

    draw_table_restore();
    vertex_table_restore();
}
