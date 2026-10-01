/* mp_start_restore.c: see mp_start_restore.h. */
#include "mp_start_restore.h"

#include "mp_signatures.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* swmenu_setCloseKeepsMode 0x0045DC3A, the whole function:
 *   55 8B EC         push ebp; mov ebp,esp
 *   8B 45 08         mov eax,[ebp+8]
 *   A3 abs32         mov [g_bCloseKeepsMode],eax      masked
 *   5D C3            pop ebp; ret
 * Masked, this is the idiom for storing one int into a global and matches 22 times, so it is not
 * searched for. It is proven at swmenu_close + 0xC0, the distance in every retail image. */
static const uint8_t SIG_MP_SWMENU_CLOSE_KEEPS_MODE[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xA3, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xC3
};
static const uint8_t MSK_MP_SWMENU_CLOSE_KEEPS_MODE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};

/* How far behind swmenu_close the setter starts, and how many head bytes the proof skips so that
 * a branch another module may have written over the head does not fail it. Six is push ebp,
 * mov ebp esp, mov eax [ebp+8], the first instruction boundary past five. Nothing here detours
 * the site; the six are the proof's own. */
#define KEEPS_MODE_BEHIND_CLOSE 0xC0u
#define KEEPS_MODE_HEAD_BYTES   6u

typedef void(__cdecl *keeps_mode_fn)(int32_t keep);

static keeps_mode_fn keeps_mode;

bool mp_start_restore_install(void)
{
    uintptr_t close_site = mp_signatures_address(MP_SITE_SWMENU_CLOSE);
    uintptr_t site;

    keeps_mode = NULL;
    if (close_site == 0u) {
        log_warning("close-keeps-mode cannot be placed: swmenu_close did not resolve, so no "
                    "RESTORE_DONE after a lobby restore");
        return false;
    }
    site = signature_find_at(close_site + KEEPS_MODE_BEHIND_CLOSE,
                             SIG_MP_SWMENU_CLOSE_KEEPS_MODE, MSK_MP_SWMENU_CLOSE_KEEPS_MODE,
                             sizeof SIG_MP_SWMENU_CLOSE_KEEPS_MODE, KEEPS_MODE_HEAD_BYTES);
    if (site == 0u) {
        log_warning("close-keeps-mode is not where it stands in the retail image, %08X, so no "
                    "RESTORE_DONE after a lobby restore",
                    (unsigned)(close_site + KEEPS_MODE_BEHIND_CLOSE));
        return false;
    }
    keeps_mode = (keeps_mode_fn)site;
    return true;
}

void mp_start_restore_keep_mode(void)
{
    if (keeps_mode != NULL) {
        keeps_mode(1);
    }
}
