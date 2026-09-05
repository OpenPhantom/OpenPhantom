/* cheats_free_camera.c: the free camera, its two engine sites and the state it flies with.
 *
 * It holds the simulation through sim_pause rather than by any means of its own, and writes the
 * camera pose after the engine has composed it rather than fighting the original for the fields.
 *
 * ==============================================================================================
 * SIZE NOTE
 *
 * Over the 600 line mark, under the 900 hard limit, and the size is not really the file's.
 * One function, hook_camera_update, is most of it. The rest of the file is a pair of signatures,
 * the hotkey, the wheel source and the install pass, and none of that is large.
 *
 * So the honest seam is not between two halves of this file, it is inside that function, which
 * is itself well past the 150 line mark where the rules say split. It reads input, decides a
 * mode, moves an eye, asks the floor probe whether there is ground under the player, and then
 * writes the pose the engine has just composed. Those are four jobs and they could be four
 * functions.
 *
 * That is a decomposition rather than a move: it changes where state lives between steps that
 * currently share one stack frame, so it can change behaviour, and the free camera is flown by
 * hand rather than covered by a test. It is left whole here deliberately, and named so the next
 * person does not have to work out why the file is long.
 *
 * Unrelated to the size, and worth a separate pass: this file's own evidence for the two camera
 * sites is duplicated in cheats_jump_boost.c, left there when these were one file.
 *
 * Split out of cheats_openphantom.c; nothing changed in the move. */
#include "cheats_openphantom.h"
#include "cheats_internal.h"
#include "floor_probe.h"

#include "sim_pause.h"
#include "overlay_input.h"

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

/* 0x00418544, nine bytes: push ebp / mov ebp,esp / sub esp,0x88, identical to camera_sites.c's
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
 * and the euler updateCam just wrote, and not taken on the strength of an earlier report alone.
 * Read only once, at free camera's own rising edge, to compute a look-at rather than to drive
 * anything ongoing; see that site's own comment for why. */
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

/* GetAsyncKeyState reads the physical key regardless of which window has it, so without this the
 * player would drift while the game sits alt-tabbed in the background and something else entirely
 * is holding E or Q. Compares the foreground window's owning process to this one instead of
 * tracking a HWND of our own, which needs nothing set up anywhere else in this DLL. */
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

/* NULL until dev_overlay.c wires it in; see this pointer's own type, declared in
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

/* The bound key ends the flight and brings the player to the camera. It was the plain exit
 * key first; the teleport is the thing worth binding, and F4 covers leaving without moving.
 *
 * Free camera's own mouse look claims the cursor, which leaves both the dev panel
 * and the game's own pause menu unreachable: no cursor, no click, and Escape does not work either
 * while the simulation this cheat paused is what would normally answer it. Without a keyboard-only
 * way out, turning this cheat on is a one-way door. 0 = unbound; the panel's hotkey row (see
 * overlay_model.c) is how a key gets into this. */
static int32_t freecam_hotkey;
static bool    freecam_hotkey_was_down;

/* Two keys, two intentions, so neither needs a toggle explaining which is armed. The bound key
 * (see the hotkey row in overlay_model.c) means "the player comes HERE"; F4 means "put it back,
 * nobody moved". The teleport is the one on a key of the player own choosing because it is what
 * the camera is usually being flown for; F4 is fixed because a fallback that always means the same
 * thing is worth more than one more thing to configure.
 *
 * This is the fly-somewhere-and-continue workflow the old noclip was for, without any of what sank
 * it: one position write while the simulation is still frozen, not a player simulating against its
 * own state machine while something else drags it around. */
#define FREECAM_RETURN_KEY 0x73          /* VK_F4. Alt+F4 still closes the game: Alt is not read */

