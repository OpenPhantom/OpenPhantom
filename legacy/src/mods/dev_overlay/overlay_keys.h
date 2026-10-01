/* overlay_keys.h: getting around the panel with the keyboard alone.
 *
 * The pad has stepped from row to row since it was added and the mouse has always been able to do
 * everything; the keyboard could scroll the list and close the panel, and nothing else. A machine
 * with neither a mouse nor a pad could open this panel and change nothing in it, and the comment
 * beside the refused keys in open_key_row.c described the missing half as though it were there.
 *
 * So the keyboard gets a row of its own, which the model keeps, and this is what the six keys do
 * with it. It lives beside the window procedure rather than in it because it touches none of that
 * file's state: it reads the model and the layout, and it is the whole of the difference between a
 * panel that can be driven from a keyboard and one that cannot.
 */
#ifndef DEV_OVERLAY_OVERLAY_KEYS_H
#define DEV_OVERLAY_OVERLAY_KEYS_H

#include <stdbool.h>
#include <stdint.h>

/* The arrows, Home, End, Return and the two page keys. True when the key was one of them and has
 * been acted on, which is what tells the caller to swallow it.
 *
 * Asked AFTER the key capture and the typed value have had their chance, so that binding an arrow
 * to a cheat still works and typing into a field is undisturbed, and before the panel's own
 * Escape, which means something else again.
 *
 * `panel_visible` is false whenever the panel is open and NOT being drawn, which is the flying
 * free camera and the placement mode. Both of those hand every key they do not use straight on,
 * so without this the arrows would move a selection nobody can see, left and right would change
 * the tab behind the picture, and Return would RUN whatever that invisible selection happened to
 * be sitting on. It is a parameter rather than a question asked here because the caller already
 * knows the answer and asking again would be the same state with two readers.
 *
 * `coarse` is whether the modifier is down, which only a number row reads and which it reads
 * as the row's larger step. It is handed in for the same reason `panel_visible` is: this file
 * reads the model and the layout and nothing of Windows, which is what lets a test drive
 * every key it handles. */
bool overlay_keys_navigate(int32_t virtual_key, bool panel_visible, bool coarse);

/* What Left or Right means on the row at `at`, which the keyboard and the pad both ask:
 *
 *   1. a heading folds or unfolds;
 *   2. a row of words walks to the next word;
 *   3. a number, or the track under it, changes by one press;
 *   4. anything else answers false, and the caller changes the tab.
 *
 * `by` is -1 for Left and 1 for Right; `coarse` is the modifier, which only a number reads
 * and which a pad has none of. It answers whether the ROW took the key, not whether anything
 * was written: a number row does not change the tab because a settings file could not be
 * written or because the handle was already at the end it was pushed towards.
 *
 * It is one function and not two because the pad used to change the tab with Left and Right
 * unconditionally while the keyboard folded headings, and a second rule written beside the
 * first is the pair that comes apart. An index of -1 answers false, which is what the
 * keyboard has when nothing is selected. */
bool overlay_keys_sideways(int32_t at, int32_t by, bool coarse);

#endif /* DEV_OVERLAY_OVERLAY_KEYS_H */
