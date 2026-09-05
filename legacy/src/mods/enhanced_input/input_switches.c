/* input_switches.c: the live half of this DLL's configuration, Strafe and FreeLook.
 *
 * The seam taken here is the one enhanced_input.c had already measured and named: the block of
 * setters and availability queries the controls screen calls, plus the once-a-second re-read that
 * drives the same two setters from the file. Not one line of it patches a byte, reads a player
 * record or runs on a substep, which is what makes it a different responsibility from the phase
 * thunks it used to sit beside. The thunks were rejected as the seam for the opposite reason: they
 * read eleven fields of the install state between them.
 *
 * What this file needs from enhanced_input.c is three answers, and it asks for them rather than
 * keeping a second copy of the state: whether the phase pointers are ours, whether the keyboard
 * axis reader resolved, and a way to drop the pending pair when sideways walking is switched off.
 */
#include "input_switches.h"

#include "enhanced_input.h"
#include "free_look.h"
#include "input_config.h"
#include "strafe_walk.h"

#include "common/frame_hook.h"
#include "common/ini.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

/* The section this DLL owns, spelled here as it is in enhanced_input.c: the files do not share a
 * header for one string, and one copy each is cheaper than a header that exists for it. */
#define INPUT_SECTION "enhanced_input"

/* ==============================================================================================
 * What the two check boxes on the controls screen drive
 *
 * Both are settings of the mouse-look scheme rather than switches for the DLL itself, and both are
 * live because their machinery is installed unconditionally and gated by a plain bool: the phase
 * thunks for sideways walking, the two camera detours for free look. Neither setter patches a byte
 * of the host, and each refuses, and says why, when what it drives cannot run in this session,
 * so a switch can never claim more than the DLL can deliver.
 *
 * Neither box exists at all when MouseLook=0, because install returns before the menu is patched.
 * ============================================================================================ */
bool enhanced_input_strafe_enabled(void)
{
    return input_config()->strafe;
}

/* Whether sideways walking could be switched on at all, which is a different question from whether
 * it is on. The controls screen asks it before offering the box, for the same reason free look is
 * asked: a switch that could never turn anything misleads rather than fails. */
bool enhanced_input_strafe_available(void)
{
    return enhanced_input_is_active() && enhanced_input_keyboard_axis_resolved();
}

void enhanced_input_set_strafe(bool enabled)
{
    if (input_config()->strafe == enabled) {
        return;
    }
    if (enabled && !enhanced_input_is_active()) {
        log_warning("sideways walking was switched on, but the player phases are not hooked, so "
                    "the setting is not applied and not saved");
        return;
    }
    if (enabled && !enhanced_input_keyboard_axis_resolved()) {
        log_warning("sideways walking was switched on, but the keyboard axis reader did not "
                    "resolve, so there is no key to read, the setting is not applied and not "
                    "saved");
        return;
    }

    input_config_set_strafe(enabled);

    /* The angle latched in the model root belongs to the feature that is being switched off, and
     * nothing would come back to walk it down, so it is dropped here and the next driven substep
     * starts from zero. The pending pair is one substep of lifetime and is cleared with it. */
    strafe_walk_reset();
    enhanced_input_forget_pending_travel();

    if (!ini_write_int(INPUT_SECTION, "Strafe", enabled ? 1 : 0)) {
        log_warning("sideways walking is now %s, but the setting could not be written to the ini "
                    "and will be back to its old value on the next launch",
                    enabled ? "on" : "off");
        return;
    }
    log_info("sideways walking switched %s from the controls screen and saved",
             enabled ? "on" : "off");
}

bool enhanced_input_free_look_available(void)
{
    return enhanced_input_is_active() && free_look_is_installed();
}

bool enhanced_input_free_look_enabled(void)
{
    return free_look_is_enabled();
}

void enhanced_input_set_free_look(bool enabled)
{
    if (free_look_is_enabled() == enabled) {
        return;
    }
    if (!enhanced_input_is_active()) {
        log_warning("free look was switched on, but the player phases are not hooked, the "
                    "setting is not applied and not saved");
        return;
    }

    /* The one refusal a player could otherwise reach: the camera in this build was not recognised
     * at install, so there is nothing to turn. The controls screen leaves the box off the screen in
     * that case, which makes this the belt to that pair of braces. */
    if (!free_look_set_enabled(enabled)) {
        log_warning("free look was switched on, but the follow camera in this build is not the "
                    "one this feature knows, the setting is not applied and not saved");
        return;
    }

    /* No strafe_walk_reset here, unlike the strafe setter: both control schemes walk the model-root
     * latch home on every substep they do not drive it, so the mode entered next unwinds it. */

    if (!ini_write_int(INPUT_SECTION, "FreeLook", enabled ? 1 : 0)) {
        log_warning("free look is now %s, but the setting could not be written to the ini and "
                    "will be back to its old value on the next launch", enabled ? "on" : "off");
        return;
    }
    log_info("free look switched %s from the controls screen and saved, the mouse now turns the "
             "%s", enabled ? "on" : "off", enabled ? "camera" : "body");
}

/* --- Strafe and FreeLook, re-read while the game runs -------------------------------------------
 *
 * The controls screen pushes outward: it applies each switch and then writes it. Nothing read the
 * file back, so a value written by anything else, the developer overlay's own rows being the reason
 * this exists, did nothing until the next launch.
 *
 * THE COMPARISON IS AGAINST THE LAST VALUE SEEN IN THE FILE, not against the setting in force, and
 * that is the whole care in this function. Both setters can refuse: strafe needs the keyboard axis
 * and mouse look, free look needs a follow camera this build recognises. Comparing against the
 * live setting would then find a difference the setter had just declined to close, retry it a
 * second later, and write a warning to the log every second for the rest of the session.
 *
 * Once a second. The file is on disk and a whole second is invisible next to reaching for a key.
 */
#define SWITCH_POLL_FRAMES 60u

static void poll_switches(void)
{
    static uint32_t frames;
    static bool     seeded;
    static bool     seen_strafe;
    static bool     seen_free_look;
    bool            strafe;
    bool            free_look;

    if (!seeded) {
        seen_strafe    = input_config()->strafe;
        seen_free_look = free_look_is_enabled();
        seeded         = true;
    }
    if (++frames < SWITCH_POLL_FRAMES) {
        return;
    }
    frames = 0;

    strafe    = ini_read_bool(INPUT_SECTION, "Strafe", seen_strafe);
    free_look = ini_read_bool(INPUT_SECTION, "FreeLook", seen_free_look);

    if (strafe != seen_strafe) {
        seen_strafe = strafe;
        enhanced_input_set_strafe(strafe);     /* logs whichever branch it took, refusals included */
    }
    if (free_look != seen_free_look) {
        seen_free_look = free_look;
        enhanced_input_set_free_look(free_look);
    }
}

void input_switches_install(void)
{
    if (!frame_hook_add(poll_switches)) {
        log_warning("no per-frame hook, so Strafe and FreeLook are read once at startup and an "
                    "edit made while the game runs waits for the next launch");
    }
}
