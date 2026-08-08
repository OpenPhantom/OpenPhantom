/* dll_main.c: entry point of view_distance_fix.dll.
 *
 * This is the one feature with something to do on the way out: the draw-table relocation has to
 * be undone while the process is still alive. See ../common/mod_entry.h for the contract.
 */
#include "view_distance_fix.h"

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
        view_distance_fix_shutdown();
        break;
    default:
        break;
    }

    return TRUE;
}

ENGINE_FIX_ENTRY
{
    view_distance_fix_install();
}
