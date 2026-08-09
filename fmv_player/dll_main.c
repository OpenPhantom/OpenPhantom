/* dll_main.c: entry point of fmv_player.dll. See ../common/mod_entry.h for the contract. */
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
