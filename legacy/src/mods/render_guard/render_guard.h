/* render_guard.h: bound the two arrays the deferred face path fills without checking, and replace
 * a depth comparison the engine can compute as a value Direct3D does not define.
 *
 * Produces: render_guard.dll
 *
 * Three repairs, and all three are the identity on a scene and a device that stay inside what the
 * engine was written for. Two are array bounds in the deferred face submit, and a face past either
 * of them is refused exactly the way the engine already refuses one when its queue is full, which
 * every caller of that function handles. The third is an answer of zero from the depth comparison
 * mapper, substituted rather than handed on to the device.
 */
#ifndef RENDER_GUARD_H
#define RENDER_GUARD_H

void render_guard_install(void);

#endif /* RENDER_GUARD_H */
