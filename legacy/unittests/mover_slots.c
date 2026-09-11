/* mover_slots.c: the side table gives its slots back.
 *
 * For weeks it did not. Nothing ever freed a slot, so a fresh launch straight into a platform
 * level was smooth, every platform test passed, and the third level of a session was drawn
 * stepped from its first frame because the first two still owned the table. The log said so all
 * along in the unknown column and nobody had ridden a platform three levels in. These are the two
 * ways a slot comes back, and the two ways it must not.
 */
#include "unittest.h"

#include "mover_slots.h"

#include <stddef.h>
#include <stdint.h>

/* Subnode addresses the way the engine lays them out: 0x9C apart inside one mover record. */
static const void *subnode(uintptr_t mover, int index)
{
    return (const void *)(mover + 0x84u + (uintptr_t)index * 0x9Cu);
}

int main(void)
{
    static mover_slot_table_t table;
    mover_slot_t *a;
    mover_slot_t *b;

    ut_section("reserve and find");

    a = mover_slots_reserve(&table, subnode(0x10000, 0), 1u);
    ut_check(a != NULL && a->subnode == subnode(0x10000, 0), "a newcomer gets a slot of its own");
    ut_check(mover_slots_find(&table, subnode(0x10000, 0)) == a, "and is found again by address");
    ut_check(mover_slots_find(&table, subnode(0x10000, 1)) == NULL,
             "its neighbour, 0x9C along, is not mistaken for it");
    ut_check(mover_slots_reserve(&table, subnode(0x10000, 0), 50u) == a,
             "reserving an address already held answers the same slot, not a second one");

    ut_section("a slot starts clean");

    a->usable       = true;
    a->previous[9]  = 42.0f;
    a->tick_stamp   = 1u;
    mover_slots_forget(&table);
    ut_check(mover_slots_find(&table, subnode(0x10000, 0)) == NULL,
             "a level opening forgets every subnode");
    b = mover_slots_reserve(&table, subnode(0x10000, 0), 2u);
    ut_check(b != NULL && !b->usable && b->previous[9] == 0.0f,
             "the same address in the next level starts with no history, not the old level's");

    ut_section("aging");

    b->tick_stamp = 100u;
    ut_check(mover_slots_reserve(&table, subnode(0x10000, 0), 100u + MOVER_SLOT_STALE_FRAMES) == b,
             "exactly at the stale limit the slot is still its owner's");
    /* A collision partner on the same probe path: forced by filling the table's first stretch. */
    {
        uintptr_t m;
        int       taken = 0;
        int       i;

        for (i = 1; i < (int)MOVER_SLOT_COUNT; ++i) {
            m = 0x20000u + (uintptr_t)i * 0x1000u;
            if (mover_slots_reserve(&table, subnode(m, 0), 100u) != NULL) {
                ++taken;
            }
        }
        ut_checkf(taken == (int)MOVER_SLOT_COUNT - 1, "the table fills to its size, %d of %u",
                 taken, (unsigned)MOVER_SLOT_COUNT);
        ut_check(mover_slots_reserve(&table, subnode(0x777770, 0), 100u) == NULL,
                 "full of live movers, a newcomer is refused rather than given somebody's slot");
        ut_check(mover_slots_find(&table, subnode(0x10000, 0)) == b,
                 "and nobody lost theirs to the refusal");

        b = mover_slots_reserve(&table, subnode(0x777770, 0), 100u + MOVER_SLOT_STALE_FRAMES + 1u);
        ut_check(b != NULL, "once every owner is stale a newcomer takes a slot");
        ut_check(b->subnode == subnode(0x777770, 0) && b->tick_stamp == 0u && !b->usable,
                 "and takes it cleared, whoever had it");
    }

    ut_section("what the table never does");

    mover_slots_forget(&table);
    a = mover_slots_reserve(&table, subnode(0x30000, 0), 10u);
    a->tick_stamp = 10u;
    b = mover_slots_reserve(&table, subnode(0x30000, 0), 10u + MOVER_SLOT_STALE_FRAMES + 5u);
    ut_check(a == b,
             "a subnode asking for its own stale slot gets it back rather than a second one");
    ut_check(mover_slots_find(&table, NULL) == NULL, "NULL is never a subnode");

    return ut_summary("mover slots");
}
