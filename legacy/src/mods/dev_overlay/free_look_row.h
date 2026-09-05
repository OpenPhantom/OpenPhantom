/* free_look_row.h: free look, as the overlay's own row sees it.
 *
 * The same switch the controls screen offers, reachable without leaving the game to find it. The
 * key is [enhanced_input] FreeLook. On, the mouse turns the camera and the body turns toward where
 * it is travelling; off, the mouse turns the body and the camera follows.
 *
 * IT CAN BE REFUSED, and the row cannot tell in advance: the follow camera in this build has to be
 * one enhanced_input recognises. It declines and says why in the log, and this row then reads back
 * off on its next rebuild, which is the honest outcome.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * enhanced_input.dll, feature DLLs here never depend on each other at run time, and either can be
 * deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_FREE_LOOK_ROW_H
#define DEV_OVERLAY_FREE_LOOK_ROW_H

#include <stdbool.h>

/* Absent reads as OFF, matching the shipped default. */
bool free_look_row_get(void);

/* Writes it. False when the file could not be written. */
bool free_look_row_set(bool enabled);

#endif /* DEV_OVERLAY_FREE_LOOK_ROW_H */