/* How far above the player the camera may be and still bring them to it, in world units.
 *
 * MEASURED AGAINST THE PLAYER, NOT PROBED UNDER THE CAMERA, and that is the correction. The
 * first version of this asked the engine's floor probe what was under the camera. That probe is
 * scoped to the cell its point sits in, and a camera flown above the level is in no cell, so it
 * answered "no floor" for solid ground and the teleport was refused almost every time it was
 * wanted. A field session logged twelve refusals, every one of them that false void, and the
 * only teleports that got through were within a few units of standing height.
 *
 * The player is always inside the world, so their own height is a reference that cannot lie.
 * The camera's height above it is not the exact drop, since the ground under the camera may be
 * lower still, but it is the right shape of number and it is never wrong about the direction.
 *
 * EIGHTY IS MEASURED, NOT DERIVED, AND THAT DISTINCTION COST A CRASH. It was briefly raised to
 * 350 on the reasoning that the fall grace in cheats_fall_consequences.c suppresses the landing
 * damage and both deaths for ten seconds, and that ten seconds of falling at 40 units/s^2
 * clamped to 40 units/s (player+0x80, and 0x004a86f8) covers 380 units. That arithmetic is
 * correct and it answers the wrong question. The grace decides whether the player SURVIVES the
 * landing. It says nothing about whether the engine can run the fall at all.
 *
 * Field evidence, from a session logged at successively greater heights: teleports at 30.0,
 * 51.5 and 72.3 were fine, and one at 110.8 over ground at roughly 25 to 30, so a drop of
 * about 85 units, took the game down. That sits just past this limit, which is where the
 * earlier note had already put it from the engine's own fall thresholds.
 *
 * For scale, in these same units the engine takes fall damage at 3.5, kills at 6, and calls a
 * fall significant at 8, and a level's fog band runs 8 to 32. Eighty is ten times the point at
 * which a fall becomes serious to this engine, so every ordinary rooftop or cliff drop goes
 * through and only a flight well above the level is refused. Do not raise it again without a
 * session that actually survives the higher number. */
#define FREECAM_MAX_TELEPORT_DROP 80.0f
static bool freecam_teleport_pending = false;   /* raised by the BOUND key, read on the way out */
static bool freecam_return_was_down  = false;


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
 * chain rather than a break in it; enhanced_input's own free-look feature is chained on this exact
 * same site and must keep running underneath this one regardless of what this cheat is doing. */
