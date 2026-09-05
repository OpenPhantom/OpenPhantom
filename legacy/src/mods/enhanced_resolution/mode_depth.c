/* mode_depth.c: see mode_depth.h. */
#include "mode_depth.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>

#define ENGINE_BITS_16 0x10u
#define ENGINE_BITS_32 0x20u

/* --- 0x0046C663  graphics_buildModeList, the depth gate ------------------------------------- *
 *   8B 45 FC        mov eax,[ebp-4]
 *   83 78 1C 01     cmp [eax+0x1C], 1        the mode kind
 *   74 02 / EB D4   jz +2 / jmp (reject)
 *   8B 4D FC        mov ecx,[ebp-4]
 *   83 79 20 10     cmp [ecx+0x20], 0x10     THE BIT COUNT
 *   74 02 / EB C9   jz +2 / jmp (reject)
 *
 * The bare compare matches five times in the image, so the pattern carries the kind test in front
 * of it and both branches behind. The two short jumps differ between this site and findMode below,
 * which is what makes each of them unique; they are instruction encoding rather than addresses, so
 * nothing here has to be read out of an operand. */
static const uint8_t SIG_BUILD_LIST_DEPTH[] = {
    0x8B, 0x45, 0xFC, 0x83, 0x78, 0x1C, 0x01, 0x74, 0x02, 0xEB, 0xD4,
    0x8B, 0x4D, 0xFC, 0x83, 0x79, 0x20, 0x10, 0x74, 0x02, 0xEB, 0xC9
};

/* --- 0x0046BC28  graphics_findMode, the same gate ------------------------------------------- *
 * Byte for byte the same shape; only the two jump displacements differ, D8 and CD against D4 and
 * C9. Without this one a listed mode cannot be selected and a saved resolution silently falls back
 * to 640x480. */
static const uint8_t SIG_FIND_MODE_DEPTH[] = {
    0x8B, 0x45, 0xFC, 0x83, 0x78, 0x1C, 0x01, 0x74, 0x02, 0xEB, 0xD8,
    0x8B, 0x4D, 0xFC, 0x83, 0x79, 0x20, 0x10, 0x74, 0x02, 0xEB, 0xCD
};
#define OFFSET_GATE_IMMEDIATE 17u

/* --- 0x0046BF0F  graphics_setResolution, the fallback template ------------------------------ *
 *   C7 45 C0 01000000     kind  = 1
 *   C7 45 C4 10000000     bpp   = 16      <- and this is the one nothing in this tree recorded
 *   C7 45 C8 05000000     red   = 5
 *   C7 45 CC 05000000     green = 5
 *   C7 45 D0 05000000     blue  = 5
 *
 * Built on the stack when findMode has already failed, then best-partial-matched against the raw
 * mode table. The scorer weighs kind, then bit count, then the dimensions, then the channels, and
 * returns the highest scorer rather than nothing, so a template asking for 16 against a table of
 * 32-bit modes scores every entry alike and hands back an arbitrary resolution.
 *
 * The three fives are left alone ON PURPOSE. The obvious reading is that 5/5/5 should become
 * 8/8/8, and it should not: the channel widths only decide the scorer's exact-match early exit, never
 * which entry wins the partial match, and changing them would be a guess dressed as a fix. */
static const uint8_t SIG_FALLBACK_TEMPLATE[] = {
    0xC7, 0x45, 0xC0, 0x01, 0x00, 0x00, 0x00,
    0xC7, 0x45, 0xC4, 0x10, 0x00, 0x00, 0x00,
    0xC7, 0x45, 0xC8, 0x05, 0x00, 0x00, 0x00
};
#define OFFSET_TEMPLATE_IMMEDIATE 10u

typedef struct depth_site {
    const char    *name;
    const uint8_t *pattern;
    size_t         size;
    size_t         offset;         /* of the byte holding the depth */
} depth_site_t;

static const depth_site_t SITES[] = {
    { "graphics_buildModeList",  SIG_BUILD_LIST_DEPTH,  sizeof SIG_BUILD_LIST_DEPTH,
      OFFSET_GATE_IMMEDIATE },
    { "graphics_findMode",       SIG_FIND_MODE_DEPTH,   sizeof SIG_FIND_MODE_DEPTH,
      OFFSET_GATE_IMMEDIATE },
    { "graphics_setResolution",  SIG_FALLBACK_TEMPLATE, sizeof SIG_FALLBACK_TEMPLATE,
      OFFSET_TEMPLATE_IMMEDIATE }
};
#define SITE_COUNT (sizeof SITES / sizeof SITES[0])

static uint32_t bits_in_force = ENGINE_BITS_16;

uint32_t mode_depth_bits(void)
{
    return bits_in_force;
}

uint32_t mode_depth_install(uint32_t bits)
{
    uintptr_t address[SITE_COUNT];
    size_t    i;
    size_t    written = 0;

    if (bits == ENGINE_BITS_16) {
        return ENGINE_BITS_16;                 /* the shipped answer: nothing is touched */
    }
    if (bits != ENGINE_BITS_32) {
        log_warning("ModeBitDepth=%u is not a depth this understands, 16 and 32 are, so the mode "
                    "list stays at 16", (unsigned)bits);
        return ENGINE_BITS_16;
    }

    /* Resolve every site before writing any. Half of this is worse than none: the two gates without
     * the template would list modes that cannot then be selected. */
    for (i = 0; i < SITE_COUNT; ++i) {
        address[i] = signature_find_unique(SITES[i].pattern, NULL, SITES[i].size);
        if (address[i] == 0) {
            log_warning("ModeBitDepth=32 was asked for but %s did not resolve, so the mode list "
                        "stays at 16 and nothing has been written", SITES[i].name);
            return ENGINE_BITS_16;
        }
    }

    for (i = 0; i < SITE_COUNT; ++i) {
        uintptr_t at = address[i] + SITES[i].offset;
        uint8_t   held = 0;

        if (!memory_read_u8(at, &held) || held != (uint8_t)ENGINE_BITS_16 ||
            patch_write_u8(at, (uint8_t)ENGINE_BITS_32) != PATCH_RESULT_OK) {
            log_error("%s at %08X would not take the write, rolling back the %u already made and "
                      "staying at 16", SITES[i].name, (unsigned)at, (unsigned)written);
            while (written-- > 0u) {
                (void)patch_write_u8(address[written] + SITES[written].offset,
                                     (uint8_t)ENGINE_BITS_16);
            }
            return ENGINE_BITS_16;
        }
        ++written;
    }

    bits_in_force = ENGINE_BITS_32;
    log_info("ModeBitDepth=32: the three depth gates are opened at %08X, %08X and %08X. The engine "
             "now lists, selects and falls back to 32-bit modes. This needs the graphics wrapper to "
             "be offering them; asking for a depth nothing offers leaves the list empty. Two things "
             "are known to be optimistic afterwards: the video-memory test in buildModeList counts "
             "two bytes a pixel, so it now under-counts by half, and the engine's own software "
             "blitters write two-byte pixels.",
             (unsigned)(address[0] + SITES[0].offset), (unsigned)(address[1] + SITES[1].offset),
             (unsigned)(address[2] + SITES[2].offset));
    return ENGINE_BITS_32;
}
