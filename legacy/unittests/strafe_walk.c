/* strafe_walk.c: the pure logic of enhanced_input, checked without the game.
 *
 * Four things live here, and each of them exists because getting it wrong is invisible until the
 * game is running:
 *
 *   * THE TRAVEL ANGLE. The integrator multiplies the facing by a SIGNED speed, so walking
 *     backward is a negative speed along an unchanged facing. An angle built as "where do I want to
 *     go, relative to forward" double-counts that reversal, and holding only the back key would
 *     send the player FORWARD while the backward clip played. The backward rows are the reason this
 *     file was written.
 *
 *   * THE DAMPER. Its whole point is to be independent of the substep, because the substep is
 *     1/32 s or 1/64 s depending on a flag and a setting in another DLL. A damper whose factor was
 *     computed from a compile-time constant would settle twice as fast and cap twice as high in the
 *     other configuration, and nothing would say so.
 *
 *   * THE MOUSE BANK. Consume-and-zero is what makes at most one substep per frame receive a
 *     non-zero step, which is the whole safety argument for the 90-degree cap. The dormancy rule is
 *     the pause guard: while the game is paused the engine skips the poll, so the axis reader keeps
 *     answering the same non-zero number for as long as the pause lasts.
 *
 *   * FREE LOOK. Its input angle is the SAME signed-speed trap as the travel angle above, arriving
 *     from the other side: there the reversal is divided out of the angle, here it must be left in,
 *     and swapping the two rules sends the player forward while the backward key is held. Its
 *     arming gate is the one piece of logic in this DLL whose failure would reach a player as a
 *     permanently rotated cutscene, so every one of its conditions is checked on its own, and,
 *     because a release is invisible from the outside, so is the NAME each one answers with and the
 *     family it belongs to. A cutscene filed as an authored region would have a minute-old mouse
 *     angle restored on top of a deliberately placed shot.
 *
 * SIZE NOTE (rule 9): 578 lines. It is one test binary for one DLL's pure logic, and splitting it
 * would put the travel angle and free look's input angle, which are the same trap seen from two
 * sides, and which are only meaningful next to each other, into different files. Each check
 * carries the sentence that says what breaks when it fails, because a test named `case_17` teaches
 * the next reader nothing; that prose is most of the length.
 */
#include "unittest.h"

#include "free_look_math.h"
#include "mouse_look.h"
#include "strafe_walk.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define RIGHT      1.0f
#define LEFT      (-1.0f)
#define NONE       0.0f
#define FORWARD    1.0f
#define BACKWARD  (-1.0f)
#define DRIVE_FWD  1.0f
#define DRIVE_BACK (-1.0f)

/* A value that is not a number, built through the standard macro rather than by
 * dividing zero by zero: the compiler rejects the latter as a constant expression. */
#define NOT_A_NUMBER ((float)NAN)

/* The two substeps the engine can actually hand out. */
#define SUBSTEP_32 (1.0f / 32.0f)
#define SUBSTEP_64 (1.0f / 64.0f)

/* The shipped damper settings. */
#define SETTLE   0.25f
#define RATE   240.0f

static void check_angle(float strafe, float forward, float drive_sign,
                        float expected, const char *what)
{
    ut_near(strafe_walk_travel_offset(strafe, forward, drive_sign), expected, 0.001f, what);
}

/* Runs the damper for `seconds` of wall time in steps of `substep`, from `start` toward `target`. */
static float damp_for(float start, float target, float substep, float seconds,
                      float settle, float rate)
{
    float current = start;
    int   steps   = (int)(seconds / substep + 0.5f);
    int   index;

    for (index = 0; index < steps; ++index) {
        current = strafe_walk_damp_step(current, target, substep, settle, rate);
    }
    return current;
}

