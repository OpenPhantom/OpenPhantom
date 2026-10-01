/* diag_core_class.c: which logical processors are efficiency cores, built from the list Windows
 * gives of its processors and their efficiency classes.
 *
 * The frame line counts the frames that began on an efficiency core, because a game instance
 * whose window is behind another can be moved onto those cores by Windows 11 and then runs its
 * frames at a fraction of the speed. The map is the whole claim: a processor marked wrongly makes
 * that count say the opposite of what happened. Windows describes a hybrid part by giving its
 * efficiency cores a lower class than its performance cores; a machine whose processors all share
 * one class has no efficiency cores at all, whatever that class is numbered.
 */
#include "unittest.h"

#include "diag_core_class.h"

#include <stdint.h>

static void set_entry(diag_core_entry_t *entry, uint16_t group, uint8_t number,
                      uint8_t efficiency_class)
{
    entry->group            = group;
    entry->number           = number;
    entry->efficiency_class = efficiency_class;
}

/* A hybrid processor as Windows lists one: eight performance cores with two threads each on
 * logical processors 0 to 15, then twelve efficiency cores on 16 to 27. */
static unsigned hybrid_machine(diag_core_entry_t *entries)
{
    unsigned index;

    for (index = 0u; index < 28u; ++index) {
        set_entry(&entries[index], 0u, (uint8_t)index, (uint8_t)((index < 16u) ? 1u : 0u));
    }
    return 28u;
}

static void test_a_hybrid_part(void)
{
    diag_core_entry_t entries[28];
    diag_core_map_t   map;
    unsigned          count = hybrid_machine(entries);

    ut_section("a hybrid part");

    diag_core_map_build(&map, entries, count);
    ut_check(map.processor_count == 28u, "all 28 logical processors are counted");
    ut_check(map.efficient_count == 12u, "12 of them are efficiency cores");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 0u), "processor 0 is a performance core");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 15u),
             "and so is 15, the second thread of the last performance core");
    ut_check(diag_core_map_is_efficient(&map, 0u, 16u), "16 is the first efficiency core");
    ut_check(diag_core_map_is_efficient(&map, 0u, 27u), "27 the last");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 28u),
             "a processor the list never named is not called an efficiency core");
    ut_check(!diag_core_map_is_efficient(&map, 1u, 16u),
             "nor is the same number in a group the list never named");
}

static void test_one_class(void)
{
    diag_core_entry_t entries[16];
    diag_core_map_t   map;
    unsigned          index;

    ut_section("a machine whose processors share one class");

    for (index = 0u; index < 16u; ++index) {
        set_entry(&entries[index], 0u, (uint8_t)index, 0u);
    }
    diag_core_map_build(&map, entries, 16u);
    ut_check(map.efficient_count == 0u,
             "class 0 everywhere is one class, not sixteen efficiency cores");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 3u), "so no processor is one");

    for (index = 0u; index < 16u; ++index) {
        entries[index].efficiency_class = 1u;
    }
    diag_core_map_build(&map, entries, 16u);
    ut_check(map.efficient_count == 0u, "and neither is class 1 everywhere");
}

static void test_three_classes_and_groups(void)
{
    diag_core_entry_t entries[6];
    diag_core_map_t   map;

    ut_section("three classes and a second group");

    set_entry(&entries[0], 0u, 0u, 2u);
    set_entry(&entries[1], 0u, 1u, 1u);
    set_entry(&entries[2], 0u, 2u, 0u);
    set_entry(&entries[3], 1u, 5u, 0u);
    set_entry(&entries[4], 1u, 63u, 2u);
    set_entry(&entries[5], 1u, 62u, 1u);

    diag_core_map_build(&map, entries, 6u);
    ut_check(map.efficient_count == 4u,
             "every class below the highest is an efficiency class, so four of six");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 0u), "the top class is not");
    ut_check(diag_core_map_is_efficient(&map, 0u, 1u), "the middle class is");
    ut_check(diag_core_map_is_efficient(&map, 0u, 2u), "the lowest is");
    ut_check(diag_core_map_is_efficient(&map, 1u, 5u), "a processor in the second group is mapped");
    ut_check(diag_core_map_is_efficient(&map, 1u, 62u), "including near the top of its 64");
    ut_check(!diag_core_map_is_efficient(&map, 1u, 63u), "and the top class there is not");
}

static void test_out_of_range(void)
{
    diag_core_entry_t entries[3];
    diag_core_map_t   map;

    ut_section("what the map cannot hold");

    set_entry(&entries[0], 0u, 0u, 1u);
    set_entry(&entries[1], (uint16_t)DIAG_CORE_GROUPS, 0u, 0u);
    set_entry(&entries[2], 0u, 64u, 0u);

    diag_core_map_build(&map, entries, 3u);
    ut_check(map.processor_count == 3u, "every entry is counted");
    ut_check(map.efficient_count == 0u,
             "but one in a group beyond the map, or numbered past 63, is not marked");
    ut_check(!diag_core_map_is_efficient(&map, (uint16_t)DIAG_CORE_GROUPS, 0u),
             "and asking about such a group answers no");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 64u), "as does such a number");

    diag_core_map_build(&map, NULL, 5u);
    ut_check(map.processor_count == 0u && map.efficient_count == 0u,
             "no list at all builds an empty map rather than reading through a null pointer");
    ut_check(!diag_core_map_is_efficient(&map, 0u, 0u), "and an empty map answers no");
    ut_check(!diag_core_map_is_efficient(NULL, 0u, 0u), "as does no map");
}

int main(void)
{
    test_a_hybrid_part();
    test_one_class();
    test_three_classes_and_groups();
    test_out_of_range();

    return ut_summary("diag core class");
}
