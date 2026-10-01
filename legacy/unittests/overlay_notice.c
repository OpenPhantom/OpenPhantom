/* overlay_notice.c: the sentence the panel puts up when it turns something down.
 *
 * Two things are worth pinning here and the second is the whole point of the module.
 *
 * The first is ordinary: the last sentence said is the one standing, an empty one says nothing,
 * and an action clears what stood.
 *
 * The second is that nothing here reads a clock. The band costs height while it stands, so every
 * row under it sits one band lower; a band that expired by itself would move the row under the
 * pointer without the pointer moving, and in the entity spawner group the row under "Remove
 * spawned entities" is "Remove every player's spawned entities", which cannot be undone. The
 * section below waits real wall clock time and reads the sentence again. An expiry longer than
 * that wait would slip past it, so the wait is not the whole guard: the guard is that this file
 * includes nothing that can tell the time, and the wait is what makes an added one fail here
 * rather than in somebody's game.
 */
#include "unittest.h"

#include "overlay_notice.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

/* Long enough to catch the timer somebody adds "just to be safe", which is the one this module
 * exists to refuse. Paid once. */
#define WAITED_MS 2500u

static void test_what_stands(void)
{
    ut_section("the sentence that stands, and the one that replaces it");
    overlay_notice_forget();
    ut_check(overlay_notice_text() == NULL, "nothing stands until something is turned down");

    overlay_notice_say("Refused: 240 is not a value that row takes");
    ut_check(overlay_notice_text() != NULL &&
                 strcmp(overlay_notice_text(), "Refused: 240 is not a value that row takes") == 0,
             "a refusal stands, word for word");

    overlay_notice_say("Refused: the panel drives itself with that key");
    ut_check(strcmp(overlay_notice_text(), "Refused: the panel drives itself with that key") == 0,
             "and a second refusal replaces the first: two inside one action are one action's "
             "answer, and the later word about it is the one to show");

    overlay_notice_say(NULL);
    overlay_notice_say("");
    ut_check(strcmp(overlay_notice_text(), "Refused: the panel drives itself with that key") == 0,
             "nothing said clears nothing, so a caller with no sentence cannot silently take "
             "one down");
}

static void test_an_action_clears_it(void)
{
    ut_section("an action takes it down, and the same action can put one back");
    overlay_notice_forget();
    overlay_notice_say("Refused: that key is taken by the panel or by a mode");

    overlay_notice_act();
    ut_check(overlay_notice_text() == NULL, "the next click or key takes it down");

    /* The order the doors use: clear first, act second. An action that is itself turned down puts
     * its own sentence up afterwards, and that sentence stands. */
    overlay_notice_act();
    overlay_notice_say("The window shape could not be saved to the settings file");
    ut_check(overlay_notice_text() != NULL,
             "and a refusal from that very action stands, because the clearing happens before the "
             "action runs and not after it");
}

static void test_no_clock_takes_it(void)
{
    ut_section("no clock takes it down");
    overlay_notice_forget();
    overlay_notice_say("Refused: 240 is not a value that row takes");

    /* Read it across the wait as well as after it: a text() that expired on its own would be
     * caught here whether the expiry is read on the first call or the last. */
    {
        uint32_t spun;

        for (spun = 0; spun < 5u; ++spun) {
            Sleep(WAITED_MS / 5u);
            ut_check(overlay_notice_text() != NULL, "it is still standing");
        }
    }
    ut_check(overlay_notice_text() != NULL &&
                 strcmp(overlay_notice_text(), "Refused: 240 is not a value that row takes") == 0,
             "after two and a half seconds of doing nothing it still says exactly what it said: "
             "the band goes on an action and on nothing else");

    overlay_notice_act();
    ut_check(overlay_notice_text() == NULL, "and it is the action that takes it, as it always was");
}

/* The cut. The band is the panel's width and the panel's width is bounded, so a sentence written
 * too long cannot be allowed to reach the drawing: the engine clips nothing, and text past the
 * right edge lands on the game behind the panel. */
static void test_a_sentence_too_long_is_cut(void)
{
    char     asked[OVERLAY_NOTICE_CHARS * 2u];
    char     note[200];
    uint32_t i;

    ut_section("a sentence longer than the band is cut, not run off the edge");
    overlay_notice_forget();
    for (i = 0; i + 1u < sizeof asked; ++i) {
        asked[i] = 'x';
    }
    asked[sizeof asked - 1u] = '\0';

    overlay_notice_say(asked);
    ut_check(overlay_notice_text() != NULL, "it stands");
    text_format(note, sizeof note, "the band holds %u characters and the sentence came back %u",
                (unsigned)OVERLAY_NOTICE_CHARS, (unsigned)strlen(overlay_notice_text()));
    note[sizeof note - 1] = '\0';
    ut_check(strlen(overlay_notice_text()) == OVERLAY_NOTICE_CHARS, note);
    ut_check(strcmp(overlay_notice_text() + OVERLAY_NOTICE_CHARS - 2u, "..") == 0,
             "and it ends in the two dots the drawing puts on a label it had to shorten, so a "
             "sentence that was cut says it was cut");

    /* One character short of the budget is left exactly as it was written. */
    overlay_notice_forget();
    asked[OVERLAY_NOTICE_CHARS - 1u] = '\0';
    overlay_notice_say(asked);
    ut_check(overlay_notice_text() != NULL &&
                 strlen(overlay_notice_text()) == OVERLAY_NOTICE_CHARS - 1u &&
                 strchr(overlay_notice_text(), '.') == NULL,
             "a sentence that fits is left alone, dots and all");

    overlay_notice_forget();
    ut_check(overlay_notice_text() == NULL, "and closing the panel forgets it");
}

/* The one sentence that is not a refusal. The chat key row says, after a binding, that a running
 * session takes the key within a second, and in the warning colour a player read that as the
 * binding having failed. The kind travels with the sentence, so the painter colours by what the
 * sentence is and not by spelling it back apart, and whatever replaces it brings its own kind. */
static void test_a_confirmation_is_not_a_refusal(void)
{
    ut_section("a confirmation stands like a refusal and is not one");
    overlay_notice_forget();
    ut_check(!overlay_notice_confirms(), "nothing standing confirms nothing");

    overlay_notice_confirm("Saved: in a session it works within a second");
    ut_check(overlay_notice_text() != NULL &&
                 strcmp(overlay_notice_text(), "Saved: in a session it works within a second") == 0,
             "a confirmation stands word for word, in the same band");
    ut_check(overlay_notice_confirms(), "and says it is a confirmation");

    overlay_notice_say("Refused: that key already does something else");
    ut_check(!overlay_notice_confirms(),
             "a refusal after it replaces the sentence and the kind with it");

    overlay_notice_confirm("Saved: in a session it works within a second");
    overlay_notice_confirm(NULL);
    overlay_notice_confirm("");
    ut_check(overlay_notice_text() != NULL && overlay_notice_confirms(),
             "nothing confirmed clears nothing, the same rule a refusal has");

    overlay_notice_act();
    ut_check(overlay_notice_text() == NULL && !overlay_notice_confirms(),
             "and the next action takes it down like any other, kind and all");

    overlay_notice_confirm("Saved: in a session it works within a second");
    overlay_notice_forget();
    ut_check(!overlay_notice_confirms(), "closing the panel forgets it as well");
}

int main(void)
{
    test_what_stands();
    test_an_action_clears_it();
    test_no_clock_takes_it();
    test_a_sentence_too_long_is_cut();
    test_a_confirmation_is_not_a_refusal();
    return ut_summary("the refusal band's sentence");
}
