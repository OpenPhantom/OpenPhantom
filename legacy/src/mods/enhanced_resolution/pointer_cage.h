#ifndef POINTER_CAGE_H
#define POINTER_CAGE_H

#include <stdbool.h>

/* Widen the DRAWN menu cursor's clamp from the 640x480 island it ships with to the whole display
 * mode, and keep it there when the mode changes.
 *
 * This is not the same thing as the pointer confinement in focus_guard.c, which is a ClipCursor on
 * the real OS pointer. This one is about the cursor the MENUS draw for themselves: the engine
 * clamps it to a 607x447 box anchored at the menu origin, and at 1920x1080 that box is a small
 * island in the middle of the screen that the drawn cursor cannot be moved out of.
 *
 * It is deliberately NOT about centring. The engine already centres its menus; it computes
 * (W-640)/2 and (H-480)/2 at startup and on every mode change and adds them in both the draw path
 * and the hit test, in 18 and 16 places respectively, so the two cannot disagree. None of that is
 * touched here.
 *
 * `enabled` false leaves the engine exactly as it shipped and says so once. */
void pointer_cage_install(bool enabled);

/* True when the clamp really was widened, for the caller's own log. */
bool pointer_cage_is_active(void);

/* ---- the arithmetic, exposed because it is the part that can be checked without a display -----
 *
 * The clamp the engine should carry for a given display mode. The engine's own constants are
 * 640-33 and 480-33: the cursor quad is 32 pixels wide and the clamp leaves one more, so that the
 * cursor is still drawn whole at the far edge. Keeping the same margin at every mode is what makes
 * the widened clamp identical to the shipped one at 640x480, which is the reason this may
 * default to on.
 *
 * Returns false, writing nothing, for a mode too small to hold a cursor at all. That is not a
 * theoretical case: the size accessor reports zeroes before a mode is configured and the shutdown
 * path leaves the globals negative, and a clamp computed from either would be a box the cursor
 * could never be inside. */
bool pointer_cage_extent(int mode_width, int mode_height,
                         int *out_clamp_width, int *out_clamp_height);

/* The margin above, so a test states the same number the code does rather than a copy of it. */
#define POINTER_CAGE_MARGIN 33

#endif /* POINTER_CAGE_H */
