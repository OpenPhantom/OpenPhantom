/* player_help_row.c: the panel's end of the two buttons under Multiplayer.
 *
 * The buttons are carried out by another DLL, so what can be held here is everything on this side
 * of the two records: the reading of the answer and how seldom it asks while there is none, when
 * a button is offered and the word it carries when it is not, what a press files, and every
 * sentence the line under the buttons can read, each against the room that line has.
 *
 * The multiplayer is stood in for by the records themselves. This program files the answer the
 * way a session's multiplayer would and reads the ask back the way its reader would, through the
 * real common/player_help_note and the real session note. The clock is handed in, so the spacing
 * of the reads is driven and not waited for.
 *
 * The sections run in order and hand state on to each other, and the first has to stay first: it
 * is the only one that sees a process in which no answer has ever been filed.
 */
#include "unittest.h"

#include "input_freeze.h"
#include "overlay_model.h"
#include "overlay_multiplayer.h"
#include "overlay_reason.h"
#include "overlay_stubs.h"
#include "player_help_row.h"
#include "session_lock.h"

#include "common/player_help_note.h"
#include "common/session_note.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define REPAIR   ((uint8_t)PLAYER_HELP_KIND_REPAIR)
#define TELEPORT ((uint8_t)PLAYER_HELP_KIND_TELEPORT)
#define BOTH     ((uint8_t)(PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR | \
                            PLAYER_HELP_READY_CAN_TELEPORT))

/* A clock past every spacing, for the sections that are not about the spacing. */
static uint32_t now_ms = 100000u;

/* The session note as the multiplayer publishes it, and the lock's reading of it, which is what
 * the buttons ask. */
static void session(bool running, bool host)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running = running;
    note.is_host = host;
    ut_check(session_note_publish(&note),
             !running ? "no session runs" : host ? "a session runs, hosted here"
                                                 : "a session runs, with this machine a client");
    session_lock_refresh();
}

/* The answer record as the multiplayer files it. */
static void answer(uint8_t ready, uint32_t serial, uint8_t kind, uint8_t outcome, uint8_t reason)
{
    player_help_answer_t record;

    memset(&record, 0, sizeof record);
    record.version = PLAYER_HELP_NOTE_VERSION;
    record.ready   = ready;
    record.serial  = serial;
    record.kind    = kind;
    record.outcome = outcome;
    record.reason  = reason;
    if (!player_help_answer_publish(&record)) {
        ut_check(false, "an answer this program files is one the channel takes");
    }
}

/* One frame: the reading, a little later than the last one. */
static void frame(void)
{
    now_ms += 16u;
    player_help_row_tick(now_ms);
}

static bool offered(uint8_t kind)
{
    return player_help_row_offered(kind, NULL);
}

static uint32_t why_not(uint8_t kind)
{
    uint32_t reason = 0xFFFFFFFFu;

    (void)player_help_row_offered(kind, &reason);
    return reason;
}

/* The serial of the ask on file, 0 while none is. */
static uint32_t asked_serial(void)
{
    player_help_ask_t ask;

    return player_help_ask_read(&ask) ? ask.serial : 0u;
}

/* Whether the line under the buttons reads exactly `words`, through the group's own row. */
static bool last_line_is(const char *words, bool warns)
{
    overlay_row_t row;
    char          said[PLAYER_HELP_ROW_WORDS_MAX];
    bool          refused = false;

    if (overlay_multiplayer_row_count() != OVERLAY_MULTIPLAYER_ROW_COUNT ||
        !player_help_row_last(said, sizeof said, &refused) || strcmp(said, words) != 0) {
        return false;
    }
    overlay_multiplayer_row((uint32_t)OVERLAY_MULTIPLAYER_LAST, false, &row);
    return row.kind == OVERLAY_ROW_INFO && !row.available && row.warn == warns &&
           refused == warns && strncmp(row.label, "    Last: ", 10) == 0 &&
           strcmp(row.label + 10, words) == 0;
}

/* The answer record is read only in a session, and until it has been found once the operating
 * system is asked at most once a second: a name nobody filed is a failed lookup each time, and a
 * session whose multiplayer is of another build never files it. */
