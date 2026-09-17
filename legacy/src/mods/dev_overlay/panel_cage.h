/* panel_cage.h: the system cursor kept inside the panel while the panel is open.
 *
 * The panel's pointer is the system cursor, and the cursor is free to leave the panel: a mouse
 * pushed past its edge, or the sideways motion controller_input fakes from the right stick,
 * puts the pointer on the game's picture, where nothing takes a click and the eye has to go
 * looking for it. So while the panel is shown and the game has the focus, a cursor found
 * outside the panel's rectangle is put on the nearest point inside it, every frame, the warp
 * on every move the engine itself confines its play area with. Nothing is held: the moment the
 * panel is hidden or closed, or the focus goes, the cursor is free.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_PANEL_CAGE_H
#define DEV_OVERLAY_PANEL_CAGE_H

/* Once a frame, after the panel has been laid out: warps a cursor outside the panel back in
 * while the panel is shown, and does nothing otherwise. */
void panel_cage_tick(void);

#endif /* DEV_OVERLAY_PANEL_CAGE_H */
