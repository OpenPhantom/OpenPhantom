/* platform.c: see platform.h. */
#include "common/platform.h"

#include <windows.h>

bool platform_is_wine(void)
{
    static int cached = -1;      /* -1 not yet asked, 0 no, 1 yes */
    HMODULE    ntdll;

    if (cached >= 0) {
        return cached != 0;
    }

    /* GetModuleHandle and not LoadLibrary: ntdll is in every process before anything of ours runs,
     * so this takes no reference and cannot fail for a reason worth reporting. */
    ntdll  = GetModuleHandleA("ntdll.dll");
    cached = (ntdll != NULL && GetProcAddress(ntdll, "wine_get_version") != NULL) ? 1 : 0;
    return cached != 0;
}
