/* overlay_window.h: the panel's third OpenPhantom group, the shape of the window.
 *
 * A group of its own for the same reason Utilities became one: these rows answer a single question
 * a player arrives with, and burying them among two dozen unrelated settings makes that question
 * harder to answer, not easier. They also behave unlike anything in Utilities. Four of them are one
 * choice rather than four switches, and two of them cannot take effect until the game is restarted.
 *
 * Like every other row that reaches out of this DLL, these write the settings file. The keys are
 * in the [enhanced_resolution] section: WindowMode, WindowedPresent, WindowedWidth,
 * WindowedHeight, WindowedFill, PointerReleaseKey and FullscreenToggleKey. That DLL owns the
 * window and re-reads them about once a second, so a choice here takes effect within that second.
 * Nothing here calls into that DLL, because feature DLLs in this tree do not depend on each other.
 */
#ifndef DEV_OVERLAY_OVERLAY_WINDOW_H
#define DEV_OVERLAY_OVERLAY_WINDOW_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The rows this group always has: fullscreen, the four shapes, the size, the two keys, the fill
 * and the note. */
#define OVERLAY_WINDOW_BASE_ROWS 10u

/* How many sizes the list will hold. Thirty two is well past what any display reports once
 * duplicates at other depths and refresh rates are folded together; the ones seen in testing offer
 * around fifteen. The list also carries one entry of its own above the sizes, so the group's
 * ceiling is the base rows plus this plus one. */
#define OVERLAY_WINDOW_SIZE_LIST_MAX 32u
#define OVERLAY_WINDOW_ROWS_MAX     (OVERLAY_WINDOW_BASE_ROWS + OVERLAY_WINDOW_SIZE_LIST_MAX + 1u)

/* And what it draws right now, which is more than that while the size list is open. The list is a
 * fold on one row rather than a group of its own, the same shape the free-camera "how to fly" row
 * already uses, because it belongs to the row above it and closes as soon as something is
 * chosen. */
uint32_t overlay_window_row_count(void);

/* Closes the size list, so the panel opens the way the groups do: folded. Without it a list left
 * open stays open across a close and reopen, which no other fold in the panel does. */
void overlay_window_reset(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `editing_text` and `capturing` mean what they mean in overlay_utilities.h. */
void overlay_window_row(uint32_t slot, const char *editing_text, bool capturing,
                        overlay_row_t *out);

/* True for the two binding rows. Takes a slot as this group numbers it. */
bool overlay_window_row_is_key(uint32_t slot);

/* Flips a switch, or picks a mode. The four mode rows are a CHOICE and not four switches: turning
 * one on turns the others off by writing one key, and turning the lit one off goes back to the
 * shape the engine gives itself rather than leaving no mode selected. */
bool overlay_window_toggle(uint32_t slot);

bool overlay_window_commit(uint32_t slot, const char *text);
bool overlay_window_bind(uint32_t slot, int32_t virtual_key);

#endif /* DEV_OVERLAY_OVERLAY_WINDOW_H */
