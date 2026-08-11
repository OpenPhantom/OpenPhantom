/* dll_main.c: entry point of sfx_volume_save_fix.dll. See ../common/mod_entry.h for the contract. */
#include "sfx_volume_save_fix.h"

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
    sfx_volume_save_fix_install();
}
