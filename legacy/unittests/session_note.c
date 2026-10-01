/* session_note.c: the note that says a session is running, published and read in one process.
 *
 * The channel is the same one the appearance note uses and is testable here for the same reason:
 * both sides are DLLs in one process, so a test that publishes and then reads is not a simulation
 * of the real case, it is the real case with one process doing both halves.
 *
 * What is worth pinning is not the round trip, it is the two answers that decide whether a panel
 * locks a row: a read before anybody published must say so rather than inventing "no session",
 * and a record of another shape must be refused rather than read field by field.
 */
#include "unittest.h"

#include "common/session_note.h"
#include "common/shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_nothing_published_yet(void)
{
    session_note_t note;

    ut_section("before anybody has published, the read refuses rather than answering");

    note.running = true;
    note.is_host = true;
    ut_check(!session_note_read(&note),
             "a read with no publisher is a refusal, not a session that is not running");
    ut_check(note.running && note.is_host,
             "and it leaves the caller's own values alone, so a lost race cannot unlock a row");
}

/* Before the first read that finds the note, the operating system is asked at most once per retry
 * time. This runs before anything in this process has published, which is the single player case:
 * nobody ever files the note there, and a reader on the frame path must not pay a failed lookup
 * per frame for it. */
static void check_a_missing_note_is_remembered(void)
{
    session_note_t said;

    ut_section("a missing note is remembered, so a reader on the frame path "
               "does not ask every frame");

    ut_check(!session_note_input_held(5000u), "no note filed: not held");

    memset(&said, 0, sizeof said);
    said.running    = true;
    said.input_held = true;
    ut_check(session_note_publish(&said), "a hold is published after that first look");
    ut_check(!session_note_input_held(5000u + SESSION_NOTE_RETRY_MS - 1u),
             "and inside the retry time the reader does not look again");
    ut_check(session_note_input_held(5000u + SESSION_NOTE_RETRY_MS),
             "once it has passed it looks, finds the hold");
    ut_check(session_note_input_held(5000u + SESSION_NOTE_RETRY_MS + 1u),
             "and from then on every read is a copy out of memory");

    said.input_held = false;
    ut_check(session_note_publish(&said) && !session_note_input_held(0u),
             "a released hold reads released at once, whatever the clock says");
}

static void check_the_round_trip(void)
{
    session_note_t said;
    session_note_t heard;

    ut_section("what one side publishes is what the other side reads");

    memset(&said, 0, sizeof said);
    said.running = true;
    said.is_host = true;
    ut_check(session_note_publish(&said), "a running host is published");
    memset(&heard, 0, sizeof heard);
    ut_check(session_note_read(&heard), "and read back");
    ut_check(heard.running && heard.is_host, "as a running host");

    said.running = true;
    said.is_host = false;
    ut_check(session_note_publish(&said) && session_note_read(&heard),
             "a client publishes and reads too");
    ut_check(heard.running && !heard.is_host, "as a session that is not this machine's");

    said.running = false;
    said.is_host = true;
    ut_check(session_note_publish(&said) && session_note_read(&heard),
             "and an ended session is published over it");
    ut_check(!heard.running && !heard.is_host,
             "with the host bit cleared: a host that is not running is not a host");
}

/* The hold rides the byte the record always carried as reserved: same size, same version. */
static void check_the_input_hold(void)
{
    session_note_t said;
    session_note_t heard;

    ut_section("the input hold rides the record's reserved byte, and only inside a session");

    memset(&said, 0, sizeof said);
    said.running    = true;
    said.is_host    = false;
    said.input_held = true;
    memset(&heard, 0, sizeof heard);
    ut_check(session_note_publish(&said) && session_note_read(&heard),
             "a client whose pause menu holds its input publishes and reads");
    ut_check(heard.running && !heard.is_host && heard.input_held,
             "as a running client with its input held");

    said.running = false;
    ut_check(session_note_publish(&said) && session_note_read(&heard),
             "a hold published with no session under it");
    ut_check(!heard.input_held,
             "reads as free: a hold outside a session is not a hold");

    said.running    = true;
    said.input_held = false;
    ut_check(session_note_publish(&said) && session_note_read(&heard) && !heard.input_held,
             "and a released hold reads released");
}

static void check_another_shape_is_refused(void)
{
    session_note_t heard;
    uint8_t        foreign[16];

    ut_section("a record this build does not know is refused, not read field by field");

    memset(foreign, 0xAB, sizeof foreign);
    ut_check(shared_note_publish(SESSION_NOTE_NAME, foreign, sizeof foreign),
             "something else is filed under the same name");
    memset(&heard, 0, sizeof heard);
    ut_check(!session_note_read(&heard), "and the reader refuses it");
    ut_check(!heard.running, "without having written anything into the caller's record");
    ut_check(session_note_unreadable(),
             "and can say it was a note of another shape rather than no note at all");

    ut_check(session_note_publish(&(session_note_t){.running = true}) && session_note_read(&heard),
             "a note of this build's shape is read again");
    ut_check(!session_note_unreadable(), "and the flag falls with it");
}

int main(void)
{
    check_nothing_published_yet();
    check_a_missing_note_is_remembered();
    check_the_round_trip();
    check_the_input_hold();
    check_another_shape_is_refused();
    return ut_summary("session_note");
}
