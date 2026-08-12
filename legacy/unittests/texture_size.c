/* texture_size.c: what a requested texture size is allowed to do, checked without the game.
 *
 * Two of these are cross-checks against the retail image rather than against my own arithmetic:
 *
 *   * 256 is the ceiling the engine's own clamp already holds, the 0x100 in the immediate at
 *     0x00488758, so asking for 256 has to come out as "nothing to do" and write nothing at all;
 *   * a 256 by 256 world page has to come out as 65536 bytes, which is exactly the block pushed at
 *     0x0041DAB6 for the level loader to read every world page into.
 *
 * If either drifts, this has stopped matching the engine, and the feature would either rewrite an
 * instruction with the value it already holds or size that block wrongly. The second one is not a
 * cosmetic mistake: the loader reads width times height bytes into that block and checks neither
 * number.
 *
 * The honest framing of this feature is that it is a capability and not a fix, and that on the
 * artwork the game ships, no page reaches either limit, so raising them changes nothing that is
 * drawn. The shipped setting still asks for 1024 and the clamp is still rewritten; what is
 * asserted here is which requests are accepted, not that the feature writes nothing.
 */
#include "unittest.h"

#include "texture_size.h"

#include <stdint.h>

static void test_the_values_the_engine_holds(void)
{
    ut_section("the values the engine itself holds");

    /* The clamp holds 256, so this is the identity case and the reason this feature can be
     * installed on an untouched game and change nothing. */
    ut_check(texture_size_check(256, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_UNCHANGED,
             "asking for the 256 the clamp already holds writes nothing");

    /* The block holds 65536 bytes, which at one byte per pixel is 256 by 256. That is also the size
     * 98 of the 116 shipped world pages already are. */
    ut_check(texture_size_world_page_bytes(256) == 65536u,
             "a 256 by 256 world page is exactly the 65536 byte block the loader allocates");

    /* The shipped defaults: the world page key is left at the engine's own value, so that half of
     * the feature is inert until a player raises it, and the ceiling key is not. */
    ut_check(texture_size_check(256, 256, TEXTURE_SIZE_MIN_WORLD_PAGE, TEXTURE_SIZE_MAX_WORLD_PAGE)
             == TEXTURE_SIZE_UNCHANGED,
             "the shipped MaxWorldPageSize of 256 is the engine's own, so nothing is patched");
    ut_check(texture_size_check(1024, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_APPLY,
             "the shipped MaxTextureSize of 1024 is accepted and raises the clamp");
}

static void test_the_floor(void)
{
    ut_section("the floor");

    ut_check(texture_size_check(255, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_BELOW_MINIMUM,
             "one pixel below the floor is refused");
    ut_check(texture_size_check(128, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_BELOW_MINIMUM,
             "a power of two below the floor is refused too, because lowering the clamp would crop "
             "artwork the game already ships");
    ut_check(texture_size_check(0, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_BELOW_MINIMUM,
             "zero is refused as a size rather than reaching the power of two test");

    /* An ini file can hold anything a player typed. A negative number read as unsigned would be
     * enormous, and an enormous size is exactly what must never be written into the clamp. */
    ut_check(texture_size_check(-1, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_BELOW_MINIMUM,
             "a negative size is below the floor, not four billion");
    ut_check(texture_size_check(INT32_MIN, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_BELOW_MINIMUM,
             "the most negative value an ini can carry is still just below the floor");
}

static void test_the_roof(void)
{
    ut_section("the roof");

    ut_check(texture_size_check(8192, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_APPLY,
             "the roof itself is accepted");
    ut_check(texture_size_check(8193, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_ABOVE_MAXIMUM,
             "one pixel past the roof is refused");
    ut_check(texture_size_check(16384, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_ABOVE_MAXIMUM,
             "a power of two past the roof is refused too");
    ut_check(texture_size_check(INT32_MAX, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_ABOVE_MAXIMUM,
             "the largest value an ini can carry is above the roof");

    /* The range is tested first, so a value that fails both tests is reported as out of range.
     * That is what the log line says, and it is the more useful of the two things to say. */
    ut_check(texture_size_check(300000, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_ABOVE_MAXIMUM,
             "a value out of range and not a power of two is reported as out of range");
}

static void test_powers_of_two(void)
{
    ut_section("powers of two");

    /* Not a preference. The accepted size reaches surface creation unchanged, and the page builder
     * turns each axis into an addressing mask by subtracting one from it. */
    ut_check(texture_size_check(768, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_NOT_POWER_OF_TWO,
             "768 is inside the range and still refused");
    ut_check(texture_size_check(1000, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_NOT_POWER_OF_TWO,
             "a round decimal number is refused");
    ut_check(texture_size_check(257, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_NOT_POWER_OF_TWO,
             "one pixel above the floor is refused");

    ut_check(texture_size_check(512, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_APPLY, "512 is accepted");
    ut_check(texture_size_check(2048, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_APPLY, "2048 is accepted");
    ut_check(texture_size_check(4096, 256, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_APPLY, "4096 is accepted");

    /* Counted rather than recomputed, so this does not repeat the implementation's own bit trick
     * back at it: between the floor and the roof there are six powers of two and no other size may
     * get through. */
    {
        int32_t value;
        int     accepted = 0;

        for (value = (int32_t)TEXTURE_SIZE_MIN_CEILING;
             value <= (int32_t)TEXTURE_SIZE_MAX_CEILING;
             ++value) {
            texture_size_verdict_t verdict =
                texture_size_check(value, TEXTURE_SIZE_MIN_CEILING,
                                   TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING);

            if (verdict == TEXTURE_SIZE_APPLY || verdict == TEXTURE_SIZE_UNCHANGED) {
                ++accepted;
            }
        }
        ut_check(accepted == 6,
                 "exactly six ceilings pass: 256, 512, 1024, 2048, 4096 and 8192");
    }

    {
        int32_t value;
        int     accepted = 0;

        for (value = (int32_t)TEXTURE_SIZE_MIN_WORLD_PAGE;
             value <= (int32_t)TEXTURE_SIZE_MAX_WORLD_PAGE;
             ++value) {
            texture_size_verdict_t verdict =
                texture_size_check(value, TEXTURE_SIZE_MIN_WORLD_PAGE,
                                   TEXTURE_SIZE_MIN_WORLD_PAGE, TEXTURE_SIZE_MAX_WORLD_PAGE);

            if (verdict == TEXTURE_SIZE_APPLY || verdict == TEXTURE_SIZE_UNCHANGED) {
                ++accepted;
            }
        }
        ut_check(accepted == 5,
                 "exactly five world page sizes pass: 256, 512, 1024, 2048 and 4096");
    }
}

static void test_the_two_keys_differ(void)
{
    ut_section("the two keys do not share a roof");

    /* A player who copies a working MaxTextureSize into MaxWorldPageSize is refused rather than
     * given a 64 MB allocation on every level load. */
    ut_check(texture_size_check(8192, 256, TEXTURE_SIZE_MIN_WORLD_PAGE, TEXTURE_SIZE_MAX_WORLD_PAGE)
             == TEXTURE_SIZE_ABOVE_MAXIMUM,
             "8192 is a usable device ceiling and too large for a world page");
    ut_check(texture_size_check(4096, 256, TEXTURE_SIZE_MIN_WORLD_PAGE, TEXTURE_SIZE_MAX_WORLD_PAGE)
             == TEXTURE_SIZE_APPLY,
             "4096 is the largest world page, which is a 16 MB block");
}

static void test_what_unchanged_is_measured_against(void)
{
    ut_section("what unchanged is measured against");

    /* Against the value the instruction holds, not against the floor of the range. The two are the
     * same number for both keys today, and a version that compared against the floor would pass
     * every other case here and then rewrite a site with the value it already has. */
    ut_check(texture_size_check(512, 512, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_UNCHANGED,
             "a site already holding 512 declines a request for 512");
    ut_check(texture_size_check(256, 512, TEXTURE_SIZE_MIN_CEILING, TEXTURE_SIZE_MAX_CEILING)
             == TEXTURE_SIZE_APPLY,
             "the floor is written when the site holds something else");

    /* A nonsense range must let nothing through rather than everything. */
    ut_check(texture_size_check(1024, 256, TEXTURE_SIZE_MAX_CEILING, TEXTURE_SIZE_MIN_CEILING)
             == TEXTURE_SIZE_BELOW_MINIMUM,
             "a range whose floor is above its roof accepts nothing");
}

static void test_world_page_bytes(void)
{
    ut_section("pixels in, bytes out");

    ut_check(texture_size_world_page_bytes(512) == 262144u,
             "twice the axis is four times the block");
    ut_check(texture_size_world_page_bytes(TEXTURE_SIZE_MAX_WORLD_PAGE) == 16777216u,
             "the largest accepted world page is 16 MB and still fits in 32 bits");
    ut_check(texture_size_world_page_bytes(TEXTURE_SIZE_MIN_WORLD_PAGE) == 0x10000u,
             "the floor of the range is the block size the engine already allocates");
}

static void test_effective_ceiling(void)
{
    ut_section("which ceiling crops an upload");

    ut_check(texture_size_effective_ceiling(0u, 256u) == 256u,
             "a ceiling that was refused or left alone leaves the engine's own 256 cropping");
    ut_check(texture_size_effective_ceiling(2048u, 256u) == 2048u,
             "an applied ceiling is the one that crops");

    /* The pair the warning stands on: a world page is loaded by one patch and uploaded through the
     * other, so growing the block alone buys a page that loads and is then cropped. */
    ut_check(1024u > texture_size_effective_ceiling(0u, 256u),
             "a 1024 world page with the clamp left alone would be cropped on upload");
    ut_check(!(1024u > texture_size_effective_ceiling(2048u, 256u)),
             "the same page is not cropped once the clamp has been raised to 2048");
}

int main(void)
{
    test_the_values_the_engine_holds();
    test_the_floor();
    test_the_roof();
    test_powers_of_two();
    test_the_two_keys_differ();
    test_what_unchanged_is_measured_against();
    test_world_page_bytes();
    test_effective_ceiling();

    return ut_summary("texture_size");
}
