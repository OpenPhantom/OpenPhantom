/* debug_register: the x86 debug register bit packing.
 *
 * Every value here is checked against what the manual specifies rather than against what the code
 * happens to produce, because the failure mode is silent. A wrong condition or length arms a
 * breakpoint that simply never fires, and a watch that never fires is indistinguishable from a
 * field that nothing writes, which is the exact conclusion this instrument exists to avoid
 * reaching by accident.
 */
#include "debug_register.h"

#include "unittest.h"

static void test_arming_a_slot(void)
{
    uint32_t dr7;

    ut_section("arming a four byte write watch");

    dr7 = debug_register_arm(0u, 0u, DEBUG_WATCH_WRITE, DEBUG_LENGTH_4);
    ut_check((dr7 & 0x1u) != 0u, "slot 0's local enable is bit 0");
    ut_check((dr7 & 0x2u) == 0u,
             "and its global enable, bit 1, is left clear: this is a per thread watch and a global "
             "one would ask the processor for behaviour nothing here wants");
    ut_check(((dr7 >> 16) & 0xFu) == 0xDu,
             "slot 0's four bit field reads 1101: condition 01 for write in the low two bits, "
             "length 11 for four bytes in the high two");

    dr7 = debug_register_arm(0u, 1u, DEBUG_WATCH_WRITE, DEBUG_LENGTH_4);
    ut_check((dr7 & 0x4u) != 0u, "slot 1's local enable is bit 2, two bits per slot");
    ut_check(((dr7 >> 20) & 0xFu) == 0xDu, "and its field sits four bits further up, at bit 20");

    dr7 = debug_register_arm(0u, 3u, DEBUG_WATCH_READ_WRITE, DEBUG_LENGTH_1);
    ut_check((dr7 & 0x40u) != 0u, "slot 3's local enable is bit 6");
    ut_check(((dr7 >> 28) & 0xFu) == 0x3u,
             "a one byte read and write watch is condition 11 with length 00");
}

static void test_slots_do_not_disturb_each_other(void)
{
    uint32_t dr7;

    ut_section("one slot at a time");

    dr7 = debug_register_arm(0u, 0u, DEBUG_WATCH_WRITE, DEBUG_LENGTH_4);
    dr7 = debug_register_arm(dr7, 2u, DEBUG_WATCH_READ_WRITE, DEBUG_LENGTH_2);
    ut_check(((dr7 >> 16) & 0xFu) == 0xDu, "arming slot 2 leaves slot 0's field untouched");
    ut_check((dr7 & 0x1u) != 0u, "and leaves its enable set");
    ut_check(((dr7 >> 24) & 0xFu) == 0x7u, "slot 2 gets condition 11 with length 01");

    dr7 = debug_register_disarm(dr7, 2u);
    ut_check((dr7 & 0x10u) == 0u, "disarming clears slot 2's own enable, bit 4");
    ut_check(((dr7 >> 24) & 0xFu) == 0u, "and clears its condition and length");
    ut_check((dr7 & 0x1u) != 0u && ((dr7 >> 16) & 0xFu) == 0xDu,
             "while slot 0 is still armed exactly as it was, which is what lets this instrument "
             "share the registers with anything else already using them");

    dr7 = debug_register_arm(0xDu << 16 | 1u, 0u, DEBUG_WATCH_READ_WRITE, DEBUG_LENGTH_1);
    ut_check(((dr7 >> 16) & 0xFu) == 0x3u,
             "re-arming a slot replaces its field rather than merging bits into the old one");
}

static void test_a_value_nobody_checked_is_refused(void)
{
    uint32_t armed = debug_register_arm(0u, 0u, DEBUG_WATCH_WRITE, DEBUG_LENGTH_4);

    ut_section("refusing nonsense rather than arming it");

    ut_check(debug_register_arm(armed, 4u, DEBUG_WATCH_WRITE, DEBUG_LENGTH_4) == armed,
             "there are four slots, so slot 4 is refused and the register comes back unchanged");
    ut_check(debug_register_arm(armed, 0u, (debug_watch_t)2, DEBUG_LENGTH_4) == armed,
             "condition 10 is reserved on this processor and is refused rather than written");
    ut_check(debug_register_arm(armed, 0u, DEBUG_WATCH_WRITE, (debug_length_t)2) == armed,
             "length 10 means eight bytes, which this target does not have, so it is refused");
    ut_check(debug_register_disarm(armed, 9u) == armed, "and disarming a slot that cannot exist "
             "changes nothing");
}

static void test_reading_and_clearing_the_status(void)
{
    ut_section("which slot fired");

    ut_check(debug_register_fired(0x1u, 0u), "DR6 bit 0 says slot 0 fired");
    ut_check(!debug_register_fired(0x1u, 1u), "and says nothing about slot 1");
    ut_check(debug_register_fired(0x8u, 3u), "bit 3 is slot 3");
    ut_check(!debug_register_fired(0xFF00u, 0u),
             "the high bits of DR6 are not breakpoint reports and are not read as one");
    ut_check(!debug_register_fired(0xFu, 7u), "a slot that cannot exist never reads as fired");

    ut_check(debug_register_acknowledge(0xFu, 1u) == 0xDu,
             "acknowledging clears only that slot's own bit, since the processor never clears "
             "these and a handler that left them set would see a stale slot forever");
    ut_check(debug_register_acknowledge(0x0u, 0u) == 0x0u,
             "acknowledging a slot that did not fire changes nothing");
}

static void test_alignment(void)
{
    ut_section("an address the processor can actually watch");

    ut_check(debug_register_address_is_aligned(0x00882184u, DEBUG_LENGTH_4),
             "a four byte watch on a four byte aligned address is fine");
    ut_check(!debug_register_address_is_aligned(0x00882186u, DEBUG_LENGTH_4),
             "a misaligned four byte watch is refused, because it would not fail, it would simply "
             "never fire and read as a field nothing writes");
    ut_check(debug_register_address_is_aligned(0x00882186u, DEBUG_LENGTH_2),
             "the same address is fine for a two byte watch");
    ut_check(debug_register_address_is_aligned(0x00882187u, DEBUG_LENGTH_1),
             "and any address at all is fine for one byte");
    ut_check(!debug_register_address_is_aligned(0x00882184u, (debug_length_t)2),
             "a length that does not exist is refused rather than treated as some default");
}

int main(void)
{
    test_arming_a_slot();
    test_slots_do_not_disturb_each_other();
    test_a_value_nobody_checked_is_refused();
    test_reading_and_clearing_the_status();
    test_alignment();

    return ut_summary("debug_register");
}
