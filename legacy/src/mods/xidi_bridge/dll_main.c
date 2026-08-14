/* dll_main.c: entry point of xidi_bridge.dll. See ../common/mod_entry.h for the contract. */
#include "xidi_bridge.h"

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
    xidi_bridge_install();
}
