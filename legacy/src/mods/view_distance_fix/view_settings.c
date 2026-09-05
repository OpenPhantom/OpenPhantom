/* view_settings.c: every key this DLL reads, the defaults behind them, the clamps they are held
 * to, and the few that are re-read while the game runs.
 *
 * THE SEAM. This is the seam the file it came out of had already named for itself: reading the
 * settings touches no engine memory, resolves no signature, places no detour, and is the only
 * part of the feature a reader looking for a default cares about. It took the configuration
 * record with it and left every hook behind.
 *
 * The record is not owned here. The install sequence holds the one copy and passes a pointer in,
 * so the poll below writes into the same object the hooks read and there is never a second copy
 * to fall out of step with the first.
 */
#include "view_settings.h"

#include "cell_watchdog.h"
#include "fog_regime.h"
#include "frame_governor.h"

#include "common/ini.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

#define VIEW_DISTANCE_SECTION "view_distance_fix"

/* The measurement switch below lives in the diagnostics section, where every other
 * measurement in this ini lives. Declaring the name here rather than including another
 * feature's header keeps this DLL standing on its own. */
#define DIAGNOSTICS_SECTION   "diagnostics"

/* ============================================================================================ */
float view_settings_clamp(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void view_settings_load(view_distance_config_t *config)
{
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
    config->level_open_seconds  = view_settings_clamp(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelOpenSeconds", 0.0f), 0.0f, 30.0f);
    config->level_open_range    = view_settings_clamp(
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
    config->cutscene_range      = view_settings_clamp(
        ini_read_float(VIEW_DISTANCE_SECTION, "CutsceneViewRange", 0.0f), 0.0f, 2.5f);
    config->poly_depth_bias     = ini_read_bool (VIEW_DISTANCE_SECTION, "PolyDepthBias", true);
    config->translucent_fog     = ini_read_bool (VIEW_DISTANCE_SECTION, "TranslucentFog", false);
    config->dither              = ini_read_bool (VIEW_DISTANCE_SECTION, "Dither", false);
    config->level_fade_seconds  = view_settings_clamp(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelFadeSeconds", 0.4f), 0.0f, 10.0f);
    config->log_fog_band        = ini_read_bool (VIEW_DISTANCE_SECTION, "LogFogBand", false);
    config->level_open_fog_start = view_settings_clamp(
        ini_read_float(VIEW_DISTANCE_SECTION, "LevelOpenFogStart", 0.0f), 0.0f, 4000.0f);
    config->level_open_fog_end   = view_settings_clamp(
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

    /* This was raised to 4.0 once, on reasoning that field testing then DISPROVED. Kept here
     * rather than quietly reverted, because the reasoning was wrong in a way worth remembering.
     *
     * The argument was: RelocateDrawTable makes the 16384-entry cell table safe with a proven
     * 1.93x reserve, cell_watchdog_budget() already folds that into the radius cap, and the
     * remaining wall, the 16384-slot vertex cache, is watched in real time with an alarm at 75%,
     * earlier than the cells' 90%. All of that is true and none of it was enough: at 3.0 and 4.0
     * the game showed exactly the failure cell_watchdog.c's own comments already named,
     * "torn geometry until the level reloads", and it did not self-correct.
     *
     * What the argument missed: the counters do not climb, they JUMP. cell_watchdog.c documents
     * this for cells, "the counter jumped from under 7680 to 8189 in ONE frame, the gentle
     * back-off never got its turn, only the emergency brake", and the same is true of the vertex
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
     * VERTEX CACHE FULL line. That is evidence the relocation WORKS, not evidence 2.5 is safe:
     * the counter still jumps rather than climbs, and this ceiling has been wrong once already on
     * an argument that sounded just as sound. So: one step, to 2.5, not back to 4.0, and it stays
     * here pending its own field test rather than being trusted on the strength of this one. */
    config->view_range_scale = view_settings_clamp(config->view_range_scale, 1.0f, 2.5f);
    config->npc_range_scale  = view_settings_clamp(config->npc_range_scale, 1.0f, 2.0f);
    if (config->fog_scale <= 0.0f) {
        config->fog_scale = config->view_range_scale;
    }
    config->fog_scale = view_settings_clamp(config->fog_scale, 1.0f, 4.0f);
    /* Zero is a legal setting and means "step immediately", so the lower bound is 0 and not the
     * usual minimum. Ten seconds is long enough that anything above it is a typing mistake. */
    if (!(config->fog_settle_seconds >= 0.0f)) { config->fog_settle_seconds = 0.0f; }
    config->fog_settle_seconds = view_settings_clamp(config->fog_settle_seconds, 0.0f, 10.0f);
    if (config->two_sided_max < 1)  { config->two_sided_max = 1; }
    if (config->two_sided_max > 64) { config->two_sided_max = 64; }
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
void view_settings_poll(view_distance_config_t *config, float *effective_view_scale)
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
                                    config->frame_backoff);

        if (wanted != config->frame_backoff) {
            config->frame_backoff = wanted;
            frame_governor_set_enabled(wanted, config->view_range_scale);
        }
    }

    /* Strict mode, on the same schedule, so the overlay's row takes effect within the second like
     * every other row rather than at the next level. Nothing is configured when it changes: the
     * frame tick reads it directly, and the governor and the watchdog are left installed and
     * measuring so that turning it back off restores their judgement rather than a stale one. */
    {
        bool wanted = ini_read_bool(VIEW_DISTANCE_SECTION, "StrictViewRange",
                                    config->strict_view_range);

        if (wanted != config->strict_view_range) {
            config->strict_view_range = wanted;
            if (wanted) {
                log_warning("StrictViewRange=1: the draw distance is now held at exactly "
                            "ViewRangeScale=%.2f and NOTHING will lower it. The cell watchdog "
                            "still measures and still warns in this file, but it can no longer "
                            "act, and the draw table overflowing writes over the bucket list "
                            "heads rather than stopping. See the key's own comment in "
                            "engine_fixes.ini before leaving this on.",
                            (double)config->view_range_scale);
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
                                      config->authored_fog);

        if (authored != config->authored_fog) {
            config->authored_fog = authored;
            fog_regime_set_authored_band(authored);
        }
    }

    /* How near the band sits, read on the same schedule so it can be tuned with the game up.
     * That is the whole point of polling this one: the right number is a matter of looking at it,
     * and a restart between each try makes that a long evening. */
    {
        float scale = view_settings_clamp(ini_read_float(VIEW_DISTANCE_SECTION, "FogBandScale",
                                                         config->fog_band_scale),
                                          0.25f, 1.0f);

        if (scale != config->fog_band_scale) {
            config->fog_band_scale = scale;
            fog_regime_set_band_scale(scale);
        }
    }

    requested = view_settings_clamp(ini_read_float(VIEW_DISTANCE_SECTION, "ViewRangeScale",
                                                   config->view_range_scale),
                                    1.0f, 2.5f);
    if (requested == config->view_range_scale) {
        return;
    }

    log_info("ViewRangeScale changed on disk, %.2f -> %.2f, adopting it",
             (double)config->view_range_scale, (double)requested);
    config->view_range_scale = requested;
    *effective_view_scale = requested;
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
void view_settings_publish_effective_scale(float scale)
{
    static float published = -1.0f;

    if (published >= 0.0f && scale > published - 0.005f && scale < published + 0.005f) {
        return;
    }
    published = scale;
    (void)ini_write_float(VIEW_DISTANCE_SECTION, "EffectiveViewRange", scale, 2);
}
