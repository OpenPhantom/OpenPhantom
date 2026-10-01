/* freeze_anim_row.c: see freeze_anim_row.h. */
#include "freeze_anim_row.h"

#include "sim_pause.h"

#include "common/ini.h"
#include "common/logging.h"

#define FREEZE_SECTION "dev_overlay"
#define FREEZE_KEY     "PauseFreezesAnimation"

void freeze_anim_row_load(void)
{
    bool freeze = ini_read_bool(FREEZE_SECTION, FREEZE_KEY, false);

    sim_pause_set_freeze_animation(freeze);
    if (freeze) {
        log_info("%s=1, so a pause holds the animations with the world, on the engine's own "
                 "draw latch", FREEZE_KEY);
    }
}

bool freeze_anim_row_get(void)
{
    return sim_pause_freeze_animation();
}

bool freeze_anim_row_set(bool enabled)
{
    sim_pause_set_freeze_animation(enabled);
    return ini_write_int(FREEZE_SECTION, FREEZE_KEY, enabled ? 1 : 0);
}

bool freeze_anim_row_available(void)
{
    return sim_pause_freeze_animation_is_available();
}
