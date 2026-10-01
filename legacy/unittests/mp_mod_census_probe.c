/* mp_mod_census_probe.c: a DLL with nothing in it, for the census tests to copy into a mods folder
 * of their own and load. It carries no version resource, so a census finds it outside this
 * release, and it never goes near the game: it is built into the tests' folder only.
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
