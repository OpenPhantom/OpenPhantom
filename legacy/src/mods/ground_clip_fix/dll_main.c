/* dll_main.c: entry point of ground_clip_fix.dll. See ../common/mod_entry.h for the contract. */
#include "ground_clip_fix.h"

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
    ground_clip_fix_install();
}
