/* cheats_free_camera.c: the free camera, its two engine sites and the state it flies with.
 *
 * It holds the simulation through sim_pause rather than by any means of its own, and writes the
 * camera pose after the engine has composed it rather than fighting the original for the fields.
 *
 * Split out of cheats_openphantom.c; nothing changed in the move. */
#include "cheats_openphantom.h"
#include "cheats_internal.h"

#include "sim_pause.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint8_t SIG_CAMERA_VIEW[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                     /* mov eax,[gView]                    */
    0xD9, 0x40, 0x38,                                 /* fld [eax+0x38]  previous camera yaw */
    0xD9, 0x5D, 0xE0,
    0xD9, 0x45, 0xD8,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00                /* fadd [camera yaw offset]            */
};
static const uint8_t MSK_CAMERA_VIEW[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_CAMERA_VIEW) == sizeof(MSK_CAMERA_VIEW),
               "the camera-view pattern and its mask are different lengths");
#define OFFSET_CAMERA_VIEW_OBJECT   0x01u   /* operand of mov eax,[gView]                       */

/* 0x00418544, nine bytes: push ebp / mov ebp,esp / sub esp,0x88 - identical to camera_sites.c's
 * own CAMERA_UPDATE_PROLOGUE_SIZE, deliberately, since a mismatched prologue size on the SAME
 * chained site would misplace the trampoline for whichever DLL installs second. */
static const uint8_t SIG_CAMERA_UPDATE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x52,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x01,
    0x6A, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x1C,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x4D, 0xC4
};
static const uint8_t MSK_CAMERA_UPDATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CAMERA_UPDATE) == sizeof(MSK_CAMERA_UPDATE),
               "the camera-update pattern and its mask are different lengths");
#define CAMERA_UPDATE_PROLOGUE_SIZE  9u

#define CAMERA_ANCHOR_X_OFFSET   0x14u
#define CAMERA_ANCHOR_Y_OFFSET   0x18u
#define CAMERA_ANCHOR_Z_OFFSET   0x1Cu
#define CAMERA_EULER_PITCH_OFFSET 0x34u
#define CAMERA_EULER_YAW_OFFSET   0x38u

/* eyeX/Y/Z, view+0x24/0x28/0x2c: eyeX = anchorX + (the local rig offset, view+0x04/0x08, rotated
 * by the current euler); eyeZ = anchorZ + offsetZ(view+0x0c), unrotated. Confirmed directly by
 * decompiling FUN_004181b9, the function that builds these every rendered frame from the anchor
 * and the euler updateCam just wrote - not taken on the strength of an earlier report alone.
 * Read only once, at free camera's own rising edge, to compute a look-at rather than to drive
 * anything ongoing - see that site's own comment for why. */
#define CAMERA_EYE_X_OFFSET      0x24u
#define CAMERA_EYE_Y_OFFSET      0x28u
#define CAMERA_EYE_Z_OFFSET      0x2Cu

#define DEG_TO_RAD               0.017453293f
#define RAD_TO_DEG               57.295780f
#define FREECAM_MOVE_SPEED       12.0f    /* world units/second, only the STARTING value now that
                                            * the wheel adjusts it at runtime (see freecam_speed) -
                                            * still a first guess, pending a field round, for what
                                            * the wheel then tunes away from */
#define FREECAM_MOUSE_DEGREES_PER_COUNT 0.15f   /* degrees of turn per pixel of cursor delta */
#define FREECAM_PITCH_LIMIT      89.0f    /* degrees; kept short of vertical to avoid a gimbal flip */
#define FREECAM_MIN_SPEED        0.5f     /* world units/second */
#define FREECAM_MAX_SPEED        200.0f   /* world units/second */
#define FREECAM_SPEED_PER_NOTCH  1.1f     /* multiplicative, Blender's own fly-mode feel: constant
                                            * ratio per notch reads as even control at both the slow
                                            * and the fast end, where a constant per-notch ADD would
                                            * feel enormous down low and glacial up high */

static bool is_game_foreground(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner_pid = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner_pid);
    return owner_pid == GetCurrentProcessId();
}

/* NULL until dev_overlay.c wires it in - see this pointer's own type, declared in
 * cheats_openphantom.h, for why it is a pointer rather than a direct call. */
static cheats_openphantom_wheel_source_fn_t wheel_source;

void cheats_openphantom_set_wheel_source(cheats_openphantom_wheel_source_fn_t fn)
{
    wheel_source = fn;
}

/* Free camera. Owned entirely by this file, seeded from wherever the camera already is the moment
 * the cheat switches on: nothing else may be trusted to hold still. */
