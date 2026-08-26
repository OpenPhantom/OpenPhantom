/* character_scan: the arithmetic the character census rests on.
 *
 * The census itself reads a live process and cannot run here. What can run here is every decision
 * it makes once the numbers are in hand, and those are the decisions that would quietly produce a
 * wrong report rather than an obvious crash: a slot offset that walks off the end of a pool, a
 * radius test that drops a character standing exactly on the boundary, a NaN that reads as a
 * character falling through the floor, and a name field that is not text.
 */
#include "character_scan.h"

#include "unittest.h"

#include <math.h>
#include <string.h>

/* The pool the engine actually hands out character records from. The spawn path zeroes 0x81 dwords
 * of a record, so the payload is 0x204 bytes and a slot is that plus its four byte link word. */
#define REAL_ELEMENT_SIZE 0x204u
#define REAL_SLOT_STRIDE  0x208u

static void test_the_slot_walk_matches_the_allocator(void)
{
    ut_section("where a slot starts");

    ut_check(character_scan_slot_stride(REAL_ELEMENT_SIZE) == REAL_SLOT_STRIDE,
             "a slot is its payload plus the four byte link word the allocator returns past");
    ut_check(character_scan_slot_offset(REAL_ELEMENT_SIZE, 0u) ==
                 CHARACTER_POOL_SLOT_ARRAY_OFFSET,
             "slot zero begins where the six dword header ends, at +0x18");
    ut_check(character_scan_slot_offset(REAL_ELEMENT_SIZE, 1u) ==
                 CHARACTER_POOL_SLOT_ARRAY_OFFSET + REAL_SLOT_STRIDE,
             "each further slot is one whole stride on, link word included");
    ut_check(character_scan_slot_offset(REAL_ELEMENT_SIZE, 3u) ==
                 CHARACTER_POOL_SLOT_ARRAY_OFFSET + (3u * REAL_SLOT_STRIDE),
             "and the walk stays linear rather than accumulating a rounding step per slot");
}

static void test_a_pool_that_cannot_be_walked_says_so(void)
{
    ut_section("refusing to walk nonsense");

    ut_check(character_scan_slot_stride(0u) == 0u,
             "an element size of zero has no stride, so the walk stops instead of spinning on one "
             "slot forever");
    ut_check(character_scan_slot_offset(0u, 4u) == 0u,
             "and no offset is produced from it either, since zero is the caller's stop signal");
    ut_check(character_scan_slot_offset(CHARACTER_POOL_ELEMENT_SIZE_MAX + 1u, 0u) == 0u,
             "an element size past the ceiling is refused before it can be multiplied out");
    ut_check(character_scan_slot_offset(REAL_ELEMENT_SIZE, 0xFFFFFFFFu) == 0u,
             "an index whose offset would not fit in 32 bits answers zero rather than wrapping "
             "round to a small, plausible looking offset inside the pool");
}

static void test_which_slots_hold_a_character(void)
{
    ut_section("occupied and free");

    ut_check(!character_scan_slot_is_live(CHARACTER_POOL_FREE_LINK),
             "a link word of -1 is the free marker the allocator scans for, so that slot is empty");
    ut_check(character_scan_slot_is_live(0u),
             "zero is a live slot, not an empty one: it is the list terminator the last allocation "
             "wrote, and reading it as free would hide the oldest character in the level");
    ut_check(character_scan_slot_is_live(0x006C4DB0u),
             "any other value is the link to the next occupied slot, so that slot is occupied too");
    ut_check(character_scan_slot_is_live(0xFFFFFFFEu),
             "one away from the free marker is still occupied; only -1 exactly means free");
}

static void test_a_pool_header_has_to_describe_itself(void)
{
    ut_section("pool sanity");

    ut_check(character_scan_pool_is_sane(REAL_ELEMENT_SIZE, 256u),
             "the real element size with an ordinary capacity is accepted");
    ut_check(!character_scan_pool_is_sane(REAL_ELEMENT_SIZE, 0u),
             "a capacity of zero is refused: there is nothing to walk and it is not a pool");
    ut_check(!character_scan_pool_is_sane(CHARACTER_POOL_ELEMENT_SIZE_MIN - 1u, 256u),
             "an element too small to hold the fields the census reads is refused, not read");
    ut_check(!character_scan_pool_is_sane(REAL_ELEMENT_SIZE, CHARACTER_POOL_CAPACITY_MAX + 1u),
             "and a capacity past the ceiling is refused, because the pointer probably is not a "
             "pool at all");
    ut_check(character_scan_pool_is_sane(CHARACTER_POOL_ELEMENT_SIZE_MIN,
                                         CHARACTER_POOL_CAPACITY_MAX),
             "both bounds are inclusive at the edge, so a pool sitting exactly on one is walked");
}

