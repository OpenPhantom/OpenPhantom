/* sw_blit_guard.h: silence the software menu blitter so a 32-bit frame buffer can be reached.
 *
 * The 2-D layer does not go through Direct3D. A menu bitmap is loaded, converted to 16 bits by
 * stdBitmap_ConvertTo16, run-length compressed by swrle_compressVBuffer, and then written pixel by
 * pixel into the locked back buffer by swrle_blit. Every one of those steps has two bytes a pixel
 * built into it.
 *
 * At ModeBitDepth=32 the compressor allocates from a raster description that no longer describes
 * two-byte pixels, the allocation fails, and the return is not checked: the retail build faults
 * writing to address zero at 0x0046130F, on the first menu bitmap, before a level can be reached.
 *
 * This turns both entry points into an immediate return. Both are cdecl and both end in a plain
 * ret, so the caller's stack is already correct and a single 0xC3 at the entry is the whole patch.
 *
 * What that costs: every piece of 2-D art disappears. Menu backgrounds, the loading bar, the HUD.
 * Menu text and the mouse cursor are textured quads through Direct3D and are unaffected, so the
 * menus stay readable and navigable.
 *
 * This is a DIAGNOSTIC, not a port, and IT IS NOT ENOUGH. It was written to get far enough into a
 * 32-bit session to look at the sky, and it does silence these two, but the next site along assumes
 * the same thing: the save-game thumbnail loader copies a literal byte count into a buffer built
 * from a stack raster description and runs off the end of the stack. Two bytes a pixel is spread
 * through the 2-D code rather than gathered in its blitters, so guards cannot clear a path through
 * it. mode_depth.h records how far the depth change gets and where it stops.
 *
 * Kept because the two entry points and their calling convention are worth not rediscovering. It is
 * tied to ModeBitDepth=32 and can do nothing at all at the shipped depth.
 */
#ifndef ENHANCED_RESOLUTION_SW_BLIT_GUARD_H
#define ENHANCED_RESOLUTION_SW_BLIT_GUARD_H

#include <stdbool.h>

/* True if both entry points were resolved and both now return immediately. False leaves the engine
 * exactly as it was, including the case where one resolved and the other did not. */
bool sw_blit_guard_install(void);

#endif /* ENHANCED_RESOLUTION_SW_BLIT_GUARD_H */
