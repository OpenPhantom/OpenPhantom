/* view_settings.c: every key this DLL reads, the defaults behind them, the clamps they are held
 * to, and the few that are re-read while the game runs.
 *
 * The seam. This is the seam the file it came out of had already named for itself: reading the
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
#include "view_host_value.h"

#include "common/host_settings_note.h"
#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
     * again. The two implementations differ in device state, and every attempt to switch back
     * while a level was up left nothing fogged; why is not known, since every render state commit
     * re-issues the table mode. So it is not offered live: the panel carries the two band
     * switches, which are pure arithmetic, and this one waits for a restart. */
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
     * Second attempt, and the difference from the first is not more reasoning about the existing
     * wall, it is that the wall itself moved. RelocateVertexCache=1 (default, vertex_table.c) is
     * no longer a real-time watch on a fixed 16384-slot ceiling; it is a relocated 32768-slot
     * buffer, and a field session confirmed the relocation itself: engine_fixes.log shows all
     * 15/15 operands written and the watchdog's alarm rescaled to 24576, then a played session
     * with a widened FOV, thousands of decals and nearly 4800 mover poses produced not one
     * Vertex cache full line. That is evidence the relocation works, not evidence 2.5 is safe:
     * the counter still jumps rather than climbs, and this ceiling has been wrong once already on
     * an argument that sounded just as sound. So: one step, to 2.5, not back to 4.0, and it stays
     * here pending its own field test rather than being trusted on the strength of this one. */
    config->view_range_scale = view_settings_clamp(config->view_range_scale,
                                                   VIEW_SETTINGS_RANGE_MIN,
                                                   VIEW_SETTINGS_RANGE_MAX);
    config->range_pinned     = false;
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
 * Why a poll and not a call. The overlay lives in its own DLL, and feature DLLs in this project
 * never depend on each other at run time: any one of them can be deleted from mods\ without
 * breaking the others. The ini is a channel both already have and neither owns.
 *
 * What it costs. One profile read a second. That is a file the operating system has cached and is
 * measured in tens of microseconds, so amortised across sixty frames it is well under a microsecond
 * each. Worth stating rather than assuming, since this project has already been caught once by a
 * cheap looking call inside a per-frame path, but a once-a-second read is a different order of
 * thing from a per-object syscall. */
#define SCALE_POLL_FRAMES 60u

/* ============================================================================================ */
/* A multiplayer host's values.
 *
 * In a session the host's draw distance, fog band and authored band are the ones in force on
 * every machine, and a client takes them from memory: common/host_settings_note, published by the
 * multiplayer DLL, which this DLL may not call. The ini is never written with them, so this
 * machine's own values are simply there again when the record says the session is over, or when
 * there is no record at all, which is what single player has. Source hands its clients a
 * replicated console variable the same way.
 *
 * The own values are kept apart from the configuration record the hooks read, because while the
 * host's are in force that record holds the host's, and a key missing from the ini has to fall
 * back to this machine's own and not to the host's. */

#define RANGE_BIT    (1u << HOST_SETTING_VIEW_RANGE_SCALE)
#define BAND_BIT     (1u << HOST_SETTING_FOG_BAND_SCALE)
#define AUTHORED_BIT (1u << HOST_SETTING_AUTHORED_FOG_BAND)

typedef struct host_values {
    bool     own_known;
    float    own_range;
    float    own_band;
    bool     own_authored;
    uint16_t in_force;          /* the host_setting_id_t bits the last poll took from the host */
    uint8_t  generation;        /* the generation of the record they came from */
    float    band_in_force;     /* what the last poll left in force, for the acknowledgement */
    bool     authored_in_force;
    bool     pin_said;
    bool     filed_once;        /* the acknowledgement, as last filed */
    uint16_t filed_in_force;
    uint8_t  filed_generation;
    float    filed_scale;
    uint32_t answers;
    bool     refusal_warned;
} host_values_t;

static host_values_t host;

/* What the host names for this DLL's three keys. Each is false while no session runs, while the
 * host did not name it, and on a machine whose multiplayer is not loaded. */
typedef struct host_said {
    bool    range_named;
    bool    band_named;
    bool    authored_named;
    float   range;
    float   band;
    float   authored;
    uint8_t generation;
} host_said_t;