static void test_the_radius_test(void)
{
    const float origin[3] = { 0.0f, 0.0f, 0.0f };
    const float three_four_five[3] = { 3.0f, 4.0f, 0.0f };
    const float above[3] = { 0.0f, 0.0f, 6.0f };

    ut_section("who is near enough to report");

    ut_near((double)character_scan_distance(three_four_five, origin), 5.0, 0.0001,
            "distance is the straight line through all three axes, not a horizontal one");
    ut_check(character_scan_within_radius(three_four_five, origin, 6.0f),
             "a character inside the radius is reported");
    ut_check(!character_scan_within_radius(three_four_five, origin, 4.0f),
             "and one outside it is not");
    ut_check(character_scan_within_radius(three_four_five, origin, 5.0f),
             "exactly on the boundary counts as inside, so a character standing at the limit does "
             "not flicker in and out of consecutive reports");
    ut_check(character_scan_within_radius(above, origin, 6.0f),
             "height counts toward the distance: a character directly overhead is near, which is "
             "the whole point when one is sinking through the floor under the player");
    ut_check(!character_scan_within_radius(origin, origin, 0.0f),
             "a radius of zero matches nothing, including a character at the player's own feet");
    ut_check(!character_scan_within_radius(origin, origin, -1.0f),
             "and a negative radius matches nothing rather than being squared into a positive one");
}

static void test_which_way_a_character_is_moving(void)
{
    const float high[3] = { 10.0f, 20.0f, 23.5f };
    const float low[3]  = { 10.0f, 20.0f, 22.8f };

    ut_section("gaining or losing height");

    ut_near((double)character_scan_vertical_delta(low, high), -0.7, 0.0001,
            "Z is up, so a character now lower than it was gives a negative step");
    ut_check(character_scan_classify(character_scan_vertical_delta(low, high)) ==
                 CHARACTER_MOTION_SINKING,
             "and that reads as sinking, which is the case this observer exists to catch");
    ut_check(character_scan_classify(character_scan_vertical_delta(high, low)) ==
                 CHARACTER_MOTION_RISING,
             "the same step upward reads as rising");
    ut_check(character_scan_classify(0.0f) == CHARACTER_MOTION_STILL,
             "a character the engine did not move at all gives exactly zero and reads as still");
    ut_check(character_scan_classify(CHARACTER_MOTION_EPSILON * 0.5f) == CHARACTER_MOTION_STILL,
             "a step under the epsilon is rounding, not motion");
    ut_check(character_scan_classify(-CHARACTER_MOTION_EPSILON) == CHARACTER_MOTION_SINKING,
             "the epsilon itself counts as motion, so the threshold has no silent gap at its edge");
}

static void test_a_number_that_is_not_a_number(void)
{
    const float sane[3] = { 1.0f, 2.0f, 3.0f };
    float       broken[3];

    broken[0] = (float)NAN;
    broken[1] = 2.0f;
    broken[2] = 3.0f;

    ut_section("a position that did not read properly");

    ut_check(character_scan_classify((float)NAN) == CHARACTER_MOTION_STILL,
             "a NaN step reads as still rather than as sinking: it says the two positions cannot "
             "be compared, which is not the same as a character going down");
    ut_check(!character_scan_within_radius(broken, sane, 100.0f),
             "a NaN position is outside every radius rather than inside every one, so a bad read "
             "drops out of the report instead of filling it");
    ut_near((double)character_scan_distance(broken, sane), 0.0, 0.0001,
            "and its distance answers zero rather than carrying the NaN into the log");
}

static void test_the_name_field(void)
{
    char name[16];

    ut_section("turning the fixed width name field into a string");

    character_scan_copy_name("obinpc03\0\0\0\0", 12u, name, sizeof(name));
    ut_check(strcmp(name, "obinpc03") == 0, "an ordinary terminated name comes through unchanged");

    character_scan_copy_name("abcdefghijkl", 12u, name, sizeof(name));
    ut_check(strcmp(name, "abcdefghijkl") == 0,
             "a name filling all twelve bytes with no terminator is still terminated on the way "
             "out, which is the case that would otherwise run the log formatter off the end");

    character_scan_copy_name("ab\x01\x7F""cd\0\0\0\0\0\0", 12u, name, sizeof(name));
    ut_check(strcmp(name, "ab..cd") == 0,
             "bytes outside printable ASCII become full stops, so a wrong pointer is visible in "
             "the log as punctuation instead of writing control characters into it");

    character_scan_copy_name("obinpc03", 8u, name, 4u);
    ut_check(strcmp(name, "obi") == 0,
             "a destination smaller than the field truncates and still terminates");

    name[0] = 'x';
    character_scan_copy_name(NULL, 12u, name, sizeof(name));
    ut_check(name[0] == '\0', "a null source gives an empty string rather than the previous one");

    name[0] = 'x';
    character_scan_copy_name("obinpc03", 0u, name, sizeof(name));
    ut_check(name[0] == '\0', "and so does a source of no length");

    /* Nothing to assert beyond not writing: a zero length destination has nowhere to put even a
       terminator, so the only correct behaviour is to leave it alone. */
    character_scan_copy_name("obinpc03", 8u, name, 0u);
    ut_check(1, "a destination of no length is left untouched rather than terminated out of range");
}

