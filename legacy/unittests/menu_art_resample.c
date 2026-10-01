/* menu_art_resample.c: the map, the rounding and the replication, checked without a game.
 *
 * Everything this module decides is arithmetic, so all of it is here. What is not here is
 * the hook that swaps the buffer into the engine's surface, which cannot be tested without one.
 */
#include "unittest.h"

#include "menu_art_resample.h"

#include <stdint.h>
#include <string.h>

/* The ratios that matter, from the shipped converter and from real displays: 6.0 and 4.5 are what a
 * 4K set is made at, 2.0 and 1.667 are what a 1280x800 screen wants, 2.25 is 1080p vertically. */
static void test_the_map_covers_every_source_pixel(void)
{
    static const int32_t DEST[]   = { 1280, 1920, 2560, 3840, 4095, 800, 1080, 1440 };
    static const int32_t SOURCE[] = { 640, 480 };
    size_t d;
    size_t s;

    for (s = 0; s < sizeof SOURCE / sizeof SOURCE[0]; ++s) {
        for (d = 0; d < sizeof DEST / sizeof DEST[0]; ++d) {
            int32_t source_extent = SOURCE[s];
            int32_t dest_extent   = DEST[d];
            char    seen[640];
            int32_t i;
            int32_t outside = 0;
            int32_t missed  = 0;

            if (dest_extent < source_extent) {
                continue;
            }
            /* Every pixel is checked and the tally is reported once per pair of extents: two
             * lines a pair, not one a pixel. */
            memset(seen, 0, sizeof seen);
            for (i = 0; i < dest_extent; ++i) {
                int32_t at = menu_art_resample_source_index(i, dest_extent, source_extent);

                if (at < 0 || at >= source_extent) {
                    ++outside;
                    continue;
                }
                seen[at] = 1;
            }
            for (i = 0; i < source_extent; ++i) {
                if (!seen[i]) {
                    ++missed;
                }
            }
            ut_checkf(outside == 0,
                      "%d into %d: every destination pixel maps inside the source, so the "
                      "replication is a copy rather than a read off the end of the picture "
                      "(%d outside)", (int)source_extent, (int)dest_extent, (int)outside);
            ut_checkf(missed == 0,
                      "%d into %d: and every source pixel is reached at least once when the "
                      "picture grows, so nothing in the artwork is dropped on the way up (%d "
                      "missed). menu_preview.c's own map fails this: its step is truncated, so "
                      "the last ten source columns of a six times upscale are never sampled and "
                      "the right edge is cropped", (int)source_extent, (int)dest_extent,
                      (int)missed);
        }
    }
}

static void test_the_map_is_monotonic(void)
{
    int32_t previous = -1;
    int32_t backwards = 0;
    int32_t i;

    for (i = 0; i < 2160; ++i) {
        int32_t at = menu_art_resample_source_index(i, 2160, 480);

        if (at < previous) {
            ++backwards;
        }
        previous = at;
    }
    ut_checkf(backwards == 0,
              "the map never goes backwards across 480 rows into 2160, so a replicated picture "
              "cannot come out with its rows out of order (%d steps back)", (int)backwards);
}

static void test_a_whole_ratio_is_exact_replication(void)
{
    ut_check(menu_art_resample_source_index(0, 1280, 640) == 0 &&
             menu_art_resample_source_index(1, 1280, 640) == 0 &&
             menu_art_resample_source_index(2, 1280, 640) == 1 &&
             menu_art_resample_source_index(3, 1280, 640) == 1,
          "at exactly two times, destination pixels pair off onto source pixels in order, which is "
          "the property a whole ratio should have and is the easiest thing to get wrong by half a "
          "pixel");
    ut_check(menu_art_resample_source_index(1279, 1280, 640) == 639,
          "and the last destination pixel is the last source pixel rather than one past it");
}

static void test_the_fractional_run_lengths(void)
{
    int32_t counts[8];
    int32_t run = 0;
    int32_t previous = menu_art_resample_source_index(0, 800, 480);
    int32_t i;

    memset(counts, 0, sizeof counts);
    for (i = 0; i <= 800; ++i) {
        int32_t at = (i < 800) ? menu_art_resample_source_index(i, 800, 480) : -1;

        if (at == previous) {
            run++;
            continue;
        }
        if (run > 0 && run < 8) {
            counts[run]++;
        }
        previous = at;
        run = 1;
    }
    ut_check(counts[1] == 160 && counts[2] == 320 && counts[3] == 0,
          "480 rows into 800 gives runs of one and two only, 160 and 320 of them. A source row is "
          "therefore one or two destination rows thick and never three, so a one pixel line varies "
          "in weight but never disappears and never doubles twice");
}

static void test_the_scaled_size_matches_the_converter(void)
{
    ut_check(menu_art_resample_scaled(640, 6.0f) == 3840 &&
             menu_art_resample_scaled(480, 4.5f) == 2160,
          "the converter's own ratios give the sizes the converted artwork actually has, so a "
          "resampled picture and a converted one are the same SIZE. They are not always the same "
          "picture: where the product is not already whole the two maps put a boundary one pixel "
          "apart, which is measured in the header rather than claimed away");
    ut_check(menu_art_resample_scaled(479, 4.5f) == 2156,
          "including the odd one: 479 by 4.5 is 2155.5 and Saveback.BMP on disk is 2156 tall, so "
          "the rounding has to go away from zero at the half rather than to even");
    ut_check(menu_art_resample_scaled(640, 0.0f) == 640 &&
             menu_art_resample_scaled(640, -1.0f) == 640,
          "a ratio that is not positive leaves the value alone rather than returning zero or a "
          "negative size");
}

