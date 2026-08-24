/* lift_droid_fix.c: install-order wiring for the two independent fixes. See lift_droid_fix.h. */
#include "lift_droid_fix.h"
#include "activation_race_fix.h"
#include "projectile_cleanup_fix.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#define LIFT_DROID_FIX_SECTION "lift_droid_fix"

void lift_droid_fix_install(void)
{
    bool enabled;
    bool fix_activation_race;
    bool fix_projectile_cleanup;

    log_init("lift_droid_fix", false);

    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the five known lift droids keep both bugs");
        return;
    }

    enabled = ini_read_bool(LIFT_DROID_FIX_SECTION, "Enabled", true);
    if (!enabled) {
        log_info("Enabled=0, the five known lift droids keep both bugs");
        return;
    }

    fix_activation_race     = ini_read_bool(LIFT_DROID_FIX_SECTION, "FixActivationRace", true);
    fix_projectile_cleanup  = ini_read_bool(LIFT_DROID_FIX_SECTION, "FixProjectileCleanup", true);

    activation_race_fix_install(fix_activation_race);
    projectile_cleanup_fix_install(fix_projectile_cleanup);
}