static float freecam_x, freecam_y, freecam_z;
static float freecam_yaw, freecam_pitch;   /* degrees */
static bool  freecam_valid = false;
static bool  freecam_cursor_hidden = false;   /* true while THIS cheat owns the OS cursor */

/* The scroll wheel adjusts this, Blender-fly-mode style, rather than it being the fixed
 * FREECAM_MOVE_SPEED constant every other tuned number in this file starts as. Deliberately a
 * file-level static rather than reseeded at the rising edge alongside position/orientation: a
 * speed dialled in once is worth keeping across a toggle off and back on, the same way Blender
 * itself remembers it walk speed between uses rather than resetting it every time. */
static float freecam_speed = FREECAM_MOVE_SPEED;

static LARGE_INTEGER freecam_perf_frequency;
static LONGLONG      freecam_last_tick;
static POINT         freecam_cursor_anchor;

/* The exit hotkey. Free camera's own mouse look claims the cursor, which leaves both the dev panel
 * and the game's own pause menu unreachable - no cursor, no click, and Escape does not work either
 * while the simulation this cheat paused is what would normally answer it. Without a keyboard-only
 * way out, turning this cheat on is a one-way door. 0 = unbound; the panel's hotkey row (see
 * overlay_model.c) is how a key gets into this. */
static int32_t freecam_hotkey;
static bool    freecam_hotkey_was_down;

int32_t cheats_openphantom_freecam_hotkey(void)
{
    return freecam_hotkey;
}

void cheats_openphantom_freecam_set_hotkey(int32_t virtual_key)
{
    freecam_hotkey = virtual_key;
    freecam_hotkey_was_down = false;   /* do not treat the binding key's own release as an edge */
}

/* Chained: this file's own override runs strictly AFTER the original updateCam, on every DLL's
 * behalf whichever order they loaded in (see common/detour.h). Calling the original unconditionally,
 * before even looking at whether the cheat is on, is what keeps this a well-behaved link in that
 * chain rather than a break in it - enhanced_input's own free-look feature is chained on this exact
 * same site and must keep running underneath this one regardless of what this cheat is doing. */
