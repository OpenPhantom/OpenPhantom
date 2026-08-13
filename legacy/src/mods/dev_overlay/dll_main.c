/* dll_main.c: entry point of dev_overlay.dll. See ../common/mod_entry.h for the contract. */
#include "dev_overlay.h"

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
    dev_overlay_install();
}
