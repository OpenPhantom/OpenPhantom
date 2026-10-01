/* input_switches.c: the live half of this DLL's configuration: Strafe, FreeLook, CameraFollow and
 * AirControl.
 *
 * The seam taken here is the one enhanced_input.c had already measured and named: the block of
 * setters and availability queries the controls screen calls, plus the once-a-second re-read that
 * drives all four setters from the file. Not one line of it patches a byte, reads a player
 * record or runs on a substep, so it is a different responsibility from the phase thunks it used
 * to sit beside. The thunks were rejected as the seam for the opposite reason: they
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
#include "camera_follow.h"
#include "strafe_walk.h"

#include "frame_clock.h"

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
    free_look_refresh_aim_pairing();
    enhanced_input_forget_pending_travel();

    /* The camera follow is armed from the strafe setting as it stood at LAUNCH, and without
     * this it would keep that answer for the rest of the session: ticking sideways walking on
     * here would leave the follow refusing to run, having already said so in the install log,
     * with nothing to explain why. */
    camera_follow_configure(input_config()->camera_follow, enabled,
                            input_config()->camera_follow_settle_seconds,
                            input_config()->camera_follow_rate,
                            input_config()->camera_follow_strength,
                            input_config()->camera_follow_max_degrees);

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

void enhanced_input_set_camera_follow(bool enabled)
{
    if (input_config()->camera_follow == enabled) {
        return;
    }

    /* The dependency is not enforced here, and the reason is worth keeping.
     *
     * The passive camera is built on free look, so the two have to move together, and the obvious
     * place to do it is right here: ask free look to switch on before arming this. That was tried
     * and it desynced. Free look REFUSES while the player phases are not running, exactly the
     * state the game is in while the developer menu is open, so the one moment a player is ever
     * going to tick this box is the one moment free look will not take it. The row then read ON
     * with the feature off, and the poll below had already recorded the new value so it never
     * retried.
     *
     * The dependency lives in the ini instead: the menu row writes BOTH keys, and the poll applies
     * them in order once the phases are running again, using the refusal handling it already has.
     * What is left here is only the half that can never refuse. */

    /* Two holders of one setting, and both have to be told. camera_follow.c owns the answer for
     * the sideways walk's own lean, and free_look.c keeps a copy of the same switch because it
     * reads it on the render clock, where reaching for the ini would be wrong. Telling only one
     * of them made the developer menu's row look dead: it wrote the file, the file was
     * not read back, and the feature kept whatever it had been given at launch. */
    input_config_set_camera_follow(enabled);
    camera_follow_configure(enabled, input_config()->strafe,
                            input_config()->camera_follow_settle_seconds,
                            input_config()->camera_follow_rate,
                            input_config()->camera_follow_strength,
                            input_config()->camera_follow_max_degrees);
    free_look_set_passive_follow(enabled);
    /* The pairing follows the switch the player just moved, so the scheme this row promises is
     * the scheme they get without restarting or finding two keys by hand. */
    free_look_refresh_aim_pairing();

    if (!ini_write_int(INPUT_SECTION, "CameraFollow", enabled ? 1 : 0)) {
        log_warning("the passive camera is now %s, but the setting could not be written to the ini "
                    "and will be back to its old value on the next launch",
                    enabled ? "on" : "off");
        return;
    }
    log_info("the passive camera is now %s: once you stop turning the camera it drifts back behind "
             "the body, and since free look has already turned the body to face where it travels, "
             "that is behind the direction of travel", enabled ? "on" : "off");
}

void enhanced_input_set_air_control(bool enabled)
{
    if (input_config()->air_control == enabled) {
        return;
    }

    input_config_set_air_control(enabled);
    free_look_set_air_control(enabled);

    if (!ini_write_int(INPUT_SECTION, "AirControl", enabled ? 1 : 0)) {
        log_warning("steering a jump is now %s, but the setting could not be written to the "
                    "ini and will be back to its old value on the next launch",
                    enabled ? "on" : "off");
        return;
    }
    log_info("steering a jump is now %s. It turns the body toward the stick while you are off "
             "the ground and forces nothing, so the speed you launched with is redirected "
             "rather than renewed", enabled ? "on" : "off");
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

    /* The passive camera cannot outlive it: it is built on free look turning the body to face its
     * travel, so with free look gone it would be aiming the camera at a heading that no longer
     * follows the player anywhere. Safe to do from here, unlike the other direction, because
     * switching something OFF never refuses. */
    if (!enabled && input_config()->camera_follow) {
        enhanced_input_set_camera_follow(false);
    }
    if (!enabled && input_config()->air_control) {
        enhanced_input_set_air_control(false);
    }
}

