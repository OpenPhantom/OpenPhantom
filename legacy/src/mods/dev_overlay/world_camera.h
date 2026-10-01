/* world_camera.h: the camera numbers the frame was drawn with, read for the placement mode.
 *
 * One site gives all of them. The world pass's frame setup copies the canvas centre and the focal
 * length out of the current camera into three cells, once a frame, before any vertex is projected,
 * and the projection reads those cells for every vertex. The same few instructions name the cell
 * that holds the current camera, whose world transform the world pass applies to every vertex. So
 * reading these at the end of the scene gives exactly the numbers the picture was made with.
 *
 * Only read, never written, and not a head of anything. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_WORLD_CAMERA_H
#define DEV_OVERLAY_WORLD_CAMERA_H

#include "world_pick.h"

#include <stdbool.h>

/* Finds the cells. Idempotent; says once what it found. */
void world_camera_resolve(void);

/* Whether the cells were found on this executable. */
bool world_camera_available(void);

/* The numbers as they stand now: false before the cells resolved, with no camera, or when a number
 * is not one a picture could have been drawn with (not finite, a focal length under a pixel, a
 * transform that is no clean rotation). With the check of which way the transform is read, the eye
 * the view record keeps against the camera's origin, made on every read until it can tell and said
 * once when it does. */
bool world_camera_read(world_pick_camera_t *out);

#endif /* DEV_OVERLAY_WORLD_CAMERA_H */
