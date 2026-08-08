/* fov_menu.h: a field-of-view slider in the engine's own video options screen.
 *
 * Not an overlay and not a hotkey: a real widget of the engine's own SW_SLIDER class, drawn by
 * the engine's own toolkit, next to gamma and the resolution list. Nothing here draws a pixel.
 */
#ifndef FOV_MENU_H
#define FOV_MENU_H

/* Called by variable_fov_install() once the camera hook stands. Does nothing if it does not. */
void fov_menu_install(void);

#endif /* FOV_MENU_H */