static void __cdecl hook_camera_update(void)
{
    void **view_slot;
    void  *view;

    own_state.camera_update_original();

    if (!own_state.cheats[CHEATS_OWN_FREECAM].on) {
        if (freecam_valid) {
            /* Falling edge: hand the world back. The camera itself needs no un-write - the very
             * next updateCam call recomputes it from the player the ordinary way, since nothing
             * here touches state (+0x00) or anything else the follow logic reads. */
            sim_pause_hold(SIM_PAUSE_FREE_CAMERA, false);
            if (freecam_cursor_hidden) {
                ShowCursor(TRUE);
                freecam_cursor_hidden = false;
            }
            freecam_valid = false;
        }
        return;
    }

    if (own_state.camera_view_address == 0) {
        return;
    }
    view_slot = (void **)own_state.camera_view_address;
    view = *view_slot;
    if (view == NULL) {
        return;
    }

    if (!freecam_valid) {
        /* Rising edge: seed POSITION from wherever the camera already is, so switching on never
         * snaps the view, and pause the simulation - see sim_pause.h for why
         * this one flag is enough to stop the player and the whole world simulating out from
         * under a camera that is no longer looking through the player's own eyes. */
        freecam_x = *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET);
        freecam_y = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET);
        freecam_z = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET);

        /* ORIENTATION is computed fresh - a look-at from the retail camera's own eye position
         * toward its anchor (which tracks the player every frame; see updateCam's own site
         * comment above) - rather than copied from the raw euler fields the way position is.
         * Field-reported: switching free camera on could start it pointing "at the sky". The raw
         * pitch is periodic (FUN_004181b9 wraps it every frame, confirmed by decompiling it) so
         * that alone should not have caused it - sin/cos already handle any wrap correctly - which
         * points instead at the retail camera's own momentary rotation (lag catching up, a look at
         * something tall) simply not being a sensible starting orientation for a DIFFERENT
         * camera's use. Anchor and eye are both positions, immune to whatever the retail camera's
         * rotation happened to be doing, so a look-at from one to the other is deterministic
         * regardless of the cause. */
        {
            float eye_x = *(float *)((uint8_t *)view + CAMERA_EYE_X_OFFSET);
            float eye_y = *(float *)((uint8_t *)view + CAMERA_EYE_Y_OFFSET);
            float eye_z = *(float *)((uint8_t *)view + CAMERA_EYE_Z_OFFSET);
            float dx = freecam_x - eye_x;
            float dy = freecam_y - eye_y;
            float dz = freecam_z - eye_z;
            float horiz = sqrtf(dx * dx + dy * dy);

            if (horiz > 0.0001f || fabsf(dz) > 0.0001f) {
                freecam_yaw   = atan2f(-dx, dy) * RAD_TO_DEG;
                freecam_pitch = atan2f(dz, horiz) * RAD_TO_DEG;
            } else {
                /* Degenerate: eye and anchor coincide (a fixed or world-locked camera, most
                 * likely). Nothing to look at from nowhere away, so this falls back to whatever
                 * the raw fields hold, at least clamped into this file's own range. */
                freecam_yaw   = *(float *)((uint8_t *)view + CAMERA_EULER_YAW_OFFSET);
                freecam_pitch = *(float *)((uint8_t *)view + CAMERA_EULER_PITCH_OFFSET);
            }
            if (freecam_pitch > FREECAM_PITCH_LIMIT) {
                freecam_pitch = FREECAM_PITCH_LIMIT;
            }
            if (freecam_pitch < -FREECAM_PITCH_LIMIT) {
                freecam_pitch = -FREECAM_PITCH_LIMIT;
            }
        }
        sim_pause_hold(SIM_PAUSE_FREE_CAMERA, true);
        QueryPerformanceFrequency(&freecam_perf_frequency);
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            freecam_last_tick = now.QuadPart;
        }
        GetCursorPos(&freecam_cursor_anchor);
        ShowCursor(FALSE);
        freecam_cursor_hidden = true;
        /* Discard whatever the wheel accumulated while this cheat was off - overlay_input.c
         * observes it unconditionally, panel open or closed, cheat on or off, so without this a
         * scroll from minutes ago would show up as a sudden speed jump on the very first frame. */
        if (wheel_source != NULL) {
            (void)wheel_source();
        }
        freecam_valid = true;
    }

    /* Panel-aware mouse handling was tried here (skip look/movement and leave the cursor alone
     * while the dev panel is open) and REVERTED after a field-reported regression: closing the
     * panel left freecam_yaw climbing on its own every frame with no mouse input at all, and
     * looking unresponsive at the same time - both symptoms of the OS cursor being unable to reach
     * this cheat's anchor point, most likely enhanced_resolution's own ClipCursor confinement
     * (focus_guard.c) interacting with the re-anchor on panel close. Reverted to the simple,
     * previously-working shape rather than chase that live.
     *
     * That attempt was solving the wrong problem anyway. The actual complaint was not "I want the
     * panel usable while flying", it was "I have no way OUT of free camera once the mouse is
     * claimed" - the panel is unreachable without a working cursor and Escape does nothing while
     * this cheat has the simulation paused, so a player who did not already know a hotkey existed
     * was simply stuck. The exit hotkey below is the actual fix: keyboard only, needs no cursor at
     * all, and does not touch how the mouse behaves while flying. Whether the panel itself is ever
     * usable mid-flight is a separate question, unresolved, and no longer the one that mattered. */
    if (freecam_hotkey != 0 && is_game_foreground()) {
        bool hotkey_down = (GetAsyncKeyState(freecam_hotkey) & 0x8000) != 0;

        if (hotkey_down && !freecam_hotkey_was_down) {
            freecam_hotkey_was_down = hotkey_down;
            own_state.cheats[CHEATS_OWN_FREECAM].on = false;
            return;   /* the falling-edge cleanup above runs on the NEXT call to this hook */
        }
        freecam_hotkey_was_down = hotkey_down;
    }

    if (is_game_foreground()) {
        float dt;
        POINT cursor_now;

        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            dt = (freecam_perf_frequency.QuadPart > 0)
                     ? (float)((double)(now.QuadPart - freecam_last_tick) /
                               (double)freecam_perf_frequency.QuadPart)
                     : 0.0f;
            freecam_last_tick = now.QuadPart;
        }
        if (dt < 0.0f || dt > 0.25f) {
            dt = 0.0f;   /* a stall, or the very first tick since the rising edge above */
        }

        /* Mouse look: cursor-delta polling rather than enhanced_input's own raw-input thread.
         * Reusing that thread would mean either reaching into another mod's DLL (this project's
         * mods do not depend on each other) or duplicating its hard-won shim workaround for
         * WMAIN.EXE's own application-compatibility fix (see raw_mouse.c's own header comment) -
         * both worse than the jitter this simpler path accepts for what is a debug camera, not
         * competitive aim. */
        if (GetCursorPos(&cursor_now)) {
            long dx = cursor_now.x - freecam_cursor_anchor.x;
            long dy = cursor_now.y - freecam_cursor_anchor.y;

            if (dx != 0 || dy != 0) {
                /* Yaw (X) field-tested inverted from the first build and flipped, and stayed
                 * flipped - confirmed correct. Pitch (Y) was flipped in that same round on the
                 * assumption both axes were backward together; field testing showed that guess
                 * wrong - X's flip was right, Y's undid a pitch sign that was already correct - so
                 * this puts pitch back to its first-build sign while keeping yaw's fix. */
                freecam_yaw   -= (float)dx * FREECAM_MOUSE_DEGREES_PER_COUNT;
                freecam_pitch -= (float)dy * FREECAM_MOUSE_DEGREES_PER_COUNT;
                if (freecam_pitch > FREECAM_PITCH_LIMIT) {
                    freecam_pitch = FREECAM_PITCH_LIMIT;
                }
                if (freecam_pitch < -FREECAM_PITCH_LIMIT) {
                    freecam_pitch = -FREECAM_PITCH_LIMIT;
                }
                SetCursorPos(freecam_cursor_anchor.x, freecam_cursor_anchor.y);
            }
        }

        /* Scroll wheel adjusts fly speed, the same feel Blender's own fly/walk navigation uses.
         * overlay_input.c observes WM_MOUSEWHEEL unconditionally (panel open or closed) precisely
         * because this needs it while FLYING, which is exactly when the panel is closed - see that
         * file's own comment for how it confirmed the message actually reaches its hook (the
         * engine's top-level window procedure special-cases only four message types and falls
         * through to the same registered-handler chain for everything else, wheel included).
         * Multiplicative per notch rather than additive, so the same scroll feels proportionate
         * whether the current speed is barely-crawling or already fast. wheel_source is NULL if
         * dev_overlay.c never wired it in (its own site did not resolve) - then this simply never
         * fires, the same as every other optional site in this file failing quietly. */
        if (wheel_source != NULL) {
            int32_t wheel = wheel_source();

            if (wheel != 0) {
                float notches = (float)wheel / (float)WHEEL_DELTA;

                freecam_speed *= powf(FREECAM_SPEED_PER_NOTCH, notches);
                if (freecam_speed < FREECAM_MIN_SPEED) {
                    freecam_speed = FREECAM_MIN_SPEED;
                }
                if (freecam_speed > FREECAM_MAX_SPEED) {
                    freecam_speed = FREECAM_MAX_SPEED;
                }
            }
        }

        /* WASD along the full 3D view direction (so looking up while holding W climbs, exactly
         * the free-fly feel the pitch-redirect attempt on the PLAYER tried and failed at - the
         * difference is that nothing here is fighting a third-person camera's own resting angle,
         * because nothing here is a third-person camera anymore, it is this cheat's own camera).
         * E/Q add a world-vertical on top, independent of pitch, for straight up/down without
         * needing to look at the sky or the floor first.
         *
         * FIELD-TESTED, WRONG, THEN FIXED FROM THE ENGINE'S OWN CODE RATHER THAN A SECOND GUESS.
         * The first build of this used fwd_x=+sin(yaw)*cos(pitch) and right_y=-sin(yaw), a self-
         * consistent guess with no independent evidence behind it. Field test: "W doesn't always
         * go forward" - correct near yaw=0, where sin(0)=0 hides the error, and increasingly wrong
         * as yaw grew. Rather than guess a second time, this is now read out of the engine itself:
         * FUN_004181b9 (the function that builds the render eye position every frame) calls
         * FUN_0047cfbc, a generic euler-degrees-to-rotation-matrix utility used at roughly fifty
         * sites across the binary, whose matrix decodes to
         *
         *     right   = ( cos(yaw),             sin(yaw),            0          )
         *     forward = (-sin(yaw)*cos(pitch),   cos(yaw)*cos(pitch), sin(pitch))
         *
         * at zero roll (this camera's roll, +0x3c, is never written by anything this file found).
         * Independently cross-checked against the game's OWN built-in debug free-cam (arrow keys,
         * FUN_004190c1 -> FUN_00418349): its translation is worldX += dx*cos(yaw)-dy*sin(yaw),
         * worldY += dx*sin(yaw)+dy*cos(yaw), which for pure forward (dx=0,dy=1) gives exactly
         * (-sin(yaw), cos(yaw)), matching the formula above at pitch=0. Two independent sites
         * agree, which is what "confirmed" means in this file rather than "guessed once more". */
        {
            float forward = 0.0f;
            float strafe  = 0.0f;
            float vertical = 0.0f;

            if ((GetAsyncKeyState('W') & 0x8000) != 0) { forward  += 1.0f; }
            if ((GetAsyncKeyState('S') & 0x8000) != 0) { forward  -= 1.0f; }
            if ((GetAsyncKeyState('D') & 0x8000) != 0) { strafe   += 1.0f; }
            if ((GetAsyncKeyState('A') & 0x8000) != 0) { strafe   -= 1.0f; }
            if ((GetAsyncKeyState('E') & 0x8000) != 0) { vertical += 1.0f; }
            if ((GetAsyncKeyState('Q') & 0x8000) != 0) { vertical -= 1.0f; }

            if (forward != 0.0f || strafe != 0.0f || vertical != 0.0f) {
                float yaw_rad   = freecam_yaw * DEG_TO_RAD;
                float pitch_rad = freecam_pitch * DEG_TO_RAD;
                float cos_yaw   = cosf(yaw_rad);
                float sin_yaw   = sinf(yaw_rad);
                float cos_pitch = cosf(pitch_rad);
                float sin_pitch = sinf(pitch_rad);
                float fwd_x     = -sin_yaw * cos_pitch;
                float fwd_y     = cos_yaw * cos_pitch;
                float fwd_z     = sin_pitch;
                float right_x   = cos_yaw;
                float right_y   = sin_yaw;
                float planar    = sqrtf(forward * forward + strafe * strafe);
                float scale     = (planar > 1.0f) ? (1.0f / planar) : 1.0f;   /* no diagonal boost */
                float speed     = freecam_speed * dt;

                freecam_x += (fwd_x * forward + right_x * strafe) * scale * speed;
                freecam_y += (fwd_y * forward + right_y * strafe) * scale * speed;
                freecam_z += fwd_z * forward * scale * speed + vertical * speed;
            }
        }
    }

    *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET)    = freecam_x;
    *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET)    = freecam_y;
    *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET)    = freecam_z;
    *(float *)((uint8_t *)view + CAMERA_EULER_PITCH_OFFSET) = freecam_pitch;
    *(float *)((uint8_t *)view + CAMERA_EULER_YAW_OFFSET)   = freecam_yaw;
}