static void test_the_motion_names(void)
{
    ut_section("what the report calls each case");

    ut_check(strcmp(character_scan_motion_text(CHARACTER_MOTION_SINKING), "sinking") == 0,
             "sinking is spelled out in the log rather than left as a number to look up");
    ut_check(strcmp(character_scan_motion_text(CHARACTER_MOTION_RISING), "rising") == 0,
             "rising likewise");
    ut_check(strcmp(character_scan_motion_text(CHARACTER_MOTION_STILL), "still") == 0,
             "still likewise");
    ut_check(character_scan_motion_text((character_motion_t)99) != NULL,
             "and a value that is not one of the three still returns a printable string, because "
             "the alternative is a null pointer reaching a format string");
}

static void test_remembering_a_height_between_reports(void)
{
    character_track_t table[4];
    float             change = 999.0f;

    ut_section("height remembered across reports");

    character_scan_track_reset(table, 4u);
    ut_check(!character_scan_track(table, 4u, 0x1000u, 26.1f, &change),
             "the first sighting of a record has nothing to compare against and says so, rather "
             "than reporting a change of zero it cannot actually vouch for");

    change = 999.0f;
    ut_check(character_scan_track(table, 4u, 0x1000u, 25.9f, &change),
             "the second sighting can compare");
    ut_near((double)change, -0.2, 0.0001,
            "and reports the drop the engine's own one step field could not show, which is the "
            "whole reason this table exists");

    change = 999.0f;
    (void)character_scan_track(table, 4u, 0x1000u, 25.9f, &change);
    ut_near((double)change, 0.0, 0.0001, "a character that did not move reports no change");

    change = 999.0f;
    (void)character_scan_track(table, 4u, 0x1000u, 26.4f, &change);
    ut_near((double)change, 0.5, 0.0001, "and one that rose reports a positive change");

    ut_check(!character_scan_track(table, 4u, 0x2000u, 10.0f, &change),
             "a different record is tracked separately rather than inheriting the first one");
    change = 999.0f;
    (void)character_scan_track(table, 4u, 0x1000u, 26.4f, &change);
    ut_near((double)change, 0.0, 0.0001,
            "and the original record still holds its own height afterwards");

    character_scan_track_reset(table, 4u);
    ut_check(!character_scan_track(table, 4u, 0x1000u, 26.4f, &change),
             "a reset forgets everything, which is what a new level needs: a slot address from the "
             "previous one would be compared against a different character");
}

static void test_the_tracking_table_at_its_limits(void)
{
    character_track_t table[2];
    float             change = 0.0f;

    ut_section("a table smaller than the level");

    character_scan_track_reset(table, 2u);
    ut_check(!character_scan_track(table, 2u, 0x10u, 1.0f, &change), "first record takes a slot");
    ut_check(!character_scan_track(table, 2u, 0x20u, 2.0f, &change),
             "second record takes the other");
    ut_check(!character_scan_track(table, 2u, 0x30u, 3.0f, &change),
             "a third on a full table evicts rather than refusing, so the report keeps going with "
             "a gap in it instead of falling silent");

    ut_check(!character_scan_track(NULL, 4u, 0x10u, 1.0f, &change),
             "no table means nothing can be remembered, reported as an unknown record");
    ut_check(!character_scan_track(table, 0u, 0x10u, 1.0f, &change),
             "and neither can an empty one");
    ut_check(!character_scan_track(table, 2u, 0u, 1.0f, &change),
             "a record address of zero is the free marker, so it is never tracked as a character");

    character_scan_track_reset(NULL, 4u);
    ut_check(1, "resetting a null table does nothing rather than writing through it");
}

int main(void)
{
    test_the_slot_walk_matches_the_allocator();
    test_a_pool_that_cannot_be_walked_says_so();
    test_which_slots_hold_a_character();
    test_a_pool_header_has_to_describe_itself();
    test_the_radius_test();
    test_which_way_a_character_is_moving();
    test_a_number_that_is_not_a_number();
    test_the_name_field();
    test_the_motion_names();
    test_remembering_a_height_between_reports();
    test_the_tracking_table_at_its_limits();

    return ut_summary("character_scan");
}
