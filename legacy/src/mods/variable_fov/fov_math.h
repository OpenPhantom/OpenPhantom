/* fov_math.h: the arithmetic of the field of view, with no engine and no Windows in it.
 *
 * Everything here is a pure function of its arguments, which is the point: the parts that are
 * easy to get subtly wrong are the parts that can be tested without launching the game.
 * See unittests/fov_math.c.
 */
#ifndef FOV_MATH_H
#define FOV_MATH_H

#include <stdbool.h>

typedef enum aspect_mode {
    ASPECT_MODE_HOR_PLUS = 0,   /* keep the authored 46.826 deg vertical, widen horizontally    */
    ASPECT_MODE_VERT_MINUS,     /* keep the authored 60 deg horizontal, let the vertical follow */
    ASPECT_MODE_STRETCH,        /* touch nothing at all (what an unpatched exe does)            */
    ASPECT_MODE_COUNT
} aspect_mode_t;

typedef enum menu_3d_mode {
    MENU_3D_MODE_STRETCH = 0,   /* the 640x480 menu box fills the screen width */
    MENU_3D_MODE_PILLARBOX,     /* the box keeps 4:3 inside the screen         */
    MENU_3D_MODE_NATIVE,        /* the box stays 640 physical pixels           */
    MENU_3D_MODE_COUNT
} menu_3d_mode_t;

/* The engine's own setters clamp fovDeg to [5,179] and run straight into a trap doing so:
 * bapdraw_drawWorld computes fovH = fovDeg + 3.0 and takes tan(fovH/2), so from fovDeg >= 177
 * that tangent is NEGATIVE, all six cell tests invert and the world walk collects NOTHING, an
 * empty picture. We write cam+0x38 immediately before rdCamera_BuildProjection and bypass their
 * clamp anyway, so we clamp ourselves, with room to spare. */
#define FOV_MIN_DEGREES   5.0f
#define FOV_MAX_DEGREES 170.0f

float fov_clamp_float(float value, float minimum, float maximum);

/* Returns the HORIZONTAL field of view this canvas should use, in degrees, already clamped.
 * Returns 0.0f for ASPECT_MODE_STRETCH, which is the signal to leave the engine's field alone. */
float fov_horizontal_degrees(aspect_mode_t mode, float base_vertical_degrees,
                             float extra_degrees, int width, int height);

/* focal = half_width / tan(hfov/2). This is what rdCamera_BuildProjection derives at 0x47615E,
 * and through g_projScale it IS the projection for both axes. */
float fov_focal_pixels(float horizontal_degrees, float half_width);

/* The vertical that FALLS OUT of the horizontal and the canvas. Not a setting; this engine has
 * one focal length serving both axes, so a separate vertical field of view is structurally
 * impossible. Pass the engine's own half extents ((x1-x0)/2, (y1-y0)/2). */
float fov_vertical_degrees(float horizontal_degrees, float half_width, float half_height);

/* The value that must go into the cell replacing [0x4A8888] so the 3-D front-end menu keeps its
 * placement at this resolution and this focal length. */
float fov_menu_focal_cell(menu_3d_mode_t mode, int width, int height, float focal_pixels);

/* Slider notch <-> extra degrees. `notch_count` is the number of notches, so the valid notch
 * range is 0 .. notch_count-1. Both functions clamp. */
int   fov_notch_from_degrees(float degrees, float step_degrees, int notch_count);
float fov_degrees_from_notch(int notch, float step_degrees, int notch_count);

#endif /* FOV_MATH_H */
