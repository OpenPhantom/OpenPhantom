/* cinematic_gate.c: see cinematic_gate.h. */
#include "common/cinematic_gate.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x00430ED9  the cutscene lock's own entry ----------------------------------------------- *
 *   55 8B EC                     push ebp / mov ebp,esp
 *   51                           push ecx
 *   C7 45 FC 00000000            mov  [ebp-4], 0
 *   83 3D <g_cinematicLock> 00   cmp  dword [g_cinematicLock], 0
 *   75 11                        jnz  ...
 *
 * The four address bytes are WILDCARDED and the address is read back out of the operand, which is
 * what keeps this working across the three builds that ship at this file size and past a forced
 * relocation. With them masked the pattern still matches exactly once in the retail image.
 *
 * The lock is a LEVEL, not a flag: the engine raises it to 1, 5 or 99 depending on what took the
 * camera, and anything above zero means the player is not driving. That is all this needs to know,
 * so the value is tested rather than compared. */
static const uint8_t SIG_CINEMATIC_LOCK[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83,
    0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x11
};
static const uint8_t MSK_CINEMATIC_LOCK[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1
};
_Static_assert(sizeof SIG_CINEMATIC_LOCK == sizeof MSK_CINEMATIC_LOCK,
               "the cinematic lock pattern and its mask are different lengths");

#define OFFSET_LOCK_OPERAND 13u

static const volatile int32_t *cinematic_lock;
static bool                    resolved;

bool cinematic_gate_install(void)
{
    uintptr_t site;
    uint32_t  address = 0;

    if (resolved) {
        return cinematic_lock != NULL;
    }
    resolved = true;

    site = signature_find_unique(SIG_CINEMATIC_LOCK, MSK_CINEMATIC_LOCK,
                                 sizeof SIG_CINEMATIC_LOCK);
    if (site == 0) {
        log_warning("the cutscene lock did not resolve, so the camera compensation cannot stand "
                    "aside for a scripted camera and behaves exactly as it did before");
        return false;
    }
    if (!memory_read_u32(site + OFFSET_LOCK_OPERAND, &address) ||
        !memory_is_inside_image(address, sizeof(int32_t))) {
        log_warning("the cutscene lock operand at %08X reads %08X, which is not inside the image, "
                    "refused", (unsigned)site, (unsigned)address);
        return false;
    }

    cinematic_lock = (const volatile int32_t *)(uintptr_t)address;
    log_info("cutscene lock at %08X (site %08X). While it is raised the camera compensation hands "
             "the engine its own constants back, because a placed camera is not a followed one and "
             "smoothing it lags or overshoots what the script just set.",
             (unsigned)address, (unsigned)site);
    return true;
}

bool cinematic_gate_script_owns_camera(void)
{
    return cinematic_lock != NULL && *cinematic_lock != 0;
}
