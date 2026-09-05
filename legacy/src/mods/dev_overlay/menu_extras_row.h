/* menu_extras_row.h: whether this project's own settings appear in the game's own menus.
 *
 * Four widgets were added to screens the game shipped without them: a field of view slider on the
 * video options screen, and free look, sideways walking and mouse sensitivity on the controls one.
 * They ship OFF, so those screens look as they did in 1999, and this row turns them on.
 *
 * The settings themselves are not affected either way. They are ini keys and they have rows of
 * their own in this panel; this decides only whether the game's own menus offer them as well.
 *
 * The mouse sensitivity slider is covered too, and that was the one real decision here: mouse look
 * ships on, so that slider is its only adjustment inside the game. The answer is this panel's own
 * sensitivity row rather than a widget left behind on a screen meant to look untouched. Either the
 * screen is the one the game shipped or it is not.
 *
 * IT TAKES EFFECT ON THE NEXT LAUNCH, and that is structural rather than laziness. Both screens
 * are patched by repointing the engine's own widget table once, while the game starts, and this
 * project has no path that puts such a table back. So the row writes the setting and says when it
 * will be seen.
 *
 * ONE ROW, TWO KEYS, in two different DLLs, and it reads ON only when both are on. A half state can
 * only be reached by editing the file by hand, and reporting that as ON would be a claim about a
 * screen that is only half changed.
 */
#ifndef DEV_OVERLAY_MENU_EXTRAS_ROW_H
#define DEV_OVERLAY_MENU_EXTRAS_ROW_H

#include <stdbool.h>

/* True when both keys are on. Absent reads as OFF, matching the shipped defaults. */
bool menu_extras_row_get(void);

/* Writes both. False when either could not be written, so a partial change is reported as a
 * failure rather than shown as success. */
bool menu_extras_row_set(bool enabled);

#endif /* DEV_OVERLAY_MENU_EXTRAS_ROW_H */
