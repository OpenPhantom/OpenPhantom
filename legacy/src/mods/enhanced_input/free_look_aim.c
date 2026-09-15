/* free_look_aim.c: see free_look_aim.h. */
#include "free_look_aim.h"

#include "camera_sites.h"
#include "free_look_math.h"
#include "player_record.h"
#include "player_sites.h"

#include "common/detour.h"
#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long the body keeps being driven to the camera after an attack starts. Long enough that a
 * held trigger keeps the body on the camera, short enough that it does not fight the next movement
 * input. Counted down in real seconds off the live substep, never in frames. */
#define AIM_SNAP_TAIL_SECONDS 0.35f

/* __cdecl from the bytes: the function at 0x0044B804 has four plain rets after epilogues and
 * no ret imm16, so the caller clears its argument. */
typedef int32_t (__cdecl *auto_aim_fn_t)(int32_t kind);

/* The fire handler takes NOTHING: its prologue reads no argument slot, and its only caller reaches
 * it through a stored pointer with no push. A thunk declared with a parameter would leave the
 * stack believing one was there. */
typedef void (__cdecl *fire_shot_fn_t)(void);

/* Plr_StartFire: takes nothing, returns nothing, and runs for every weapon. */
typedef void (__cdecl *start_fire_fn_t)(void);

/* The one instance, handed over at install; nothing here is reachable before then. */
static free_look_state_t *aim_state;

static float read_field(const uint8_t *record, int offset)
{
    return *(const float *)(record + offset);
}

static void write_field(uint8_t *record, int offset, float value)
{
    *(float *)(record + offset) = value;
}

/* The one number. Both the shot and the chest are driven from this and from no other source, so
 * the weapon is always on the line the bolt leaves along.
 *
 * The bolt's direction is built inside the fire handler as `heading + [pPlayer+0x178]`, from a
 * heading read LIVE at that instant, several substeps after Plr_AutoAim ran. So the heading
 * swapped across Plr_AutoAim aims its search cone alone; what decides where the shot
 * goes is the body's facing right now, plus this cell.
 *
 * `lock` is what Plr_AutoAim put in the cell ONCE, captured when it ran, a target bearing relative
 * to the CAMERA, because the camera yaw is the heading it was shown. Adding the camera-to-body
 * angle resolves both terms into the body's frame, the frame the bolt is built in.
 *
 * It is assigned, never accumulated. That is not a style choice. The handler this feeds is polled
 * from the tail of phase 1 on EVERY substep while an attack is armed; it returns early until the
 * animation marker fires, so a version that read the cell and added to it compounded once per
 * substep and saturated the clamp at every angle within a few frames. A player walking
 * forward-right fired at ninety degrees. Reading only the captured lock makes the result depend on
 * nothing that has already been written.
 *
 * Nothing is restored afterwards: the engine clears the cell itself before the fire handler
 * returns, which is also what phase 7 below uses as the signal that the attack is over. */
static float aim_offset_for(const uint8_t *record, float lock)
{
    float heading = read_field(record, PLAYER_HEADING);
    float total;
    float swing;

    if (!isfinite(heading) || !isfinite(lock)) {
        return lock;
    }

    /* SUBTRACTED, because the two halves count opposite ways round. The sideways input counts RIGHT
     * as positive, and this offset counts LEFT as positive, since left is the direction increasing
     * heading turns. Added rather than subtracted, a right-hand key would swing the weapon left,
     * which is the sort of thing that reads as the aim being broken rather than inverted.
     *
     * The latched value is the one free look was handed this substep, so it is already zero when
     * the sideways walk is switched off. That is deliberate: with no sideways walk there is no
     * sideways input to spend, and the mouse is alone with the aiming exactly as before. */
    swing = aim_state->aim_strafe * aim_state->config.aim_strafe_swing;
    if (!isfinite(swing)) {
        swing = 0.0f;
    }
    total = free_look_wrap180(lock + free_look_wrap180(aim_state->camera_yaw - heading) - swing);

    if (total >  aim_state->config.aim_twist_max) { total =  aim_state->config.aim_twist_max; }
    if (total < -aim_state->config.aim_twist_max) { total = -aim_state->config.aim_twist_max; }
    return total;
}

static void __cdecl hook_fire_shot(void)
{
    fire_shot_fn_t original = (fire_shot_fn_t)aim_state->fire_shot_detour.original;
    uint8_t       *record;
    float          total;

    if (free_look_is_steering() && aim_state->camera_yaw_valid &&
        aim_state->config.aim_keeps_movement) {
        record = player_sites_record(aim_state->player);
        if (record != NULL) {
            total = aim_offset_for(record, aim_state->aim_lock_valid ? aim_state->aim_lock : 0.0f);
            if (isfinite(total)) {
                /* Rebuilt against the heading the body has RIGHT NOW: this handler is polled every
                 * substep and the body keeps turning toward its travel in between, while the bolt
                 * is built from a heading read at this very instant.
                 *
                 * No chest write here. This is phase 1, and phase 2's Plr_Steer writes that node
                 * unconditionally a moment later in the same substep, a twist written here could
                 * never be drawn. */
                write_field(record, PLAYER_CHEST_CLAIM, total);
            }
        }
    }
    original();
}

/* An attack has begun, for every weapon.
 *
 * The auto-aim below arms the same thing and used to be the only one that did, which was wrong for
 * the six weapon slots whose autoAimMode is zero: the engine never calls it for those, so the body
 * went on facing its travel while the shot was built against the camera. This runs first and runs
 * always, so the aim snap now arms on the attack itself rather than on an assist the weapon may not
 * have asked for.
 *
 * The original is called unconditionally and no other state is touched. Arming is one float. */
