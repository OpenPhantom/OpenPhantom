/* strafe_row.h: sideways walking, as the overlay's own row sees it.
 *
 * The same switch the controls screen offers, reachable without leaving the game to find it. The
 * key is [enhanced_input] Strafe.
 *
 * IT CAN BE REFUSED, and the row cannot tell in advance. Strafe needs mouse look, because the
 * engine's turnWheel is the only turn channel and driving it sideways clears the mouse with it, and
 * it needs the keyboard axis reader to have resolved. enhanced_input declines and says why in the
 * log; this row will then read back off on its next rebuild, which is the honest outcome and the
 * reason nothing here tries to predict it.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * enhanced_input.dll, feature DLLs here never depend on each other at run time, and either can be
 * deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_STRAFE_ROW_H
#define DEV_OVERLAY_STRAFE_ROW_H

#include <stdbool.h>

/* Absent reads as OFF, matching the shipped default. */
bool strafe_row_get(void);

/* Writes it. False when the file could not be written, which the caller shows by leaving the row
 * where it was rather than reporting a state the game is not in. */
bool strafe_row_set(bool enabled);

#endif /* DEV_OVERLAY_STRAFE_ROW_H */
