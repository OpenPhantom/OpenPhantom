/* stick.h: the one piece of gamepad arithmetic two DLLs both need, in one place so they cannot
 * drift apart.
 *
 * WHY IT IS RADIAL. A per-axis deadzone leaves a SQUARE dead region, and the corner of that square
 * is 1.41 times its edge. A stick pushed to a true diagonal is therefore still dead on both axes at
 * a deflection that would already be live on either axis alone, and the boundary the player feels
 * changes with the direction they push. Taking the deadzone out of the MAGNITUDE instead gives one
 * circular boundary that is the same in every direction, which is what Microsoft's own XInput
 * documentation recommends.
 *
 * WHY IT RESCALES. Cutting the deadzone out without rescaling means the first live sample is
 * already at the deadzone's own value: the output jumps from 0.0 straight to 0.24 the instant the
 * boundary is crossed. Rescaling the remaining range back onto 0..1 makes the first live sample
 * nearly zero, so the stick starts moving the player from a standstill rather than from a lurch.
 * The engine's own joystick handling does neither, which is a large part of why a pad has always
 * felt coarse in this game: see the note in enhanced_input/pad_stick.h.
 */
#ifndef COMMON_STICK_H
#define COMMON_STICK_H

#include <stdbool.h>

/* One thumbstick, deadzone removed and the remainder rescaled, both axes in [-1, 1].
 *
 * `raw_x` and `raw_y` are the signed 16-bit values XInput reports, and Y is positive UP as XInput
 * reports it; a caller that wants screen or world coordinates negates it itself rather than having
 * a convention baked in here.
 *
 * Returns false and leaves both outputs untouched when the stick is inside the deadzone, so a
 * caller can tell "centred" from "pushed to nearly nothing" without comparing floats against an
 * epsilon of its own. */
bool stick_apply_radial_deadzone(short raw_x, short raw_y, float deadzone, float *out_x,
                                 float *out_y);

/* The magnitude of a vector this function already produced, which is what a caller wants for a
 * speed or a walk/run threshold. Never above 1, because the function above caps what it returns,
 * and the clamp here is kept so a caller passing its own pair cannot exceed the range either. A
 * raw XInput pair describes a SQUARE and a stick held to a corner is 1.41 long, so anything
 * measuring one of those directly wants this rather than a bare sqrt. */
float stick_magnitude(float x, float y);

#endif /* COMMON_STICK_H */