static void __cdecl hook_camera_update(void)
{
    void **view_slot;
    void  *view;


    own_state.camera_update_original();

    if (!own_state.cheats[CHEATS_OWN_FREECAM].on) {
        if (freecam_valid) {
            /* The teleport, if the bound key asked for it, and before the simulation is released: one
             * write into a world that is still frozen, so the player's own physics resumes FROM
             * the new place rather than being fought at it. That ordering is the whole difference
             * between this and the noclip this feature replaced.
             *
             * The position is all that moves. Velocity, mode and heading keep whatever they held
             * when flight began, so stepping out mid-air is a fall from wherever the camera was
             * left, which is the point of the key. The follow camera needs nothing either way:
             * the very next updateCam recomputes it from the player, wherever the player now is.
             *
             * NOTHING IS REFUSED HERE, AND THAT IS A CORRECTION. A floor test was added and then
             * taken back out. It asked the engine's own probe whether there was ground under the
             * camera and declined the teleport when there was not, or when the drop was over
             * eighty units. Both refusals fired constantly in ordinary use, because that probe is
             * scoped to the CELL its point sits in: fly above the level and the point is in no
             * cell, so no floor polygon is ever offered and the answer is "void" whatever is
             * really underneath. A field session logged twelve refusals and not one was the height
             * cap; every one was that false void, and every teleport that did go through was
             * within a few units of standing height. Flying up and dropping the player in is what
             * this key is for, so a guard that removes it whenever the camera leaves the level's
             * own cells costs more than the fall it was guarding against. */
            if (freecam_teleport_pending) {
                void **player_slot = (void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;
                uint8_t *player = (player_slot != NULL) ? (uint8_t *)*player_slot : NULL;

                /* Too high to survive the arrival? Asked before anything is written, because refusing
                 * has to leave the player exactly where they were. A refusal then falls through to the
                 * same path the plain return key takes, so the camera snaps back to the player and the
                 * key reads as "not from here" rather than as a key that did nothing.
                 *
                 * Height only. There is no floor probe here on purpose; see the note on
                 * FREECAM_MAX_TELEPORT_DROP for the one that was tried and what it cost. */
                if (player != NULL) {
                    const float *standing = (const float *)(player + PLAYER_POSITION_OFFSET);
                    float        above    = freecam_z - standing[2];
                    float        player_air = 0.0f;

                    /* The player may be off the ground themselves, mid-fall from a previous teleport or
                     * stood on something with a long way down. Then the drop is farther than the
                     * camera's height above them and measuring only that would wave through exactly the
                     * fall this refuses. The probe is asked HERE, at the player, and that is the one
                     * place it can be trusted: the player is inside the world, so the cell lookup it
                     * depends on succeeds. Asking it about the camera is what broke this before. */
                    if (floor_probe_below(standing, &player_air) == FLOOR_PROBE_FOUND) {
                        above += player_air;
                    }

                    if (above > FREECAM_MAX_TELEPORT_DROP) {
                        log_info("the teleport was refused and the camera returned instead: it is %.0f "
                                 "units of drop, past the %.0f this engine can finish a fall from",
                                 (double)above, (double)FREECAM_MAX_TELEPORT_DROP);
                        player = NULL;
                    }
                }

                if (player != NULL) {
                    /* BOTH copies of the position, and this is the whole of why dropping from
                     * height did not work. Plr_CommitPose (0x0044C06B) opens with
                     *
                     *     if (pPlayer+0xA0 != 0) { +0x118 = +0x124; ... }
                     *
                     * so on any frame the player is moving, pos is replaced wholesale by
                     * desiredPos. Writing pos alone survived only while the player happened to
                     * be standing still. The moment the movement phase ran it recomputed
                     * desiredPos from our new position with its own ground resolution applied,
                     * committed that back over pos, and the player arrived at the right x and y
                     * planted on the floor. Writing desiredPos ALONE was tried even earlier and
                     * left them where they started, which is the same bug from the other side.
                     * Both, and neither copy can put them back.
                     *
                     * The camera's own Z, deliberately. Standing the player on the floor beneath
                     * the camera was tried and taken back out: dropping somebody in from height
                     * is the thing people want this key for. Nothing bounds the drop either; see
                     * the note above for the guard that was tried there and what it cost. */
                    float *position = (float *)(player + PLAYER_POSITION_OFFSET);
                    float *desired  = (float *)(player + PLAYER_DESIRED_POSITION_OFFSET);
                    float *vertical = (float *)(player + PLAYER_VERTICAL_VELOCITY_OFFSET);

                    position[0] = desired[0] = freecam_x;
                    position[1] = desired[1] = freecam_y;
                    position[2] = desired[2] = freecam_z;

                    /* From rest. The resolver decays this by gravity every step and adds it to
                     * the height, so zero is a fall that starts the instant the world resumes.
                     * Left alone it would carry whatever the player held when flight began,
                     * which for somebody who was running is a sideways launch rather than a
                     * drop. */
                    *vertical = 0.0f;

                    /* The arrival is a FALL, from whatever altitude the camera was flown to, and
                     * it is not one the player chose to take. The same five consequences jump
                     * boost suppresses are suppressed for it, ending on the first landing. See
                     * cheats_openphantom_grant_fall_grace. */
                    cheats_openphantom_grant_fall_grace();

                    /* F4 means "put me there and play", and the panel holds the simulation just as
                     * the camera does; without this the move would not resolve until the panel was
                     * closed by hand, which is what made the first attempt look like it did nothing
                     * until then. */
                    overlay_input_close();

                    log_info("the free camera teleport key dropped the player at %.1f %.1f %.1f",
                             (double)freecam_x, (double)freecam_y, (double)freecam_z);
                } else if (player_slot == NULL || *player_slot == NULL) {
                    log_warning("the teleport key asked to drop the player at the camera, but "
                                "there is no player record to move; the camera returned instead");
                }
            }
            freecam_teleport_pending = false;

            /* Falling edge: hand the world back. The camera itself needs no un-write, because the very
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
         * snaps the view, and pause the simulation; see sim_pause.h for why
         * this one flag is enough to stop the player and the whole world simulating out from
         * under a camera that is no longer looking through the player's own eyes. */
        freecam_teleport_pending = false;
        freecam_return_was_down  = (GetAsyncKeyState(FREECAM_RETURN_KEY) & 0x8000) != 0;
        freecam_x = *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET);
        freecam_y = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET);
        freecam_z = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET);

        /* ORIENTATION is computed fresh, a look-at from the retail camera's own eye position
         * toward its anchor (which tracks the player every frame; see updateCam's own site
         * comment above), rather than copied from the raw euler fields the way position is.
         * Field-reported: switching free camera on could start it pointing "at the sky". The raw
         * pitch is periodic (FUN_004181b9 wraps it every frame, confirmed by decompiling it) so
         * that alone should not have caused it, since sin/cos already handle any wrap correctly, which
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
        /* Discard whatever the wheel accumulated while this cheat was off; overlay_input.c
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
     * looking unresponsive at the same time, both symptoms of the OS cursor being unable to reach
     * this cheat's anchor point, most likely enhanced_resolution's own ClipCursor confinement
     * (focus_guard.c) interacting with the re-anchor on panel close. Reverted to the simple,
     * previously-working shape rather than chase that live.
     *
     * That attempt was solving the wrong problem anyway. The actual complaint was not "I want the
     * panel usable while flying", it was "I have no way OUT of free camera once the mouse is
     * claimed"; the panel is unreachable without a working cursor and Escape does nothing while
     * this cheat has the simulation paused, so a player who did not already know a hotkey existed
     * was simply stuck. The bound key below is the actual fix: keyboard only, needs no cursor at
     * all, and does not touch how the mouse behaves while flying. Whether the panel itself is ever
     * usable mid-flight is a separate question, unresolved, and no longer the one that mattered. */
    /* The bound key brings the player. It ends the flight and drops them wherever the camera
     * is, which is what the camera is usually being flown FOR, so it is the action worth putting
     * on a key of the player's own choosing. The plain return is on F4 below.
     *
     * The write itself happens on the falling edge above, one call later, so both keys share a
     * single exit path and neither has to know how the other ends the flight. */
    if (freecam_hotkey != 0 && is_game_foreground()) {
        bool hotkey_down = (GetAsyncKeyState(freecam_hotkey) & 0x8000) != 0;

        if (hotkey_down && !freecam_hotkey_was_down) {
            freecam_hotkey_was_down = hotkey_down;
            freecam_teleport_pending = true;
            own_state.cheats[CHEATS_OWN_FREECAM].on = false;
            return;   /* the falling-edge cleanup above runs on the NEXT call to this hook */
        }
        freecam_hotkey_was_down = hotkey_down;
    }

    /* F4 leaves without moving anybody, which is the other half of what a free camera is for:
     * looking at something and then carrying on from where you actually were. Fixed rather than
     * bindable because it is the fallback, and because a fixed key that always means "put it back"
     * is worth more than one more thing to configure.
     *
     * Edge-detected like the bound key, and its held state is seeded at the rising edge below for
     * the same reason: a key already down when the flight begins is not a request. */
    if (is_game_foreground()) {
        bool return_down = (GetAsyncKeyState(FREECAM_RETURN_KEY) & 0x8000) != 0;

        if (return_down && !freecam_return_was_down) {
            freecam_return_was_down = return_down;
            own_state.cheats[CHEATS_OWN_FREECAM].on = false;
            return;   /* no pending teleport: the falling edge just hands the world back */
        }
        freecam_return_was_down = return_down;
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

            /* THIS CHEAT IS NOT THE ONLY THING THAT MOVES THE POINTER, and every unrecoverable
             * dive reported against this camera has come from assuming it is.
             *
             * Three others write it. focus_guard.c confines it with ClipCursor, so a warp to a
             * point outside the cage lands somewhere else entirely. The engine recentres it
             * itself: control_recentreMouse at 0x0046A115 ends in `push 0xF0 / push 0x140 /
             * call SetCursorPos`, a fixed screen point that cursor_anchor.c repoints at the
             * window but does not remove. And the panel is now held open for the whole flight,
             * so its own pointer handling runs alongside this.
             *
             * Either of those turns the difference below into a CONSTANT that is not hand
             * movement and that arrives again on the next frame, and the frame after. A steady
             * dy drives pitch onto its own clamp and pins it there: the camera stares at the
             * floor, W flies into it, and no hand movement lifts it because the bias comes back
             * before the next frame is drawn. It reads as the camera diving under the map and
             * refusing to come up, which is exactly how it was reported, and the reporter
             * confirmed these two guards fixed it on the rig it happened on. It was never
             * reproduced here, which is the point: the writer this collides with is not
             * something every machine has.
             *
             * A file-level comment further up already records this failing once before, when
             * panel-aware handling left the yaw climbing on its own with no mouse input. It was
             * reverted rather than fixed, so the trap stayed set.
             *
             * Two guards, and between them they close it whoever else is writing:
             *
             * ONE, a jump this large is not a hand. A quarter of the screen in a single frame
             * is around seventy degrees of turn at this sensitivity, which nobody asks for on
             * purpose, while somebody else's warp clears it easily. Such a frame contributes no
             * rotation at all and simply re-syncs the anchor, so the next frame measures real
             * movement from wherever the pointer was put.
             *
             * TWO, the anchor follows the pointer rather than the request. Where SetCursorPos
             * actually landed is read back below, because a cage can refuse the position asked
             * for and a stale anchor then measures the same refusal for ever.
             *
             * The floor of 64 keeps this sane if the metrics come back as nothing, which they
             * do on a session with no desktop to ask about. */
            long limit_x = (long)GetSystemMetrics(SM_CXSCREEN) / 4;
            long limit_y = (long)GetSystemMetrics(SM_CYSCREEN) / 4;

            if (limit_x < 64) { limit_x = 64; }
            if (limit_y < 64) { limit_y = 64; }
            if ((dx > limit_x) || (dx < -limit_x) || (dy > limit_y) || (dy < -limit_y)) {
                freecam_cursor_anchor = cursor_now;
                dx = 0;
                dy = 0;
            }

            if (dx != 0 || dy != 0) {
                /* Yaw (X) field-tested inverted from the first build and flipped, and stayed
                 * flipped, confirmed correct. Pitch (Y) was flipped in that same round on the
                 * assumption both axes were backward together; field testing showed that guess
                 * wrong: X's flip was right, Y's undid a pitch sign that was already correct, so
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
                /* Where it LANDED, which is not always where it was sent; see above. */
                (void)GetCursorPos(&freecam_cursor_anchor);
            }
        }

        /* Scroll wheel adjusts fly speed, the same feel Blender's own fly/walk navigation uses.
         * overlay_input.c observes WM_MOUSEWHEEL unconditionally (panel open or closed) precisely
         * because this needs it while FLYING, which is exactly when the panel is closed; see that
         * file's own comment for how it confirmed the message actually reaches its hook (the
         * engine's top-level window procedure special-cases only four message types and falls
         * through to the same registered-handler chain for everything else, wheel included).
         * Multiplicative per notch rather than additive, so the same scroll feels proportionate
         * whether the current speed is barely-crawling or already fast. wheel_source is NULL if
         * dev_overlay.c never wired it in (its own site did not resolve), and then this simply never
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
         * the free-fly feel the pitch-redirect attempt on the PLAYER tried and failed at; the
         * difference is that nothing here is fighting a third-person camera's own resting angle,
         * because nothing here is a third-person camera anymore, it is this cheat's own camera).
         * E/Q add a world-vertical on top, independent of pitch, for straight up/down without
         * needing to look at the sky or the floor first.
         *
         * FIELD-TESTED, WRONG, THEN FIXED FROM THE ENGINE'S OWN CODE RATHER THAN A SECOND GUESS.
         * The first build of this used fwd_x=+sin(yaw)*cos(pitch) and right_y=-sin(yaw), a self-
         * consistent guess with no independent evidence behind it. Field test: "W doesn't always
         * go forward". Correct near yaw=0, where sin(0)=0 hides the error, and increasingly wrong
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


/* Free camera needs both sites, the pause flag and the camera object pointer, to mean anything:
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
             "chained at %08X; WASD moves along the view, mouse looks, E/Q move vertically",
             (unsigned)view_address, (unsigned)update_site);
    return true;
}