static void test_travel_angle(void)
{
    ut_section("the travel angle");

    /* Nothing held, and the walk-forward case: the feature must be invisible. */
    check_angle(NONE, NONE,     DRIVE_FWD,   0.0f, "no input is no offset");
    check_angle(NONE, FORWARD,  DRIVE_FWD,   0.0f, "forward alone is unchanged");

    /* The row that matters. Backward alone must be a true no-op, or vanilla backpedalling breaks. */
    check_angle(NONE, BACKWARD, DRIVE_BACK,  0.0f, "backward alone is unchanged");

    /* A lone sideways key. `forward` is the player's OWN input and stays 0 even though a walk is
     * forced on their behalf, which is what makes this a right angle rather than a diagonal. */
    check_angle(RIGHT, NONE, DRIVE_FWD, -90.0f, "right alone walks to the right");
    check_angle(LEFT,  NONE, DRIVE_FWD, +90.0f, "left alone walks to the left");

    /* Diagonals, forward. Positive is left, so right is negative. */
    check_angle(RIGHT, FORWARD, DRIVE_FWD, -45.0f, "forward and right is a right diagonal");
    check_angle(LEFT,  FORWARD, DRIVE_FWD, +45.0f, "forward and left is a left diagonal");

    /* Diagonals, backward, and the signs INVERT against the forward pair, because the drive
     * itself is reversed. Getting these two the same way round as the forward pair is exactly the
     * defect this file exists to catch. */
    check_angle(RIGHT, BACKWARD, DRIVE_BACK, +45.0f, "backward and right leans the drive left");
    check_angle(LEFT,  BACKWARD, DRIVE_BACK, -45.0f, "backward and left leans the drive right");

    /* The angle is symmetric and never leaves the half turn the walk cycle can carry. */
    check_angle(RIGHT, FORWARD, DRIVE_FWD, -strafe_walk_travel_offset(LEFT, FORWARD, DRIVE_FWD),
                "left and right are mirror images");
}

static void test_damper(void)
{
    float at_32;
    float at_64;
    float current;
    int   steps;

    ut_section("the damper");

    /* FRAMERATE INDEPENDENCE, with the rate cap lifted out of the way so the exponential alone is
     * under test. Equal wall time must give equal angle whatever the substep. */
    at_32 = damp_for(0.0f, 90.0f, SUBSTEP_32, 0.25f, SETTLE, 1.0e9f);
    at_64 = damp_for(0.0f, 90.0f, SUBSTEP_64, 0.25f, SETTLE, 1.0e9f);
    ut_near(at_32, at_64, 0.01f, "1/32 and 1/64 agree after a settle time, cap lifted");

    /* And that value is the definition of the setting: 90 % of the gap is gone. */
    ut_near(at_32, 81.0f, 0.05f, "one settle time closes 90 % of the gap");
    ut_near(damp_for(0.0f, 90.0f, SUBSTEP_32, 0.50f, SETTLE, 1.0e9f), 89.1f, 0.05f,
                "two settle times close 99 %");

    /* THE SAME, with the shipped cap in force. While the cap is what limits the step the two
     * substeps advance at the same rate, so they must still agree. */
    at_32 = damp_for(0.0f, 90.0f, SUBSTEP_32, 0.25f, SETTLE, RATE);
    at_64 = damp_for(0.0f, 90.0f, SUBSTEP_64, 0.25f, SETTLE, RATE);
    ut_near(at_32, at_64, 0.01f, "1/32 and 1/64 agree with the shipped rate cap");
    ut_near(at_32, RATE * 0.25f, 0.01f, "a 90-degree gap is rate-limited, not eased");

    /* THE RATE CAP itself: one step can never travel further than the rate allows, and the cap is
     * a RATE, so it halves with the substep. */
    ut_near(strafe_walk_damp_step(0.0f, 90.0f, SUBSTEP_32, SETTLE, RATE),
                RATE * SUBSTEP_32, 0.001f, "the first step at 1/32 is the rate cap");
    ut_near(strafe_walk_damp_step(0.0f, 90.0f, SUBSTEP_64, SETTLE, RATE),
                RATE * SUBSTEP_64, 0.001f, "the first step at 1/64 is half of it");

    /* IT SETTLES, exactly, and in finite time, which is what lets the body latch be given up. */
    current = 0.0f;
    for (steps = 0; steps < 1000 && current != 90.0f; ++steps) {
        current = strafe_walk_damp_step(current, 90.0f, SUBSTEP_32, SETTLE, RATE);
    }
    ut_check(current == 90.0f && steps < 1000, "the angle lands exactly on its target");

    /* THE DECAY that clears the latch when Stand is left. It must reach exactly zero, or the model
     * root would be left holding a residue for good. */
    current = 90.0f;
    for (steps = 0; steps < 1000 && current != 0.0f; ++steps) {
        current = strafe_walk_damp_step(current, 0.0f, SUBSTEP_32, SETTLE, RATE);
    }
    ut_check(current == 0.0f && steps < 1000, "the decay to zero lands exactly on zero");
    ut_check(steps < (int)(2.0f / SUBSTEP_32), "and it gets there inside two seconds");

    /* MONOTONE, never overshooting: every step moves toward the target and none past it. */
    current = -90.0f;
    for (steps = 0; steps < 200; ++steps) {
        float next = strafe_walk_damp_step(current, 45.0f, SUBSTEP_32, SETTLE, RATE);
        if (next < current || next > 45.0f) {
            ut_checkf(0, "the damper overshot or went backwards (%+.5f -> %+.5f)",
                   (double)current, (double)next);
            break;
        }
        current = next;
    }
    ut_check(steps == 200, "the damper is monotone and never overshoots");

    /* The two degenerate inputs, both of which have a defined answer rather than an accident. */
    ut_near(strafe_walk_damp_step(10.0f, 90.0f, SUBSTEP_32, 0.0f, RATE), 90.0f, 0.001f,
                "a settle time of zero is the old instant step");
    ut_near(strafe_walk_damp_step(10.0f, 90.0f, 0.0f, SETTLE, RATE), 10.0f, 0.001f,
                "no time passed is no movement");
    ut_near(strafe_walk_damp_step(10.0f, 90.0f, -1.0f, SETTLE, RATE), 10.0f, 0.001f,
                "a negative substep cannot make the damper diverge");
}

