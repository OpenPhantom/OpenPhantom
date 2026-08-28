/* sound_owner.c: the one decision sound_lifetime_fix makes.
 *
 * The fix clears a pinned voice's owner handle only when that handle points into the calling
 * thread's own stack, because the whole defect is a pointer to a local outliving the frame that
 * held it. Everything else about the module is a detour and a signature, which a console test
 * cannot exercise; this predicate is the part that decides whether a channel is touched at all, and
 * getting it wrong in either direction is a real failure.
 *
 * Wrong in one direction leaves the crash in place. Wrong in the other clears a handle that a live
 * owner is still waiting on, which silently breaks the engine's own lifetime protocol: the owner
 * never learns its voice ended and keeps a channel index that has been handed to somebody else.
 *
 * The extent is expressed the way the thread information block records it, with the limit as the
 * low address and the base as the high one, and it is half open at the top: base is one past the
 * last usable byte, so an owner handle exactly at base belongs to no frame of this stack.
 */
#include "unittest.h"

#include "sound_lifetime_fix.h"

#include <stdint.h>

/* Numbers in the shape of the real thing, taken from the crash this was written for: the stack ran
 * 00130000..001B0000 and the three dangling handles sat at 001AFC2C and 001AFB9C. */
#define LIMIT ((uintptr_t)0x00130000u)
#define BASE  ((uintptr_t)0x001B0000u)

int main(void)
{
    ut_section("a handle inside the stack is the defect");
    ut_check(sound_owner_is_on_stack(0x001AFC2Cu, LIMIT, BASE),
             "an address inside the extent is on the stack");
    ut_check(sound_owner_is_on_stack(LIMIT, LIMIT, BASE),
             "the lowest committed byte is on the stack");
    ut_check(sound_owner_is_on_stack(BASE - 1u, LIMIT, BASE),
             "the last usable byte is on the stack");

    ut_section("a handle outside it is left alone");
    ut_check(!sound_owner_is_on_stack(BASE, LIMIT, BASE),
             "the extent is half open, so base itself is not on the stack");
    ut_check(!sound_owner_is_on_stack(LIMIT - 1u, LIMIT, BASE),
             "one byte below the limit is not on the stack");
    ut_check(!sound_owner_is_on_stack(0x0086D580u, LIMIT, BASE),
             "an owner handle in the engine's data section is left alone");
    ut_check(!sound_owner_is_on_stack(0x10000000u, LIMIT, BASE),
             "a heap owner well above the stack is left alone");

    ut_section("nothing to detach");
    ut_check(!sound_owner_is_on_stack(0u, LIMIT, BASE),
             "a null owner handle is not on any stack");

    ut_section("an extent that describes no memory holds nothing");
    ut_check(!sound_owner_is_on_stack(0x001AFC2Cu, BASE, LIMIT),
             "limit and base the wrong way round refuses rather than inverting the test");
    ut_check(!sound_owner_is_on_stack(0x001AFC2Cu, BASE, BASE),
             "an empty extent contains no address");
    ut_check(!sound_owner_is_on_stack(0u, 0u, 0u),
             "a thread with no recorded stack refuses every handle");

    return ut_summary("sound owner handle");
}
