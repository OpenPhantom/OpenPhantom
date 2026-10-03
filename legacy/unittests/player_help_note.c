/* The two records between the developer menu's buttons and the multiplayer: their shape, what is
 * refused on the way in and on the way out, both directions in one process, and the comparison a
 * reader holds a press against its mark with.
 *
 * One process is the right place for it, because that is the situation the records serve: the two
 * DLLs live in one process and the channel between them is the operating system. */
#include "unittest.h"

#include "common/player_help_note.h"
#include "common/shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A value no field of a sound record holds, so a record that was touched shows it. */
#define UNTOUCHED 0xA5

static bool all_untouched(const void *record, size_t size)
{
    const uint8_t *at = (const uint8_t *)record;
    size_t         i;

    for (i = 0; i < size; ++i) {
        if (at[i] != UNTOUCHED) {
            return false;
        }
    }
    return true;
}

static player_help_ask_t a_press(uint8_t kind, uint32_t serial)
{
    player_help_ask_t ask;

    memset(&ask, 0, sizeof ask);
    ask.version = PLAYER_HELP_NOTE_VERSION;
    ask.kind    = kind;
    ask.serial  = serial;
    return ask;
}

static player_help_answer_t an_answer(uint8_t ready)
{
    player_help_answer_t answer;

    memset(&answer, 0, sizeof answer);
    answer.version = PLAYER_HELP_NOTE_VERSION;
    answer.ready   = ready;
    return answer;
}

static void check_the_shape(void)
{
    ut_section("the shape: two records of fixed size, no byte of padding in either");
    ut_check(sizeof(player_help_ask_t) == 8u, "an ask is 8 bytes");
    ut_check(offsetof(player_help_ask_t, version) == 0u &&
                 offsetof(player_help_ask_t, kind) == 2u &&
                 offsetof(player_help_ask_t, flags) == 3u &&
                 offsetof(player_help_ask_t, serial) == 4u,
             "the version at 0, the kind at 2, the flags at 3 and the serial at 4");
    ut_check(sizeof(player_help_answer_t) == 12u, "an answer is 12 bytes");
    ut_check(offsetof(player_help_answer_t, version) == 0u &&
                 offsetof(player_help_answer_t, ready) == 2u &&
                 offsetof(player_help_answer_t, outcome) == 3u &&
                 offsetof(player_help_answer_t, serial) == 4u &&
                 offsetof(player_help_answer_t, kind) == 8u &&
                 offsetof(player_help_answer_t, reason) == 9u &&
                 offsetof(player_help_answer_t, released) == 10u,
             "the version at 0, the ready bits at 2, the outcome at 3, the serial at 4, the kind "
             "at 8, the reason at 9 and what was released at 10");
    ut_check(sizeof(player_help_ask_t) <= SHARED_NOTE_BYTES &&
                 sizeof(player_help_answer_t) <= SHARED_NOTE_BYTES,
             "and both fit the shared note's payload");
    ut_check(PLAYER_HELP_NOTE_VERSION == 1u, "the version both sides were built against is 1");
    ut_check(shared_note_name_is_sound(PLAYER_HELP_ASK_NOTE_NAME) &&
                 shared_note_name_is_sound(PLAYER_HELP_ANSWER_NOTE_NAME) &&
                 strcmp(PLAYER_HELP_ASK_NOTE_NAME, PLAYER_HELP_ANSWER_NOTE_NAME) != 0,
             "the two names are ones the channel files, and they are two names");
}

static void check_nothing_before_a_publication(void)
{
    player_help_ask_t    ask;
    player_help_answer_t answer;

    ut_section("before anybody published");
    memset(&ask, UNTOUCHED, sizeof ask);
    memset(&answer, UNTOUCHED, sizeof answer);
    ut_check(!player_help_ask_read(&ask) && !player_help_answer_read(&answer),
             "there is nothing to read, which a reader takes as nobody listening");
    ut_check(all_untouched(&ask, sizeof ask) && all_untouched(&answer, sizeof answer),
             "and the caller's records are left alone");
    ut_check(!player_help_ask_read(NULL) && !player_help_answer_read(NULL),
             "nowhere to read into is refused");
}

