/* mp_phases.c: the replicated phase loop, driven over a local record with no game.
 *
 * The loop's contract is the engine's, and every clause of it decides what a mode does, so each
 * is pinned here by a descriptor built to expose it: a wrong pin, a missed re-read after the call,
 * a flag not cleared, or a NULL plan entry that still ran would each change the trace.
 */
#include "unittest.h"

#include "mp_phases.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The record fields the loop reads and writes, as dword indices into a local block. */
#define RECORD_WORDS       (0x3ACu / 4u)
#define MODE_CHANGED_WORD  (0x5Cu / 4u)
#define DESCRIPTOR_WORD    (0x60u / 4u)

#define OP_RUN   0u
#define OP_PIN   1u
#define OP_SKIP  2u

#define TRACE_MAX 64u

static uint32_t record[RECORD_WORDS];
static uint32_t trace[TRACE_MAX];
static size_t   traced;

static void note(uint32_t tag)
{
    if (traced < TRACE_MAX) {
        trace[traced] = tag;
    }
    ++traced;
}

#define DEFAULT_PHASE(n) static void __cdecl default_##n(void) { note(n); }
DEFAULT_PHASE(0)  DEFAULT_PHASE(1)  DEFAULT_PHASE(2)  DEFAULT_PHASE(3)  DEFAULT_PHASE(4)
DEFAULT_PHASE(5)  DEFAULT_PHASE(6)  DEFAULT_PHASE(7)  DEFAULT_PHASE(8)  DEFAULT_PHASE(9)
DEFAULT_PHASE(10) DEFAULT_PHASE(11) DEFAULT_PHASE(12)

static const mp_phases_plan_t full_plan = {{
    &default_0, &default_1, &default_2, &default_3, &default_4, &default_5, &default_6,
    &default_7, &default_8, &default_9, &default_10, &default_11, &default_12
}};

/* Mode-owned phases. Tags from 100 up so they cannot be mistaken for a default index. */
#define TAG_MODE_A       100u
#define TAG_MODE_ENTER   101u
#define TAG_MODE_RESHAPE 102u

static void __cdecl mode_a(void) { note(TAG_MODE_A); }

/* A mode entry as the engine does it: point the record at the new descriptor and raise the
 * mode-changed flag, which ends the walk. */
static uint32_t entered_descriptor[MP_PHASES_COUNT];

static void __cdecl mode_enter(void)
{
    note(TAG_MODE_ENTER);
    record[DESCRIPTOR_WORD]   = (uint32_t)(uintptr_t)entered_descriptor;
    record[MODE_CHANGED_WORD] = 1u;
}

/* The awkward case: swap the descriptor without raising the flag, so the very next read of the
 * cursor's word comes from the new descriptor. */
static uint32_t reshaped_descriptor[MP_PHASES_COUNT];

static void __cdecl mode_reshape(void)
{
    note(TAG_MODE_RESHAPE);
    record[DESCRIPTOR_WORD] = (uint32_t)(uintptr_t)reshaped_descriptor;
}

static uint32_t fn(void (__cdecl *phase)(void))
{
    return (uint32_t)(uintptr_t)phase;
}

static void begin(const uint32_t *descriptor)
{
    memset(record, 0, sizeof(record));
    record[DESCRIPTOR_WORD] = (uint32_t)(uintptr_t)descriptor;
    traced = 0;
}

