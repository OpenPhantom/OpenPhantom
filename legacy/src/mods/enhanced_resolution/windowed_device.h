/* windowed_device.h: ask DirectDraw for a window instead of the whole display, and change nothing
 * else.
 *
 * ==============================================================================================
 * One immediate, and the reason it is only one
 *
 * stdDisplay_ddSetMode at 0x00492C43 assembles its cooperative flags in two instructions:
 *
 *   00492C4F  C7 85 EC FB FF FF 11 00 00 00   mov  [ebp-0x414],0x11
 *   00492C5F  80 CC 08                        or   ah,8
 *   00492C81  FF 52 50                        call SetCooperativeLevel
 *
 * 0x11 is DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE and the `or` adds DDSCL_FPUSETUP, giving 0x811. Change
 * that one immediate to 0x08 and the same two instructions produce 0x808, DDSCL_NORMAL with
 * DDSCL_FPUSETUP kept. The device is then windowed and everything else the engine does is
 * untouched: it still calls SetDisplayMode, still creates its primary as
 * PRIMARYSURFACE|3DDEVICE|COMPLEX|FLIP with a back buffer count, still takes its render target
 * from GetAttachedSurface, still presents with Flip.
 *
 * ==============================================================================================
 * Why the elaborate version was wrong, recorded so that it is not rebuilt
 *
 * The first build of this file replaced the whole function: DDSCL_NORMAL, no SetDisplayMode, a
 * primary with DDSCAPS_PRIMARYSURFACE alone, a clipper, a separately created offscreen back buffer
 * in an explicit 16-bit format, and the Flip rewritten as a clipped Blt. That is the arrangement
 * every DirectX 6 and 7 sample uses for a windowed device, and it worked as far as the device
 * itself went: alt-tab became free and the graphics wrapper stopped resetting. It was still wrong
 * here, for three reasons that only appeared once it ran.
 *
 * The engine READS BACK from its front buffer. swmenu_open 0x0045D9F5 renders one last frame and
 * then copies the FRONT buffer into a system-memory snapshot, which message 0x15 at 0x0045D4E0
 * paints over the whole back buffer on every frame a pause page is up. The loading screen does the
 * same at 0x004469CC. Both take the source format from the front record, which says the mode's
 * size and 16 bits. Make the primary the desktop and those two reads take a 32-bit desktop apart
 * as 16-bit words: a full-surface write of real image data at the wrong scale and the wrong depth,
 * underneath correctly drawn widgets. That was the reported corruption, and it is why a windowed
 * primary cannot simply be the desktop for this engine.
 *
 * The present left the GPU. In DDSCL_NORMAL the wrapper this project ships puts a primary that is
 * not part of a flip chain into system memory, so a Blt into it can never be a hardware copy. It
 * falls through StretchRect, UpdateSurface and two D3DXLoadSurfaceFromSurface paths and lands on a
 * hand written per-pixel converter, once per frame at desktop size. Measured as a halving of the
 * frame rate. Keeping the flip chain lets the render target and the primary both be
 * D3DPOOL_DEFAULT, and the present stays a copy that never leaves the card.
 *
 * And the format fight was unwinnable. Left unstated, the back buffer inherited the desktop's 32
 * bits and the software 2-D layer, which writes two-byte pixels by hand, turned the menus magenta.
 * Stated as 565 the menus came right and the scene did not. Stated as 555 nothing drew at all,
 * because that converter supports exactly one format pair, R5G6B5 to X8R8G8B8. No value satisfied
 * both halves of the engine, because the real problem was that the surfaces were no longer the
 * mode's.
 *
 * Keeping the engine's own surfaces makes all three questions disappear rather than answering
 * them. The flip chain is the mode's size and the mode's format, so the readbacks are reading what
 * they were written to read, the 2-D layer is writing where it always wrote, and the present is
 * the engine's own Flip.
 *
 * ==============================================================================================
 * What this does not do
 *
 * Nothing about the window itself. WindowMode in window_mode.h shapes that, and the two are
 * independent: this decides whether the device owns the display, that decides how large the window
 * is and whether it has a frame.
 */
#ifndef WINDOWED_DEVICE_H
#define WINDOWED_DEVICE_H

#include <stdbool.h>

typedef struct windowed_device_config {
    /* Off unless the player asked for it. A device that owns the display is what every previous
     * release shipped and it stays the default. */
    bool enabled;
} windowed_device_config_t;

/* Resolves the site, writes the one immediate and logs which branch it took, including the branch
 * where it is switched off. Returns true only when the device will really be built windowed. */
bool windowed_device_install(const windowed_device_config_t *config);

/* Keeps the graphics wrapper's own setting in step with WindowedPresent, so that turning this on
 * is one decision rather than two.
 *
 * The wrapper decides how big the surfaces it hands the engine are, and with its DdrawWriteToGDI
 * off it hands out the DESKTOP's size whatever resolution the game is rendering. That is the wrong
 * size for every windowed arrangement and the right one for none of them, so the setting is not
 * really a choice: it is a thing that has to be true for WindowedPresent to work at all. Asking a
 * player to hand-edit a third-party file to make one of our own switches function is a poor deal
 * and it was how this shipped for exactly one session.
 *
 * Its name describes something it does not do here. The blit it is named for needs the surface to
 * be emulated and the game to not be using Direct3D, and this game fails the second test from its
 * first scene, so that path never runs. All it does for us is decide which arm of the wrapper's
 * surface sizing is taken.
 *
 * Writes only when the value differs, logs whenever it writes, and does nothing at all when the
 * file is not there, which is the case on a machine with no wrapper installed. The wrapper reads
 * its file once at startup, so a change lands on the next run: the same restart WindowedPresent
 * already needs. */
void windowed_device_align_wrapper(bool windowed_present);

#endif /* WINDOWED_DEVICE_H */
