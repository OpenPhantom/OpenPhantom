/* mp_enemy_pack.c: putting something that may not be there into a delta coded field.
 *
 * The whole point of the offset is that zero can mean nothing, so the checks that matter are the
 * ones at zero: a real value of zero must survive the round trip, and a packed zero must read as
 * nothing. Everything else here is the two boundaries.
 */
#include "unittest.h"

#include "mp_enemy_pack.h"

#include <stdbool.h>
#include <stdint.h>

static void test_zero_is_a_real_value(void)
{
    uint32_t value = 0xAAu;

    ut_check(mp_enemy_pack_one(0u, 254u) != 0u,
             "a real value of zero packs to something, because a packed zero never travels");
    ut_check(mp_enemy_unpack_one(mp_enemy_pack_one(0u, 254u), &value) && value == 0u,
             "and it comes back as zero");
    ut_check(!mp_enemy_unpack_one(0u, &value),
             "a packed zero reads as nothing at all");
    ut_check(value == 0u, "and leaves the caller's value where it was");
}

static void test_the_boundaries(void)
{
    uint32_t value = 0;

    ut_check(mp_enemy_unpack_one(mp_enemy_pack_one(254u, 254u), &value) && value == 254u,
             "254 is the largest that fits a byte offset by one");
    ut_check(mp_enemy_pack_one(255u, 254u) == 0u, "255 does not, and is refused rather than cut");
    ut_check(mp_enemy_pack_one(5u, 4u) == 0u, "a value past the caller's own limit is refused");
    ut_check(mp_enemy_pack_one(0u, 255u) == 0u,
             "and a limit the field itself cannot hold refuses everything, rather than letting "
             "one value through that would come back as nothing");
}

static void test_the_pair(void)
{
    uint32_t low = 0xAAu;
    uint32_t high = 0xAAu;

    ut_check(mp_enemy_unpack_pair(mp_enemy_pack_pair(0u, 0u, 254u, 255u), &low, &high) &&
                 low == 0u && high == 0u,
             "a pair of zeros survives, which a field of zero could never have carried");
    ut_check(mp_enemy_unpack_pair(mp_enemy_pack_pair(3u, 200u, 254u, 255u), &low, &high) &&
                 low == 3u && high == 200u,
             "and so does an ordinary pair");
    ut_check(mp_enemy_pack_pair(255u, 0u, 254u, 255u) == 0u, "a low past its limit refuses both");
    ut_check(mp_enemy_pack_pair(0u, 256u, 254u, 255u) == 0u, "and so does a high past its own");
    low = 0xAAu;
    ut_check(!mp_enemy_unpack_pair(0u, &low, &high) && low == 0xAAu,
             "an empty field reads as nothing and touches neither half");
}

int main(void)
{
    test_zero_is_a_real_value();
    test_the_boundaries();
    test_the_pair();

    return ut_summary("mp_enemy_pack");
}
