/* crt_copy_fix.c: the backwards copy that reads four bytes before its source.
 *
 * ==============================================================================================
 * THE DEFECT
 *
 * MSVC inlined a hand-written, backwards-running copy loop 40 times into this image. It LOADS
 * Before it checks the bound:
 *
 *     0x492219  8B 0C 06   mov ecx,[esi+eax]
 *     0x49221C  89 0C 07   mov [edi+eax],ecx
 *     0x49221F  83 E8 08   sub eax,8
 *     0x492222  8B 0C 1E   mov ecx,[esi+ebx]      ; second, unrolled branch
 *     0x492225  7C 0B      jl  ...
 *     0x492227  89 0C 1F   mov [edi+ebx],ecx
 *     0x49222A  83 EB 08   sub ebx,8
 *     0x49222D  8B 0C 06   mov ecx,[esi+eax]      ; load before the test
 *     0x492230  7D EA      jge 0x49221C
 *
 * It reads [-4 .. N-4] and writes [0 .. N-4]. On the FIRST row that read is `pixels - 4`.
 *
 * On heap memory this never shows, because mem_alloc puts a 0x10-byte header in front. It becomes
 * fatal exactly when the source is a locked DirectDraw surface whose preceding page is not
 * mapped, which is what the user's crash report showed:
 *
 *     ACCESS_VIOLATION at 0049222D, READ at 09BEEFFC
 *     esi=09BEEFFC  ->  row start 09BEF000, page aligned
 *
 * And because the copy dies on the FIRST row, the menu backdrop was never filled in any of the
 * four observed cases. The frozen pause background is therefore probably garbage in the retail
 * state as well; this repairs a picture, not only a crash.
 *
 * ==============================================================================================
 * THE REPLACEMENT
 *
 * Not an insertion, the whole loop is replaced. 25 bytes are available (0x492219..0x492231),
 * 11 are needed:
 *
 *     8B 0C 06   mov ecx,[esi+eax]
 *     89 0C 07   mov [edi+eax],ecx
 *     83 E8 04   sub eax,4
 *     7F F5      jg  back              ; rel8 = 0x492219 - 0x492224 = -0x0B
 *     90 x14                           ; padding, exactly up to `popal` at 0x492232
 *
 * Reads [0 .. N-4], writes [0 .. N-4], N/4 iterations, descending in steps of four. THE SAME
 * ORDER as the original, which matters because the backwards direction is what makes overlapping
 * ranges safe. The only difference is the removed load at -4.
 *
 * ==============================================================================================
 * Why this does not use a unique signature
 *
 * The site is DELIBERATELY not unique. The pattern occurs 40 times and all 40 are the same
 * defect, so signature_find_unique() would refuse every one of them. This scans for all matches
 * with the same strictness instead: the 25 bytes about to be replaced are read back and compared
 * before each write.
 *
 * The 44-byte window carries NEITHER an address NOR a rel32 (both rel8 branches are
 * self-relative), so this patch is ASLR-proof. It is idempotent: a second run no longer finds the
 * rewritten sequence.
 */
#include "crt_copy_fix.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CRT_SECTION        "crt_copy_fix"
#define EXPECTED_SITES     40

/* The window starts at the `cmp eax,0` (retail 0x492206); the loop follows 19 bytes later. */
static const uint8_t BACKWARD_COPY_PATTERN[] = {
    0x83, 0xF8, 0x00, 0x74, 0x27, 0x83, 0xE8, 0x04, 0x8B, 0xD8,
    0x83, 0xC0, 0x04, 0x83, 0xEF, 0x04, 0x83, 0xEE, 0x04,
    /* --- everything from here is replaced, offset 19 --- */
    0x8B, 0x0C, 0x06, 0x89, 0x0C, 0x07, 0x83, 0xE8, 0x08,
    0x8B, 0x0C, 0x1E, 0x7C, 0x0B, 0x89, 0x0C, 0x1F, 0x83, 0xEB, 0x08,
    0x8B, 0x0C, 0x06, 0x7D, 0xEA
};

#define PATCH_OFFSET 19u
#define PATCH_LENGTH 25u

static const uint8_t FIXED_LOOP[PATCH_LENGTH] = {
    0x8B, 0x0C, 0x06,                    /* mov ecx,[esi+eax] */
    0x89, 0x0C, 0x07,                    /* mov [edi+eax],ecx */
    0x83, 0xE8, 0x04,                    /* sub eax,4         */
    0x7F, 0xF5,                          /* jg  -0x0b         */
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

typedef struct crt_copy_fix_state {
    bool     installed;
    unsigned sites_found;
    unsigned sites_patched;
    unsigned sites_refused;
} crt_copy_fix_state_t;

static crt_copy_fix_state_t crt_state;

static void patch_all_sites(void)
{
    const uint8_t *text = (const uint8_t *)host_image_text();
    size_t         text_size = host_image_text_size();
    size_t         last_start;
    size_t         offset;
    uintptr_t      first_site = 0;

    if (text == NULL || text_size <= sizeof(BACKWARD_COPY_PATTERN)) {
        return;
    }
    last_start = text_size - sizeof(BACKWARD_COPY_PATTERN);

    for (offset = 0; offset <= last_start; ++offset) {
        uintptr_t site;
        uintptr_t loop;

        if (text[offset] != BACKWARD_COPY_PATTERN[0]) {
            continue;
        }
        if (memcmp(text + offset, BACKWARD_COPY_PATTERN,
                   sizeof(BACKWARD_COPY_PATTERN)) != 0) {
            continue;
        }

        site = (uintptr_t)(text + offset);
        loop = site + PATCH_OFFSET;
        ++crt_state.sites_found;
        if (first_site == 0) {
            first_site = site;
        }

        /* Read back, even though the pattern already matched: those 25 bytes are part of the same
         * pattern, but the rule is the rule and it costs nothing. */
        if (!patch_validate_bytes(loop, BACKWARD_COPY_PATTERN + PATCH_OFFSET, PATCH_LENGTH)) {
            ++crt_state.sites_refused;
            continue;
        }
        if (patch_write_bytes(loop, FIXED_LOOP, PATCH_LENGTH) == PATCH_RESULT_OK) {
            ++crt_state.sites_patched;
        } else {
            ++crt_state.sites_refused;
        }
    }

    if (crt_state.sites_found == 0) {
        log_warning("the backwards copy was not found, this image does not have it, or it looks "
                    "different. Nothing changed.");
        return;
    }

    log_info("repaired %u of %u sites (%u refused), first at %08X. It used to read four bytes "
             "BEFORE the row start, invisible on the heap, fatal on a locked DirectDraw surface "
             "whose preceding page is not mapped.",
             crt_state.sites_patched, crt_state.sites_found, crt_state.sites_refused,
             (unsigned)first_site);

    if (crt_state.sites_found != EXPECTED_SITES) {
        log_warning("%u sites expected, %u found, this image differs from the retail build, but "
                    "every single replacement was read back before it was written",
                    (unsigned)EXPECTED_SITES, crt_state.sites_found);
    }
}

void crt_copy_fix_install(void)
{
    log_init("crt_copy_fix", false);

    if (crt_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }
    if (!ini_read_bool(CRT_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the backwards copy keeps reading four bytes too far");
        return;
    }

    crt_state.installed = true;
    patch_all_sites();
}
