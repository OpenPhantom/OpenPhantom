/* mode_depth.h: which colour depth the whole mode chain runs at.
 *
 * The engine is 16 bit ON THE MODE LIST AND RENDERER SIDE, and that is three hardcoded comparisons
 * rather than a pervasive assumption. The DirectDraw enumeration callback itself already handles
 * every depth: it switches on the bit count through a jump table, converts pitch to pixels with a
 * `pitch >> 2` arm for 32, and sizes the surface generically as `w * h * (bpp >> 3)`. So a 32-bit
 * mode is RECORDED correctly, with correct channel widths and shifts, and then thrown away by:
 *
 *   graphics_buildModeList   cmp [ecx+0x20], 0x10   the options screen never lists it
 *   graphics_findMode        cmp [ecx+0x20], 0x10   a saved resolution never resolves to an index
 *   graphics_setResolution   mov [ebp-0x3C], 0x10   the fallback template asks for 16
 *
 * That third one matters and is the reason this is all or nothing. When findMode fails, the engine
 * builds a descriptor on the stack out of immediates and best-partial-matches it against the raw
 * mode table. Asking for 16 against a table holding only 32 scores every entry the same and returns
 * an arbitrary mode, so a half-applied change is worse than none: the two gates alone would list
 * modes that cannot then be selected, and a saved resolution would quietly fall back.
 *
 * WHY ANYONE WOULD WANT THIS. At 16 bits a channel has 5 bits, and the engine fades a level in over
 * four seconds with a full-screen black quad, which multiplies the whole picture. As the multiplier
 * slides, a large area of one flat colour crosses a quantisation boundary all at once and steps as
 * a block while lit geometry does not, so the edge between them flashes. That was measured, and it
 * is the visible half of a precision problem that costs every gradient in the game.
 *
 * It needs the wrapper to be handing out 32-bit surfaces as well; the engine asking for a depth it
 * is not offered leaves the list empty, and no patch here can conjure one.
 *
 * HOW FAR 32 ACTUALLY GETS, MEASURED. All three sites open, DdrawOverrideBitMode=32 in dxwrapper:
 * the wrapper creates a genuine D3DFMT_X8R8G8B8 device at the full desktop resolution and the whole
 * Direct3D scene draws through it correctly. The renderer side of the sentence at the top of this
 * file is not the problem.
 *
 * The 2-D layer is. It never goes near Direct3D: it locks the back buffer and writes pixels. Two
 * bytes a pixel is not confined to the blitters that do the writing, it is baked into allocation
 * sizes and into literal byte counts beside them, and each one only shows up once the one before it
 * has been dealt with. Three were reached in order, and the third is the shape of the rest:
 *
 *   0x004612D0  swrle_compressVBuffer  sizes its allocation by doubling a raster field, the
 *                                      allocation fails, the null is not checked, and it faults
 *                                      writing to zero on the first menu bitmap
 *   0x004616CC  swrle_blit             writes two-byte pixels into the locked back buffer
 *   0x00451B8A  the save thumbnail     copies a literal 0x3840 dwords, which is 160 x 120 x 3, into
 *                                      a buffer created from a raster description built on the
 *                                      stack, and runs off the end of the stack
 *
 * Silencing the first two is one byte each and it does let the menus draw their text, but it only
 * moves the fault on to the third. Finishing this means auditing every software write to the back
 * buffer, not patching a handful of functions, so it is PARKED rather than half done. See
 * sw_blit_guard.h for the two that are silenced and what that costs.
 *
 * Ships at 16, and 32 is not a supported setting.
 */
#ifndef ENHANCED_RESOLUTION_MODE_DEPTH_H
#define ENHANCED_RESOLUTION_MODE_DEPTH_H

#include <stdbool.h>
#include <stdint.h>

/* `bits` is 16 or 32; anything else is refused and 16 is kept. Returns the depth actually in force,
 * so the caller and the enumeration filter agree without either re-deriving it. All three sites are
 * written or none of them are. */
uint32_t mode_depth_install(uint32_t bits);

/* The depth in force, for the enumeration filter and for log lines. 16 until install says otherwise. */
uint32_t mode_depth_bits(void);

#endif /* ENHANCED_RESOLUTION_MODE_DEPTH_H */
