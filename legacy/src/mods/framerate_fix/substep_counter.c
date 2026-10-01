/* substep_counter.c: see substep_counter.h. */
#include "substep_counter.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>

/* 0x4757DB, the tail of the substep loop, where the simulation step counter is incremented:
 *
 *     004757DB  A1 60884B00     mov  eax, [g_tickCounter]
 *     004757E0  83 C0 01        add  eax, 1
 *     004757E3  A3 60884B00     mov  [g_tickCounter], eax
 *     004757E8  D9 05 28878600  fld  [g_simTime]
 *     004757EE  D8 05 14878600  fadd [dt]
 *     004757F4  D9 1D 28878600  fstp [g_simTime]
 *
 * Every absolute operand is wildcarded so the pattern carries no address at all, and the counter
 * is read out of the operand it matched. This is the SIMULATION STEP counter, which is a different
 * cell from the per-frame animation counter the rest of this DLL uses. */
static const uint8_t SIG_SUBSTEP_COUNTER[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x01,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SUBSTEP_COUNTER[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_SUBSTEP_COUNTER == sizeof MSK_SUBSTEP_COUNTER,
               "the substep counter pattern and its mask are different lengths");

/* The `mov eax, [address]` at the head of the pattern carries the cell in its only operand. */
#define OFFSET_SUBSTEP_COUNTER 0x01u

static const volatile uint32_t *counter_cell;
static bool                     counter_attempted;

bool substep_counter_resolve(void)
{
    uintptr_t site;
    uint32_t  address;

    /* Asked once per feature that wants it, and a second failure would log the same warning a
     * second time and read as two separate faults. */
    if (counter_attempted) {
        return counter_cell != NULL;
    }
    counter_attempted = true;

    site = signature_find_unique(SIG_SUBSTEP_COUNTER, MSK_SUBSTEP_COUNTER,
                                 sizeof SIG_SUBSTEP_COUNTER);
    if (site == 0) {
        log_warning("the substep counter pattern did not resolve, so there is no way to tell one "
                    "simulation step from the next");
        return false;
    }
    if (!memory_read_u32(site + OFFSET_SUBSTEP_COUNTER, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        log_warning("the substep counter operand at %08X is not an address inside the image",
                    (unsigned)(site + OFFSET_SUBSTEP_COUNTER));
        return false;
    }

    counter_cell = (const volatile uint32_t *)(uintptr_t)address;
    log_info("the simulation step counter is at %08X, from the increment at %08X",
             (unsigned)address, (unsigned)site);
    return true;
}

bool substep_counter_read(uint32_t *out_step)
{
    if (out_step == NULL || counter_cell == NULL) {
        return false;
    }
    *out_step = *counter_cell;
    return true;
}

uintptr_t substep_counter_cell(void)
{
    return (uintptr_t)counter_cell;
}
