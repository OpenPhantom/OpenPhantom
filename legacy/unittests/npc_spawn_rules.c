/* npc_spawn_rules.c: the decisions of the copies' savegame, checked without the game.
 *
 * What would be silent if it were wrong: every save refused for good after one ride on a copied
 * gun, since the gun's index stays in the player after it dismounts; a foreign block read into a
 * buffer it does not fit; copies raised in the middle of a load, raised twice, or raised from a
 * load that never finished.
 */
#include "unittest.h"

#include "npc_spawn_block.h"
#include "npc_spawn_rules.h"

#include <string.h>

#define MOUNTED 0x004B5480u
#define WALKING 0x004B5300u

int main(void)
{
    npc_spawn_hold_t hold;

    ut_section("riding a copy");
    ut_check(npc_spawn_rides(MOUNTED, MOUNTED, 256u, 0u) &&
                 npc_spawn_rides(MOUNTED, MOUNTED, 383u, 0u),
             "mounted on a gun whose index is a copy's key: on a copy");
    ut_check(!npc_spawn_rides(MOUNTED, MOUNTED, 12u, 0u) &&
                 !npc_spawn_rides(MOUNTED, MOUNTED, 384u, 0u),
             "a level's own gun, or an index past the copies, is no copy");
    ut_check(!npc_spawn_rides(WALKING, MOUNTED, 300u, 0u),
             "and on foot the gun's index left in the player means nothing");
    ut_check(npc_spawn_rides(MOUNTED, MOUNTED, 300u, 300u) &&
                 !npc_spawn_rides(MOUNTED, MOUNTED, 300u, 301u),
             "on this copy is the key and nothing else");
    ut_check(!npc_spawn_rides(0u, 0u, 300u, 0u), "with no mounted mode known, never");

    ut_section("a block the loader hands over");
    ut_check(npc_spawn_block_plan(1u, 60, 7172u) == NPC_SPAWN_BLOCK_READ,
             "version 1 that fits is read");
    ut_check(npc_spawn_block_plan(1u, 7172, 7172u) == NPC_SPAWN_BLOCK_READ, "a full one too");
    ut_check(npc_spawn_block_plan(1u, 7173, 7172u) == NPC_SPAWN_BLOCK_STEP_OVER,
             "one byte more than the buffer holds is stepped over, never read into it");
    ut_check(npc_spawn_block_plan(2u, 60, 7172u) == NPC_SPAWN_BLOCK_STEP_OVER,
             "a version this build does not know is stepped over");
    ut_check(npc_spawn_block_plan(1u, 3, 7172u) == NPC_SPAWN_BLOCK_STEP_OVER &&
                 npc_spawn_block_plan(1u, -1, 7172u) == NPC_SPAWN_BLOCK_STEP_OVER,
             "and so is a length too short for the length word, or a negative one");

    ut_section("a load that finishes");
    memset(&hold, 0, sizeof hold);
    (void)npc_spawn_hold_new_world(&hold);          /* 0x19: the world is built */
    npc_spawn_hold_read(&hold, 3u);                 /* 0xB: the block */
    ut_check(!npc_spawn_hold_due(&hold), "the copies wait while the load goes on");
    npc_spawn_hold_load_over(&hold);                /* 0x18 */
    ut_check(npc_spawn_hold_due(&hold) && hold.held == 3u, "and are due once it is over");
    npc_spawn_hold_clear(&hold);
    ut_check(!npc_spawn_hold_due(&hold), "and raised once only");
    npc_spawn_hold_load_over(&hold);
    ut_check(!npc_spawn_hold_due(&hold), "a later 0x18 with nothing read raises nothing");

    ut_section("a load that does not finish");
    memset(&hold, 0, sizeof hold);
    (void)npc_spawn_hold_new_world(&hold);
    npc_spawn_hold_read(&hold, 2u);
    ut_check(npc_spawn_hold_new_world(&hold) == 2u, "a new world before 0x18 drops the two");
    npc_spawn_hold_load_over(&hold);
    ut_check(!npc_spawn_hold_due(&hold), "so the 0x18 of that next world raises nothing");

    ut_section("a load that reads no copy");
    memset(&hold, 0, sizeof hold);
    (void)npc_spawn_hold_new_world(&hold);
    npc_spawn_hold_read(&hold, 0u);
    npc_spawn_hold_load_over(&hold);
    ut_check(!npc_spawn_hold_due(&hold), "an empty block holds nothing to raise");

    ut_section("the epoch counts every world");
    memset(&hold, 0, sizeof hold);
    (void)npc_spawn_hold_new_world(&hold);
    (void)npc_spawn_hold_new_world(&hold);
    ut_check(hold.epoch == 2u, "each world built and each level ended is a new one");

    return ut_summary("npc_spawn_rules");
}