/* Free camera needs both sites - the pause flag and the camera object pointer - to mean anything:
 * a camera that could roam but never stopped the world moving underneath it is not this feature,
 * and neither is a pause with nothing to look through. A partial resolve here is not offered as
 * half a feature; either both resolve or the cheat says why and stays unavailable. */
bool install_freecam(void)
{
    uintptr_t view_site;
    uintptr_t update_site;
    uint32_t  view_address = 0;

    /* Idempotent, and called here rather than assumed: dev_overlay.c installs this too, but
     * whichever of the two runs first, both need it resolved and neither should rely on the other
     * having got there. Asking the one owner is also what stops this feature and the panel
     * disagreeing about whether the cell is available at all. */
    if (!sim_pause_install()) {
        log_warning("free camera: the simulation pause gate did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }

    view_site = signature_find_unique(SIG_CAMERA_VIEW, MSK_CAMERA_VIEW, sizeof SIG_CAMERA_VIEW);
    if (view_site == 0 ||
        !memory_read_u32(view_site + OFFSET_CAMERA_VIEW_OBJECT, &view_address) ||
        !memory_is_inside_image(view_address, sizeof(void *))) {
        log_warning("free camera: the camera object site did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }

    update_site = signature_find_detour_target(SIG_CAMERA_UPDATE, MSK_CAMERA_UPDATE,
                                               sizeof SIG_CAMERA_UPDATE,
                                               CAMERA_UPDATE_PROLOGUE_SIZE);
    if (update_site == 0) {
        log_warning("free camera: the camera update site did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }
    if (!detour_install(&own_state.camera_update_detour, update_site,
                        (const void *)&hook_camera_update, CAMERA_UPDATE_PROLOGUE_SIZE)) {
        log_warning("free camera: the camera update site at %08X could not be detoured, that "
                    "cheat stays unavailable", (unsigned)update_site);
        return false;
    }

    own_state.camera_update_original = (camera_update_fn_t)own_state.camera_update_detour.original;
    own_state.camera_view_address    = (uintptr_t)view_address;
    log_info("free camera: pausing through sim_pause, camera object pointer at %08X, update "
             "chained at %08X - WASD moves along the view, mouse looks, E/Q move vertically",
             (unsigned)view_address, (unsigned)update_site);
    return true;
}