static void test_mouse_bank(void)
{
    mouse_bank_t bank;
    float        span = -1.0f;
    float        step;
    int          frame;

    ut_section("the mouse bank");

    /* A fresh bank is disarmed: nothing is collected until somebody has proved it is consuming. */
    mouse_bank_reset(&bank);
    mouse_bank_frame(&bank, 100.0f, 0.007f, 0.125f);
    ut_near(mouse_bank_take(&bank, &span), 0.0f, 0.001f,
                "a bank nobody has consumed from yet collects nothing");

    /* Armed by that consume, it now sums whole frames and hands them over in one piece. */
    mouse_bank_frame(&bank, 3.0f, 0.007f, 0.125f);
    mouse_bank_frame(&bank, 4.0f, 0.007f, 0.125f);
    mouse_bank_frame(&bank, 5.0f, 0.007f, 0.125f);
    ut_near(mouse_bank_take(&bank, &span), 12.0f, 0.001f, "three frames arrive as one step");
    ut_near(span, 0.021f, 0.0001f, "and the span is the wall time they took");

    /* CONSUME AND ZERO. The second substep of the same frame must get nothing, because that is the
     * whole reason a single capped step can never reach the 180-degree ambiguity. */
    ut_near(mouse_bank_take(&bank, &span), 0.0f, 0.001f, "the second consume gets nothing");
    ut_near(span, 0.0f, 0.0001f, "and no span with it");

    /* THE PAUSE GUARD. The poll is skipped while the game is paused, so the reader answers the
     * same non-zero sample every frame. Nothing may be waiting when play resumes. */
    mouse_bank_reset(&bank);
    (void)mouse_bank_take(&bank, &span);            /* arm it, as a running substep would */
    for (frame = 0; frame < 600; ++frame) {         /* four seconds of pause at 150 fps */
        mouse_bank_frame(&bank, 250.0f, 0.0067f, 0.125f);
    }
    ut_near(mouse_bank_take(&bank, &span), 0.0f, 0.001f,
                "a frozen sample cannot pile up while nothing consumes");

    /* And the very next frames are collected again: the bank comes back by itself. */
    mouse_bank_frame(&bank, 7.0f, 0.0067f, 0.125f);
    ut_near(mouse_bank_take(&bank, &span), 7.0f, 0.001f, "the bank re-arms on the next consume");

    /* THE RATE CLAMP. Measured against the bank's own wall time, and hard-capped on top. */
    step = mouse_look_clamp_step(1000.0f, 0.010f, 720.0f, 90.0f);
    ut_near(step, 7.2f, 0.001f, "a spike is limited to the rate times the real span");
    step = mouse_look_clamp_step(-1000.0f, 0.010f, 720.0f, 90.0f);
    ut_near(step, -7.2f, 0.001f, "and symmetrically the other way");
    step = mouse_look_clamp_step(4.0f, 0.010f, 720.0f, 90.0f);
    ut_near(step, 4.0f, 0.001f, "an ordinary movement is not touched");
    step = mouse_look_clamp_step(1000.0f, 1.0f, 720.0f, 90.0f);
    ut_near(step, 90.0f, 0.001f, "the hard cap wins over a long span");
    step = mouse_look_clamp_step(1000.0f, 0.0f, 720.0f, 90.0f);
    ut_near(step, 90.0f, 0.001f, "no span at all falls back to the hard cap, not to zero");

    /* The cap is what keeps the step below the 180-degree ambiguity, so state it as the property
     * it is rather than as an arithmetic coincidence. */
    ut_check(mouse_look_clamp_step(1.0e9f, 1.0e9f, 3600.0f, 90.0f) < 180.0f,
               "no clamped step can reach the ambiguous half turn");
}