static host_said_t ask_the_host(void)
{
    host_said_t     said;
    host_settings_t record;
    uint32_t        now = GetTickCount();

    memset(&said, 0, sizeof said);
    said.range_named    = host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &said.range, now);
    said.band_named     = host_settings_value(HOST_SETTING_FOG_BAND_SCALE, &said.band, now);
    said.authored_named = host_settings_value(HOST_SETTING_AUTHORED_FOG_BAND, &said.authored,
                                              now);
    if ((said.range_named || said.band_named || said.authored_named) &&
        host_settings_read(&record)) {
        said.generation = record.generation;
    }
    return said;
}

static void hand_over(unsigned bit, bool from_host)
{
    host.in_force = (uint16_t)(from_host ? (host.in_force | bit) : (host.in_force & ~bit));
}

/* Said once per change of hands, whichever way, with this machine's own value beside the host's
 * and the word that the file was not touched. */
static void say_the_hand(const char *what, bool from_host, bool was_host, float value, float own)
{
    if (from_host == was_host) {
        return;
    }
    if (from_host) {
        log_info("the host's %s %.2f is used for this session; this machine's own %.2f stays in "
                 "engine_fixes.ini", what, (double)value, (double)own);
    } else {
        log_info("the host's %s no longer applies: back to this machine's own %.2f from "
                 "engine_fixes.ini", what, (double)own);
    }
}

static void poll_authored(view_distance_config_t *config, const host_said_t *said)
{
    bool from_host = false;
    bool was_host  = (host.in_force & AUTHORED_BIT) != 0u;
    bool authored;

    host.own_authored = ini_read_bool(VIEW_DISTANCE_SECTION, "AuthoredFogBand", host.own_authored);
    authored = view_host_pick_authored(said->authored_named, said->authored, host.own_authored,
                                       &from_host);
    if (from_host != was_host) {
        if (from_host) {
            log_info("the host's authored fog band (%s) is used for this session; this machine's "
                     "own (%s) stays in engine_fixes.ini", authored ? "on" : "off",
                     host.own_authored ? "on" : "off");
        } else {
            log_info("the host's authored fog band no longer applies: back to this machine's own "
                     "(%s) from engine_fixes.ini", authored ? "on" : "off");
        }
    }
    hand_over(AUTHORED_BIT, from_host);
    host.authored_in_force = authored;
    if (authored != config->authored_fog) {
        config->authored_fog = authored;
        fog_regime_set_authored_band(authored);
    }
}

/* How near the band sits, read on the same schedule so it can be tuned with the game up. That is
 * the whole point of polling this one: the right number is a matter of looking at it, and a
 * restart between each try makes that a long evening. */
static void poll_fog_band(view_distance_config_t *config, const host_said_t *said)
{
    view_choice_t band;
    float         own;

    host.own_band = ini_read_float(VIEW_DISTANCE_SECTION, "FogBandScale", host.own_band);
    own  = view_host_pick_fog_band(false, 0.0f, host.own_band).value;
    band = view_host_pick_fog_band(said->band_named, said->band, host.own_band);
    say_the_hand("fog band", band.from_host, (host.in_force & BAND_BIT) != 0u, band.value, own);
    hand_over(BAND_BIT, band.from_host);
    host.band_in_force = band.value;
    if (band.value != config->fog_band_scale) {
        config->fog_band_scale = band.value;
        fog_regime_set_band_scale(band.value);
    }
}

/* Adopts the draw distance when it has changed. Assigning the config value is not enough on its
 * own: effective_view_scale is what the range hook actually multiplies by, and the watchdog only
 * ever lowers it, so a raise has to reset it. Lowering resets it too, which hands the watchdog a
 * fresh start rather than leaving it braked from a scale that is no longer set. The host's value
 * goes the same way, so the governor and the watchdog treat it as the target, and a slow machine
 * still lowers what it cannot afford. */
