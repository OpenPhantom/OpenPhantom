/* menu_scale_3d.h: the two hooks that put a menu's 3-D models where they belong, at the size they
 * were authored at.
 *
 * The cell below is what the three matrix-scale reads in sw3d_draw are repointed at, so the
 * install needs its address; the hooks are installed from there too. See menu_scale_3d.c for what
 * each one computes and why neither can be done by repointing a constant.
 *
 * Internal to the menu scale files.
 */
#ifndef MENU_SCALE_3D_H
#define MENU_SCALE_3D_H

#include <stdint.h>

extern float menu_sw3d_model_scale;

void __cdecl hook_sw3d_draw(void *widget);
void __cdecl hook_sw3d_project(float *offset, const int32_t *rect);

#endif /* MENU_SCALE_3D_H */
