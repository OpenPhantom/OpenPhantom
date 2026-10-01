/* dll_main.c: entry point of crash_report.dll.
 *
 * DllMain disables thread notifications and, as the process ends, writes the running count of
 * first-chance access violations one last time. The install runs from engine_fix_install, which
 * the loader calls after LoadLibrary has returned, outside the loader lock, with the game image
 * fully mapped.
 */
#include "crash_report.h"

#include "common/mod_entry.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        crash_report_shutdown();
    }

    return TRUE;
}

ENGINE_FIX_ENTRY
{
    crash_report_install();
}