static void __cdecl hook_start_fire(void)
{
    start_fire_fn_t original = (start_fire_fn_t)aim_state->start_fire_detour.original;

    aim_state->aim_hold_seconds = aim_state->config.aim_snap ? AIM_SNAP_TAIL_SECONDS : 0.0f;
    original();
}

/* The auto-aim searches a sixteen-degree cone about the player's heading. Under free look the body
 * faces where the feet go, so without this a player running past an enemy while looking at it
 * would lock onto whatever is in front of the feet. The swap is exact: the heading is read twice
 * inside the original and both reads are covered by an offset across the one call.
 *
 * It is also the only byte-proven signal this DLL has that an attack has begun, so it starts the
 * aim snap. */
static int32_t __cdecl hook_auto_aim(int32_t kind)
{
    auto_aim_fn_t original = (auto_aim_fn_t)aim_state->auto_aim_detour.original;
    uint8_t      *record;
    float         saved;
    int32_t       result;

    aim_state->aim_hold_seconds = aim_state->config.aim_snap ? AIM_SNAP_TAIL_SECONDS : 0.0f;

    record = player_sites_record(aim_state->player);
    if (!free_look_is_steering() || !aim_state->camera_yaw_valid || record == NULL) {
        return original(kind);
    }

    saved = read_field(record, PLAYER_HEADING);
    write_field(record, PLAYER_HEADING, aim_state->camera_yaw);
    result = original(kind);
    write_field(record, PLAYER_HEADING, saved);

    /* The lock is captured here and nowhere else. This is the one moment the engine's own target
     * bearing exists in the cell; from here on the cell is ours and is assigned, never read back.
     * Capturing rather than re-reading stops the per-substep poll from compounding.
     *
     * No chest write here either: phase 6 is followed by phase 2 of the next substep, whose
     * Plr_Steer writes that node unconditionally, so a twist written here lasts at most one
     * substep and usually not even that. */
    if (aim_state->config.aim_keeps_movement && aim_state->fire_shot_armed) {
        float lock = read_field(record, PLAYER_CHEST_CLAIM);
        float aimed;

        aim_state->aim_lock       = isfinite(lock) ? lock : 0.0f;
        aim_state->aim_lock_valid = true;

        aimed = aim_offset_for(record, aim_state->aim_lock);
        if (isfinite(aimed)) {
            write_field(record, PLAYER_CHEST_CLAIM, aimed);
        }
    }
    return result;
}

/* ==============================================================================================
 * The three detours on the attack path, start_fire, fire_shot and auto_aim. All three are
 * OPTIONAL: each failure is a named degraded mode rather than a refusal, because the camera
 * half is already standing by the time these run and there is no way to take a detour back out
 * again.
 *
 * fire_shot BEFORE auto_aim, and that order is load-bearing: hook_auto_aim only captures the
 * engine's target lock when fire_shot_armed is already true, so the reverse order would silently
 * drop the lock for whichever attack happened to land in between.
 * ============================================================================================ */
void free_look_aim_install(free_look_state_t *state)
{
    aim_state = state;

    /* First, because it is the only one of the three that fires for every weapon. Optional like
     * the other two: without it the aim snap falls back to being armed by the auto-aim alone, which
     * is the behaviour that was reported as the weapon aiming forwards while the character pointed
     * somewhere else. */
    if (aim_state->camera.start_fire != 0 &&
        !detour_install(&aim_state->start_fire_detour, aim_state->camera.start_fire,
                        (const void *)hook_start_fire, PLAYER_START_FIRE_PROLOGUE_SIZE)) {
        log_warning("Plr_StartFire at %08X could not be detoured, so the aim snap is armed by the "
                    "auto-aim alone and the weapons that do not use one keep aiming along the "
                    "camera while the body faces its travel",
                    (unsigned)aim_state->camera.start_fire);
    }

    /* This lets the aim snap stop stealing the walk. Without it the shot still leaves
     * along the BODY, so the snap has to keep turning the body to aim, and holding the trigger
     * keeps walking the player forward whatever key is pressed.
     *
     * Installed whatever the setting says, so the setting stays live. The hook reads
     * config.aim_keeps_movement on every call and returns the original untouched when it is off,
     * so a detour that is present costs nothing while the feature is not wanted. Installing it
     * conditionally was the reason the setting could only ever be switched off during a session:
     * a detour cannot be added afterwards, so a player who turned the pairing on had to relaunch
     * before it meant anything. Nothing here writes the host until detour_install does. */
    if (aim_state->camera.fire_shot != 0) {
        if (detour_install(&aim_state->fire_shot_detour, aim_state->camera.fire_shot,
                           (const void *)hook_fire_shot, PLAYER_FIRE_SHOT_PROLOGUE_SIZE)) {
            aim_state->fire_shot_armed = true;
        } else {
            log_warning("the fire handler at %08X could not be detoured, the shot keeps following "
                        "the body, so the aim snap has to keep turning it and movement stays "
                        "locked while firing", (unsigned)aim_state->camera.fire_shot);
        }
    }

    if (aim_state->camera.auto_aim != 0 &&
        !detour_install(&aim_state->auto_aim_detour, aim_state->camera.auto_aim,
                        (const void *)hook_auto_aim, PLAYER_AUTO_AIM_PROLOGUE_SIZE)) {
        log_warning("Plr_AutoAim at %08X could not be detoured, the auto-aim cone stays centred "
                    "on the body and only the aim snap brings it round",
                    (unsigned)aim_state->camera.auto_aim);
    }
}