/* ==============================================================================================
 * FREE LOOK
 * ============================================================================================ */

/* Every one of the nine conditions open, so a single test can close exactly one of them at a time
 * and prove that this one closes the gate. */
static void arm_gate(free_look_gate_t *gate)
{
    gate->enabled         = true;
    gate->record_valid    = true;
    gate->module_state    = PLAYER_MODULE_PHASES_RUN;
    gate->mode_index      = 0;
    gate->view_valid      = true;
    gate->view_state      = BAPVIEW_STATE_FOLLOW;
    gate->camera_override = 0;
    gate->snap_countdown  = 0;
    gate->region_known    = true;
    gate->region_flags    = 0;
}

static void check_input_angle(float strafe, float forward, float expected, const char *what)
{
    float actual = 0.0f;

    if (!free_look_input_angle(strafe, forward, &actual)) {
        ut_checkf(0, "%s (no angle was produced at all)", what);
        return;
    }
    ut_near(actual, expected, 0.001f, what);
}

static void test_free_look_angles(void)
{
    float angle = -1.0f;

    ut_section("free look: wrapping and the input angle");

    ut_near(free_look_wrap360(0.0f),     0.0f,   0.001f, "wrap360 leaves zero alone");
    ut_near(free_look_wrap360(-90.0f),   270.0f, 0.001f, "wrap360 lifts a negative angle");
    ut_near(free_look_wrap360(450.0f),   90.0f,  0.001f, "wrap360 folds past a full turn");
    ut_near(free_look_wrap360(-720.5f),  359.5f, 0.001f, "wrap360 folds two whole turns down");
    ut_near(free_look_wrap180(190.0f),  -170.0f, 0.001f, "wrap180 takes the short way round");
    ut_near(free_look_wrap180(180.0f),   180.0f, 0.001f, "wrap180 keeps an exact half turn");
    ut_near(free_look_wrap180(-190.0f),  170.0f, 0.001f, "and symmetrically the other way");

    /* A value that is not a number must SURVIVE, not be normalised into a plausible zero: the
     * caller's finiteness gate is what releases the camera, and it can only see what reaches it. */
    ut_check(isnan(free_look_wrap360(NOT_A_NUMBER)) != 0,
               "wrap360 passes a non-number through to the caller's finiteness gate");
    ut_check(!isfinite(free_look_wrap360(HUGE_VALF)),
               "wrap360 of an infinity is not finite either");

    /* The row this section exists for. The sideways walk divides the drive's sign out of its
     * angle because backward is a negative speed along an unchanged facing. Free look turns the
     * body to face its travel and forces the drive forward, so backward must be a HALF TURN. */
    check_input_angle(NONE, BACKWARD, 180.0f, "backward alone is a half turn, not a no-op");
    check_input_angle(NONE, FORWARD,    0.0f, "forward alone is straight ahead");
    check_input_angle(RIGHT, NONE,    -90.0f, "right alone is a right angle to the right");
    check_input_angle(LEFT,  NONE,    +90.0f, "left alone is a right angle to the left");
    check_input_angle(RIGHT, FORWARD, -45.0f, "forward and right is a right diagonal");
    check_input_angle(LEFT,  FORWARD, +45.0f, "forward and left is a left diagonal");

    /* And the backward diagonals keep the SAME handedness as the forward ones, which is the exact
     * opposite of the sideways walk's rule and is why the two must not share a function. */
    check_input_angle(RIGHT, BACKWARD, -135.0f, "backward and right stays to the right");
    check_input_angle(LEFT,  BACKWARD, +135.0f, "backward and left stays to the left");

    ut_check(!free_look_input_angle(NONE, NONE, &angle), "no input produces no angle");
    ut_near(angle, -1.0f, 0.001f, "and the output is left untouched when it does not");
}