static void check_what_an_ask_may_be(void)
{
    player_help_ask_t ask;

    ut_section("an ask: the empty one, or a press with its serial");
    ask = a_press(PLAYER_HELP_KIND_NONE, 0u);
    ut_check(player_help_ask_publish(&ask),
             "the empty ask the overlay files at load, kind NONE and serial 0, is published");
    ask = a_press(PLAYER_HELP_KIND_REPAIR, 1u);
    ut_check(player_help_ask_publish(&ask), "a repair with serial 1 is published");
    ask = a_press(PLAYER_HELP_KIND_TELEPORT, 0xFFFFFFFFu);
    ask.flags = PLAYER_HELP_ASK_F_OVERLAY_HOLDS;
    ut_check(player_help_ask_publish(&ask),
             "and so is a teleport with the highest serial and the overlay holding the player");

    ut_check(!player_help_ask_publish(NULL), "no record is refused");
    ask = a_press(PLAYER_HELP_KIND_REPAIR, 1u);
    ask.version = 0u;
    ut_check(!player_help_ask_publish(&ask),
             "a record whose version was never filled in is refused");
    ask.version = (uint16_t)(PLAYER_HELP_NOTE_VERSION + 1u);
    ut_check(!player_help_ask_publish(&ask), "and so is one of a later version");
    ask = a_press((uint8_t)(PLAYER_HELP_KIND_TELEPORT + 1), 1u);
    ut_check(!player_help_ask_publish(&ask), "a kind this build does not know is refused");
    ask = a_press(PLAYER_HELP_KIND_REPAIR, 1u);
    ask.flags = 0x02u;
    ut_check(!player_help_ask_publish(&ask), "and a flag it does not know");
    ask = a_press(PLAYER_HELP_KIND_REPAIR, 0u);
    ut_check(!player_help_ask_publish(&ask),
             "a press with serial 0 is refused: a reader could not tell it from no press");
    ask = a_press(PLAYER_HELP_KIND_NONE, 7u);
    ut_check(!player_help_ask_publish(&ask),
             "and a serial on the empty ask is refused: it would be a press of nothing");
    ask = a_press(PLAYER_HELP_KIND_NONE, 0u);
    ask.flags = PLAYER_HELP_ASK_F_OVERLAY_HOLDS;
    ut_check(!player_help_ask_publish(&ask), "the empty ask carries no flag either");
}

static void check_what_an_answer_may_be(void)
{
    player_help_answer_t answer;
    uint8_t              code;

    ut_section("an answer: ready bits, an outcome and a reason this build knows");
    answer = an_answer(0u);
    ut_check(player_help_answer_publish(&answer),
             "ready 0 with nothing answered, which is nobody listening, is published");
    answer = an_answer(PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR |
                       PLAYER_HELP_READY_CAN_TELEPORT);
    ut_check(player_help_answer_publish(&answer), "and so are all three ready bits");
    /* A reader that armed on a serial it found and has nothing to say about it yet. */
    answer.serial = 9u;
    ut_check(player_help_answer_publish(&answer),
             "an answer may name a serial with no outcome yet: which outcome goes with which "
             "serial is the writer's, and the ready bits it carries must still arrive");

    for (code = PLAYER_HELP_OUTCOME_NONE; code <= PLAYER_HELP_OUTCOME_REFUSED; ++code) {
        answer = an_answer(PLAYER_HELP_READY_LISTENING);
        answer.serial  = 3u;
        answer.kind    = PLAYER_HELP_KIND_TELEPORT;
        answer.outcome = code;
        ut_checkf(player_help_answer_publish(&answer), "outcome %u is published", (unsigned)code);
    }
    for (code = PLAYER_HELP_REASON_NONE; code < PLAYER_HELP_REASON_COUNT; ++code) {
        answer = an_answer(PLAYER_HELP_READY_LISTENING);
        answer.serial  = 3u;
        answer.kind    = PLAYER_HELP_KIND_TELEPORT;
        answer.outcome = PLAYER_HELP_OUTCOME_REFUSED;
        answer.reason  = code;
        ut_checkf(player_help_answer_publish(&answer), "reason %u is published", (unsigned)code);
    }

    ut_check(!player_help_answer_publish(NULL), "no record is refused");
    answer = an_answer(PLAYER_HELP_READY_LISTENING);
    answer.version = 0u;
    ut_check(!player_help_answer_publish(&answer),
             "a record whose version was never filled in is refused");
    answer = an_answer(0x08u);
    ut_check(!player_help_answer_publish(&answer), "a ready bit this build does not know");
    answer = an_answer(PLAYER_HELP_READY_LISTENING);
    answer.outcome = (uint8_t)(PLAYER_HELP_OUTCOME_REFUSED + 1);
    ut_check(!player_help_answer_publish(&answer), "an outcome past the last one");
    answer = an_answer(PLAYER_HELP_READY_LISTENING);
    answer.reason = (uint8_t)PLAYER_HELP_REASON_COUNT;
    ut_check(!player_help_answer_publish(&answer), "a reason past the last one");
    answer = an_answer(PLAYER_HELP_READY_LISTENING);
    answer.kind = (uint8_t)(PLAYER_HELP_KIND_TELEPORT + 1);
    ut_check(!player_help_answer_publish(&answer), "and a kind past the last one");
}

