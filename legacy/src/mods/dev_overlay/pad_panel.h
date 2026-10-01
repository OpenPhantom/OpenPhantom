/* pad_panel.h: the panel from the pad, one frame at a time.
 *
 * Reads the pad through pad_input.c and hands the frame to the panel: the opening button, or
 * buttons together, open or close it on the press, and while it is open the left stick glides the
 * pointer, the D-pad steps it from row to row and switches the tab, A presses where it is (held,
 * it drags a slider), B is Escape, the triggers move the slider under the pointer, the right stick
 * scrolls and the bumpers page. While the free camera flies the panel takes nothing; the camera
 * reads the same frame itself.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_PAD_PANEL_H
#define DEV_OVERLAY_PAD_PANEL_H

/* Once a frame, before the panel's own update, so a press lands on the frame it was made. */
void pad_panel_tick(void);

#endif /* DEV_OVERLAY_PAD_PANEL_H */
