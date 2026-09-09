/* stick.c: see stick.h. */
#include "stick.h"

#include <math.h>

#define STICK_RAW_FULL 32767.0f

bool stick_apply_radial_deadzone(short raw_x, short raw_y, float deadzone, float *out_x,
                                 float *out_y)
{
    float x;
    float y;
    float magnitude;
    float scaled;

    if (out_x == NULL || out_y == NULL) {
        return false;
    }
    if (!(deadzone >= 0.0f) || !(deadzone < 1.0f)) {
        deadzone = 0.0f;      /* a nonsense deadzone is no deadzone, never a divide by zero below */
    }

    x         = (float)raw_x / STICK_RAW_FULL;
    y         = (float)raw_y / STICK_RAW_FULL;
    magnitude = (float)sqrt((double)(x * x + y * y));

    if (magnitude < deadzone || magnitude <= 0.0f) {
        return false;
    }

    /* The direction comes from the TRUE magnitude and the speed is capped afterwards. Clamping the
     * magnitude first and then dividing by it leaves the direction unnormalised: a stick held to
     * the corner of a square range divided 1,1 by 1 and came back 1.41 long, so a diagonal ran 41
     * percent faster than a straight push. The clamp was put there to prevent exactly that. */
    scaled = (magnitude - deadzone) / (1.0f - deadzone);
    if (scaled > 1.0f) {
        scaled = 1.0f;
    }

    *out_x = (x / magnitude) * scaled;
    *out_y = (y / magnitude) * scaled;
    return true;
}

float stick_magnitude(float x, float y)
{
    float magnitude = (float)sqrt((double)(x * x + y * y));

    return (magnitude > 1.0f) ? 1.0f : magnitude;
}
