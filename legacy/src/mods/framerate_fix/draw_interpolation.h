/* draw_interpolation.h: the two patches that make what is DRAWN advance every frame.
 *
 * Neither of them moves a simulation quantity. Both change only what the renderer builds out of
 * the numbers the simulation has already produced.
 */
#ifndef DRAW_INTERPOLATION_H
#define DRAW_INTERPOLATION_H

#include <stdbool.h>

/* Interpolates the drawn PITCH and ROLL the way the engine already interpolates the drawn yaw. */
void draw_interpolation_install_euler(void);

/* Removes the pose throttle so the joint matrices are rebuilt every frame. */
void draw_interpolation_install_pose_throttle(void);

#endif /* DRAW_INTERPOLATION_H */
