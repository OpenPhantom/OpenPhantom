/* The clip each engine starter plays, and whether an actor carries it, with no game.
 *
 * A wrong answer here does not look wrong in the field: a clip picked one branch off is still a
 * clip every hero carries, and a bound one too far passes every actor but the one whose count is
 * exactly the clip, which then ends the process. So the setter's choice is pinned against every
 * branch of the bytes, the bound at the exact count, and the shipped actors and swing rows by their
 * numbers.
 */
#include "unittest.h"

#include "mp_starter_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NONE 0xFFFFFFFFu

/* The setter's raw mode as the bytes run it, one statement per test, 0x0044B4A1 to 0x0044B553. */
static uint32_t setter_bytes(uint32_t slot_84, uint32_t wanted)
{
    uint32_t slot_88;

    if (slot_84 == wanted) {             /* 0x0044B4A1 */
        if (slot_84 == 0u) {             /* 0x0044B4AC, return at 0x0044B4BE */
            return NONE;
        }
        wanted = 0u;                     /* 0x0044B4B5 */
    }
    slot_88 = wanted;                    /* 0x0044B4CD */
    if (slot_88 == 1u) {                 /* 0x0044B4D9 */
        return 0x20u;                    /* 0x0044B4EF */
    }
    if (slot_84 == 0u) {                 /* 0x0044B4FF */
        return 0x22u;                    /* 0x0044B515 */
    }
    if (slot_88 == 0u) {                 /* 0x0044B525 */
        return 0x23u;                    /* 0x0044B53B */
    }
    return 0x21u;                        /* 0x0044B553 */
}

static uint32_t clip_of(uint32_t equipped, uint32_t wanted)
{
    uint32_t clip = 0u;

    return mp_starter_weapon_clip(equipped, wanted, &clip) ? clip : NONE;
}

static void check_the_setter(void)
{
    static const uint32_t VECTORS[9][3] = {
        { 0u, 1u, 0x20u }, { 0u, 2u, 0x22u }, { 2u, 0u, 0x23u }, { 2u, 3u, 0x21u },
        { 1u, 2u, 0x21u }, { 2u, 1u, 0x20u }, { 1u, 0u, 0x23u }, { 2u, 2u, 0x23u },
        { 1u, 1u, 0x23u },
    };
    uint32_t equipped;
    uint32_t wanted;
    size_t   i;
    unsigned apart = 0u;

    ut_section("the weapon setter's draw clip");
    for (i = 0; i < 9u; ++i) {
        ut_checkf(clip_of(VECTORS[i][0], VECTORS[i][1]) == VECTORS[i][2],
                  "slot %u asked for %u plays 0x%02X", (unsigned)VECTORS[i][0],
                  (unsigned)VECTORS[i][1], (unsigned)VECTORS[i][2]);
    }
    ut_check(clip_of(0u, 0u) == NONE,
             "empty hands asked for empty hands play nothing: the setter returns before its clip");
    for (equipped = 0u; equipped < 14u; ++equipped) {
        for (wanted = 0u; wanted < 14u; ++wanted) {
            apart += clip_of(equipped, wanted) != setter_bytes(equipped, wanted) ? 1u : 0u;
        }
    }
    ut_checkf(apart == 0u, "every pair of slots up to 13 plays what the bytes play (%u apart)",
              apart);
    ut_check(mp_starter_weapon_clip(3u, 4u, NULL), "a clip is chosen with nowhere to put it");
    ut_check(MP_STARTER_CLIP_DRAW_SABRE == 0x20u && MP_STARTER_CLIP_SWAP == 0x21u &&
                 MP_STARTER_CLIP_FROM_NONE == 0x22u && MP_STARTER_CLIP_HOLSTER == 0x23u,
             "the four draw clips are the ordinals 0x20 to 0x23 the setter pushes");
    ut_check(MP_STARTER_CLIP_PUSH == 0x71u && MP_STARTER_CLIP_MIDAIR == 0x55u,
             "the push starter pushes 0x71, the midair row names 0x55");

    ut_section("the weapon table's rows");
    ut_check(mp_starter_weapon_slot_ok(0u) && mp_starter_weapon_slot_ok(1u),
             "empty hands and the sabre have rows");
    ut_check(mp_starter_weapon_slot_ok(11u), "so has slot 11, the last gun");
    ut_check(!mp_starter_weapon_slot_ok(12u),
             "slot 12 has none: its row would be the swing table's first, whose node name the "
             "lookup refuses with an assert");
    ut_check(!mp_starter_weapon_slot_ok(13u) && !mp_starter_weapon_slot_ok(255u),
             "and nothing past it has one");
}

