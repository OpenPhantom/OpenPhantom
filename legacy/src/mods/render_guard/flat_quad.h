/* flat_quad.h: the engine's flat screen quad, drawn with vertices every driver draws.
 *
 * The engine has one routine for an untextured quad on the screen (0x00419660): the fade veil,
 * the letterbox bars, the menu backdrops, the black the loading bar is repainted over between
 * steps. It writes every vertex with rhw = 0 and, on a 16-bit depth buffer, z = 1.0, the far
 * plane. NVIDIA and AMD draw that. Intel does not: an Intel UHD laptop lost the developer panel's
 * fills and the movie player's curtain (repaired in common/screen_fill.c, for those two callers
 * only) and, once those were back, still had the loading screen's frames and percentages piling
 * up, since the black behind the bar never landed.
 *
 * This replaces the routine with the same code, the recreation's forty lines, with rhw = 1 and
 * z = 0, the values Direct3D defines for a transformed vertex; z = 0 is also what the routine
 * itself writes on any depth buffer that is not 16-bit. Both arms are kept: the immediate one
 * makes the routine's own three host calls, the queued one hands the fan to the engine's sorted
 * queue as before. The targets are read out of the routine's body and the bytes around each are
 * checked first; if anything does not read, nothing is replaced and the log says so. On a
 * driver that drew the original the pixels are the same.
 */
#ifndef FLAT_QUAD_H
#define FLAT_QUAD_H

/* Installs the replacement when GuardFlatQuads is on. Logs what it did. */
void flat_quad_install(void);

#endif /* FLAT_QUAD_H */
