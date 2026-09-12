/* dry_at_start.c: see dry_at_start.h. */
#include "dry_at_start.h"

#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- The rule, in footstep_tick 0x00437AC0 and the print pass 0x004382F8 -------------------- *
 *
 *   00437BB8  83 7D F8 0C           cmp [ebp-8],12         floor material 12, 13 or 14 is wet
 *   00437BBE  83 7D F8 0E           cmp [ebp-8],14
 *   00437BCC  A1 E4 15 88 00        mov eax,[g_gameTime]   wall seconds since the process started
 *   00437BD1  89 82 08 01 00 00     mov [edx+0x108],eax    the stamp
 *
 *   004385B9  ...                   if material not 12..14 and g_gameTime - [thing+0x108] < 8.0
 *                                       lay the wet print, sprite 6, for 4 s
 *
 * The clock is the Time module's, set once when the module installs at startup, so it starts at
 * zero with the process. A stamp of zero therefore passes the test for the first eight seconds
 * of every session, on every body that has never been wet. Caught in the act with the footstep
 * observer in diagnostics: "wet prints begin ... the stamp is 7.28 s old (clock 7.28, stamp
 * 0.00)", on metal, on a save loaded seven seconds after launch.
 *
 * --- bapobj_init 0x00412270, the store at 0x00412359 ------------------------------------------ *
 *   C7 82 00 01 00 00 FF FF FF FF   mov dword [edx+0x100],-1
 *   8B 45 08                        mov eax,[ebp+8]
 *   C7 80 08 01 00 00 00 00 00 00   mov dword [eax+0x108],0        <- the stamp, imm32 at +13
 *   8B 4D 08                        mov ecx,[ebp+8]
 *   C7 81 0C 01 00 00 00 00 00 00   mov dword [ecx+0x10C],0
 *   8B 55 08                        mov edx,[ebp+8]
 *   C7 82 D4 00 00 00 00 00 80 3F   mov dword [edx+0xD4],1.0f      the mass, which makes it unique
 *
 * The function zeroes the whole record with `rep stosd` first and then writes the fields that are
 * not zero, so this store is the one place a new body's stamp is decided.
 *
 * --- bapobj_restoreObject 0x00410F36, the store at 0x00410FF4 ------------------------------- *
 *   83 C4 08                        add esp,8
 *   8B 95 E8 FE FF FF               mov edx,[ebp-0x118]
 *   C7 82 08 01 00 00 00 00 00 00   mov dword [edx+0x108],0        <- the stamp, imm32 at +15
 *   8B 85 E4 FE FF FF               mov eax,[ebp-0x11C]
 *   5F 5E 8B E5 5D C3               pop edi / pop esi / mov esp,ebp / pop ebp / ret
 *
 * The restore copies the whole saved record over the body and then clears this one field, so a
 * stamp from the previous process, which was on a different clock, cannot come back. That is
 * the right idea and this keeps it: the value written is the one that means "long ago" instead
 * of the one that means "now".
 *
 * Neither pattern carries an address, so both survive a relocation. Both immediates or neither:
 * a body restored dry and a body spawned wet would be a stranger state than the one repaired. */
static const uint8_t SIG_INIT_STORES[] = {
    0xC7, 0x82, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x8B, 0x45, 0x08,
    0xC7, 0x80, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0xC7, 0x81, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x55, 0x08,
    0xC7, 0x82, 0xD4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F
};
#define INIT_STAMP_IMMEDIATE 19u

static const uint8_t SIG_RESTORE_STORE[] = {
    0x83, 0xC4, 0x08,
    0x8B, 0x95, 0xE8, 0xFE, 0xFF, 0xFF,
    0xC7, 0x82, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x85, 0xE4, 0xFE, 0xFF, 0xFF,
    0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3
};
#define RESTORE_STAMP_IMMEDIATE 15u

/* Minus a thousand million seconds, as a float. The test is "clock minus stamp below eight", and
 * the clock counts up from zero, so any stamp this far in the past fails it for the life of the
 * process; the first real footstep on water overwrites it with the clock. */
#define LONG_AGO_BITS 0xCE6E6B28u

enum {
    SITE_INIT_STORES,
    SITE_RESTORE_STORE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("bapobj_init stamp",    SIG_INIT_STORES),
    SIGNATURE_ENTRY("bapobj_restore stamp", SIG_RESTORE_STORE)
};

static patch_journal_t journal;

bool dry_at_start_install(void)
{
    uintptr_t init_site;
    uintptr_t restore_site;

    signature_resolve_table(sites, SITE_COUNT);
    init_site    = sites[SITE_INIT_STORES].address;
    restore_site = sites[SITE_RESTORE_STORE].address;
    if (init_site == 0 || restore_site == 0) {
        log_warning("the wet stamp stores did not both resolve, so a body spawned or restored in "
                    "the first eight seconds still leaves wet prints on dry ground");
        return false;
    }

    patch_journal_reset(&journal);
    if (patch_journal_repoint_operand(&journal, init_site + INIT_STAMP_IMMEDIATE, 0u,
                                      LONG_AGO_BITS) != PATCH_RESULT_OK ||
        patch_journal_repoint_operand(&journal, restore_site + RESTORE_STAMP_IMMEDIATE, 0u,
                                      LONG_AGO_BITS) != PATCH_RESULT_OK) {
        patch_journal_undo(&journal);
        log_warning("one of the two wet stamp stores was refused, so both are as they were "
                    "(idempotent: a second run finds the new value here)");
        return false;
    }

    log_info("a spawned or restored body starts dry: the wet stamp stores at %08X and %08X "
             "write a time long past instead of zero, which the print pass read as wet for the "
             "first eight seconds of the process",
             (unsigned)(init_site + INIT_STAMP_IMMEDIATE),
             (unsigned)(restore_site + RESTORE_STAMP_IMMEDIATE));
    return true;
}