static void check_the_bound(void)
{
    ut_section("the bound is strict where the engine's is not");
    ut_check(!mp_starter_clip_fits(34u, 16u), "clip 34 on an actor with 16 is refused");
    ut_check(!mp_starter_clip_fits(35u, 35u),
             "clip 35 on an actor with 35 is refused: the engine's jle lets it through and reads "
             "the entry behind the table");
    ut_check(mp_starter_clip_fits(35u, 36u), "clip 35 on an actor with 36 plays");
    ut_check(!mp_starter_clip_fits(0u, 0u), "an actor with no clip has not even clip 0");
    ut_check(mp_starter_clip_fits(0u, 1u), "an actor with one has clip 0");
    ut_check(!mp_starter_clip_fits(0x20u, 0x80000000u),
             "a count the engine's signed compare reads as negative has no clip");
    ut_check(mp_starter_clip_fits(0x20u, 0x7FFFFFFFu), "the largest positive count has them all");
}

/* The clip counts of the shipped actors that carry more than 25, which are all that carry any of
 * these clips; the other 296 of 303 carry fewer than 26. */
static void check_the_census(void)
{
    static const struct {
        const char *name;
        uint32_t    count;
        bool        draws;
        bool        push;
        bool        midair;
    } ACTORS[] = {
        { "obiwan", 114u, true, true, true },    { "quigon", 114u, true, true, true },
        { "panaka", 83u, true, false, false },   { "queen", 71u, true, false, false },
        { "quiweap", 55u, true, false, false },  { "quigung", 39u, true, false, false },
        { "sithjedi", 37u, true, false, false }, { "any other", 25u, false, false, false },
    };
    size_t   i;
    uint32_t clip;

    ut_section("the shipped actors");
    for (i = 0; i < sizeof ACTORS / sizeof ACTORS[0]; ++i) {
        bool draws = true;

        for (clip = MP_STARTER_CLIP_DRAW_SABRE; clip <= MP_STARTER_CLIP_HOLSTER; ++clip) {
            draws = draws && mp_starter_clip_fits(clip, ACTORS[i].count);
        }
        ut_checkf(draws == ACTORS[i].draws, "%s with %u clip(s) %s the four draw clips",
                  ACTORS[i].name, (unsigned)ACTORS[i].count, ACTORS[i].draws ? "has" : "lacks");
        ut_checkf(mp_starter_clip_fits(MP_STARTER_CLIP_PUSH, ACTORS[i].count) == ACTORS[i].push,
                  "%s %s the push clip", ACTORS[i].name, ACTORS[i].push ? "has" : "lacks");
        ut_checkf(mp_starter_clip_fits(MP_STARTER_CLIP_MIDAIR, ACTORS[i].count) ==
                      ACTORS[i].midair,
                  "%s %s the midair clip", ACTORS[i].name, ACTORS[i].midair ? "has" : "lacks");
    }
}

/* The clip column of the shipped swing table at 0x004B4E00, 28 rows of 0x20 bytes. */
static void check_the_swing_rows(void)
{
    static const uint32_t SHIPPED[28] = {
        0x47u, 0x48u, 0x49u, 0x4Au, 0x4Bu, 0x4Cu, 0x47u, 0x48u, 0x49u, 0x4Au,
        0x4Bu, 0x4Cu, 0x4Du, 0x4Eu, 0x4Fu, 0x50u, 0x51u, 0x54u, 0x5Au, 0x5Bu,
        0x52u, 0x53u, 0x57u, 0x5Cu, 0x55u, 0x6Eu, 0x6Fu, 0x70u,
    };
    uint32_t clip = 0u;
    size_t   row;
    size_t   overlays = 0u;
    size_t   which = 0u;

    ut_section("the swing rows");
    ut_check(mp_starter_swing_overlay_clip(0x55u, &clip) && clip == 0x55u,
             "a row naming 0x55 plays it on the overlay channel");
    ut_check(!mp_starter_swing_overlay_clip(0x4Fu, &clip) &&
                 !mp_starter_swing_overlay_clip(0x47u, NULL) &&
                 !mp_starter_swing_overlay_clip(0x6Eu, NULL) &&
                 !mp_starter_swing_overlay_clip(0u, NULL),
             "rows naming 0x4F, 0x47, 0x6E or 0 play on the base channel");
    for (row = 0; row < 28u; ++row) {
        if (mp_starter_swing_overlay_clip(SHIPPED[row], NULL)) {
            ++overlays;
            which = row;
        }
    }
    ut_checkf(overlays == 1u && which == MP_STARTER_MIDAIR_ROW,
              "in the shipped table one row plays on the overlay channel, row %u", (unsigned)which);
}

int main(void)
{
    check_the_setter();
    check_the_bound();
    check_the_census();
    check_the_swing_rows();

    return ut_summary("engine starter rule");
}
