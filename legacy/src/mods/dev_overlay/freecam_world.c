/* freecam_world.c: see freecam_world.h. */
#include "freecam_world.h"

#include "input_freeze.h"
#include "sim_pause.h"

#include "common/ini.h"
#include "common/logging.h"

#define FREECAM_SECTION        "dev_overlay"
#define FREECAM_WORLD_RUNS_KEY "FreeCameraWorldRuns"

static bool world_runs;

void freecam_world_load(void)
{
    world_runs = ini_read_bool(FREECAM_SECTION, FREECAM_WORLD_RUNS_KEY, false);
    if (world_runs && sim_pause_freeze_animation()) {
        /* The two rows are one or the other, and a file edited by hand can say both. The
         * world running is the one that means something on its own, so it wins, and the
         * freeze goes off for the session; the file is left as written. */
        sim_pause_set_freeze_animation(false);
        log_info("free camera: %s=1 and PauseFreezesAnimation=1 together; the world runs and "
                 "the freeze is off for this session", FREECAM_WORLD_RUNS_KEY);
    }
    log_info("free camera: the world %s while it flies (%s=%d)",
             world_runs ? "runs" : "is held", FREECAM_WORLD_RUNS_KEY, world_runs ? 1 : 0);
}

bool freecam_world_runs(void)
{
    return world_runs;
}

void freecam_world_set_runs(bool runs)
{
    world_runs = runs;
    if (!ini_write_int(FREECAM_SECTION, FREECAM_WORLD_RUNS_KEY, runs ? 1 : 0)) {
        log_warning("free camera: %s could not be written to the settings file; the choice "
                    "holds for this session only", FREECAM_WORLD_RUNS_KEY);
    }
}

void freecam_world_apply(void)
{
    sim_pause_let_run(world_runs);
    input_freeze_hold(INPUT_FREEZE_FREE_CAMERA, world_runs);
}

void freecam_world_release(void)
{
    sim_pause_let_run(false);
    input_freeze_hold(INPUT_FREEZE_FREE_CAMERA, false);
}
