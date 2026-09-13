/* screen_fill.h: a flat filled rectangle in screen pixels, drawn through the engine's own
 * renderer with vertices every driver draws.
 *
 * The engine has one routine for an untextured quad on the screen (0x00419660: the fade veil,
 * the letterbox bars, the menu backdrop panels), and two DLLs draw through it, the developer
 * panel for every one of its fills and the movie player for its post-movie curtain. It writes
 * every vertex with rhw = 0 and, on a 16-bit depth buffer, z = 1.0, the far plane. NVIDIA and
 * AMD draw that. An Intel UHD laptop drew neither the panel's fills nor the curtain, the text and
 * the pointer beside them still there: a quad at the far plane loses a LESS depth test against a
 * cleared buffer, and a zero rhw is a division the driver may drop the primitive over.
 *
 * The routine's immediate arm is three engine calls around a four-vertex array: the render state
 * word, no texture, a triangle fan of transformed vertices. This reads those three targets out
 * of the routine's own body and makes the same calls with vertices of its own, z 0 and rhw 1,
 * the values Direct3D defines for a transformed vertex. The edges snap as the routine snaps
 * them. When the arm does not read as expected the routine itself is called, as before.
 */
#ifndef COMMON_SCREEN_FILL_H
#define COMMON_SCREEN_FILL_H

#include <stdbool.h>
#include <stdint.h>

/* `quad_routine` is the resolved entry of the engine's filled-shape routine. Returns true when
 * the fills will be drawn with this module's own vertices, false when they fall back to the
 * routine; either way screen_fill() draws afterwards. */
bool screen_fill_resolve(uintptr_t quad_routine);

/* A filled rectangle from (x0, y0) to (x1, y1) in screen pixels, `argb` packed as the engine
 * packs a colour, drawn there and then. Nothing is drawn before screen_fill_resolve(). */
void screen_fill(float x0, float y0, float x1, float y1, uint32_t argb);

#endif /* COMMON_SCREEN_FILL_H */
