/* translucent_fog.c: see translucent_fog.h. */
#include "translucent_fog.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x00487E12  std3D_deferFace, where an alpha-blended face loses its fog ------------------- *
 *   8B 6C 24 40          mov  ebp, [esp+0x40]      the face's state word
 *   F7 C5 00000600       test ebp, 0x60000         either alpha-blend bit
 *   74 75                jz   +0x75                opaque: nothing to do
 *   F7 C5 00002000       test ebp, 0x200000        the one blend mode that keeps its fog
 *   75 03                jnz  +3
 *   83 E5 BF             and  ebp, 0xFFFFFFBF      CLEARS BIT 0x40, D3DRENDERSTATE_FOGENABLE
 *
 * No address anywhere in the run, so no mask is needed, and it matches once.
 *
 * The patch is the last byte, BF to FF: `and ebp, -1`, which changes nothing. Three bytes stay
 * three bytes, no branch moves, and reading it back is what makes it idempotent. */
static const uint8_t SIG_TRANSLUCENT_FOG_OFF[] = {
    0x8B, 0x6C, 0x24, 0x40, 0xF7, 0xC5, 0x00, 0x00, 0x06, 0x00,
    0x74, 0x75, 0xF7, 0xC5, 0x00, 0x00, 0x20, 0x00, 0x75, 0x03,
    0x83, 0xE5, 0xBF
};
#define OFFSET_AND_IMMEDIATE 22u
#define IMMEDIATE_CLEARS_FOG 0xBFu     /* and ebp, ~0x40 */
#define IMMEDIATE_KEEPS_FOG  0xFFu     /* and ebp, -1, a no-op */

void translucent_fog_install(bool keep_fog_on_translucent)
{
    uintptr_t site;
    uint8_t   immediate = 0;

    if (!keep_fog_on_translucent) {
        return;                                /* the engine's own behaviour */
    }

    site = signature_find_unique(SIG_TRANSLUCENT_FOG_OFF, NULL, sizeof SIG_TRANSLUCENT_FOG_OFF);
    if (site == 0) {
        log_warning("TranslucentFog=1 was asked for but the fog-disable in the deferred face "
                    "submit did not resolve, so translucent faces keep losing their fog");
        return;
    }
    if (!memory_read_u8(site + OFFSET_AND_IMMEDIATE, &immediate)) {
        return;
    }
    if (immediate == IMMEDIATE_KEEPS_FOG) {
        return;                                /* already done, idempotent */
    }
    if (immediate != IMMEDIATE_CLEARS_FOG) {
        log_warning("the immediate at %08X reads %02X rather than the expected %02X, refused",
                    (unsigned)(site + OFFSET_AND_IMMEDIATE), immediate, IMMEDIATE_CLEARS_FOG);
        return;
    }
    if (patch_write_u8(site + OFFSET_AND_IMMEDIATE, IMMEDIATE_KEEPS_FOG) != PATCH_RESULT_OK) {
        log_error("the immediate at %08X is not writable",
                  (unsigned)(site + OFFSET_AND_IMMEDIATE));
        return;
    }

    log_info("TranslucentFog=1: the fog-enable clear at %08X is a no-op, so an alpha-blended face "
             "keeps the fog every other face gets. This is a DIAGNOSTIC. The engine drops fog on "
             "translucent faces, and the level-of-detail cross-fade makes geometry translucent "
             "WHILE IT CROSSES, so a surface loses its fog for the length of the transition and "
             "reads at full brightness against a background already saturated to the fog colour. "
             "That is a band at a fixed distance from the camera, moving with it, invisible when "
             "the band saturates beyond the draw cut, and steady when the camera is still.",
             (unsigned)(site + OFFSET_AND_IMMEDIATE));
}
