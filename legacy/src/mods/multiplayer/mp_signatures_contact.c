/* mp_signatures_contact.c: the engine's own delivery of a contact to a task node. See the header.
 */
#include "mp_signatures_contact.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* task_run, from its head through the second of its two refusals: the null node and the empty
 * contact slot at +0x18, each answered with nought. The jump the first refusal takes to the shared
 * epilogue is a displacement and is masked; everything else is the function's own shape, and the
 * first sixteen bytes alone occur once in the retail image. */
static const uint8_t SIG_MP_TASK_RUN[32] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08, 0x89, 0x45, 0xFC,
    0x83, 0x7D, 0xFC, 0x00, 0x75, 0x07, 0x33, 0xC0, 0xE9, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x4D, 0xFC, 0x83, 0x79, 0x18, 0x00
};
static const uint8_t MSK_MP_TASK_RUN[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_MP_TASK_RUN) == sizeof(MSK_MP_TASK_RUN),
               "the task run pattern and its mask differ in length");

static signature_t contact_sites[MP_CONTACT_SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("task_run", SIG_MP_TASK_RUN, MSK_MP_TASK_RUN)
};

static bool resolved_once;

size_t mp_signatures_contact_resolve(void)
{
    size_t resolved;

    if (resolved_once) {
        return contact_sites[MP_CONTACT_SITE_TASK_RUN].address != 0u ? 1u : 0u;
    }
    resolved_once = true;
    resolved = signature_resolve_table(contact_sites, (size_t)MP_CONTACT_SITE_COUNT);
    log_info("%u of %u contact delivery site(s) resolved; the engine's own task run is at %08X",
             (unsigned)resolved, (unsigned)MP_CONTACT_SITE_COUNT,
             (unsigned)contact_sites[MP_CONTACT_SITE_TASK_RUN].address);
    return resolved;
}

uintptr_t mp_signatures_contact_address(mp_contact_site_t site)
{
    if ((size_t)site >= (size_t)MP_CONTACT_SITE_COUNT) {
        return 0u;
    }
    (void)mp_signatures_contact_resolve();
    return contact_sites[site].address;
}