/* Both directions through the operating system, in the order a session has them: the overlay's
 * empty ask, the multiplayer's ready bits, a press, and the answer to it. */
static void check_the_round_trip(void)
{
    player_help_ask_t    ask;
    player_help_ask_t    asked;
    player_help_answer_t answer;
    player_help_answer_t answered;

    ut_section("the records cross, in both directions, field for field");
    ask = a_press(PLAYER_HELP_KIND_NONE, 0u);
    memset(&asked, UNTOUCHED, sizeof asked);
    ut_check(player_help_ask_publish(&ask) && player_help_ask_read(&asked) &&
                 asked.kind == PLAYER_HELP_KIND_NONE && asked.serial == 0u && asked.flags == 0u,
             "the empty ask is read back as no press, so a reader arming on it marks 0");

    answer = an_answer(PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR);
    memset(&answered, UNTOUCHED, sizeof answered);
    ut_check(player_help_answer_publish(&answer) && player_help_answer_read(&answered) &&
                 answered.ready == (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR) &&
                 answered.serial == 0u && answered.outcome == PLAYER_HELP_OUTCOME_NONE,
             "the ready bits of a listening session arrive, with nothing answered yet");

    ask = a_press(PLAYER_HELP_KIND_TELEPORT, 41u);
    ask.flags = PLAYER_HELP_ASK_F_OVERLAY_HOLDS;
    ut_check(player_help_ask_publish(&ask) && player_help_ask_read(&asked) &&
                 memcmp(&asked, &ask, sizeof ask) == 0,
             "a press is read back byte for byte: its kind, its flag and its serial");
    ut_check(player_help_serial_after(asked.serial, 0u),
             "and it is after the mark of 0 the reader took from the empty ask");

    answer.serial   = 41u;
    answer.kind     = PLAYER_HELP_KIND_TELEPORT;
    answer.outcome  = PLAYER_HELP_OUTCOME_REFUSED;
    answer.reason   = PLAYER_HELP_REASON_OVERLAY_HOLDS;
    answer.released = 0x0105u;
    ut_check(player_help_answer_publish(&answer) && player_help_answer_read(&answered) &&
                 memcmp(&answered, &answer, sizeof answer) == 0,
             "and the answer to it is read back byte for byte, naming the serial it answers");
}

/* What is filed is not always what this build files: a DLL of another build writes another
 * version or another size, and a note can hold a value this build never writes. Each is filed
 * here through the channel itself, past the publisher that would refuse it. */
