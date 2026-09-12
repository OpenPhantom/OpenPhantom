/* dismemberment_row.h: the panel's switch for lightsaber dismemberment.
 *
 * The row's only effect is one key in the settings file, [dismemberment] Mode. It does not reach
 * into dismemberment.dll, and it could not: feature DLLs in this tree never call each other, and
 * the panel does not know whether that one is even installed. The DLL re-reads the key about once a
 * second and applies it, so pressing the row changes the game within that second and the file
 * carries the choice into the next run.
 *
 * Off is the shipped setting. Severing a limb on the killing blow changes how the game plays rather
 * than repairing it, and anything in that class ships at the value that leaves the game alone.
 *
 * Why this is a switch and not three rows. The key takes 0, 1 and 2, where 1 corrects which limb
 * the engine's own seven authored severings take and 2 also severs on the killing blow. Nobody
 * wants 1 on purpose: it is the half of the feature that exists so the other half can be tested.
 * So the row writes 2 or 0, and a reader who has set 1 by hand sees the row lit and keeps their
 * setting until they press it.
 */
#ifndef DISMEMBERMENT_ROW_H
#define DISMEMBERMENT_ROW_H

#include <stdbool.h>

/* True while the key is anything other than off. */
bool dismemberment_row_get(void);

/* Writes 2 when `on`, 0 otherwise. False when the file could not be written, which the caller
 * reports the same way every other row here does. */
bool dismemberment_row_set(bool on);

#endif /* DISMEMBERMENT_ROW_H */
