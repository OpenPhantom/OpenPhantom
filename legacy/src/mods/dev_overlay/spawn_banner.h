/* spawn_banner.h: the one line the placement mode puts at the top of the picture.
 *
 * The mode takes the whole screen away from the panel: the menu hides, the pointer comes free and
 * what is left is a ghost under the pointer and a line of help at the bottom. What was missing was
 * the answer to the two questions a player has while standing in it, which of them is being placed
 * and how many are already out, and any word at all when it ends. A click that was refused and a
 * click that was the sixteenth look exactly the same from behind the pointer.
 *
 * The wording is here because it is the only part of this worth a test: the drawing needs a screen
 * and the counters need a running mode, and neither of those says whether the sentence is right.
 */
#ifndef DEV_OVERLAY_SPAWN_BANNER_H
#define DEV_OVERLAY_SPAWN_BANNER_H

#include <stddef.h>
#include <stdint.h>

/* What is being placed and how many stand already. `cap` is how many may: zero means a session
 * whose host has not said, and then the line does not promise a number it does not have. A label
 * that is empty is a mode with nothing chosen, which the panel's own row refuses to enter from and
 * the placement key can still reach. Always terminated. */
void spawn_banner_placing(char *out, size_t size, const char *label, uint32_t alive, uint32_t cap);

/* What the mode did, shown for a moment after it ends. It is the same tally the log writes, so a
 * player who saw the line and a reader who has the log are looking at one set of numbers. */
void spawn_banner_ended(char *out, size_t size, uint32_t placed, uint32_t removed,
                        uint32_t refused);

/* While the free camera has the mouse the mode stands still. The log has said so since the mode
 * was written and the picture said nothing, so a player watched their clicks do nothing. */
const char *spawn_banner_waiting(void);

#endif /* DEV_OVERLAY_SPAWN_BANNER_H */
