/* mp_snapshot_history.c: the ring by tick, and the wraparound check that makes it correct.
 *
 * Store and fetch is the easy half. The half that matters is that a slot reused by a tick a full
 * ring later does not hand the old tick back: fetching by a tick checks the stored snapshot's own
 * tick, so an overwritten slot returns nothing for the tick it used to hold. The bracket is the
 * client's interpolation fetch, and it refuses a pair with a missing end rather than blending
 * across a gap it cannot fill.
 */
#include "unittest.h"

#include "mp_snapshot.h"
#include "mp_snapshot_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static mp_snapshot_history_t s_history;

static void store_tick(mp_snapshot_history_t *history, uint32_t tick)
{
    mp_snapshot_t snapshot;

    mp_snapshot_clear(&snapshot);
    snapshot.tick = tick;
    mp_snapshot_history_store(history, &snapshot);
}

static void check_store_and_fetch(void)
{
    mp_snapshot_history_t *history = &s_history;

    ut_section("store and fetch by tick");

    mp_snapshot_history_init(history);
    ut_check(mp_snapshot_history_newest(history) == NULL, "an empty ring has no newest");
    ut_check(mp_snapshot_history_get(history, 5u) == NULL, "and holds no tick");

    store_tick(history, 100u);
    store_tick(history, 101u);
    store_tick(history, 102u);

    ut_check(mp_snapshot_history_get(history, 101u) != NULL, "a stored tick is fetched");
    ut_check(mp_snapshot_history_get(history, 101u)->tick == 101u, "and it is that tick");
    ut_check(mp_snapshot_history_get(history, 103u) == NULL, "a tick never stored is not");
    ut_check(mp_snapshot_history_newest(history) != NULL &&
             mp_snapshot_history_newest(history)->tick == 102u, "the newest is the latest stored");
}

static void check_wraparound(void)
{
    mp_snapshot_history_t *history = &s_history;

    ut_section("a slot reused a ring later does not return the old tick");

    mp_snapshot_history_init(history);
    store_tick(history, 5u);
    ut_check(mp_snapshot_history_get(history, 5u) != NULL, "tick 5 is held");

    /* A ring length later lands in the same slot and overwrites it. */
    store_tick(history, 5u + MP_SNAPSHOT_HISTORY);
    ut_check(mp_snapshot_history_get(history, 5u) == NULL,
             "tick 5 is gone once its slot was reused");
    ut_check(mp_snapshot_history_get(history, 5u + MP_SNAPSHOT_HISTORY) != NULL,
             "and the newer tick in that slot is held");

    ut_check(mp_snapshot_history_newest(history)->tick == 5u + MP_SNAPSHOT_HISTORY,
             "the newest is the newer one");
}

static void check_bracket(void)
{
    mp_snapshot_history_t *history = &s_history;
    const mp_snapshot_t   *from = NULL;
    const mp_snapshot_t   *to = NULL;

    ut_section("the interpolation bracket");

    mp_snapshot_history_init(history);
    store_tick(history, 200u);
    store_tick(history, 201u);
    store_tick(history, 202u);

    ut_check(mp_snapshot_history_bracket(history, 201u, 202u, &from, &to),
             "a pair both present brackets");
    ut_check(from != NULL && to != NULL && from->tick == 201u && to->tick == 202u,
             "and hands back the two in order");

    ut_check(!mp_snapshot_history_bracket(history, 202u, 203u, &from, &to),
             "a pair with a missing end does not bracket");
    ut_check(!mp_snapshot_history_bracket(history, 150u, 151u, &from, &to),
             "nor one entirely off the held range");
}

static void check_tick_wrap_newest(void)
{
    mp_snapshot_history_t *history = &s_history;

    ut_section("newest stays right across the tick counter's own wrap");

    mp_snapshot_history_init(history);
    store_tick(history, 0xFFFFFFFEu);
    store_tick(history, 0xFFFFFFFFu);
    store_tick(history, 0u);   /* the counter wrapped past its maximum */

    ut_check(mp_snapshot_history_newest(history) != NULL &&
             mp_snapshot_history_newest(history)->tick == 0u,
             "the wrapped tick is newer than the maximum, not older");
}

int main(void)
{
    check_store_and_fetch();
    check_wraparound();
    check_bracket();
    check_tick_wrap_newest();

    return ut_summary("mp_snapshot_history");
}