static void test_the_replication(void)
{
    static const uint16_t SOURCE[4] = { 0x0000, 0x1234, 0x5678, 0xFFFF };
    uint16_t              dest[16];
    size_t                i;
    size_t                unwritten = 0;

    memset(dest, 0xAA, sizeof dest);
    ut_check(menu_art_resample_16(SOURCE, 2, 2, 2, dest, 4, 4, 4),
          "a two by two picture doubles into four by four");

    ut_check(dest[0] == 0x0000 && dest[1] == 0x0000 && dest[2] == 0x1234 && dest[3] == 0x1234,
          "the first row is each source pixel twice, in order");
    ut_check(memcmp(&dest[0], &dest[4], 4 * sizeof(uint16_t)) == 0,
          "and the second row is the first one again, because both come from source row 0. That is "
          "the row reuse that makes this cheap, and it is checked because a copy of the wrong row "
          "would still look plausible");
    ut_check(dest[8] == 0x5678 && dest[10] == 0xFFFF,
          "the third row has moved on to source row 1");

    for (i = 0; i < 16; ++i) {
        if (dest[i] == 0xAAAA) {
            ++unwritten;
        }
    }
    ut_checkf(unwritten == 0,
              "every destination pixel was written, so nothing is left holding whatever the "
              "allocator handed over (%u untouched)", (unsigned)unwritten);
}

static void test_transparency_survives(void)
{
    static const uint16_t SOURCE[4] = { 0x0000, 0x0001, 0x0800, 0x0000 };
    uint16_t              dest[36];
    size_t                zeros = 0;
    size_t                invented = 0;
    size_t                i;

    ut_check(menu_art_resample_16(SOURCE, 2, 2, 2, dest, 6, 6, 6),
          "a two by two picture at three times");
    for (i = 0; i < 36; ++i) {
        bool matches_a_source = dest[i] == 0x0000 || dest[i] == 0x0001 ||
                                dest[i] == 0x0800;

        if (!matches_a_source) {
            ++invented;
        }
        if (dest[i] == 0x0000) {
            zeros++;
        }
    }
    ut_checkf(invented == 0,
              "every destination pixel is a value that was already in the source (%u were not). "
              "This is the whole transparency rule: an exactly zero pixel is a SKIP to this "
              "engine, so a filter that invented a value between 0x0001 and 0x0000 would turn a "
              "dark opaque pixel transparent, and one that invented a value between 0x0000 and "
              "0x0800 would put an opaque halo around a transparent edge", (unsigned)invented);
    ut_check(zeros == 18,
          "and exactly the two transparent source pixels' worth of area is still transparent, "
          "nine destination pixels each");
}

static void test_the_refusals(void)
{
    static const uint16_t SOURCE[4] = { 1, 2, 3, 4 };
    uint16_t              dest[4]   = { 0xAAAA, 0xAAAA, 0xAAAA, 0xAAAA };

    ut_check(!menu_art_resample_16(SOURCE, 4, 4, 4, dest, 2, 2, 2),
          "shrinking is refused rather than done badly: below a ratio of one, source pixels are "
          "dropped and a one pixel border comes out dashed instead of thinner");
    ut_check(!menu_art_resample_16(SOURCE, 2, 2, 2, dest, 2, 1, 2),
          "and one axis shrinking is enough to refuse, because a picture squashed on one axis is "
          "not a picture anybody asked for");
    ut_check(!menu_art_resample_16(NULL, 2, 2, 2, dest, 4, 4, 4) &&
             !menu_art_resample_16(SOURCE, 2, 2, 2, NULL, 4, 4, 4),
          "a missing buffer on either side is refused rather than written through");
    ut_check(!menu_art_resample_16(SOURCE, 0, 2, 2, dest, 4, 4, 4) &&
             !menu_art_resample_16(SOURCE, 2, 2, 2, dest, 4, 0, 4),
          "and a zero extent, which is how an unreadable surface header reads");
    ut_check(!menu_art_resample_16(SOURCE, 2, 2, 1, dest, 4, 4, 4),
          "a pitch narrower than the picture is refused, because reading rows at that stride walks "
          "diagonally through the buffer and off the end of it");
    ut_check(dest[0] == 0xAAAA && dest[3] == 0xAAAA,
          "and every one of those left the destination untouched");
}

int main(void)
{
    ut_section("the map from a destination pixel to a source pixel");
    test_the_map_covers_every_source_pixel();
    test_the_map_is_monotonic();
    test_a_whole_ratio_is_exact_replication();
    test_the_fractional_run_lengths();

    ut_section("the size a ratio gives");
    test_the_scaled_size_matches_the_converter();

    ut_section("the replication itself");
    test_the_replication();
    test_transparency_survives();

    ut_section("the refusals");
    test_the_refusals();

    return ut_summary("menu_art_resample");
}