static void poll_range(view_distance_config_t *config, float *effective_view_scale,
                       const host_said_t *said)
{
    bool          was_host = (host.in_force & RANGE_BIT) != 0u;
    view_choice_t range;
    float         own;

    host.own_range = ini_read_float(VIEW_DISTANCE_SECTION, "ViewRangeScale", host.own_range);
    own   = view_host_pick_range(false, false, 0.0f, host.own_range).value;
    range = view_host_pick_range(config->range_pinned, said->range_named, said->range,
                                 host.own_range);
    say_the_hand("draw distance", range.from_host, was_host, range.value, own);
    hand_over(RANGE_BIT, range.from_host);
    if (config->range_pinned && !host.pin_said && (said->range_named || own != range.value)) {
        host.pin_said = true;
        log_info("the draw distance stays at %.2f, because no cell watchdog is installed to catch "
                 "an overflow: this machine's own %.2f is not taken, and neither is a host's",
                 (double)range.value, (double)own);
    }
    if (range.value == config->view_range_scale) {
        return;
    }
    if (range.from_host && was_host) {
        log_info("the host's draw distance moved, %.2f -> %.2f, adopting it for this session",
                 (double)config->view_range_scale, (double)range.value);
    } else if (!range.from_host && !was_host) {
        log_info("ViewRangeScale changed on disk, %.2f -> %.2f, adopting it",
                 (double)config->view_range_scale, (double)range.value);
    }
    config->view_range_scale = range.value;
    *effective_view_scale    = range.value;
    /* Both watchdogs start again from here. The reader has just said what they want, and either of
     * them still braked from a setting nobody is asking for any more would quietly ignore it. */
    cell_watchdog_reset_ceiling();
    frame_governor_reset(range.value);
}

/* Re-reads the keys that can change while the game runs and adopts what has changed. */
void view_settings_poll(view_distance_config_t *config, float *effective_view_scale)
{
    static uint32_t frames;
    host_said_t     said;

    if (++frames < SCALE_POLL_FRAMES) {
        return;
    }
    frames = 0;

    /* This machine's own values, the first time: what the install read, before any host's value
     * could have taken their place in the record. */
    if (!host.own_known) {
        host.own_known    = true;
        host.own_range    = config->view_range_scale;
        host.own_band     = config->fog_band_scale;
        host.own_authored = config->authored_fog;
    }

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

    /* Which fog band to compute, how near it sits and the draw distance, on the same schedule and
     * through the same channel, each the host's while a session's host names it. */
    said = ask_the_host();
    host.generation = said.generation;
    poll_authored(config, &said);
    poll_fog_band(config, &said);
    poll_range(config, effective_view_scale, &said);
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
 * output only: nothing reads it back into the engine.
 *
 * Not while a session's host decides the draw distance: the number would be the host's, written
 * into this machine's file. The acknowledgement below carries it instead, and the first value after
 * the session goes to the ini whatever it is, since the cache would otherwise take an equal one for
 * the one already on disk. The decision is view_host_writes_effective, where a test holds it. */

/* The acknowledgement the multiplayer's report reads, host_taken_view_distance_fix: which of the
 * host's values this DLL applies and the draw distance in force after this machine's governor and
 * watchdog. Filed here because this is where that number is known every frame, and only when it
 * says something new: another hand, another generation, or a scale that moved by what it shows. A
 * refused filing is warned about once and not tried again until something changes, so a channel
 * that cannot open costs no system call per frame. */
static void acknowledge(float scale)
{
    host_settings_taken_t taken;
    bool                  same;

    if (!host.filed_once && host.in_force == 0u) {
        return;   /* nothing of a host's has been in force here: single player, or a host */
    }
    same = host.filed_once && host.in_force == host.filed_in_force &&
           host.generation == host.filed_generation &&
           (host.in_force == 0u || view_host_shows_the_same(scale, host.filed_scale));
    if (same) {
        return;
    }
    memset(&taken, 0, sizeof taken);
    taken.in_force   = host.in_force;
    taken.generation = host.generation;
    taken.effective[HOST_SETTING_VIEW_RANGE_SCALE]  = scale;
    taken.effective[HOST_SETTING_FOG_BAND_SCALE]    = host.band_in_force;
    taken.effective[HOST_SETTING_AUTHORED_FOG_BAND] = host.authored_in_force ? 1.0f : 0.0f;
    taken.published  = ++host.answers;
    if (!host_settings_publish_taken(VIEW_DISTANCE_SECTION, &taken) && !host.refusal_warned) {
        host.refusal_warned = true;
        log_warning("the acknowledgement of the host's settings could not be filed as "
                    "host_taken_%s, so the multiplayer's report will say no note answered",
                    VIEW_DISTANCE_SECTION);
    }
    host.filed_once       = true;
    host.filed_in_force   = host.in_force;
    host.filed_generation = host.generation;
    host.filed_scale      = scale;
}

void view_settings_publish_effective_scale(float scale)
{
    static float published = -1.0f;

    acknowledge(scale);
    if (view_host_writes_effective((host.in_force & RANGE_BIT) != 0u, scale, &published)) {
        (void)ini_write_float(VIEW_DISTANCE_SECTION, "EffectiveViewRange", scale, 2);
    }
}
