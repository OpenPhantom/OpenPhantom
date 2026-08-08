/* focus_guard.c: the release rule, exhaustively, without a window.
 *
 * This feature takes process-global operating-system state: a ClipCursor rectangle. The whole
 * safety argument for doing that is one sentence, "it is released on every path that takes the
 * foreground away", and that sentence is a property of focus_guard_actions() alone, because the
 * caller only ever performs what that function returns.
 *
 * So the test does not sample. It enumerates all 64 combinations of the six inputs and asserts the
 * invariants over every one of them, and then walks the sequences a real session produces: an
 * Alt-Tab out and back, a minimise, a foreign window that steals the foreground for one frame, and
 * the configuration key being turned off while a clip is held. A clip left standing while the game
 * is not in front is a worse bug than the one the clip exists to fix, so that is the case with the
 * most checks on it.
 */
#include "unittest.h"

#include "focus_guard.h"

#include <stdbool.h>

static focus_guard_inputs_t make(bool state_known, bool had_focus, bool has_focus,
                                 bool confining, bool confine_wanted, bool reacquire_wanted)
{
    focus_guard_inputs_t inputs;

    inputs.state_known      = state_known;
    inputs.had_focus        = had_focus;
    inputs.has_focus        = has_focus;
    inputs.confining        = confining;
    inputs.confine_wanted   = confine_wanted;
    inputs.reacquire_wanted = reacquire_wanted;
    return inputs;
}