static void test_free_look_offset(void)
{
    float interpolated;
    float offset;

    ut_section("free look: the offset, and that the camera never follows the body");

    /* The engine's own interpolation, including across the seam: a body at 350 degrees turning to
     * 10 must interpolate the SHORT way, or the camera would sweep the long way round once per
     * wrap. */
    ut_near(free_look_interpolated_heading(100.0f, 140.0f, 0.5f), 120.0f, 0.001f,
                "the interpolated heading is half way between two substeps");
    ut_near(free_look_interpolated_heading(350.0f, 10.0f, 0.5f), 360.0f, 0.001f,
                "and it crosses the seam the short way");
    ut_near(free_look_interpolated_heading(100.0f, 140.0f, 0.0f), 100.0f, 0.001f,
                "alpha 0 is the previous substep exactly");
    ut_near(free_look_interpolated_heading(100.0f, 140.0f, 1.0f), 140.0f, 0.001f,
                "alpha 1 is the current substep exactly");

    /* THE ROUND TRIP, which is the whole feature in one line: the engine adds our offset to its
     * interpolated heading, so that sum has to be the camera yaw we asked for. */
    interpolated = free_look_interpolated_heading(350.0f, 10.0f, 0.5f);
    offset       = free_look_offset(75.0f, interpolated);
    ut_near(free_look_wrap360(interpolated + offset), 75.0f, 0.001f,
                "interpolated heading plus the offset is the wanted camera yaw");

    offset = free_look_offset(10.0f, 350.0f);
    ut_near(offset, 20.0f, 0.001f, "the offset itself crosses the seam as a short turn");
    ut_near(free_look_wrap360(350.0f + offset), 10.0f, 0.001f, "and it round-trips there too");

    /* The camera is never pulled back toward the body, and these rows are what would catch a leash
     * creeping back in. The property under test is that the round trip holds for ANY separation,
     * including the half turn a backward key produces, the case that made the old leash drag the
     * camera round with the body. */
    interpolated = 0.0f;
    offset       = free_look_offset(180.0f, interpolated);
    ut_near(free_look_wrap360(interpolated + offset), 180.0f, 0.001f,
                "a camera exactly opposite the body survives the round trip");

    offset = free_look_offset(179.0f, 359.0f);
    ut_near(free_look_wrap360(359.0f + offset), 179.0f, 0.001f,
                "and so does a half turn taken across the seam");

    /* The body turning does NOT move the camera: same wanted yaw, four very different headings,
     * and the reconstructed camera yaw has to come out the same every time. */
    {
        const float headings[] = { 0.0f, 90.0f, 180.0f, 270.0f };
        size_t      i;

        for (i = 0; i < sizeof(headings) / sizeof(headings[0]); ++i) {
            float o = free_look_offset(42.0f, headings[i]);

            ut_near(free_look_wrap360(headings[i] + o), 42.0f, 0.001f,
                        "the camera yaw is independent of where the body points");
        }
    }
}

