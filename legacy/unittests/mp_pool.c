/* mp_pool.c: the reserve latch's decision over its boundaries, and the latch driven in a process
 * with no game in it.
 *
 * The decision is one comparison and both of its mistakes are silent in review: off by one toward
 * generous and the last free slot inside the reserve is handed out, off by one toward strict and
 * a pool with room refuses forever. Neither shows up at run time as anything but a crash much
 * later or a body that never spawns.
 *
 * The no-game half is its own claim: every way of asking the latch for a slot on a machine where
 * nothing resolved must come back as a refusal, counted, with nothing dereferenced. A caller
 * cannot tell "no game" from "no room" and does not need to; both answers are NULL.
 */
#include "unittest.h"

#include "mp_pool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void check_decision(void)
{
    ut_section("the decision, over its boundaries");

    ut_check(mp_pool_may_take(0u, 255u, 64u), "an empty pool grants a slot");
    ut_check(mp_pool_may_take(190u, 255u, 64u),
             "at 190 live of 255 there are 65 free, one more than the reserve, so it grants");
    ut_check(!mp_pool_may_take(191u, 255u, 64u),
             "at 191 live the free count equals the reserve, so it refuses");
    ut_check(!mp_pool_may_take(254u, 255u, 64u), "one free slot inside the reserve is refused");
    ut_check(mp_pool_may_take(254u, 255u, 0u),
             "with no reserve the very last slot is still grantable");
    ut_check(!mp_pool_may_take(255u, 255u, 0u), "a full pool refuses even with no reserve");
    ut_check(!mp_pool_may_take(300u, 255u, 0u),
             "a live count past the capacity is a corrupt walk, not a grant");
    ut_check(!mp_pool_may_take(0u, 0u, 0u), "a capacity of zero never grants");
    ut_check(!mp_pool_may_take(0u, 255u, 255u),
             "a reserve at the capacity refuses everything, which is why configure clamps");
    ut_check(mp_pool_may_take(0u, 255u, 254u),
             "a reserve one below the capacity leaves exactly one grantable slot");
}

static void check_clamp(void)
{
    ut_section("the clamp");

    ut_check(mp_pool_reserve() == MP_POOL_RESERVE_DEFAULT,
             "the default reserve stands before anyone configures");

    mp_pool_configure(1000u);
    ut_check(mp_pool_reserve() == MP_POOL_RESERVE_MAX,
             "a reserve past the maximum is clamped to it rather than kept");

    mp_pool_configure(0u);
    ut_check(mp_pool_reserve() == 0u, "a reserve of zero is a choice, not an error");

    mp_pool_configure(MP_POOL_RESERVE_DEFAULT);
    ut_check(mp_pool_reserve() == MP_POOL_RESERVE_DEFAULT, "and the default can be put back");
}

static void check_without_a_game(void)
{
    uint32_t refusals_before;

    ut_section("in a process with no game");

    refusals_before = mp_pool_refusals();
    ut_check(mp_pool_take("the test") == NULL,
             "with nothing resolved a take is a refusal, not a crash");
    ut_check(mp_pool_refusals() == refusals_before + 1u, "and the refusal is counted");
    ut_check(mp_pool_outstanding() == 0u, "nothing is recorded as held");

    ut_check(mp_pool_take(NULL) == NULL, "a caller with no name is still refused safely");

    mp_pool_give(NULL);
    ut_check(mp_pool_outstanding() == 0u, "a NULL given back is a logged caller error, not a slot");

    mp_pool_provoke_full();
    ut_check(mp_pool_outstanding() == 0u,
             "the provocation skips cleanly where the allocator never resolved");
}

int main(void)
{
    check_decision();
    check_clamp();
    check_without_a_game();

    return ut_summary("mp_pool");
}
