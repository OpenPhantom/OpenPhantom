#include "fov_math.h"

#include <math.h>
#include <stdbool.h>

#define PI_F 3.14159265358979323846f

#define DEGREES_TO_RADIANS(degrees) ((degrees) * (PI_F / 180.0f))
#define RADIANS_TO_DEGREES(radians) ((radians) * (180.0f / PI_F))

/* The authored 4:3 canvas the engine was designed against. */
#define AUTHORED_ASPECT_WIDTH  4.0f
#define AUTHORED_ASPECT_HEIGHT 3.0f

/* The menu box is authored in 640x480 space; FUN_0045C3D8 places it through that. */
#define MENU_SPACE_WIDTH  640.0f
#define MENU_SPACE_HEIGHT 480.0f

float fov_clamp_float(float value, float minimum, float maximum)
{
    /* Written as a failed >= rather than a < so that NaN lands on the minimum instead of
     * propagating into the projection. */
    if (!(value >= minimum)) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

float fov_horizontal_degrees(aspect_mode_t mode, float base_vertical_degrees,
                             float extra_degrees, int width, int height)
{
    float tangent_half_vertical;
    float horizontal;

    if (width <= 0 || height <= 0) {
        return 60.0f;                              /* the shipped value; harmless as a fallback */
    }
    if (mode == ASPECT_MODE_STRETCH) {
        return 0.0f;                               /* signal: leave the field completely alone */
    }

    tangent_half_vertical = tanf(DEGREES_TO_RADIANS(base_vertical_degrees * 0.5f));

    if (mode == ASPECT_MODE_VERT_MINUS) {
        /* Keep the byte-native horizontal angle; the vertical follows the frame, which is what an
         * unpatched engine does on a widescreen buffer (60 deg H, about 36 deg V at 16:9). */
        horizontal = 2.0f * RADIANS_TO_DEGREES(
            atanf(tangent_half_vertical * (AUTHORED_ASPECT_WIDTH / AUTHORED_ASPECT_HEIGHT)));
    } else {
        /* Hor+: keep the byte-native VERTICAL angle and widen horizontally, the mapping that
         * shows the whole authentic 4:3 image plus more world at the sides. */
        horizontal = 2.0f * RADIANS_TO_DEGREES(
            atanf(tangent_half_vertical * ((float)width / (float)height)));
    }

    horizontal += extra_degrees;

    return fov_clamp_float(horizontal, FOV_MIN_DEGREES, FOV_MAX_DEGREES);
}

float fov_focal_pixels(float horizontal_degrees, float half_width)
{
    float tangent;

    if (!(half_width > 0.0f) || !(horizontal_degrees > 0.0f)) {
        return 0.0f;
    }

    tangent = tanf(DEGREES_TO_RADIANS(horizontal_degrees * 0.5f));
    if (!(tangent > 0.0f)) {
        return 0.0f;
    }

    return half_width / tangent;
}

float fov_vertical_degrees(float horizontal_degrees, float half_width, float half_height)
{
    float focal = fov_focal_pixels(horizontal_degrees, half_width);

    if (!(focal > 0.0f) || !(half_height > 0.0f)) {
        return 0.0f;
    }

    return 2.0f * RADIANS_TO_DEGREES(atanf(half_height / focal));
}

/* FUN_0045C3D8 computes  S = menuScale / C  with menuScale = 640/W and C = our cell, and lands a
 * 640x480-space pixel at  screenX = (px - 320) * S * focal + W/2.
 * We want S*focal to be the scale we chose, so  C = (640/W) * focal / scale. */
float fov_menu_focal_cell(menu_3d_mode_t mode, int width, int height, float focal_pixels)
{
    float scale;
    float cell;

    if (width <= 0 || height <= 0 || !(focal_pixels > 0.0f)) {
        return 0.0f;                               /* nothing sensible to say; caller keeps its own */
    }

    switch (mode) {
    case MENU_3D_MODE_PILLARBOX:
        scale = (float)height / MENU_SPACE_HEIGHT;  /* a 4:3 box inside the screen */
        break;
    case MENU_3D_MODE_NATIVE:
        scale = 1.0f;                               /* 640 physical pixels, unpatched look */
        break;
    case MENU_3D_MODE_STRETCH:
    default:
        scale = (float)width / MENU_SPACE_WIDTH;    /* the box fills the screen width */
        break;
    }

    if (!(scale > 0.0f)) {
        return 0.0f;
    }

    cell = (MENU_SPACE_WIDTH / (float)width) * focal_pixels / scale;
    return (cell > 0.0f) ? cell : 0.0f;
}

int fov_notch_from_degrees(float degrees, float step_degrees, int notch_count)
{
    int notch;

    if (notch_count < 1) {
        return 0;
    }
    if (!(degrees > 0.0f) || !(step_degrees > 0.0f)) {
        return 0;                                  /* also catches NaN */
    }

    notch = (int)(degrees / step_degrees + 0.5f);
    if (notch < 0) {
        notch = 0;
    }
    if (notch > notch_count - 1) {
        notch = notch_count - 1;
    }
    return notch;
}

float fov_degrees_from_notch(int notch, float step_degrees, int notch_count)
{
    if (notch_count < 1) {
        return 0.0f;
    }
    if (notch < 0) {
        notch = 0;
    }
    if (notch > notch_count - 1) {
        notch = notch_count - 1;
    }
    return (float)notch * step_degrees;
}