static void test_free_look_gate(void)
{
    free_look_gate_t gate;

    ut_section("free look: the arming gate");

    arm_gate(&gate);
    ut_check(free_look_gate_is_armed(&gate), "an ordinary follow camera arms the feature");

    /* Each condition on its own must be able to close the gate. A gate that only closes when
     * several things go wrong at once is the shape that ships a rotated cutscene. */
    arm_gate(&gate); gate.enabled = false;
    ut_check(!free_look_gate_is_armed(&gate), "switched off releases");

    arm_gate(&gate); gate.record_valid = false;
    ut_check(!free_look_gate_is_armed(&gate), "no player record releases");

    arm_gate(&gate); gate.module_state = 4;
    ut_check(!free_look_gate_is_armed(&gate), "the phases not running releases (death included)");

    arm_gate(&gate); gate.mode_index = -1;
    ut_check(!free_look_gate_is_armed(&gate), "a mode that cannot be told apart releases");

    arm_gate(&gate); gate.mode_index = PLAYER_MODE_MAX + 1;
    ut_check(!free_look_gate_is_armed(&gate), "and so does one outside the table");

    arm_gate(&gate); gate.view_valid = false;
    ut_check(!free_look_gate_is_armed(&gate), "an unreadable camera object releases");

    arm_gate(&gate); gate.view_state = 1;
    ut_check(!free_look_gate_is_armed(&gate), "a fixed look-at camera releases");

    arm_gate(&gate); gate.view_state = 2;
    ut_check(!free_look_gate_is_armed(&gate), "a world-fixed camera releases");

    arm_gate(&gate); gate.camera_override = 1;
    ut_check(!free_look_gate_is_armed(&gate), "a script owning the camera releases");

    arm_gate(&gate); gate.snap_countdown = 1;
    ut_check(!free_look_gate_is_armed(&gate),
               "a snap in progress releases, this is what scrubs a saved free-look offset");

    arm_gate(&gate); gate.region_flags = 0x01u;
    ut_check(!free_look_gate_is_armed(&gate), "an authored fixed-look-at region releases");
    arm_gate(&gate); gate.region_flags = 0x02u;
    ut_check(!free_look_gate_is_armed(&gate), "an authored fixed-heading region releases");
    arm_gate(&gate); gate.region_flags = 0x04u;
    ut_check(!free_look_gate_is_armed(&gate),
               "a CUT region releases even though its camera state is still FOLLOW");
    arm_gate(&gate); gate.region_flags = 0x08u;
    ut_check(!free_look_gate_is_armed(&gate), "a world-fixed region releases");

    /* The fast-lag bit only shortens the damper, so it must NOT release: doing so would give up a
     * camera the player can steer perfectly well. */
    arm_gate(&gate); gate.region_flags = 0x10u;
    ut_check(free_look_gate_is_armed(&gate), "the fast-lag bit alone does NOT release");

    /* A region we could not resolve must not be read as "no flags set". */
    arm_gate(&gate); gate.region_known = false; gate.region_flags = 0x0Fu;
    ut_check(free_look_gate_is_armed(&gate),
               "an unresolved region falls back to the camera state alone");
    arm_gate(&gate); gate.region_known = false; gate.view_state = 2;
    ut_check(!free_look_gate_is_armed(&gate),
               "and the camera state still catches every fixed family without it");
}

/* The gate has to say WHICH condition fired, and it has to put each one in the right family: a
 * scripted camera drops the wanted yaw, an authored region gives it back. Getting a cutscene into
 * the second family would restore a minute-old mouse angle on top of a deliberately placed shot. */
static void test_free_look_release_reasons(void)
{
    free_look_gate_t gate;
    int              reason;

    ut_section("free look: which condition released, and which family it is in");

    arm_gate(&gate);
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_ARMED, "an armed gate names no reason");

    arm_gate(&gate); gate.enabled = false;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_SWITCHED_OFF,
               "the switch names itself");
    arm_gate(&gate); gate.record_valid = false;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_NO_PLAYER_RECORD,
               "a missing player record names itself");
    arm_gate(&gate); gate.module_state = 4;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_PHASES_NOT_RUNNING,
               "the phases not running names itself");
    arm_gate(&gate); gate.mode_index = -1;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_MODE_UNKNOWN,
               "an unknown mode names itself");
    arm_gate(&gate); gate.camera_override = 1;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_SCRIPT_FORCED_REGION,
               "a script forcing a region names itself");
    arm_gate(&gate); gate.snap_countdown = 3;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_SNAP_RUNNING,
               "a snap in progress names itself");
    arm_gate(&gate); gate.view_valid = false;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_NO_CAMERA_OBJECT,
               "an unreadable camera object names itself");
    arm_gate(&gate); gate.view_state = 1;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_CAMERA_NOT_FOLLOW,
               "a camera that has left its follow state names itself");
    arm_gate(&gate); gate.region_flags = 0x08u;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_CAMERA_REGION,
               "an authored region names itself");

    /* The order is the classification. A forced region drives the camera object into exactly the
     * state an authored one does, so the scripted test has to win when both are true, otherwise
     * every cutscene would be filed as an authored region and handed the old yaw back. */
    arm_gate(&gate);
    gate.camera_override = 1;
    gate.view_state      = 1;
    gate.region_flags    = 0x01u;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_SCRIPT_FORCED_REGION,
               "a script forcing a region OUTRANKS the region it forced");
    arm_gate(&gate);
    gate.snap_countdown = 1;
    gate.view_state     = 2;
    ut_check(free_look_gate_refusal(&gate) == FREE_LOOK_RELEASE_SNAP_RUNNING,
               "and a snap outranks the camera state it produced");

    ut_check(free_look_release_is_authored_region(FREE_LOOK_RELEASE_CAMERA_REGION),
               "an authored region is the family that keeps the wanted yaw");
    ut_check(free_look_release_is_authored_region(FREE_LOOK_RELEASE_CAMERA_NOT_FOLLOW),
               "and so is the camera state it sets");
    ut_check(!free_look_release_is_authored_region(FREE_LOOK_RELEASE_SCRIPT_FORCED_REGION),
               "a script is not");
    ut_check(!free_look_release_is_authored_region(FREE_LOOK_RELEASE_SNAP_RUNNING),
               "nor is a snap, this is what stops a load restoring a stale angle");
    ut_check(!free_look_release_is_authored_region(FREE_LOOK_RELEASE_PHASES_NOT_RUNNING),
               "nor is a cutscene or the death arm");
    ut_check(!free_look_release_is_authored_region(FREE_LOOK_RELEASE_SWITCHED_OFF),
               "nor is switching the feature off");

    /* Every reason must have a name of its own. A log line that says "released (7)" is a log line
     * nobody can act on, and a duplicated name hides which of two conditions fired. */
    for (reason = FREE_LOOK_ARMED; reason <= FREE_LOOK_RELEASE_NOT_A_NUMBER; ++reason) {
        int other;

        ut_check(free_look_release_name((free_look_release_t)reason)[0] != '\0',
                   "every release reason has a name");
        for (other = FREE_LOOK_ARMED; other < reason; ++other) {
            if (strcmp(free_look_release_name((free_look_release_t)reason),
                       free_look_release_name((free_look_release_t)other)) == 0) {
                ut_check(false, "two release reasons share one name");
            }
        }
    }
}

