/* poly_bias.c: see poly_bias.h. */
#include "poly_bias.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x00419DBB  the bias read, in bapdraw's transform pass ---------------------------------- *
 *   8A 47 36                 mov   al, [edi+0x36]        the polygon's signed bias byte
 *   C6 44 24 13 3F           mov   byte [esp+0x13], 0x3F
 *   84 C0                    test  al, al
 *   C6 44 24 30 00           mov   byte [esp+0x30], 0
 *   74 17                    jz    +0x17                 zero byte skips the whole thing
 *   0F BE C8                 movsx ecx, al
 *   89 4C 24 2C              mov   [esp+0x2C], ecx
 *   DB 44 24 2C              fild  dword [esp+0x2C]
 *   D8 0D <scale>            fmul  dword [scale]         1/64, so the byte spans about +-2 units
 *
 * The four scale bytes are WILDCARDED and the address is read back out of the operand, which is
 * what the repository asks for and what keeps this working across the three builds that ship at
 * this file size. With them masked the pattern still matches exactly once. */
static const uint8_t SIG_POLY_BIAS_SCALE[] = {
    0x8A, 0x47, 0x36, 0xC6, 0x44, 0x24, 0x13, 0x3F, 0x84, 0xC0,
    0xC6, 0x44, 0x24, 0x30, 0x00, 0x74, 0x17, 0x0F, 0xBE, 0xC8,
    0x89, 0x4C, 0x24, 0x2C, 0xDB, 0x44, 0x24, 0x2C, 0xD8, 0x0D,
    0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_POLY_BIAS_SCALE[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0
};
_Static_assert(sizeof SIG_POLY_BIAS_SCALE == sizeof MSK_POLY_BIAS_SCALE,
               "the polygon bias pattern and its mask are different lengths");

#define OFFSET_SCALE_OPERAND 30u
#define EXPECTED_SCALE       0.015625f     /* 1/64 */

void poly_bias_install(bool enabled)
{
    uintptr_t site;
    uint32_t  address = 0;
    float     scale = 0.0f;

    if (enabled) {
        return;                            /* the engine's own behaviour, and the shipped answer */
    }

    site = signature_find_unique(SIG_POLY_BIAS_SCALE, MSK_POLY_BIAS_SCALE,
                                 sizeof SIG_POLY_BIAS_SCALE);
    if (site == 0) {
        log_warning("PolyDepthBias=0 was asked for but the bias scale did not resolve, so the "
                    "engine keeps its own depth bias");
        return;
    }
    if (!memory_read_u32(site + OFFSET_SCALE_OPERAND, &address) ||
        !memory_is_inside_image(address, sizeof(float)) ||
        !memory_try_read((uintptr_t)address, &scale, sizeof(scale))) {
        log_warning("the bias scale operand at %08X reads %08X, which is not a readable address "
                    "inside the image, refused", (unsigned)site, (unsigned)address);
        return;
    }
    /* Read back before writing, which is what makes this idempotent and what catches a build whose
     * pattern matched by accident. */
    if (scale != EXPECTED_SCALE) {
        log_warning("the bias scale at %08X holds %.6f rather than the expected %.6f, refused",
                    (unsigned)address, (double)scale, (double)EXPECTED_SCALE);
        return;
    }
    if (patch_write_f32((uintptr_t)address, 0.0f) != PATCH_RESULT_OK) {
        log_error("the bias scale at %08X is not writable", (unsigned)address);
        return;
    }

    log_info("PolyDepthBias=0: the per-polygon depth bias scale at %08X (site %08X) is zeroed, so "
             "every polygon biases the depth by nothing. This is a DIAGNOSTIC. The fog alpha is "
             "baked into a shared vertex from whichever polygon reached it first, so two polygons "
             "with different bias bytes give that vertex two different fogs depending on gather "
             "order, which changes as the camera moves. Zeroing the scale removes the difference. "
             "It also removes the depth sorting the bias exists for, so coplanar surfaces may "
             "fight.", (unsigned)address, (unsigned)site);
}
