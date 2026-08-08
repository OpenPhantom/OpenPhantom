/* dll_main.c: entry point of hud_ratio_scaling.dll. See ../common/mod_entry.h for the contract. */
#include "hud_ratio_scaling.h"

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
    hud_ratio_scaling_install();
}
