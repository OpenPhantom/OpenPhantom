/* camera_anchor.c: the 63 bytes that replace the camera's anchor mean, checked offline.
 *
 * This encoder is the one place in framerate_fix where a single wrong byte does NOT announce
 * itself. The replacement is written into the middle of a live x87 sequence inside
 * bapview_updateCam: a bad ModRM makes the camera read or write the wrong stack slot, a bad
 * length leaves a fragment of the original mean behind, and either way the game keeps running and
 * the camera is quietly wrong. There is no crash to find and no log line to read.
 *
 * So every byte is asserted individually against the encoding that was verified by disassembly,
 * rather than against a golden blob, a blob would pass just as happily if both the encoder and
 * the expectation were wrong in the same way.
 *
 *   per axis, 18 bytes:   D9 45 <a>  fld  [ebp-<a>]      anchor[i]
 *                         D8 65 <t>  fsub [ebp-<t>]      - target[i]
 *                         D8 0D <&k> fmul [k]            * k
 *                         D8 45 <t>  fadd [ebp-<t>]      + target[i]
 *                         D9 5D <a>  fstp [ebp-<a>]      -> anchor[i]
 *
 * The slot pairs come from the retail bytes at 0x00418623: anchor in [ebp-0x38/-0x34/-0x30] and
 * the alpha-interpolated look-at target in [ebp-0x1C/-0x18/-0x14].
 */
#include "unittest.h"

#include "camera_compensation.h"

#include <stdint.h>
#include <string.h>

static void check_byte(const uint8_t *buffer, size_t index, uint8_t expected, const char *what)
{
    ut_checkf(buffer[index] == expected, "%s (byte %u is %02X, expected %02X)",
              what, (unsigned)index, buffer[index], expected);
}

/* The displacements as they appear in the ModRM disp8 field, i.e. the two's complement of the
 * frame offset: 0xC8 = -0x38, 0xE4 = -0x1C, and so on. */
static const uint8_t ANCHOR_SLOT[3] = { 0xC8, 0xCC, 0xD0 };
static const uint8_t TARGET_SLOT[3] = { 0xE4, 0xE8, 0xEC };

#define AXIS_BYTES 18u

static void test_encoding(void)
{
    uint8_t   buffer[CAMERA_ANCHOR_RUN_BYTES];
    float     k = 0.5f;
    uint32_t  cell = (uint32_t)(uintptr_t)&k;
    uint32_t  encoded;
    size_t    axis;
    size_t    base;

    memset(buffer, 0xCC, sizeof(buffer));
    ut_check(camera_compensation_build_anchor_blend(buffer, sizeof(buffer), &k),
          "the encoder accepts a correctly sized buffer");

    for (axis = 0; axis < 3; ++axis) {
        base = axis * AXIS_BYTES;

        check_byte(buffer, base + 0,  0xD9, "fld opcode");
        check_byte(buffer, base + 1,  0x45, "fld ModRM: /0, [ebp+disp8]");
        check_byte(buffer, base + 2,  ANCHOR_SLOT[axis], "fld reads the anchor slot");

        check_byte(buffer, base + 3,  0xD8, "fsub opcode");
        check_byte(buffer, base + 4,  0x65, "fsub ModRM: /4, [ebp+disp8]");
        check_byte(buffer, base + 5,  TARGET_SLOT[axis], "fsub reads the target slot");

        check_byte(buffer, base + 6,  0xD8, "fmul opcode");
        check_byte(buffer, base + 7,  0x0D, "fmul ModRM: /1, [disp32]");
        memcpy(&encoded, buffer + base + 8, sizeof(encoded));
        ut_check(encoded == cell, "fmul names the weight cell, little-endian");

        check_byte(buffer, base + 12, 0xD8, "fadd opcode");
        check_byte(buffer, base + 13, 0x45, "fadd ModRM: /0, [ebp+disp8]");
        check_byte(buffer, base + 14, TARGET_SLOT[axis], "fadd reads the same target slot");

        check_byte(buffer, base + 15, 0xD9, "fstp opcode");
        check_byte(buffer, base + 16, 0x5D, "fstp ModRM: /3, [ebp+disp8]");
        check_byte(buffer, base + 17, ANCHOR_SLOT[axis], "fstp writes the anchor slot it read");
    }
}

