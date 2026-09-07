/* camera_follow_row.h: the developer menu's switch for the passive camera follow.
 *
 * Reads and writes [enhanced_input] CameraFollow. Like every row in the Utilities group it edits
 * the settings file rather than the running DLL, so it works with enhanced_input.dll deleted and
 * the setting survives a restart for free.
 *
 * The row is only OFFERED while strafe is on, which is not tidiness: without strafe the walk never
 * leaves the player's heading, so there is nothing for the camera to follow and the switch would do
 * nothing at all. Shown unavailable rather than hidden, so somebody hunting for it finds out why
 * instead of wondering whether it exists. */
#ifndef DEV_OVERLAY_CAMERA_FOLLOW_ROW_H
#define DEV_OVERLAY_CAMERA_FOLLOW_ROW_H

#include <stdbool.h>

bool camera_follow_row_get(void);
bool camera_follow_row_set(bool enabled);

/* Whether the switch can do anything, which is exactly whether strafe is on. */
bool camera_follow_row_available(void);

#endif /* DEV_OVERLAY_CAMERA_FOLLOW_ROW_H */