static void test_the_reading_is_spaced_out(void)
{
    ut_section("the reading of the answer: only in a session, and seldom while there is none");
    ut_check(overlay_multiplayer_row_count() == (uint32_t)OVERLAY_MULTIPLAYER_LAST &&
                 !player_help_row_last(NULL, 0u, NULL),
             "before any press the group has three rows and no line about a last press");
    ut_check(!offered(REPAIR) && why_not(REPAIR) == (uint32_t)OVERLAY_REASON_NEEDS_SESSION &&
                 !offered(TELEPORT) &&
                 why_not(TELEPORT) == (uint32_t)OVERLAY_REASON_NEEDS_SESSION,
             "with no session both buttons are greyed for want of one");

    session(true, false);
    /* The first look of the session, just before the counter of milliseconds wraps. */
    player_help_row_tick(0xFFFFFF00u);
    ut_check(!offered(REPAIR) && why_not(REPAIR) == (uint32_t)OVERLAY_REASON_NONE,
             "in a session whose multiplayer has filed no answer the button is greyed and names "
             "no reason, which the panel writes as n/a");

    answer(BOTH, 0u, PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE, PLAYER_HELP_REASON_NONE);
    player_help_row_tick(0x00000100u);
    ut_check(!offered(REPAIR),
             "512 ms after a miss the record is not asked for again, across the wrap of the "
             "clock, so an answer filed in between is not seen yet");
    player_help_row_tick(0x000002E7u);
    ut_check(!offered(REPAIR), "nor at 999 ms");
    player_help_row_tick(0x000002E8u);
    ut_check(offered(REPAIR) && offered(TELEPORT),
             "at 1000 ms it is asked for again, found, and both buttons are offered");

    answer((uint8_t)(PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR), 0u,
           PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE, PLAYER_HELP_REASON_NONE);
    player_help_row_tick(0x000002E9u);
    ut_check(offered(REPAIR) && !offered(TELEPORT) &&
                 why_not(TELEPORT) == (uint32_t)OVERLAY_REASON_NONE,
             "once found, every frame reads it: a multiplayer that can repair and cannot "
             "teleport is seen one millisecond later, each button under its own bit");

    session(false, false);
    answer(BOTH, 0u, PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE, PLAYER_HELP_REASON_NONE);
    frame();
    session(true, false);
    ut_check(!offered(TELEPORT),
             "with no session the record is not read at all: what was filed meanwhile is not "
             "known when the next session begins");
    frame();
    ut_check(offered(TELEPORT), "and is read in the first frame of it");
}

