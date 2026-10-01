/* character_reentry.c: the lock a hook holds while it is inside itself.
 *
 * There is nothing to reverse engineer here, which is exactly why it is worth a test: the whole
 * value of this file is that the three cases below behave the same way every time, including the
 * two that only happen when a hook has already gone wrong.
 */
#include "unittest.h"

#include "character_reentry.h"

#include <stddef.h>

int main(void)
{
    character_reentry_t lock = { 0 };

    ut_section("a fresh lock is free");
    ut_check(!character_reentry_is_held(&lock), "zero initialised means not held");
    ut_check(character_reentry_enter(&lock), "the first entry is granted");
    ut_check(character_reentry_is_held(&lock), "and the lock is then held");

    ut_section("the second entry is refused, which is the whole point");
    /* This is the call that arrives when a hook draws through the entry point it detoured. It has
     * to be refused, and it has to be refused without the caller having to know it is nested. */
    ut_check(!character_reentry_enter(&lock), "a second entry while held is refused");
    ut_check(!character_reentry_enter(&lock), "and so is a third");
    ut_check(character_reentry_is_held(&lock), "a refused entry does not change the lock");

    ut_section("leaving gives it back exactly once");
    character_reentry_leave(&lock);
    ut_check(!character_reentry_is_held(&lock), "one leave frees it");
    ut_check(character_reentry_enter(&lock), "and the next call may enter");

    ut_section("an unbalanced leave cannot wedge it shut");
    /* A hook that took an early return may leave without having entered. Counting that down would
     * make every later entry fail, which is the failure this file exists to prevent rather than a
     * new way of producing it. */
    character_reentry_leave(&lock);
    character_reentry_leave(&lock);
    character_reentry_leave(&lock);
    ut_check(!character_reentry_is_held(&lock), "three leaves leave it free, not negative");
    ut_check(character_reentry_enter(&lock), "and an entry after them is still granted");
    character_reentry_leave(&lock);

    ut_section("a null lock is refused rather than dereferenced");
    /* A hook whose module never initialised is the one place a null can arrive, and it arrives on
     * the drawing path where a fault is a crash rather than a warning. */
    ut_check(!character_reentry_enter(NULL), "entering nothing is refused");
    ut_check(!character_reentry_is_held(NULL), "nothing is not held");
    character_reentry_leave(NULL);

    return ut_summary("character reentry");
}
