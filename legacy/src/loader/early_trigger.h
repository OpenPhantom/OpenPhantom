/* early_trigger.h: load the mods BEFORE the game has initialised anything.
 *
 * ==============================================================================================
 * WHY THIS EXISTS, a measured failure, not a precaution
 *
 * The loader originally did all its work on the game's call to DirectInputCreateA. That call is
 * outside the loader lock, which is what makes LoadLibrary legal there, and it looked like the
 * ideal trigger. It is too late.
 *
 * The proof is in the log of the first real run. The old build, which patched from DllMain,
 * wrote:
 *
 *     ui : hooked font3d_setGlyphScale ... (display mode not set yet)
 *
 * and the new build, patching from DirectInputCreateA, wrote:
 *
 *     [text_aspect_fix] hooked font3d_set_glyph_scale ... (mode size 1920x1080)
 *
 * The display mode was ALREADY SET when we installed. Graphics startup, including
 * graphics_buildModeList, which filters the DirectDraw table into the list the options screen
 * shows, runs BEFORE input startup. So the 4:3 gate was lifted after the list had already been
 * built, and the options screen offered exactly one resolution: 800x600, the only 4:3 mode this
 * DirectDraw layer reports. The patch was applied, logged as successful, and had no effect.
 *
 * ==============================================================================================
 * THE TRIGGER: the host's own entry point, patched from DllMain
 *
 * dinput.dll is a static import of WMAIN.EXE, so its DllMain runs during process initialisation,
 * before a single instruction of the game. Loading the mods THERE is what must not happen,
 * LoadLibrary under the loader lock is how deadlocks are made. But we do not have to load
 * anything there; we only have to arrange to be called back at the earliest moment the lock is
 * gone. That moment is the host's entry point.
 *
 * The mechanism is a ONE-SHOT hook with no trampoline:
 *
 *   in DllMain   save the first 5 bytes of the entry point, write `jmp our_stub` over them
 *   in the stub  restore those 5 bytes, load the mods, jump back to the entry point
 *
 * Restoring before jumping back is what makes this safe without decoding a single instruction:
 * the entry point is re-executed from its first byte, so it does not matter whether those 5 bytes
 * happened to end mid-instruction. Only one thread exists at that point, so nothing can be
 * executing them while they are swapped.
 *
 * The address comes from the PE header (AddressOfEntryPoint), not from a byte pattern; it is
 * therefore build-independent and cannot be wrong.
 *
 * DirectInputCreateA remains a fallback trigger. mod_loader_run_once() is idempotent, so whichever
 * fires first wins and the other becomes a no-op.
 */
#ifndef DINPUT_LOADER_EARLY_TRIGGER_H
#define DINPUT_LOADER_EARLY_TRIGGER_H

#include <stdbool.h>

/* Safe to call from DllMain: it only reads PE headers and rewrites five bytes. It loads nothing.
 * Returns false when the entry point could not be hooked, in which case the DirectInputCreateA
 * fallback is the only trigger and the graphics-related patches will be too late. */
bool early_trigger_arm(void);

#endif /* DINPUT_LOADER_EARLY_TRIGGER_H */