static void test_who_is_offered_what(void)
{
    ut_section("who is offered which button");
    session(true, true);
    ut_check(offered(REPAIR), "the host is offered Repair lock: a scene holds the host first");
    ut_check(!offered(TELEPORT) && why_not(TELEPORT) == (uint32_t)OVERLAY_REASON_IS_HOST,
             "and is not offered Teleport to host, because it is the host");
    answer(0u, 0u, PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(why_not(TELEPORT) == (uint32_t)OVERLAY_REASON_IS_HOST &&
                 why_not(REPAIR) == (uint32_t)OVERLAY_REASON_NONE,
             "with nobody listening the host's teleport still says host, the answer a player can "
             "act on, and the repair reads n/a");
    ut_check(!offered(PLAYER_HELP_KIND_NONE) && !offered((uint8_t)(TELEPORT + 1u)),
             "a kind that is no button is offered to nobody");

    session(true, false);
    answer(BOTH, 0u, PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(offered(REPAIR) && offered(TELEPORT), "a client of a listening session gets both");
}

/* What a press files, and that it asks the panel to close first. */
static void test_a_press(void)
{
    player_help_ask_t ask;
    const uint32_t    before = asked_serial();
    uint32_t          closes = overlay_stubs_closes;

    ut_section("a press: the panel closes, and the next serial is filed");
    ut_check(player_help_row_press(REPAIR), "a press of an offered button is accepted");
    ut_check(overlay_stubs_closes == closes + 1u, "it asks the panel to close");
    ut_check(player_help_ask_read(&ask) && ask.version == PLAYER_HELP_NOTE_VERSION &&
                 ask.kind == PLAYER_HELP_KIND_REPAIR && ask.serial == before + 1u &&
                 ask.flags == 0u,
             "and files a repair under the serial after the last one, with no flag: nothing of "
             "the overlay holds the player once the panel is shut");
    ut_check(last_line_is("repair lock asked, no answer yet", false),
             "the group has its fourth row now, and it says the press is not answered yet");

    /* The free camera in flight with the world running holds the player through its own holder,
     * which closing the panel does not let go. */
    input_freeze_hold(INPUT_FREEZE_FREE_CAMERA, true);
    ut_check(input_freeze_holders() == (uint32_t)INPUT_FREEZE_FREE_CAMERA,
             "the free camera's hold on the player is readable");
    ut_check(player_help_row_press(TELEPORT) && player_help_ask_read(&ask) &&
                 ask.kind == PLAYER_HELP_KIND_TELEPORT && ask.serial == before + 2u &&
                 ask.flags == PLAYER_HELP_ASK_F_OVERLAY_HOLDS,
             "a press under that hold files the next serial with the flag that says the overlay "
             "still holds the player");
    input_freeze_hold(INPUT_FREEZE_FREE_CAMERA, false);
    ut_check(input_freeze_holders() == 0u, "and when the camera lets go nobody holds");

    session(true, true);
    closes = overlay_stubs_closes;
    ut_check(!player_help_row_press(TELEPORT) && asked_serial() == before + 2u &&
                 overlay_stubs_closes == closes,
             "a press of a button that is not offered files nothing and closes nothing: the "
             "host's teleport is refused here, not by the session");
    ut_check(!player_help_row_press(PLAYER_HELP_KIND_NONE) && asked_serial() == before + 2u,
             "and so is a press of no button at all");
    session(true, false);
}

/* Which answer counts as the answer to the last press, and what the buttons and the line under
 * them make of it. */
static void test_the_answer_to_a_press(void)
{
    uint32_t serial;

    ut_section("the answer to the last press");
    ut_check(player_help_row_press(REPAIR), "a repair is pressed");
    serial = asked_serial();

    answer(BOTH, serial - 1u, REPAIR, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(last_line_is("repair lock asked, no answer yet", false),
             "an answer to the press before it is no answer to this one");
    answer(BOTH, serial, TELEPORT, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(last_line_is("repair lock asked, no answer yet", false),
             "nor is one that names this serial and the other button");
    answer(BOTH, serial, REPAIR, PLAYER_HELP_OUTCOME_NONE, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(last_line_is("repair lock asked, no answer yet", false),
             "nor one that names it and says nothing about it yet");

    answer(BOTH, serial, REPAIR, PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(last_line_is("repair lock found nothing holding you", false),
             "a repair that found nothing says so in plain grey: it is the ordinary answer and "
             "no refusal");
    answer(BOTH, serial, REPAIR, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(last_line_is("repair lock let go of what held you", false),
             "and one that released something says that");

    ut_check(player_help_row_press(TELEPORT), "a teleport is pressed");
    serial = asked_serial();
    ut_check(last_line_is("teleport asked, no answer yet", false) && offered(TELEPORT),
             "the line is the new press's at once, and the button stays offered until the "
             "session has taken the ask");
    answer(BOTH, serial, TELEPORT, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(!offered(TELEPORT) && why_not(TELEPORT) == (uint32_t)OVERLAY_REASON_UNDER_WAY &&
                 strcmp(overlay_reason_word(why_not(TELEPORT)), "running") == 0,
             "while the session works on the teleport its button is greyed and reads running");
    ut_check(offered(REPAIR) && last_line_is("teleport is on its way", false),
             "the other button stays offered, and the line says the teleport is on its way");
    answer(BOTH, serial, TELEPORT, PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_ELSEWHERE);
    frame();
    ut_check(offered(TELEPORT) && last_line_is("no teleport, host in another level", true),
             "a refusal ends it: the button is offered again and the line says why, as a "
             "refusal, in the warning colour");

    ut_check(player_help_row_press(TELEPORT), "a second teleport is pressed");
    serial = asked_serial();
    answer(BOTH, serial, TELEPORT, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
    frame();
    ut_check(!offered(TELEPORT) && player_help_row_press(REPAIR) && offered(TELEPORT),
             "a repair pressed while a teleport is open takes the last press over, so the open "
             "answer greys nothing any more: the repair is the way out of a teleport that waits");

    session(false, false);
    ut_check(!offered(REPAIR) && why_not(REPAIR) == (uint32_t)OVERLAY_REASON_NEEDS_SESSION &&
                 !player_help_row_press(REPAIR) && asked_serial() == serial + 1u,
             "when the session ends the buttons are greyed and a press files nothing");
    ut_check(last_line_is("repair lock asked, no answer yet", false),
             "the line stays, since it is about the last press and not about the session");
}

/* Every sentence against the room the line has, which is a row's label less its prefix. A label
 * that is too long is cut without a word, so the only place a sentence can be held to the room
 * is here, before it is cut. */
static void test_every_sentence_fits(void)
{
    static const struct { uint8_t kind; uint8_t outcome; const char *words; } FIXED[] = {
        { REPAIR,   PLAYER_HELP_OUTCOME_NONE,    "repair lock asked, no answer yet" },
        { REPAIR,   PLAYER_HELP_OUTCOME_OPEN,    "repair lock is still at work" },
        { REPAIR,   PLAYER_HELP_OUTCOME_DONE,    "repair lock let go of what held you" },
        { REPAIR,   PLAYER_HELP_OUTCOME_NOTHING, "repair lock found nothing holding you" },
        { REPAIR,   PLAYER_HELP_OUTCOME_REFUSED, "repair lock was refused" },
        { TELEPORT, PLAYER_HELP_OUTCOME_NONE,    "teleport asked, no answer yet" },
        { TELEPORT, PLAYER_HELP_OUTCOME_OPEN,    "teleport is on its way" },
        { TELEPORT, PLAYER_HELP_OUTCOME_DONE,    "teleport put you beside the host" },
        { TELEPORT, PLAYER_HELP_OUTCOME_NOTHING, "teleport had nothing to do" },
        { TELEPORT, PLAYER_HELP_OUTCOME_REFUSED, "teleport was refused" }
    };
    static const uint8_t KINDS[] = { REPAIR, TELEPORT };
    char     words[64];
    char     other[64];
    char     bare[64];
    uint32_t i;
    uint32_t k;
    uint8_t  reason;

    ut_section("every sentence of the line, against the 37 characters it has");
    for (i = 0; i < sizeof FIXED / sizeof FIXED[0]; ++i) {
        player_help_row_sentence(FIXED[i].kind, FIXED[i].outcome, PLAYER_HELP_REASON_NONE, words,
                                 sizeof words);
        ut_checkf(strcmp(words, FIXED[i].words) == 0 &&
                      strlen(words) < PLAYER_HELP_ROW_WORDS_MAX,
                  "button %u, outcome %u with no reason reads \"%s\", %u characters",
                  (unsigned)FIXED[i].kind, (unsigned)FIXED[i].outcome, words,
                  (unsigned)strlen(words));
    }
    for (k = 0; k < sizeof KINDS / sizeof KINDS[0]; ++k) {
        player_help_row_sentence(KINDS[k], PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NONE,
                                 bare, sizeof bare);
        for (reason = PLAYER_HELP_REASON_NONE + 1; reason < PLAYER_HELP_REASON_COUNT; ++reason) {
            player_help_row_sentence(KINDS[k], PLAYER_HELP_OUTCOME_REFUSED, reason, words,
                                     sizeof words);
            player_help_row_sentence(KINDS[k], PLAYER_HELP_OUTCOME_NOTHING, reason, other,
                                     sizeof other);
            ut_checkf(strcmp(words, bare) != 0 && strlen(words) < PLAYER_HELP_ROW_WORDS_MAX &&
                          strcmp(words, other) == 0,
                      "button %u, reason %u has a sentence of its own that fits, \"%s\", %u "
                      "characters, and reads the same for a press with nothing to do",
                      (unsigned)KINDS[k], (unsigned)reason, words, (unsigned)strlen(words));
            if (reason > PLAYER_HELP_REASON_NONE + 1) {
                player_help_row_sentence(KINDS[k], PLAYER_HELP_OUTCOME_REFUSED,
                                         (uint8_t)(reason - 1u), other, sizeof other);
                ut_checkf(strcmp(words, other) != 0,
                          "button %u, reason %u does not read like the reason before it",
                          (unsigned)KINDS[k], (unsigned)reason);
            }
        }
    }
    player_help_row_sentence(REPAIR, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE, bare,
                             sizeof bare);
    for (reason = PLAYER_HELP_REASON_NONE + 1; reason < PLAYER_HELP_REASON_COUNT; ++reason) {
        player_help_row_sentence(REPAIR, PLAYER_HELP_OUTCOME_DONE, reason, words, sizeof words);
        player_help_row_sentence(REPAIR, PLAYER_HELP_OUTCOME_REFUSED, reason, other,
                                 sizeof other);
        ut_checkf(strcmp(words, bare) != 0 && strcmp(words, other) != 0 &&
                      strlen(words) < PLAYER_HELP_ROW_WORDS_MAX,
                  "a repair done that left something standing for reason %u says so and fits, "
                  "\"%s\", %u characters, and does not read as a refusal",
                  (unsigned)reason, words, (unsigned)strlen(words));
    }
    player_help_row_sentence(REPAIR, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_MENU_OPEN,
                             words, sizeof words);
    ut_check(strcmp(words, "freed, but a menu is open") == 0,
             "a repair done under an open menu reads \"freed, but a menu is open\"");
    player_help_row_sentence(TELEPORT, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_MENU_OPEN,
                             words, sizeof words);
    ut_check(strcmp(words, "teleport put you beside the host") == 0,
             "a teleport that landed has no reason to give");
    player_help_row_sentence(TELEPORT, PLAYER_HELP_OUTCOME_REFUSED,
                             (uint8_t)PLAYER_HELP_REASON_COUNT, words, sizeof words);
    ut_check(strcmp(words, "teleport was refused") == 0,
             "a reason past the last one reads as a refusal with no reason");
    player_help_row_sentence(TELEPORT, (uint8_t)(PLAYER_HELP_OUTCOME_REFUSED + 1u),
                             PLAYER_HELP_REASON_NONE, words, sizeof words);
    ut_check(strcmp(words, "teleport asked, no answer yet") == 0,
             "and an outcome past the last one as a press nobody answered");
    memset(words, 'x', sizeof words);
    player_help_row_sentence(REPAIR, PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE, words,
                             8u);
    ut_check(strcmp(words, "repair ") == 0, "a buffer too small is filled and terminated");
}

/* The words the two buttons put into a chip. overlay_reason.h holds every word under ten
 * characters, because a chip is paid for out of the name beside it. */
static void test_the_words_in_the_chip(void)
{
    uint32_t reason;

    ut_section("the words a greyed button carries");
    for (reason = 0; reason < (uint32_t)OVERLAY_REASON_COUNT; ++reason) {
        ut_checkf(strlen(overlay_reason_word(reason)) < 10u,
                  "reason %u reads \"%s\", under ten characters", (unsigned)reason,
                  overlay_reason_word(reason));
    }
    ut_check(strcmp(overlay_reason_word((uint32_t)OVERLAY_REASON_NEEDS_SESSION), "MP only") == 0 &&
                 strcmp(overlay_reason_sentence((uint32_t)OVERLAY_REASON_NEEDS_SESSION),
                        "Only in a multiplayer session") == 0,
             "no session reads MP only, with the sentence that spells it out");
    ut_check(strcmp(overlay_reason_word((uint32_t)OVERLAY_REASON_IS_HOST), "host") == 0 &&
                 strcmp(overlay_reason_sentence((uint32_t)OVERLAY_REASON_IS_HOST),
                        "You are the host") == 0,
             "the host's teleport reads host, with its sentence");
    ut_check(strcmp(overlay_reason_word((uint32_t)OVERLAY_REASON_UNDER_WAY), "running") == 0 &&
                 overlay_reason_sentence((uint32_t)OVERLAY_REASON_UNDER_WAY) == NULL,
             "a press being worked on reads running, and the line under the buttons is its "
             "sentence");
    ut_check(!overlay_reason_is_final((uint32_t)OVERLAY_REASON_NEEDS_SESSION) &&
                 !overlay_reason_is_final((uint32_t)OVERLAY_REASON_IS_HOST) &&
                 !overlay_reason_is_final((uint32_t)OVERLAY_REASON_UNDER_WAY),
             "none of the three outranks the session lock, which takes nothing of this group");
}

/* What the overlay files when it loads. Last, because it files over whatever the presses above
 * left, which a load never does: at load nothing has been pressed. */
static void test_the_empty_ask_at_load(void)
{
    player_help_ask_t ask;

    ut_section("the empty ask the overlay files when it loads");
    player_help_row_install();
    ut_check(player_help_ask_read(&ask) && ask.version == PLAYER_HELP_NOTE_VERSION &&
                 ask.kind == PLAYER_HELP_KIND_NONE && ask.serial == 0u && ask.flags == 0u,
             "kind NONE and serial 0: a note the session's reader finds at its first look, and "
             "a mark of 0 for it to hold the first press against");
}

int main(void)
{
    test_the_reading_is_spaced_out();
    test_who_is_offered_what();
    test_a_press();
    test_the_answer_to_a_press();
    test_every_sentence_fits();
    test_the_words_in_the_chip();
    test_the_empty_ask_at_load();
    return ut_summary("the two buttons under Multiplayer");
}