/* ============================================================================================ */
static void test_every_combination(void)
{
    int  bits;
    int  release_missing = 0;
    int  both_clip_actions = 0;
    int  confine_without_focus = 0;
    int  confine_when_unwanted = 0;
    int  both_input_actions = 0;
    int  input_without_change = 0;
    int  input_when_unwanted = 0;
    int  input_on_first_tick = 0;

    for (bits = 0; bits < 64; ++bits) {
        focus_guard_inputs_t inputs = make((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0,
                                           (bits & 8) != 0, (bits & 16) != 0, (bits & 32) != 0);
        unsigned actions = focus_guard_actions(&inputs);

        bool release   = (actions & FOCUS_ACTION_RELEASE) != 0;
        bool confine   = (actions & FOCUS_ACTION_CONFINE) != 0;
        bool acquire   = (actions & FOCUS_ACTION_ACQUIRE) != 0;
        bool unacquire = (actions & FOCUS_ACTION_UNACQUIRE) != 0;

        /* The invariant this feature lives on. */
        if (inputs.confining && (!inputs.has_focus || !inputs.confine_wanted) && !release) {
            ++release_missing;
        }
        if (release && confine) {
            ++both_clip_actions;
        }
        if (confine && !inputs.has_focus) {
            ++confine_without_focus;
        }
        if (confine && !inputs.confine_wanted) {
            ++confine_when_unwanted;
        }

        if (acquire && unacquire) {
            ++both_input_actions;
        }
        if ((acquire || unacquire) && inputs.has_focus == inputs.had_focus) {
            ++input_without_change;
        }
        if ((acquire || unacquire) && !inputs.reacquire_wanted) {
            ++input_when_unwanted;
        }
        if ((acquire || unacquire) && !inputs.state_known) {
            ++input_on_first_tick;
        }
    }

    ut_check(release_missing == 0,
          "a held confinement is released whenever the foreground is not ours or the key is off");
    ut_check(both_clip_actions == 0, "confine and release are never asked for together");
    ut_check(confine_without_focus == 0, "the pointer is never confined without the foreground");
    ut_check(confine_when_unwanted == 0, "the pointer is never confined when the key is off");
    ut_check(both_input_actions == 0, "acquire and unacquire are never asked for together");
    ut_check(input_without_change == 0, "the engine is only told about an observed focus CHANGE");
    ut_check(input_when_unwanted == 0, "the engine is not touched when the key is off");
    ut_check(input_on_first_tick == 0, "the first tick sends the engine nothing: it only observes");
}

/* ============================================================================================ */
static void test_the_alt_tab(void)
{
    /* Focused and confining, then the foreground goes away. */
    focus_guard_inputs_t away = make(true, true, false, true, true, true);
    unsigned             out  = focus_guard_actions(&away);

    ut_check((out & FOCUS_ACTION_RELEASE) != 0, "Alt-Tab out releases the pointer");
    ut_check((out & FOCUS_ACTION_UNACQUIRE) != 0, "Alt-Tab out tells the engine its input is gone");
    ut_check((out & FOCUS_ACTION_CONFINE) == 0, "Alt-Tab out does not re-confine");

    /* And back. */
    {
        focus_guard_inputs_t back = make(true, false, true, false, true, true);
        unsigned             in   = focus_guard_actions(&back);

        ut_check((in & FOCUS_ACTION_ACQUIRE) != 0, "Alt-Tab back re-acquires the input devices");
        ut_check((in & FOCUS_ACTION_CONFINE) != 0, "Alt-Tab back confines the pointer again");
        ut_check((in & FOCUS_ACTION_RELEASE) == 0, "Alt-Tab back does not release");
    }
}

static void test_the_steady_states(void)
{
    focus_guard_inputs_t held = make(true, true, true, true, true, true);
    unsigned             a    = focus_guard_actions(&held);

    ut_check(a == FOCUS_ACTION_CONFINE,
          "a focused frame that changes nothing only refreshes the clip");

    {
        /* Away and staying away: nothing at all, and in particular no repeated Unacquire. */
        focus_guard_inputs_t gone = make(true, false, false, false, true, true);
        ut_check(focus_guard_actions(&gone) == FOCUS_ACTION_NONE,
              "a background frame that changes nothing does nothing");
    }
}

static void test_the_awkward_paths(void)
{
    /* The key is switched off while a clip is held, the release must not wait for a focus loss. */
    focus_guard_inputs_t switched_off = make(true, true, true, true, false, true);
    unsigned             off = focus_guard_actions(&switched_off);

    ut_check((off & FOCUS_ACTION_RELEASE) != 0,
          "turning the confinement off releases a clip that is already held");
    ut_check((off & FOCUS_ACTION_CONFINE) == 0, "and does not re-apply it");

    /* The very first tick, with the game already in front: confine, but say nothing to the engine,
     * at that point its devices are still acquired and a resync would drain them for nothing. */
    {
        focus_guard_inputs_t first = make(false, false, true, false, true, true);
        unsigned             f = focus_guard_actions(&first);

        ut_check(f == FOCUS_ACTION_CONFINE, "the first focused tick only confines");
    }

    /* The first tick with the game NOT in front (launched behind something): still nothing to the
     * engine, and nothing to release because nothing was taken. */
    {
        focus_guard_inputs_t first = make(false, false, false, false, true, true);
        ut_check(focus_guard_actions(&first) == FOCUS_ACTION_NONE,
              "the first background tick does nothing");
    }

    /* Confinement off, re-acquire on: the Alt-Tab repair works on its own. */
    {
        focus_guard_inputs_t back = make(true, false, true, false, false, true);
        unsigned             in = focus_guard_actions(&back);

        ut_check(in == FOCUS_ACTION_ACQUIRE,
              "the input repair works with the confinement switched off");
    }

    /* Confinement on, re-acquire off: the pointer repair works on its own. */
    {
        focus_guard_inputs_t away = make(true, true, false, true, true, false);
        unsigned             out = focus_guard_actions(&away);

        ut_check(out == FOCUS_ACTION_RELEASE,
              "the pointer repair works with the input repair switched off");
    }

    ut_check(focus_guard_actions(NULL) == FOCUS_ACTION_NONE, "a null argument does nothing");
}

/* ============================================================================================ *
 * A whole session, driven the way the frame hook drives it: the caller's own `confining` flag is
 * carried from tick to tick, so a rule that only looks correct one step at a time still cannot
 * leave a clip standing.
 * ============================================================================================ */
static void test_a_session(void)
{
    static const bool focus_sequence[] = {
        true, true, true,          /* playing */
        false, false, false, false,/* Alt-Tab: the task switcher, then another application */
        true, true,                /* back */
        false,                     /* the Win key */
        false, false,
        true,                      /* back again */
        false,                     /* minimised */
        true,                      /* restored */
        true
    };
    bool confining = false;
    bool had_focus = false;
    bool known = false;
    int  leaked = 0;
    size_t step;

    for (step = 0; step < sizeof(focus_sequence) / sizeof(focus_sequence[0]); ++step) {
        focus_guard_inputs_t inputs = make(known, had_focus, focus_sequence[step],
                                           confining, true, true);
        unsigned actions = focus_guard_actions(&inputs);

        if ((actions & FOCUS_ACTION_RELEASE) != 0) {
            confining = false;
        }
        if ((actions & FOCUS_ACTION_CONFINE) != 0) {
            confining = true;
        }
        if (!focus_sequence[step] && confining) {
            ++leaked;
        }
        had_focus = focus_sequence[step];
        known = true;
    }

    ut_check(leaked == 0, "no frame of the session ends with the pointer confined and no foreground");
    ut_check(confining, "the session ends focused, so it ends confined");
}

int main(void)
{
    test_every_combination();
    test_the_alt_tab();
    test_the_steady_states();
    test_the_awkward_paths();
    test_a_session();

    return ut_summary("focus_guard");
}
