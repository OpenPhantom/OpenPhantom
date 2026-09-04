/* sw_blit_guard.c: see sw_blit_guard.h. */
#include "sw_blit_guard.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x004612D0  swrle_compressVBuffer ------------------------------------------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   81 EC 88000000        sub  esp, 0x88
 *   53 56 57              push ebx, esi, edi
 *   8B 75 08              mov  esi, [ebp+8]        the source buffer
 *   83 C6 0C              add  esi, 0x0C           its raster description
 *   B9 13000000           mov  ecx, 19             copied whole, as 19 dwords
 *
 * The nineteen dwords are what makes this the right function rather than any other prologue: the
 * raster description is copied to the stack and the third of its fields is doubled to size the
 * allocation, which is where two bytes a pixel is baked in. No address in the run, and it matches
 * once at ten bytes; twenty are used because they are free and they name the function. */
static const uint8_t SIG_COMPRESS_VBUFFER[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x53,
    0x56, 0x57, 0x8B, 0x75, 0x08, 0x83, 0xC6, 0x0C, 0xB9, 0x13
};

/* --- 0x004616CC  swrle_blit ------------------------------------------------------------------ *
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 EC 6C              sub  esp, 0x6C
 *   53 56 57              push ebx, esi, edi
 *   8B 45 14              mov  eax, [ebp+0x14]     the destination buffer
 *   8B 48 0C              mov  ecx, [eax+0x0C]     its width
 *   89 4D D0              mov  [ebp-0x30], ecx
 *   8B 55 14 / 8B 42 10   and its height
 *
 * Matches once. Both functions end in a plain ret, so the callers clean their own arguments and an
 * immediate return leaves the stack correct. */
static const uint8_t SIG_RLE_BLIT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x6C, 0x53, 0x56, 0x57, 0x8B,
    0x45, 0x14, 0x8B, 0x48, 0x0C, 0x89, 0x4D, 0xD0, 0x8B, 0x55
};

#define BYTE_PUSH_EBP 0x55u
#define BYTE_RET      0xC3u

typedef struct guard_site {
    const char    *name;
    const uint8_t *pattern;
    size_t         size;
} guard_site_t;

static const guard_site_t SITES[] = {
    { "swrle_compressVBuffer", SIG_COMPRESS_VBUFFER, sizeof SIG_COMPRESS_VBUFFER },
    { "swrle_blit",            SIG_RLE_BLIT,         sizeof SIG_RLE_BLIT }
};
#define SITE_COUNT (sizeof SITES / sizeof SITES[0])

bool sw_blit_guard_install(void)
{
    uintptr_t address[SITE_COUNT];
    size_t    i;
    size_t    written = 0;

    /* Resolve both before writing either. Silencing the compressor alone would be worse than
     * silencing neither: the blitter would then read raw pixels as run-length opcodes. */
    for (i = 0; i < SITE_COUNT; ++i) {
        address[i] = signature_find_unique(SITES[i].pattern, NULL, SITES[i].size);
        if (address[i] == 0) {
            log_warning("the 32-bit frame buffer needs the software menu blitter silenced but %s "
                        "did not resolve, so nothing has been written and the menus will fault on "
                        "the first bitmap", SITES[i].name);
            return false;
        }
    }

    for (i = 0; i < SITE_COUNT; ++i) {
        uint8_t held = 0;

        if (!memory_read_u8(address[i], &held)) {
            held = 0;
        }
        if (held == BYTE_RET) {
            ++written;
            continue;                          /* already silenced, idempotent */
        }
        if (held != BYTE_PUSH_EBP ||
            patch_write_u8(address[i], BYTE_RET) != PATCH_RESULT_OK) {
            log_error("%s at %08X reads %02X rather than the expected %02X, or would not take the "
                      "write; rolling back the %u already made",
                      SITES[i].name, (unsigned)address[i], held, (unsigned)BYTE_PUSH_EBP,
                      (unsigned)written);
            while (written-- > 0u) {
                (void)patch_write_u8(address[written], BYTE_PUSH_EBP);
            }
            return false;
        }
        ++written;
    }

    log_info("the software menu blitter is silenced at %08X and %08X for the 32-bit frame buffer. "
             "This is a DIAGNOSTIC and it removes ALL 2-D art: menu backgrounds, the loading bar "
             "and the HUD are gone. Menu text and the cursor are textured quads through Direct3D "
             "and still draw, so the menus stay navigable. The 2-D layer writes two-byte pixels "
             "into the locked back buffer from end to end, and at 32 bits the compressor's "
             "allocation fails and is not checked, which faults on the first menu bitmap. Set "
             "ModeBitDepth=16 to have the art back.",
             (unsigned)address[0], (unsigned)address[1]);
    return true;
}
