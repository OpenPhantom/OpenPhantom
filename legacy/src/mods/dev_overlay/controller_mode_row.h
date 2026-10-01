/* controller_mode_row.h: one switch for the four rows a pad player wants on together.
 *
 * Strafe, free look, the camera follow and the air steer are the control scheme a pad needs, and a
 * player looking at the panel for the first time has no way to know that from four rows with the
 * game's own names on them. This row turns all four on in one click and all four off in one click.
 * It holds no key of its own: it reads ON exactly when the four rows under it do, so switching one
 * of them off on its own is still allowed and this row then reads OFF, which is the truth. */
#ifndef DEV_OVERLAY_CONTROLLER_MODE_ROW_H
#define DEV_OVERLAY_CONTROLLER_MODE_ROW_H

#include <stdbool.h>

/* ON when strafe, free look, the camera follow and the air steer all read on. */
bool controller_mode_row_get(void);

/* Writes all four. False when any write failed, and then the rows say which by what they read. */
bool controller_mode_row_set(bool enabled);

#endif /* DEV_OVERLAY_CONTROLLER_MODE_ROW_H */
