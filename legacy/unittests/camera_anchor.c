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
 * the expectation were wrong in the same way. The run is then executed on a frame this test lays
 * out, so the slots are also proved by what the code reads and writes.
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

#include <windows.h>

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

/* The encoder's output, RUN. The bytes are wrapped in a prologue that points ebp at a frame this
 * test lays out and an epilogue that returns, and the whole thing is called on a page marked
 * executable. That is the only way to prove the slots are the right ones: the byte checks above
 * say each ModRM is what the disassembly said, this says the code they make reads the anchor
 * and the target from where the engine keeps them and writes the anchor back.
 *
 * The frame is 0x40 bytes with ebp at its top, so [ebp-0x38] is frame+0x08 and [ebp-0x1C] is
 * frame+0x24, the retail slots. The wrapper is
 *
 *     55             push ebp
 *     8B 6C 24 08    mov  ebp, [esp+8]     the frame pointer this test passes
 *     <63 bytes>
 *     5D             pop  ebp
 *     C3             ret
 */
#define FRAME_BYTES     0x40u
#define ANCHOR_OFFSET   (FRAME_BYTES - 0x38u)
#define TARGET_OFFSET   (FRAME_BYTES - 0x1Cu)

typedef void (__cdecl *anchor_blend_fn_t)(void *frame_top);

static anchor_blend_fn_t assemble(uint8_t *page, const float *k)
{
    static const uint8_t PROLOGUE[] = { 0x55, 0x8B, 0x6C, 0x24, 0x08 };
    static const uint8_t EPILOGUE[] = { 0x5D, 0xC3 };

    memcpy(page, PROLOGUE, sizeof PROLOGUE);
    if (!camera_compensation_build_anchor_blend(page + sizeof PROLOGUE, CAMERA_ANCHOR_RUN_BYTES,
                                                k)) {
        return NULL;
    }
    memcpy(page + sizeof PROLOGUE + CAMERA_ANCHOR_RUN_BYTES, EPILOGUE, sizeof EPILOGUE);
    return (anchor_blend_fn_t)(void *)page;
}

static void run_blend(anchor_blend_fn_t blend, float *anchor, const float *target)
{
    uint8_t frame[FRAME_BYTES];

    memset(frame, 0xCD, sizeof frame);
    memcpy(frame + ANCHOR_OFFSET, anchor, 3u * sizeof(float));
    memcpy(frame + TARGET_OFFSET, target, 3u * sizeof(float));
    blend(frame + FRAME_BYTES);
    memcpy(anchor, frame + ANCHOR_OFFSET, 3u * sizeof(float));
}

/* The identity that makes this patch safe to ship: at 30 fps the emitted code must reproduce
 * the mean the engine shipped, so nothing changes for anyone running at the authored frame rate.
 * The expectations are the engine's own arithmetic in float32, and the x87 sequence rounds to
 * float32 on every store. */
static void test_the_emitted_code_blends_the_anchor(void)
{
    uint8_t          *page = (uint8_t *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                                     PAGE_EXECUTE_READWRITE);
    volatile float    k;
    anchor_blend_fn_t blend;
    const float       target[3] = { 40.25f, -8.0f, 1000.125f };
    float             anchor[3];

    if (page == NULL) {
        ut_check(0, "an executable page was allocated (prerequisite)");
        return;
    }
    k = 0.5f;
    blend = assemble(page, (const float *)&k);
    ut_check(blend != NULL, "the encoder fills the run inside the wrapper");
    if (blend == NULL) {
        return;
    }

    anchor[0] = 12.5f;
    anchor[1] = 3.0f;
    anchor[2] = -20.0f;
    run_blend(blend, anchor, target);
    ut_check(anchor[0] == (12.5f + 40.25f) * 0.5f && anchor[1] == (3.0f + -8.0f) * 0.5f &&
             anchor[2] == (-20.0f + 1000.125f) * 0.5f,
             "run at k = 0.5, the emitted code leaves each anchor slot at the engine's own "
             "arithmetic mean of anchor and target, so 30 fps is unchanged");

    /* And the property the mean does NOT have: the weights sum to 1 for every k, so the result is
     * always between the two inputs. Substituting k into the mean gives weights summing to 2k,
     * and that made an earlier build lose the world at a high frame rate. The weight is read
     * through the cell each frame, so changing it here is what the live module does. */
    k = 0.999f;
    anchor[0] = 12.5f;
    anchor[1] = 3.0f;
    anchor[2] = -20.0f;
    run_blend(blend, anchor, target);
    ut_check(anchor[0] >= 12.5f && anchor[0] <= 40.25f && anchor[1] >= -8.0f &&
             anchor[1] <= 3.0f && anchor[2] >= -20.0f && anchor[2] <= 1000.125f,
             "k near 1 keeps every axis between its anchor and its target");
    ut_check(anchor[0] < 12.6f && anchor[1] > 2.9f && anchor[2] < -18.9f,
             "and within a thousandth of the way to the target, since k is the share of the "
             "anchor that is kept");

    k = 0.001f;
    anchor[0] = 12.5f;
    anchor[1] = 3.0f;
    anchor[2] = -20.0f;
    run_blend(blend, anchor, target);
    ut_check(anchor[0] > 40.0f && anchor[1] < -7.9f && anchor[2] > 999.0f,
             "k near 0 snaps the anchor onto the target");

    k = 0.0f;
    anchor[0] = 12.5f;
    anchor[1] = 3.0f;
    anchor[2] = -20.0f;
    run_blend(blend, anchor, target);
    ut_check(anchor[0] == 40.25f && anchor[1] == -8.0f && anchor[2] == 1000.125f,
             "k = 0, the placed camera's weight, puts the anchor exactly on the target");

    VirtualFree(page, 0, MEM_RELEASE);
}

int main(void)
{
    test_encoding();
    test_fill_and_length();
    test_axes_are_distinct();
    test_refuses_wrong_size();
    test_the_emitted_code_blends_the_anchor();

    return ut_summary("camera_anchor");
}