/* The bounded recovery. It is the whole of the fix for "the camera rotates at random while
 * walking", and its one hazard is the opposite defect: a limit large enough to snap the camera a
 * long way when an authored region really did re-aim the shot. */
static void test_free_look_recovery(void)
{
    ut_section("free look: taking the wanted yaw back after an authored region");

    ut_check(free_look_recovers_wanted_yaw(90.0f, 84.0f, 25.0f),
               "a few degrees of stolen recentre are taken back");
    ut_check(free_look_recovers_wanted_yaw(90.0f, 96.0f, 25.0f),
               "and so are a few in the other direction");
    ut_check(free_look_recovers_wanted_yaw(90.0f, 65.0f, 25.0f),
               "exactly the limit is still taken back");
    ut_check(!free_look_recovers_wanted_yaw(90.0f, 64.9f, 25.0f),
               "one degree past it is not, that is a re-aim, not a theft");
    ut_check(!free_look_recovers_wanted_yaw(90.0f, 270.0f, 25.0f),
               "and a half turn certainly is not");

    /* The seam. A wanted yaw of 5 and an engine yaw of 355 are ten degrees apart, not 350, and a
     * recovery that measured the long way round would refuse exactly where it is needed most. */
    ut_check(free_look_recovers_wanted_yaw(5.0f, 355.0f, 25.0f),
               "the gap is measured across the 0/360 seam");
    ut_check(free_look_recovers_wanted_yaw(355.0f, 5.0f, 25.0f),
               "and symmetrically the other way");

    ut_check(!free_look_recovers_wanted_yaw(90.0f, 90.0f, 0.0f),
               "a limit of zero switches the recovery off even when nothing has moved");
    ut_check(!free_look_recovers_wanted_yaw(90.0f, 89.0f, -1.0f),
               "and so does a negative one");
    ut_check(!free_look_recovers_wanted_yaw(90.0f, 89.0f, NOT_A_NUMBER),
               "a limit that is not a number switches it off rather than comparing against it");
    ut_check(!free_look_recovers_wanted_yaw(NOT_A_NUMBER, 89.0f, 25.0f),
               "a remembered yaw that is not a number is never taken back");
    ut_check(!free_look_recovers_wanted_yaw(90.0f, NOT_A_NUMBER, 25.0f),
               "nor is one measured against a camera yaw that is not a number");
}

int main(void)
{
    test_travel_angle();
    test_damper();
    test_mouse_bank();
    test_free_look_angles();
    test_free_look_offset();
    test_free_look_gate();
    test_free_look_release_reasons();
    test_free_look_recovery();

    return ut_summary("strafe walk, mouse bank and free look");
}
