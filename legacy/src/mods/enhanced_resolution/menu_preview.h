/* menu_preview.h: the swpic_draw hook that draws the four animated main menu previews larger.
 *
 * One entry point, because the buffers, the slots and the resampler behind it are nobody else's
 * business: the engine's own surface is never changed, and everything this owns exists only
 * between the hook being entered and the original returning. See menu_preview.c for why the
 * surface could not simply be created bigger.
 *
 * Internal to the menu scale files. It is installed by menu_scale_install.c.
 */
#ifndef MENU_PREVIEW_H
#define MENU_PREVIEW_H

void __cdecl hook_pic_draw(void *widget, void *menu);

#endif /* MENU_PREVIEW_H */