/* The fill is not cosmetic. Whatever is not overwritten keeps executing: leaving even one byte of
 * the original mean behind would leave a stray fld or fmul in the stream, unbalance the x87 stack
 * and make the camera drift a little further wrong on every single frame. */
static void test_fill_and_length(void)
{
    uint8_t buffer[CAMERA_ANCHOR_RUN_BYTES];
    float   k = 0.5f;
    size_t  index;

    memset(buffer, 0xCC, sizeof(buffer));
    camera_compensation_build_anchor_blend(buffer, sizeof(buffer), &k);

    ut_check(CAMERA_ANCHOR_RUN_BYTES == 63u, "the run is 0x418623..0x418661 inclusive");
    ut_check(3u * AXIS_BYTES == 54u, "three axes occupy 54 of the 63 bytes");

    for (index = 3u * AXIS_BYTES; index < CAMERA_ANCHOR_RUN_BYTES; ++index) {
        check_byte(buffer, index, 0x90, "the tail of the run is filled with NOP");
    }
}

/* The three axes must not share a slot. A copy-paste that left axis y reading the x slot would
 * still assemble, still balance the stack, and still look right in a disassembler at a glance,
 * the camera would simply collapse two of its three dimensions onto one. */
static void test_axes_are_distinct(void)
{
    ut_check(ANCHOR_SLOT[0] != ANCHOR_SLOT[1] && ANCHOR_SLOT[1] != ANCHOR_SLOT[2] &&
          ANCHOR_SLOT[0] != ANCHOR_SLOT[2], "the three anchor slots differ");
    ut_check(TARGET_SLOT[0] != TARGET_SLOT[1] && TARGET_SLOT[1] != TARGET_SLOT[2] &&
          TARGET_SLOT[0] != TARGET_SLOT[2], "the three target slots differ");
    ut_check(ANCHOR_SLOT[0] != TARGET_SLOT[0] && ANCHOR_SLOT[1] != TARGET_SLOT[1] &&
          ANCHOR_SLOT[2] != TARGET_SLOT[2], "no axis reads its target out of its anchor slot");
}

/* A refused encode must leave nothing half-written: the caller's next step is a 63-byte write into
 * live code, and a partially filled buffer there is the failure mode this whole file exists for. */
static void test_refuses_wrong_size(void)
{
    uint8_t buffer[CAMERA_ANCHOR_RUN_BYTES];
    float   k = 0.5f;
    size_t  index;

    memset(buffer, 0xCC, sizeof(buffer));
    ut_check(!camera_compensation_build_anchor_blend(buffer, sizeof(buffer) - 1u, &k),
          "a short buffer is refused");
    ut_check(!camera_compensation_build_anchor_blend(buffer, sizeof(buffer) + 1u, &k),
          "an oversized buffer is refused too, the run has one exact length");
    ut_check(!camera_compensation_build_anchor_blend(NULL, sizeof(buffer), &k),
          "a null buffer is refused");

    for (index = 0; index < sizeof(buffer); ++index) {
        check_byte(buffer, index, 0xCC, "a refused encode writes nothing at all");
    }
}

/* The identity that makes this patch safe to ship: at 30 fps the blend must reproduce the mean the
 * engine shipped, so nothing changes for anyone running at the authored frame rate. */
static void test_blend_matches_the_mean_at_30fps(void)
{
    const float anchor = 12.5f;
    const float target = 40.25f;
    const float k = 0.5f;

    float mean  = (anchor + target) * 0.5f;
    float blend = (anchor - target) * k + target;

    ut_check(mean == blend, "at k = 0.5 the blend is the engine's arithmetic mean");

    /* And the property the mean does NOT have: the weights sum to 1 for every k, so the result is
     * always between the two inputs. Substituting k into the mean gives weights summing to 2k,
     * which is what made an earlier build lose the world at a high frame rate. */
    blend = (anchor - target) * 0.999f + target;
    ut_check(blend >= anchor && blend <= target, "k near 1 leaves the anchor essentially where it was");
    blend = (anchor - target) * 0.001f + target;
    ut_check(blend >= anchor && blend <= target, "k near 0 snaps the anchor onto the target");
}

int main(void)
{
    test_encoding();
    test_fill_and_length();
    test_axes_are_distinct();
    test_refuses_wrong_size();
    test_blend_matches_the_mean_at_30fps();

    return ut_summary("camera_anchor");
}
