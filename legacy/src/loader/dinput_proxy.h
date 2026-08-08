/* dinput_proxy.h: the seven exports a dinput.dll is expected to have.
 *
 * ==============================================================================================
 * WHY dinput.dll is the right slot
 *
 * WMAIN.EXE imports exactly ONE function from DINPUT.dll. DirectInputCreateA. Verified in all
 * three engine builds that ship in the game folder: the import descriptor names that one function
 * and nothing else, the IAT slot is 0x008C148C, and it is reached through a single thunk at
 * 0x00499220 with exactly ONE caller, at 0x0048D0CF, in the input startup path. The result is
 * stored to a local and never tested.
 *
 * DINPUT is not a KnownDLL, so the application directory wins over System32, and the usual
 * graphics wrappers occupy DDraw / D3D8 / D3DImm rather than this one.
 *
 * That single call is the mod loader's entry point, and it is a better one than DllMain:
 *   * it runs OUTSIDE the loader lock, so LoadLibrary is legal;
 *   * the main image is fully mapped and its .text can be scanned;
 *   * the window and the graphics device already exist.
 *
 * ==============================================================================================
 * THE CHAIN
 *
 * We take the dinput.dll name, so whatever used to answer to it must be given a new one. The
 * chain target is resolved in this order and the result is logged:
 *
 *   1. `ChainDll` from [loader] in engine_fixes.ini (absolute, or relative to the game folder)
 *   2. <game folder>\dinput_orig.dll
 *   3. <system directory>\dinput.dll
 *
 * On the installation this was written for, the dinput.dll slot held dxwrapper. Renaming it to
 * dinput_orig.dll keeps it working: we forward every export to it, and it forwards on to the
 * system.
 */
#ifndef DINPUT_LOADER_DINPUT_PROXY_H
#define DINPUT_LOADER_DINPUT_PROXY_H

#include <windows.h>

/* Resolves and loads the chain target. Safe to call more than once; only the first call works.
 * Must NOT be called from DllMain. */
void dinput_proxy_open_chain(void);

#endif /* DINPUT_LOADER_DINPUT_PROXY_H */
