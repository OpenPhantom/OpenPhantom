/* dll_main.c: entry point of custom_resolution.dll. See ../common/mod_entry.h for the contract.
 *
 * No shutdown work: the one thing this DLL owns is a record it writes into the engine's own,
 * already-allocated mode table and a handful of detours, neither of which needs releasing on
 * process exit - the same reasoning every other detour-only feature in this tree already relies
 * on (see common/detour.h's own "NO UNINSTALL" section).
 */
#include "custom_resolution.h"

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
    custom_resolution_install();
}
