/* imuse_guard.c: the predicate that decides whether a call is let into iMUSE.
 *
 * This one function is the whole deterministic repair. If it says "safe" for a value that is
 * really out of range, the music DLL takes its heartbeat lock and never gives it back and the
 * music loops forever. If it says "leaks" for a value that is really fine, a legitimate setting
 * is silently refused. Both are quiet failures, which is exactly why the bounds are checked here
 * against the ones read out of the DLL rather than trusted.
 *
 * The bounds, from the body of ImSetParam:
 *     0x400  value < 0x10    UNSIGNED
 *     0x500  value < 0x80    UNSIGNED
 *     0x600  value < 0x80    UNSIGNED
 *     0x700  value < 0x80    UNSIGNED
 *     0x800  -0x2401 < value < 0x2401   SIGNED
 *     0x900  and everything else: releases the lock on its own refusal, so not our business.
 */
#include "unittest.h"

#include "imuse_guard.h"


int main(void)
{
    /* --- the four unsigned bounds ----------------------------------------------------------- */
    ut_check(!imuse_guard_set_param_leaks(0x400, 0x00), "0x400 with 0 is inside");
    ut_check(!imuse_guard_set_param_leaks(0x400, 0x0F), "0x400 with 0x0F is the last safe value");
    ut_check( imuse_guard_set_param_leaks(0x400, 0x10), "0x400 with 0x10 leaks");
    ut_check( imuse_guard_set_param_leaks(0x400, 0x7FFFFFFF), "0x400 with a huge value leaks");

    ut_check(!imuse_guard_set_param_leaks(0x500, 0x7F), "0x500 with 0x7F is the last safe value");
    ut_check( imuse_guard_set_param_leaks(0x500, 0x80), "0x500 with 0x80 leaks");
    ut_check(!imuse_guard_set_param_leaks(0x600, 0x7F), "0x600 with 0x7F is the last safe value");
    ut_check( imuse_guard_set_param_leaks(0x600, 0x80), "0x600 with 0x80 leaks");
    ut_check(!imuse_guard_set_param_leaks(0x700, 0x7F), "0x700 with 0x7F is the last safe value");
    ut_check( imuse_guard_set_param_leaks(0x700, 0x80), "0x700 with 0x80 leaks");

    /* The trap in these four: the DLL compares them UNSIGNED, so a negative value is not "below
     * the range", it is enormous. Volume -1 leaks. Getting this wrong would leave the single most
     * likely bad value unguarded. */
    ut_check(imuse_guard_set_param_leaks(0x400, -1), "0x400 with -1 leaks (compared unsigned)");
    ut_check(imuse_guard_set_param_leaks(0x500, -1), "0x500 with -1 leaks (compared unsigned)");
    ut_check(imuse_guard_set_param_leaks(0x600, -1), "0x600 with -1 leaks (compared unsigned)");
    ut_check(imuse_guard_set_param_leaks(0x700, -1), "0x700 with -1 leaks (compared unsigned)");

    /* --- the one signed bound --------------------------------------------------------------- */
    ut_check(!imuse_guard_set_param_leaks(0x800, 0), "0x800 with 0 is inside");
    ut_check(!imuse_guard_set_param_leaks(0x800,  0x2400), "0x800 with +0x2400 is the last safe value");
    ut_check(!imuse_guard_set_param_leaks(0x800, -0x2400), "0x800 with -0x2400 is the last safe value");
    ut_check( imuse_guard_set_param_leaks(0x800,  0x2401), "0x800 with +0x2401 leaks");
    ut_check( imuse_guard_set_param_leaks(0x800, -0x2401), "0x800 with -0x2401 leaks");
    /* And the mirror of the trap above: here the DLL DOES compare signed, so a negative value
     * inside the band must be let through. Treating 0x800 like the other four would refuse every
     * negative detune the game ever sets. */
    ut_check(!imuse_guard_set_param_leaks(0x800, -1), "0x800 with -1 is inside (compared signed)");

    /* --- the parameters that clean up after themselves --------------------------------------- */
    ut_check(!imuse_guard_set_param_leaks(0x900, -1), "0x900 releases its own lock, so never ours");
    ut_check(!imuse_guard_set_param_leaks(0x900, 0x7FFFFFFF), "0x900 out of range is still not ours");
    ut_check(!imuse_guard_set_param_leaks(0x000, 0x7FFFFFFF), "an unknown parameter is not ours");
    ut_check(!imuse_guard_set_param_leaks(0x1234, -5), "another unknown parameter is not ours");

    /* --- the values the game really uses ----------------------------------------------------- */
    ut_check(!imuse_guard_set_param_leaks(0x600, 0x7E), "the muscript's own volume 0x7E is let through");
    ut_check(!imuse_guard_set_param_leaks(0x600, 0x7F), "full volume 0x7F is let through");
    ut_check(!imuse_guard_set_param_leaks(0x400, 0x04), "the muscript's own 0x400 value 4 is let through");

    return ut_summary("imuse_guard");
}
