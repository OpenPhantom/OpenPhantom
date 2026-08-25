/* dll_main.c: entry point of cutscene_pose_sync.dll. See ../common/mod_entry.h for the contract. */
#include "cutscene_pose_sync.h"

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
    cutscene_pose_sync_install();
}