static bool trace_is(const uint32_t *expected, size_t count)
{
    size_t index;

    if (traced != count) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (trace[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static void check_all_run(void)
{
    static const uint32_t descriptor[MP_PHASES_COUNT] = { 0 };
    static const uint32_t expected[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

    ut_section("thirteen run words walk every default to the terminator");

    begin(descriptor);
    ut_check(mp_phases_dispatch(&full_plan, (uintptr_t)record), "the walk completes");
    ut_check(trace_is(expected, 13), "all thirteen defaults ran in order, and nothing else");
    ut_check(record[MODE_CHANGED_WORD] == 0u, "the flag is still clear");
}

static void check_pin(void)
{
    uint32_t descriptor[MP_PHASES_COUNT] = { OP_RUN, OP_RUN, OP_RUN, OP_RUN, 0, OP_PIN, 0xDEADu };
    static const uint32_t expected[] = { 0, 1, 2, 3, TAG_MODE_A, 5, 6, 7, 8, 9, 10, 11, 12 };

    ut_section("the pin holds the cursor so a six word descriptor covers thirteen phases");

    descriptor[4] = fn(&mode_a);
    begin(descriptor);
    ut_check(mp_phases_dispatch(&full_plan, (uintptr_t)record), "the walk completes");
    ut_check(trace_is(expected, 13),
             "the mode's own phase 4 ran, then the pin ran every later default unfiltered");
    ut_check(traced == 13u, "the word after the pin was never reached");
}

static void check_skip_and_withheld(void)
{
    uint32_t descriptor[MP_PHASES_COUNT] = { 0 };
    mp_phases_plan_t plan = full_plan;
    static const uint32_t expected[] = { 1, 3, 4, 5, TAG_MODE_A, 7, 8, 9, 10, 11, 12 };

    ut_section("a skip word and a withheld default both leave a gap; a mode function does not");

    descriptor[0] = OP_SKIP;                /* the seven airborne modes skip the death check */
    descriptor[2] = OP_SKIP;
    descriptor[6] = fn(&mode_a);            /* the descriptor's own function at a withheld index */
    plan.phase[6]  = NULL;                  /* the plan withholds default 6 */
    plan.phase[11] = &default_11;

    begin(descriptor);
    ut_check(mp_phases_dispatch(&plan, (uintptr_t)record), "the walk completes");
    ut_check(trace_is(expected, 11),
             "0 and 2 skipped by the descriptor, and the mode's own function ran at 6");

    plan.phase[6] = &default_6;
    plan.phase[3] = NULL;
    descriptor[6] = OP_RUN;
    begin(descriptor);
    ut_check(mp_phases_dispatch(&plan, (uintptr_t)record), "the walk completes");
    ut_check(traced == 10u && trace[0] == 1u && trace[1] == 4u,
             "a run word at a NULL plan entry runs nothing, and the cursor still advances");
}

static void check_mode_change_ends_the_walk(void)
{
    uint32_t descriptor[MP_PHASES_COUNT] = { 0 };
    static const uint32_t expected_first[] = { 0, 1, TAG_MODE_ENTER };
    static const uint32_t expected_second[] = { TAG_MODE_A, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

    ut_section("a mode entry ends the walk, clears the flag, and the next walk starts fresh");

    descriptor[2] = fn(&mode_enter);
    memset(entered_descriptor, 0, sizeof(entered_descriptor));
    entered_descriptor[0] = fn(&mode_a);
    entered_descriptor[1] = OP_PIN;

    begin(descriptor);
    ut_check(mp_phases_dispatch(&full_plan, (uintptr_t)record), "the first walk completes");
    ut_check(trace_is(expected_first, 3), "phases 3 to 12 did not run after the mode entry");
    ut_check(record[MODE_CHANGED_WORD] == 0u, "the flag was cleared on the way out");
    ut_check(record[DESCRIPTOR_WORD] == (uint32_t)(uintptr_t)entered_descriptor,
             "the record now carries the entered mode's descriptor");

    traced = 0;
    ut_check(mp_phases_dispatch(&full_plan, (uintptr_t)record), "the second walk completes");
    ut_check(trace_is(expected_second, 13),
             "the new mode's phase 0 ran, then the pin at word 1 ran the rest unfiltered");
}

static void check_pin_is_decided_after_the_call(void)
{
    uint32_t descriptor[MP_PHASES_COUNT] = { 0 };
    static const uint32_t expected[] = { 0, TAG_MODE_RESHAPE, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

    ut_section("the pin decision reads the word again after the call");

    /* Word 1 of the first descriptor swaps in a descriptor whose word 1 is the pin and whose word
     * 2 is a skip. Read again after the call, the pin holds the cursor at 1 and every later default
     * runs; a loop that kept the word it read before the call would advance to word 2 of the new
     * descriptor and skip phase 2. */
    descriptor[1] = fn(&mode_reshape);
    memset(reshaped_descriptor, 0, sizeof(reshaped_descriptor));
    reshaped_descriptor[1] = OP_PIN;
    reshaped_descriptor[2] = OP_SKIP;

    begin(descriptor);
    ut_check(mp_phases_dispatch(&full_plan, (uintptr_t)record), "the walk completes");
    ut_check(trace_is(expected, 13), "phase 2 ran, so the pin was read from the new descriptor");
    ut_check(record[MODE_CHANGED_WORD] == 0u, "no flag was raised, none was cleared");
}

static void check_refusals(void)
{
    static const uint32_t descriptor[MP_PHASES_COUNT] = { 0 };

    ut_section("refusals and the process with no game");

    begin(descriptor);
    ut_check(!mp_phases_dispatch(NULL, (uintptr_t)record), "no plan is refused");
    ut_check(!mp_phases_dispatch(&full_plan, 0), "no record is refused");
    ut_check(traced == 0u, "and nothing ran");

    begin(descriptor);
    record[DESCRIPTOR_WORD] = 0;
    ut_check(!mp_phases_dispatch(&full_plan, (uintptr_t)record),
             "a record with no descriptor stops the walk with a fault");
    ut_check(traced == 0u, "before any phase ran");

    ut_check(!mp_phases_install(), "with no cells resolved the install refuses");
    ut_check(!mp_phases_ready(), "and does not claim readiness");
    ut_check(mp_phases_foreign_slots() == 0u, "no foreign slot was counted");
    mp_phases_run_second();
    ut_check(mp_phases_second_faults() == 0u, "the second body's tick is a no-op, not a fault");
    ut_check(!mp_phases_second_dead(), "and the second body is not reported dead");
}

/* A placed body: its plan's entries run in order and the record's descriptor is never read, so a
 * mode's own phase function (the stand mode's clip selection) cannot run over the replicated
 * clips, and a record with no descriptor at all is walked rather than refused. */
static void check_placed_plan(void)
{
    uint32_t descriptor[MP_PHASES_COUNT] = { 0 };
    mp_phases_plan_t plan;
    static const uint32_t expected[] = { 1, 11, 12 };

    ut_section("a placed plan runs its own entries and reads no descriptor word");

    memset(&plan, 0, sizeof plan);
    plan.placed    = true;
    plan.phase[1]  = &default_1;
    plan.phase[11] = &default_11;
    plan.phase[12] = &default_12;

    descriptor[4] = fn(&mode_a);            /* the trap: a mode-owned phase four */
    descriptor[1] = OP_SKIP;                /* a skip the placed plan must ignore too */
    begin(descriptor);
    ut_check(mp_phases_dispatch(&plan, (uintptr_t)record), "the walk completes");
    ut_check(trace_is(expected, 3), "exactly phases 1, 11 and 12 ran, in order, and nothing else");

    begin(NULL);                            /* no descriptor at all */
    ut_check(mp_phases_dispatch(&plan, (uintptr_t)record),
             "a placed body with no descriptor is still walked");
    ut_check(trace_is(expected, 3), "and runs the same three entries");

    plan.placed = false;
    begin(NULL);
    ut_check(!mp_phases_dispatch(&plan, (uintptr_t)record),
             "the simulated walk still refuses a record without a descriptor");
}

int main(void)
{
    check_all_run();
    check_pin();
    check_skip_and_withheld();
    check_mode_change_ends_the_walk();
    check_pin_is_decided_after_the_call();
    check_refusals();
    check_placed_plan();

    return ut_summary("mp_phases");
}
