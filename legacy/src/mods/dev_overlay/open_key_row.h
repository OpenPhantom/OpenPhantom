/* open_key_row.h: which key opens this panel, as the panel's own row sees it.
 *
 * The shipped default is the key directly below Escape, and it accepts two virtual keys for it
 * because that one physical key is the backtick on a British or American layout and the caret on a
 * German one. It is a good default: every keyboard has that key, it needs no modifier, and the game
 * binds nothing to it. What it cannot do is cover a layout where that key is neither of those two,
 * and there are several, so on those machines the panel has no way in at all.
 *
 * This row is that way in, once. A player binds the key they want and it is written to the ini, so
 * the next start already has it. Reported as a keyboard layout problem rather than a missing
 * feature, which is why the fix is a binding rather than a different default.
 *
 * Unlike the other rows here this one calls straight into overlay_input, because that is the same
 * DLL: the rule against feature DLLs depending on each other does not apply inside one, and going
 * through the file would mean a key that does not work until something re-read it. It writes the
 * key as well, so the choice survives a restart.
 */
#ifndef DEV_OVERLAY_OPEN_KEY_ROW_H
#define DEV_OVERLAY_OPEN_KEY_ROW_H

#include <stdbool.h>
#include <stdint.h>

/* The bound key as a Windows virtual key code, or 0 for the shipped default, which is the key
 * below Escape and is accepted as either of the two codes that key produces. */
int32_t open_key_row_get(void);

/* Binds it, both in the running panel and in the ini. 0 restores the default. Refuses a key that
 * would leave the panel unopenable or unusable, and answers false: see the source for which. */
bool open_key_row_set(int32_t virtual_key);

#endif /* DEV_OVERLAY_OPEN_KEY_ROW_H */
