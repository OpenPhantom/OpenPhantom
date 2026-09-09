/* look_counts.h: a poll of the right stick turned into whole mouse counts, with the fraction kept.
 *
 * Separated from controller_input.c because it is pure. No pad, no clock and no SendInput, so a
 * console test can drive a hundred polls of a small steady push and read what came out.
 *
 * The fraction is the point. SendInput moves the pointer by whole counts, and at the default
 * cadence and sensitivity one poll is worth 32 counts at full deflection, so anything under about
 * a thirtieth of the stick's range is worth less than a single count on its own. Truncating each
 * poll where it stands would leave the stick dead there and give the player a look that does
 * nothing until it suddenly does. The fraction owed is carried into the next poll instead, and a
 * small steady push produces a count every few polls.
 */
#ifndef CONTROLLER_INPUT_LOOK_COUNTS_H
#define CONTROLLER_INPUT_LOOK_COUNTS_H

#include <stdbool.h>

/* What is owed from previous polls, one axis each, always under a whole count in magnitude.
 * Zero initialised is the correct starting state and look_counts_reset puts it back there. */
typedef struct look_carry {
    double x;
    double y;
} look_carry_t;

/* The most one poll may ask for on one axis. Nothing near this is reachable through the settings:
 * the sensitivity ceiling is 100000 counts a second and a gap longer than a quarter of a second is
 * discarded before it arrives here, which puts the real maximum at 25000. It is here because the
 * conversion below is a cast, and a cast is undefined for a value a long cannot hold; a caller
 * that has not clamped its own settings gets a large turn rather than the far corner of the
 * screen. Reaching it clears the carry rather than banking a debt that could never be repaid. */
#define LOOK_COUNTS_MAX 1000000.0

/* One poll. `stick_x` and `stick_y` are the stick after its deadzone, `sensitivity` is counts per
 * second at full deflection, and `seconds` is the real time since the previous poll. `out_dx` and
 * `out_dy` are always written, zero included.
 *
 * Y comes back inverted on purpose: XInput reports +1 as up on the stick, and looking up is a
 * negative, upward mouse movement on screen.
 *
 * Returns false when there is nothing to send, which at a small deflection is the ordinary case
 * and not a failure. An elapsed time of zero or less sends nothing and leaves the carry alone. A
 * value that is not finite anywhere in the arithmetic sends nothing and clears the carry, because
 * a carry that is not a number never becomes one again and every later poll would be lost with
 * it. */
bool look_counts_step(look_carry_t *carry, float stick_x, float stick_y, float sensitivity,
                      double seconds, long *out_dx, long *out_dy);

/* Forgets what is owed, so that a return to the game does not open with a count banked from
 * before it lost the foreground. */
void look_counts_reset(look_carry_t *carry);

#endif /* CONTROLLER_INPUT_LOOK_COUNTS_H */
