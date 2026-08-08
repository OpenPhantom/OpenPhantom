/* dll_main.c: entry point of crash_report.dll.
 *
 * DllMain does nothing but disable thread notifications. The install runs from
 * engine_fix_install, which the loader calls after LoadLibrary has returned, outside the loader
 * lock, with the game image fully mapped.
 */
#include "crash_report.h"

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
    crash_report_install();
}
