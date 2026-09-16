/* pad_input.h: the panel and the free camera from a controller.
 *
 * The panel reads keys and the mouse, and the free camera reads keys and the cursor; a pad
 * reached neither, apart from what controller_input fakes for the game (the right stick as
 * mouse motion, sideways, and Start as Escape). This reads the pad itself, through XInput, once
 * a frame from the hook that draws the panel, and hands it to whichever of the two is up:
 *
 *   panel open:     the left stick glides the pointer up and down and the D-pad steps it row
 *                   by row, A presses where it is (held, it drags a slider), B is Escape, the
 *                   triggers move a slider, the right stick scrolls, the bumpers page.
 *   free camera on: the left stick flies along the view, the right stick looks on both axes,
 *                   the triggers go down and up, the bumpers change speed, A ends the flight
 *                   bringing the player here (the bound key's meaning), B ends it leaving them
 *                   where they were (F4's).
 *   either:         the opening button, View unless the settings say otherwise, opens or
 *                   closes the panel on its press, and hides or shows it while the camera
 *                   flies, as the open key does.
 *
 * Nothing here reaches the game: with the panel closed and the camera off the pad is read and
 * dropped, and controller_input's own reading of it is untouched either way. The two DLLs share
 * nothing but the settings file, and the one key read from the other's section is which slot
 * the pad is in. This file is the reading; pad_panel.c is the panel's use of it, and the free
 * camera reads the frame itself.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_PAD_INPUT_H
#define DEV_OVERLAY_PAD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* The button bits as XInput reports them, so a reader needs no XInput header of its own.
 * pad_input.c asserts them against the real ones. */
#define PAD_BUTTON_DPAD_UP        0x0001u
#define PAD_BUTTON_DPAD_DOWN      0x0002u
#define PAD_BUTTON_DPAD_LEFT      0x0004u
#define PAD_BUTTON_DPAD_RIGHT     0x0008u
#define PAD_BUTTON_BACK           0x0020u   /* View on an Xbox One pad, Back on a 360 pad */
#define PAD_BUTTON_LEFT_SHOULDER  0x0100u
#define PAD_BUTTON_RIGHT_SHOULDER 0x0200u
#define PAD_BUTTON_LEFT_THUMB     0x0040u   /* the sticks pressed in */
#define PAD_BUTTON_RIGHT_THUMB    0x0080u
#define PAD_BUTTON_A              0x1000u
#define PAD_BUTTON_B              0x2000u
#define PAD_BUTTON_X              0x4000u
#define PAD_BUTTON_Y              0x8000u
#define PAD_BUTTON_START          0x0010u

/* One frame of the pad, shaped: sticks in -1..1 past a radial deadzone, triggers in 0..1,
 * buttons as held and as pressed this frame. Nothing is set when no pad answers. */
typedef struct pad_state {
    bool  present;
    float left_x, left_y;       /* the left stick, right and up positive */
    float right_x, right_y;     /* the right stick */
    float raw_x;                /* the two sticks' sideways travel before the deadzone, the
                                 * larger, for a reader that has to notice what another
                                 * reader with a deadzone of its own will act on */
    float trigger_left;
    float trigger_right;
    uint32_t held;              /* XInput's button bits */
    uint32_t pressed;           /* the bits that went down this frame */
} pad_state_t;

/* Reads the settings and says in the log what the pad does. Never fails: a missing pad is
 * found, and reported, when it is first looked for. */
void pad_input_install(void);

/* Once a frame: reads the pad into the frame's state, and answers the seconds since the last
 * read, capped. pad_panel.c calls it and drives the panel; the free camera reads the state. */
float pad_input_poll(void);

/* This frame's pad. present is false with the pad off, absent, or not yet read. */
const pad_state_t *pad_input_state(void);

/* The look rate the free camera turns at with the right stick fully over, degrees per second,
 * and the pointer's speed with the left stick fully over, screen widths per second. */
float pad_input_look_speed(void);
float pad_input_pointer_speed(void);

/* The button, or buttons together, that open or close the panel, as button bits; 0 when the
 * setting named none the reader knows. */
uint32_t pad_input_open_buttons(void);

/* The button bits a setting's words name, "LS RS", "View", "LB RB", case aside; unknown words
 * are dropped, and the log says so through the caller. Names: A B X Y LB RB LS RS View Menu
 * Back Start Up Down Left Right. */
uint32_t pad_buttons_named(const char *words);

/* The pieces the unit test exercises, with no pad behind them. */

/* One stick's raw axes shaped: a radial deadzone, the rest rescaled to a full 0..1 so the
 * first hair past the deadzone is a hair and not a jump, and the magnitude capped at 1. */
void pad_shape_stick(int16_t raw_x, int16_t raw_y, float deadzone, float *out_x, float *out_y);


#endif /* DEV_OVERLAY_PAD_INPUT_H */
