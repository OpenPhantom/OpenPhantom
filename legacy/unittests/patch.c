/* patch.c: the guards on writing into a live process, checked without one.
 *
 * These write into this program's own memory rather than into the game. That is not a stub: it is
 * the same function the fixes call, doing the same VirtualProtect, memcpy, read back and restore,
 * against a range whose contents this test knows. What it cannot cover is the engine, and nothing
 * here pretends to.
 *
 * The case worth having is the read back. A write that does not land used to be reported as a
 * success, and a feature that reports itself installed and then does nothing is the failure that
 * looks most like success.
 */
#include "unittest.h"

#include "common/patch.h"

#include <string.h>

/* Written into, so not const. Given a known pattern so a partial write would be visible. */
static unsigned char target[64] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA
};

int main(void)
{
    const unsigned char four[4] = { 0x11, 0x22, 0x33, 0x44 };

    ut_section("a write that should land");
    ut_check(patch_write_bytes((uintptr_t)target, four, sizeof four) == PATCH_RESULT_OK,
             "four bytes into a readable, writable range are accepted");
    ut_check(memcmp(target, four, sizeof four) == 0,
             "and are there afterwards, which is the read back this function now does");
    ut_check(target[4] == 0xAA,
             "with nothing written past the length asked for");

    ut_section("what it refuses");
    ut_check(patch_write_bytes((uintptr_t)target, NULL, sizeof four) ==
             PATCH_RESULT_INVALID_ARGUMENT, "no data is refused");
    ut_check(patch_write_bytes((uintptr_t)target, four, 0) == PATCH_RESULT_INVALID_ARGUMENT,
             "a length of zero is refused rather than treated as success");

    /* The first page is never mapped on Windows, so this is an address no process can write. */
    ut_check(patch_write_bytes((uintptr_t)0x10, four, sizeof four) == PATCH_RESULT_INVALID_ARGUMENT,
             "an unmapped address is refused, and named as a bad range rather than reported "
             "as a protection failure");
    ut_check(memcmp(target, four, sizeof four) == 0,
             "and none of those refusals disturbed the range that was written earlier");

    ut_section("validating before writing");
    ut_check(patch_validate_bytes((uintptr_t)target, four, sizeof four),
             "the bytes just written validate against themselves");
    ut_check(!patch_validate_bytes((uintptr_t)target, (const unsigned char *)"\xAA\xAA\xAA\xAA", 4),
             "and do not validate against what was there before, so a second install declines "
             "rather than writing again");
    ut_check(!patch_validate_bytes((uintptr_t)0x10, four, sizeof four),
             "an unmapped address validates as false rather than faulting");

    return ut_summary("patch");
}