static void check_what_a_read_refuses(void)
{
    player_help_ask_t    ask = a_press(PLAYER_HELP_KIND_REPAIR, 5u);
    player_help_ask_t    asked;
    player_help_answer_t answer = an_answer(PLAYER_HELP_READY_LISTENING);
    player_help_answer_t answered;
    uint8_t              raw[16];

    ut_section("a read refuses another version, another size and an unsound record");
    memset(&asked, UNTOUCHED, sizeof asked);
    memset(&answered, UNTOUCHED, sizeof answered);

    ask.version = (uint16_t)(PLAYER_HELP_NOTE_VERSION + 1u);
    ut_check(shared_note_publish(PLAYER_HELP_ASK_NOTE_NAME, &ask, sizeof ask) &&
                 !player_help_ask_read(&asked),
             "an ask of the next version is not read, though nothing else about it changed");
    answer.version = (uint16_t)(PLAYER_HELP_NOTE_VERSION + 1u);
    ut_check(shared_note_publish(PLAYER_HELP_ANSWER_NOTE_NAME, &answer, sizeof answer) &&
                 !player_help_answer_read(&answered),
             "and neither is an answer of the next version");

    memset(raw, 0, sizeof raw);
    ask = a_press(PLAYER_HELP_KIND_REPAIR, 5u);
    memcpy(raw, &ask, sizeof ask);
    ut_check(shared_note_publish(PLAYER_HELP_ASK_NOTE_NAME, raw, sizeof ask - 1u) &&
                 !player_help_ask_read(&asked),
             "an ask one byte short is another build's and is refused");
    ut_check(shared_note_publish(PLAYER_HELP_ASK_NOTE_NAME, raw, sizeof ask + 4u) &&
                 !player_help_ask_read(&asked),
             "and so is one four bytes longer, though it begins with a sound ask");
    answer = an_answer(PLAYER_HELP_READY_LISTENING);
    memset(raw, 0, sizeof raw);
    memcpy(raw, &answer, sizeof answer);
    ut_check(shared_note_publish(PLAYER_HELP_ANSWER_NOTE_NAME, raw, sizeof answer - 1u) &&
                 !player_help_answer_read(&answered),
             "an answer one byte short is refused");
    ut_check(shared_note_publish(PLAYER_HELP_ANSWER_NOTE_NAME, raw, sizeof answer + 4u) &&
                 !player_help_answer_read(&answered),
             "and one four bytes longer as well");

    ask = a_press(PLAYER_HELP_KIND_REPAIR, 0u);
    ut_check(shared_note_publish(PLAYER_HELP_ASK_NOTE_NAME, &ask, sizeof ask) &&
                 !player_help_ask_read(&asked),
             "an ask of the right shape that is a press with no serial is not read");
    answer.reason = (uint8_t)PLAYER_HELP_REASON_COUNT;
    ut_check(shared_note_publish(PLAYER_HELP_ANSWER_NOTE_NAME, &answer, sizeof answer) &&
                 !player_help_answer_read(&answered),
             "and an answer with a reason past the last one is not read");

    ut_check(all_untouched(&asked, sizeof asked) && all_untouched(&answered, sizeof answered),
             "and through every one of those refusals the caller's records were left alone");

    ask = a_press(PLAYER_HELP_KIND_REPAIR, 6u);
    answer = an_answer(0u);
    ut_check(player_help_ask_publish(&ask) && player_help_ask_read(&asked) &&
                 asked.serial == 6u && player_help_answer_publish(&answer) &&
                 player_help_answer_read(&answered) && answered.ready == 0u,
             "a sound record filed over a refused one is read again");
}

static void check_the_serial_comparison(void)
{
    ut_section("a press against the mark, across the wrap of the counter");
    ut_check(player_help_serial_after(1u, 0u), "the first press is after the mark of no press");
    ut_check(!player_help_serial_after(0u, 0u), "the empty ask is not after a mark of 0");
    ut_check(!player_help_serial_after(5u, 5u),
             "the press a reader armed on is not after its own mark, so it never fires");
    ut_check(player_help_serial_after(6u, 5u), "the next one is");
    ut_check(!player_help_serial_after(4u, 5u), "and the one before it is not");
    ut_check(player_help_serial_after(1u, 0xFFFFFFFFu),
             "past the wrap, serial 1 is after a mark of the highest serial");
    ut_check(player_help_serial_after(2u, 0xFFFFFFFEu),
             "and a press is after a mark it left four counts behind across the wrap");
    ut_check(!player_help_serial_after(0xFFFFFFFFu, 1u),
             "while the highest serial is not after a mark of 1");
    ut_check(player_help_serial_after(0x7FFFFFFFu, 0u),
             "half the range less one ahead still counts as after");
    ut_check(!player_help_serial_after(0x80000000u, 0u),
             "and exactly half the range ahead does not, where after and before meet");
}

int main(void)
{
    check_the_shape();
    check_nothing_before_a_publication();
    check_what_an_ask_may_be();
    check_what_an_answer_may_be();
    check_the_round_trip();
    check_what_a_read_refuses();
    check_the_serial_comparison();
    return ut_summary("player_help_note");
}
