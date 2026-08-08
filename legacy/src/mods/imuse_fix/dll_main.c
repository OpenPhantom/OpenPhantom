/* dll_main.c: entry point of imuse_fix.dll.
 *
 * Nothing to undo on the way out. This DLL takes no process-global state: it holds no window
 * hook, no capture and no timer, and the only thing it ever writes is a call into two engine
 * functions that own their own latch. See ../common/mod_entry.h for the contract.
 */
#include "imuse_fix.h"

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
    imuse_fix_install();
}
