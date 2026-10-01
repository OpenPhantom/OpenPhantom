/* mp_enemy_limb_rule.c: a limb an enemy lost, as the number its world event carries, and what a
 * client does with it.
 *
 * The number used to be the ordinal alone, offset by one; it is kept as the reference, so a limb
 * whose node was showing before the cut still travels as the very same number, and the flag for a
 * node already hidden rides in the byte above it. The decision holds the two engine facts the
 * flight stands on: the root is never cut, and a task table that runs full answers -1.
 */
#include "unittest.h"

#include "mp_enemy_limb_rule.h"
#include "mp_enemy_pack.h"

#include <stdbool.h>
#include <stdint.h>

static void check_the_number(void)
{
    uint32_t ordinal;
    uint32_t got    = 0;
    bool     before = true;

    ut_section("the ordinal offset by one, and the node's state before the cut above it");

    for (ordinal = 0; ordinal <= MP_ENEMY_LIMB_MAX_ORDINAL; ++ordinal) {
        uint32_t packed = mp_enemy_limb_pack(ordinal, false);

        if (packed != mp_enemy_pack_one(ordinal, MP_ENEMY_LIMB_MAX_ORDINAL) ||
            !mp_enemy_limb_unpack(packed, &got, &before) || got != ordinal || before) {
            break;
        }
    }
    ut_checkf(ordinal == MP_ENEMY_LIMB_MAX_ORDINAL + 1u,
              "a node that was showing travels as the old number, every ordinal round (stopped at "
              "%u)", (unsigned)ordinal);
    ut_check(mp_enemy_limb_unpack(mp_enemy_limb_pack(20u, true), &got, &before) && got == 20u &&
                 before,
             "a node hidden before the cut says so beside its ordinal");
    ut_check(mp_enemy_limb_pack(20u, true) == (21u | 0x100u), "in the byte above it");
    ut_check(mp_enemy_limb_pack(255u, false) == 0u && mp_enemy_limb_pack(255u, true) == 0u,
             "an ordinal past the byte cannot travel");
    ut_check(!mp_enemy_limb_unpack(0u, &got, &before) &&
                 !mp_enemy_limb_unpack(0x100u, &got, &before),
             "a low byte of 0 is nothing lost, whatever stands above it");
    ut_check(!mp_enemy_limb_unpack(21u, NULL, &before), "and nothing is read into nothing");
}

static void check_the_verdict(void)
{
    ut_section("a piece is thrown only where the engine would cut, and while the tasks allow");

    ut_check(mp_enemy_limb_verdict(20u, 48u, 30u) == MP_ENEMY_LIMB_THROW,
             "a limb of a 48 node model with 30 task slots free is thrown");
    ut_check(mp_enemy_limb_verdict(20u, 48u, MP_ENEMY_LIMB_TASKS_KEPT_FREE + 1u) ==
                 MP_ENEMY_LIMB_THROW,
             "and with one slot over the reserve");
    ut_check(mp_enemy_limb_verdict(20u, 48u, MP_ENEMY_LIMB_TASKS_KEPT_FREE) ==
                 MP_ENEMY_LIMB_HIDE_ONLY,
             "at the reserve the node is hidden and nothing flies");
    ut_check(mp_enemy_limb_verdict(20u, 48u, 0u) == MP_ENEMY_LIMB_HIDE_ONLY,
             "and so with a full table, where the engine would freeze the piece");
    ut_check(mp_enemy_limb_verdict(0u, 48u, 30u) == MP_ENEMY_LIMB_OUT_OF_RANGE,
             "the root is never cut: the engine's detach does nothing for node 0");
    ut_check(mp_enemy_limb_verdict(48u, 48u, 30u) == MP_ENEMY_LIMB_OUT_OF_RANGE &&
                 mp_enemy_limb_verdict(47u, 48u, 30u) == MP_ENEMY_LIMB_THROW,
             "and an ordinal at the node count is past the model, which the engine never checks");
}

int main(void)
{
    check_the_number();
    check_the_verdict();
    return ut_summary("mp_enemy_limb_rule");
}
