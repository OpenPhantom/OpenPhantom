#include "diag_install.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool diag_install_observer(signature_t *table, int index, detour_t *detour, const void *hook,
                           size_t prologue_size, const char *what)
{
    uintptr_t site = table[index].address;

    if (site == 0) {
        log_info("  %-22s OFF, the site did not resolve (%s)", table[index].name, what);
        return false;
    }
    if (!detour_install(detour, site, hook, prologue_size)) {
        log_warning("  %-22s OFF, the detour at %08X failed",
                    table[index].name, (unsigned)site);
        return false;
    }

    log_info("  %-22s %08X  %s", table[index].name, (unsigned)site, what);
    return true;
}

void *diag_derive_address(signature_t *table, int index, uint32_t offset, const char *what)
{
    uintptr_t site = table[index].address;
    uint32_t  address;

    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + offset, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        /* Naming the likely cause, because the number alone sends the reader hunting the wrong
           thing. An operand inside a function's prologue is gone once another DLL has detoured
           that function: the bytes are its jump and whatever padding followed it, and reading
           them back gives a branch displacement or a run of 90s rather than an address. */
        const bool detoured = (*(const uint8_t *)site == 0xE9u);

        log_warning("%s from %s = %08X is outside the image, refused%s",
                    what, table[index].name, (unsigned)address,
                    detoured ? ". That site starts with a branch, so another DLL has detoured it "
                               "and this operand sits inside the bytes it replaced"
                             : "");
        return NULL;
    }

    return (void *)(uintptr_t)address;
}
