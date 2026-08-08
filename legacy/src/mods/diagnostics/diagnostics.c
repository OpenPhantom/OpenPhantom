/* diagnostics.c: configuration and orchestration. The observers themselves live in
 * diag_audio.c, diag_world.c and diag_flow.c, the throttled stream in diag_log.c, the name
 * tables in diag_names.c.
 */
#include "diagnostics.h"

#include "diag_audio.h"
#include "diag_flow.h"
#include "diag_log.h"
#include "diag_world.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>

#define DIAGNOSTICS_SECTION      "diagnostics"
#define MIN_CENSUS_MILLISECONDS  100

static diagnostics_config_t diagnostics_state;
static bool                 diagnostics_installed;

const diagnostics_config_t *diagnostics_config(void)
{
    return &diagnostics_state;
}

/* The ceiling is per AREA and not a shared constant, because a clamp that silently truncates a
 * level the caller does understand is indistinguishable from a level nobody implemented. Fx has a
 * third level (the decal DRAW census); everything else stops at two. */
static int read_level_max(const char *key, int maximum)
{
    int level = ini_read_int(DIAGNOSTICS_SECTION, key, 0);

    if (level < 0) {
        return 0;
    }
    if (level > maximum) {
        return maximum;
    }
    return level;
}

static int read_level(const char *key)
{
    return read_level_max(key, 2);
}

static void load_config(void)
{
    diagnostics_state.enabled  = ini_read_bool(DIAGNOSTICS_SECTION, "Enabled", false);
    diagnostics_state.audio    = read_level("Audio");
    diagnostics_state.music    = read_level("Music");
    diagnostics_state.trigger  = read_level("Trigger");
    diagnostics_state.fsm      = read_level("Fsm");
    diagnostics_state.level    = read_level("Level");
    diagnostics_state.player   = read_level("Player");
    diagnostics_state.dialogue = read_level("Dialogue");
    diagnostics_state.fx       = read_level_max("Fx", 3);

    diagnostics_state.audio_census_ms =
        ini_read_int(DIAGNOSTICS_SECTION, "AudioCensusMilliseconds", 0);
    diagnostics_state.max_lines_per_second =
        ini_read_int(DIAGNOSTICS_SECTION, "MaxLinesPerSecond", 60);
    diagnostics_state.also_to_main_log =
        ini_read_bool(DIAGNOSTICS_SECTION, "AlsoToMainLog", false);

    if (diagnostics_state.max_lines_per_second < 0) {
        diagnostics_state.max_lines_per_second = 0;
    }
    if (diagnostics_state.audio_census_ms < 0) {
        diagnostics_state.audio_census_ms = 0;
    }
    if (diagnostics_state.audio_census_ms > 0 &&
        diagnostics_state.audio_census_ms < MIN_CENSUS_MILLISECONDS) {
        diagnostics_state.audio_census_ms = MIN_CENSUS_MILLISECONDS;
    }
}

static bool any_area_enabled(void)
{
    return diagnostics_state.enabled &&
           (diagnostics_state.audio    != 0 || diagnostics_state.music    != 0 ||
            diagnostics_state.trigger  != 0 || diagnostics_state.fsm      != 0 ||
            diagnostics_state.level    != 0 || diagnostics_state.player   != 0 ||
            diagnostics_state.dialogue != 0 || diagnostics_state.fx       != 0);
}

void diagnostics_install(void)
{
    int observers = 0;

    log_init("diagnostics", false);

    if (diagnostics_installed) {
        return;
    }

    load_config();
    if (!any_area_enabled()) {
        log_info("off (Enabled=0 or no area switched on), not one byte touched, .text is not "
                 "even read");
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, diagnostics OFF");
        return;
    }
    if (!diag_log_open(diagnostics_state.max_lines_per_second,
                       diagnostics_state.also_to_main_log)) {
        return;
    }
    diagnostics_installed = true;

    log_info("areas audio=%d music=%d trigger=%d fsm=%d level=%d player=%d dialogue=%d fx=%d "
             "| census=%dms max=%d lines/s",
             diagnostics_state.audio, diagnostics_state.music, diagnostics_state.trigger,
             diagnostics_state.fsm, diagnostics_state.level, diagnostics_state.player,
             diagnostics_state.dialogue, diagnostics_state.fx,
             diagnostics_state.audio_census_ms, diagnostics_state.max_lines_per_second);

    observers += diag_audio_install(diagnostics_state.audio, diagnostics_state.audio_census_ms);
    observers += diag_music_install(diagnostics_state.music);
    observers += diag_trigger_install(diagnostics_state.trigger);
    observers += diag_fsm_install(diagnostics_state.fsm);
    observers += diag_level_install(diagnostics_state.level);
    observers += diag_player_install(diagnostics_state.player);
    observers += diag_dialogue_install(diagnostics_state.dialogue);
    observers += diag_fx_install(diagnostics_state.fx);

    log_info("%d observers active", observers);
    diag_log_write("diagnostics: %d observers active", observers);
}
