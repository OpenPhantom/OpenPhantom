/* signature.c: the byte-pattern matcher every patch in this project stands on.
 *
 * The design rule here is "signatures, never addresses": three builds of this engine ship in one
 * installation, one of them a recompile in which 60 % of the code section differs, so every site
 * is found by what its bytes look like rather than by where it used to be. That makes this loop
 * the single most load-bearing piece of shared code in the tree, and until now it had no test,
 * because the only way to reach it was through the host executable's own code section.
 *
 * signature_count_in_buffer() is the same search over a caller-supplied buffer, which is what the
 * host version is built on. Given a buffer the answers are fixed and the interesting properties
 * become ordinary arithmetic: wildcards, overlapping hits, and counting them all rather than
 * stopping once the answer is known to be "more than one".
 */
#include "unittest.h"

#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* A haystack with deliberate structure: the needle AB CD appears three times, twice adjacent so
 * that an overlap-blind implementation would miscount, and the byte before the last occurrence is
 * a near miss that shares the needle's first byte. */
static const uint8_t HAYSTACK[] = {
    0x00, 0xAB, 0xCD, 0x11,          /* hit at 1                                */
    0xAB, 0xCD, 0xAB, 0xCD,          /* hits at 4 and 6, back to back           */
    0xAB, 0x99, 0x22, 0x33           /* near miss: right first byte, wrong tail */
};

static const uint8_t NEEDLE[]      = { 0xAB, 0xCD };
static const uint8_t NEEDLE_MISS[] = { 0xAB, 0xCE };

int main(void)
{
    size_t offsets[8];
    size_t hits;

    ut_section("finding a pattern in a buffer");

    hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NEEDLE, NULL, sizeof(NEEDLE),
                                     offsets, 8);
    ut_check(hits == 3, "all three occurrences are found");
    ut_check(offsets[0] == 1 && offsets[1] == 4 && offsets[2] == 6,
             "and reported at the offsets they actually sit at, in order");

    hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NEEDLE_MISS, NULL,
                                     sizeof(NEEDLE_MISS), offsets, 8);
    ut_check(hits == 0, "a pattern that is not there is not found");

    ut_section("counting past the reported window");

    /* The count is what every decision in this project is made on: a site that matches three times
     * is a different situation from one that matches three hundred, and a search that stopped as
     * soon as it knew the answer was "more than one" could not tell them apart. */
    hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NEEDLE, NULL, sizeof(NEEDLE),
                                     offsets, 1);
    ut_check(hits == 3, "the count keeps going after the caller's offset array is full");
    ut_check(offsets[0] == 1, "and the offsets it did report are the first ones");

    hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NEEDLE, NULL, sizeof(NEEDLE),
                                     NULL, 0);
    ut_check(hits == 3, "a caller that wants only the count may pass no array at all");

    ut_section("wildcards");

    {
        /* AB ?? matches every AB regardless of what follows, which is four places here. */
        static const uint8_t bytes[] = { 0xAB, 0x00 };
        static const uint8_t mask[]  = { 0xFF, 0x00 };

        hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), bytes, mask, sizeof(bytes),
                                         offsets, 8);
        ut_check(hits == 4, "a wildcard byte matches anything, including the near miss");
    }

    {
        /* A mask of all-must-match has to behave exactly like no mask at all. The two take
         * different paths through the matcher, and only one of them has the first-byte shortcut. */
        static const uint8_t mask[] = { 0xFF, 0xFF };

        hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NEEDLE, mask, sizeof(NEEDLE),
                                         offsets, 8);
        ut_check(hits == 3, "a fully required mask gives the same answer as no mask");
    }

    {
        /* A wildcard in the FIRST byte is the case the shortcut could get wrong: the masked path
         * must not inherit an optimisation that assumes byte zero is significant. */
        static const uint8_t bytes[] = { 0x00, 0xCD };
        static const uint8_t mask[]  = { 0x00, 0xFF };

        hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), bytes, mask, sizeof(bytes),
                                         offsets, 8);
        ut_check(hits == 3, "a wildcard in the first byte still finds every match");
    }

    ut_section("edges and refusals");

    hits = signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), HAYSTACK, NULL, sizeof(HAYSTACK),
                                     offsets, 8);
    ut_check(hits == 1 && offsets[0] == 0,
             "a pattern exactly as long as the buffer matches once, at zero");

    {
        static const uint8_t too_long[] = { 0, 0, 0, 0 };

        hits = signature_count_in_buffer(too_long, 2, too_long, NULL, sizeof(too_long),
                                         offsets, 8);
        ut_check(hits == 0, "a pattern longer than the buffer cannot match");
    }

    ut_check(signature_count_in_buffer(NULL, 16, NEEDLE, NULL, sizeof(NEEDLE), offsets, 8) == 0,
             "a null buffer is refused");
    ut_check(signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NULL, NULL, 2, offsets, 8) == 0,
             "a null pattern is refused");
    ut_check(signature_count_in_buffer(HAYSTACK, sizeof(HAYSTACK), NEEDLE, NULL, 0, offsets, 8) == 0,
             "an empty pattern is refused rather than matching everywhere");

    return ut_summary("signature");
}