/* --- The four switches, re-read while the game runs --------------------------------------------
 *
 * The controls screen pushes outward: it applies each switch and then writes it. Nothing read the
 * file back, so a value written by anything else, the developer overlay's own rows being the reason
 * this exists, did nothing until the next launch.
 *
 * The comparison is against the last value seen in the file, not against the setting in force, and
 * that is the whole care in this function. Two of the setters can refuse: strafe needs the
 * keyboard axis and mouse look, free look needs a follow camera this build recognises; the
 * passive camera and the air steer never refuse. Comparing against the live setting would then
 * find a difference the setter had just declined to close, retry it a second later, and write a
 * warning to the log every second for the rest of the session.
 *
 * The care has a second half, which cost a released build. A setter can switch ANOTHER of these off
 * as a dependency of its own: free look going off takes the passive camera and the air steer with
 * it and writes both keys to the file. All four values are read before any setter runs, so the
 * shadow for a key changed that way still holds what the file said a moment ago, which is now a
 * value that exists neither in the file nor in the running game. Every later edit to it compares
 * equal to that and is decided not to be a change, so the row flips and nothing happens for the
 * rest of the session.
 *
 * So a key this pass did NOT act on is re-read from the live setting afterwards, and a key it DID
 * act on is left alone. That keeps both halves true at once: the key that was just refused
 * keeps its file-derived shadow and is not retried, and the key that was changed underneath us is
 * corrected. Only on a pass that acted, so an idle second still costs four reads and four compares.
 *
 * Once a second. The file is on disk and a whole second is invisible next to reaching for a key.
 *
 * A second measured on the engine's frame delta, not counted in frames. This counted 60 frames and
 * called that a second, which it is only at 60 frames a second; this game's frame rate is uncapped,
 * so on a fast machine the file was being re-read three or four times as often as intended. The
 * frame count survives as the fallback for a frame clock that did not resolve, the one case where
 * there is nothing better to count.
 */
#define SWITCH_POLL_SECONDS 1.0f
#define SWITCH_POLL_FRAMES_UNCLOCKED 60u

static void poll_switches(void)
{
    static uint32_t frames;
    static float    elapsed;
    static bool     clock_seen;
    static bool     seeded;
    static bool     seen_strafe;
    static bool     seen_free_look;
    static bool     seen_camera_follow;
    static bool     seen_air_control;
    bool            strafe;
    bool            free_look;
    bool            camera_follow;
    bool            air_control;
    bool            did_strafe        = false;
    bool            did_free_look     = false;
    bool            did_camera_follow = false;
    bool            did_air_control   = false;

    if (!seeded) {
        seen_strafe        = input_config()->strafe;
        seen_free_look     = free_look_is_enabled();
        seen_camera_follow = input_config()->camera_follow;
        seen_air_control   = input_config()->air_control;
        seeded             = true;
    }
    {
        const float step = frame_clock_seconds();

        if (step > 0.0f) {
            clock_seen = true;
            elapsed += step;
        }
        ++frames;
        if (clock_seen ? (elapsed < SWITCH_POLL_SECONDS)
                       : (frames < SWITCH_POLL_FRAMES_UNCLOCKED)) {
            return;
        }
        elapsed = 0.0f;
        frames  = 0;
    }

    strafe        = ini_read_bool(INPUT_SECTION, "Strafe", seen_strafe);
    free_look     = ini_read_bool(INPUT_SECTION, "FreeLook", seen_free_look);
    camera_follow = ini_read_bool(INPUT_SECTION, "CameraFollow", seen_camera_follow);
    air_control   = ini_read_bool(INPUT_SECTION, "AirControl", seen_air_control);

    if (strafe != seen_strafe) {
        seen_strafe = strafe;
        /* logs whichever branch it took, refusals included */
        enhanced_input_set_strafe(strafe);
        did_strafe = true;
    }
    if (free_look != seen_free_look) {
        seen_free_look = free_look;
        enhanced_input_set_free_look(free_look);
        did_free_look = true;
    }
    if (camera_follow != seen_camera_follow) {
        seen_camera_follow = camera_follow;
        enhanced_input_set_camera_follow(camera_follow);
        did_camera_follow = true;
    }
    if (air_control != seen_air_control) {
        seen_air_control = air_control;
        enhanced_input_set_air_control(air_control);
        did_air_control = true;
    }

    /* A key this pass did not act on is re-read from the live setting, and a key it did act on is
     * left with the shadow the file gave it. See the note above the function for why it has to be
     * both ways round: one half stops a setting changed as a dependency from going dead, the other
     * stops a refused setter being retried once a second forever. */
    if (did_strafe || did_free_look || did_camera_follow || did_air_control) {
        if (!did_strafe)        { seen_strafe        = input_config()->strafe; }
        if (!did_free_look)     { seen_free_look     = free_look_is_enabled(); }
        if (!did_camera_follow) { seen_camera_follow = input_config()->camera_follow; }
        if (!did_air_control)   { seen_air_control   = input_config()->air_control; }
    }
}

void input_switches_install(void)
{
    if (!frame_hook_add(poll_switches)) {
        log_warning("no per-frame hook, so Strafe, FreeLook, CameraFollow and AirControl are "
                    "read once at startup and an edit made while the game runs waits for the "
                    "next launch");
    }
}
