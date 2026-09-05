/* dll_main.c: entry point of fmv_player.dll. See ../common/mod_entry.h for the contract.
 *
 * WHY THERE IS NO TEARDOWN HERE, WHICH IS NOT THE SAME AS HAVING FORGOTTEN ONE
 *
 * This DLL holds more process-global state than any other fix in this tree: two foreign modules
 * loaded with LoadLibraryW, a libVLC instance that owns threads of its own, an environment
 * variable, and a registered window class. The obvious place to give all four back is
 * DLL_PROCESS_DETACH, and enhanced_resolution's own entry point sets exactly that precedent.
 *
 * It is the wrong place for these four. DllMain runs under the loader lock, where FreeLibrary is
 * documented as a way to deadlock, and libvlc_release joins libVLC's threads, threads that
 * themselves may be sitting in a loader call. The precedent does not transfer: what
 * enhanced_resolution gives back on the way out is one ClipCursor(NULL), a single call into a
 * module that is already loaded, which is safe there and says nothing about this.
 *
 * So the state is deliberately not given back, and that costs nothing here. The mod loader
 * LoadLibrary's each DLL once and never frees one, so DLL_PROCESS_DETACH only ever runs as part of
 * the process exiting, and at that point every one of the four is reclaimed by Windows anyway. A
 * teardown that can only run in a case that does not happen, in a place where it can hang the
 * game, is worse than the leak it prevents.
 *
 * What IS cleaned up is the failure path: vlc_runtime.c unwinds its own modules and environment
 * variable when initialisation gives up halfway, because that runs on an ordinary thread outside
 * the loader lock and because leaving a half-loaded libVLC in the process would be somebody else's
 * problem later.
 */
#include "fmv_player.h"

#include "common/mod_entry.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }

    return TRUE;
}

ENGINE_FIX_ENTRY
{
    fmv_player_install();
}
