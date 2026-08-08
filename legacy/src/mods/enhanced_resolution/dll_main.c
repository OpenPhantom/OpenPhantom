/* dll_main.c: entry point of enhanced_resolution.dll.
 *
 * There is one thing to do on the way out: the pointer confinement is a ClipCursor rectangle, and
 * that is process-global state which outlives this DLL if nobody drops it. See
 * ../common/mod_entry.h for the contract.
 */
#include "enhanced_resolution.h"

#include "common/mod_entry.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(instance);
        break;
    case DLL_PROCESS_DETACH:
        enhanced_resolution_shutdown();
        break;
    default:
        break;
    }

    return TRUE;
}

ENGINE_FIX_ENTRY
{
    enhanced_resolution_install();
}
