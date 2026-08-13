/* large_textures.h: raise the ceiling on how big a texture page may be.
 *
 * Produces: large_textures.dll
 *
 * A capability rather than a fix. It repairs no defect, and on the artwork the game ships it is
 * the identity: 4000 of the 6482 exported textures were measured from their own headers and none
 * of them is larger than 256 pixels on either axis, which is exactly where the engine clamps. It
 * exists so that replacement artwork can be larger than 1999 hardware allowed, instead of being
 * cropped without a word.
 */
#ifndef LARGE_TEXTURES_H
#define LARGE_TEXTURES_H

void large_textures_install(void);

#endif /* LARGE_TEXTURES_H */
