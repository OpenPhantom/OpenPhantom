/* numeric.h: two float helpers every DLL was carrying a copy of.
 *
 * Header only, and inline on purpose: numeric_is_finite runs on the object and mover draw paths,
 * under the engine's own floating point state, where the CRT's classifier is a copy through the
 * x87 unit and a call. The bit test compiles to two integer instructions and touches no float
 * state. The clamp is a few compares and belongs beside it rather than behind a call.
 */
#ifndef COMMON_NUMERIC_H
#define COMMON_NUMERIC_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* `value` held to [minimum, maximum]. NaN reads as below the minimum through the negated
 * comparison, and either infinity becomes a bound, so a finite pair of limits guarantees a
 * finite result. The maximum wins where the two bounds cross: a ceiling is usually a number out
 * of the player's ini and a floor a property of the arithmetic, and a written ceiling below the
 * floor is a decision, not a mistake. */
static inline float numeric_clamp(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {
        value = minimum;
    }
    return (value > maximum) ? maximum : value;
}

/* Finite by the bit pattern: an exponent field of all ones is an infinity or a NaN and nothing
 * else is. */
static inline bool numeric_is_finite(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

#endif /* COMMON_NUMERIC_H */
