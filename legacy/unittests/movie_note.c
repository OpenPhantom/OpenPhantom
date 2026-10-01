/* movie_note.c: the two records the movie player and the multiplayer leave each other, published
 * and read in one process.
 *
 * The channel is the one the other notes use, and it is testable here for the same reason: both
 * sides are DLLs in one process, so a test that publishes and then reads is the real case with one
 * process doing both halves.
 *
 * What is worth pinning is what decides whether a movie is held for a host: a read before anybody
 * published must refuse rather than invent a session, a record of another shape or version must
 * be refused rather than read field by field, the gate must come back in its one consistent shape,
 * and the connection must survive whole, because a movie ends only on the connection it began on.
 */
#include "unittest.h"

#include "common/language.h"
#include "common/movie_note.h"
#include "common/shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_nothing_published_yet(void)
{
    movie_state_t state;
    movie_gate_t  gate;

    ut_section("before anybody has published, both reads refuse rather than answer");

    memset(&state, 0x5A, sizeof state);
    memset(&gate, 0x5A, sizeof gate);
    ut_check(!movie_note_read_state(&state), "no movie player has filed a state yet");
    ut_check(!movie_note_read_gate(&gate),
             "no multiplayer has filed a gate yet, which the movie player reads as no session");
    ut_check(state.movies_begun == 0x5A5Au && gate.published == 0x5A5A5A5Au,
             "and neither read wrote into the caller's record");
}

static void check_the_state_round_trip(void)
{
    movie_state_t said;
    movie_state_t heard;

    ut_section("what the movie player files is what the multiplayer reads");

    memset(&said, 0, sizeof said);
    said.movies_begun = 3u;
    said.state        = MOVIE_STATE_PLAYING;
    said.path         = MOVIE_PATH_VLC;
    said.locked       = true;
    memcpy(said.stem, "scene1", 7u);
    said.begin_ms = 123456u;
    ut_check(movie_note_publish_state(&said), "a client's movie that plays is filed");
    memset(&heard, 0, sizeof heard);
    ut_check(movie_note_read_state(&heard), "and read back");
    ut_check(heard.movies_begun == 3u && heard.state == MOVIE_STATE_PLAYING &&
             heard.path == MOVIE_PATH_VLC && heard.locked && heard.begin_ms == 123456u,
             "with every field as it was said");
    ut_check(strcmp(heard.stem, "scene1") == 0, "and the name");

    said.state      = MOVIE_STATE_ENDED;
    said.end_reason = MOVIE_END_HOST;
    said.end_ms     = 130000u;
    ut_check(movie_note_publish_state(&said) && movie_note_read_state(&heard),
             "the end is filed over it");
    ut_check(heard.state == MOVIE_STATE_ENDED && heard.end_reason == MOVIE_END_HOST &&
             heard.end_ms == 130000u, "as an end the host decided");

    ut_section("a name is only what is in front of its terminator");

    memset(said.stem, 'x', sizeof said.stem);
    memcpy(said.stem, "arena", 6u);
    ut_check(movie_note_publish_state(&said) && movie_note_read_state(&heard),
             "a field with bytes behind the terminator is filed");
    ut_check(strcmp(heard.stem, "arena") == 0 && heard.stem[6] == '\0',
             "and what follows the terminator never reaches the record");
}

static void check_an_unsound_state_is_refused(void)
{
    movie_state_t said;
    movie_state_t heard;

    ut_section("a state the multiplayer could not print or act on is not filed");

    memset(&said, 0, sizeof said);
    memcpy(said.stem, "scene2", 7u);
    said.state = MOVIE_STATE_PLAYING;
    ut_check(movie_note_publish_state(&said), "the good one first");

    said.state = (uint8_t)(MOVIE_STATE_ENDED + 1u);
    ut_check(!movie_note_publish_state(&said), "a state past ENDED is refused");
    said.state      = MOVIE_STATE_ENDED;
    said.end_reason = (uint8_t)(MOVIE_END_MAX + 1u);
    ut_check(!movie_note_publish_state(&said), "an end reason past the last one is refused");
    said.end_reason = MOVIE_END_NATURAL;
    said.path       = (uint8_t)(MOVIE_PATH_BINK + 1u);
    ut_check(!movie_note_publish_state(&said), "a path past Bink is refused");
    said.path = MOVIE_PATH_BINK;
    memset(said.stem, 'y', sizeof said.stem);
    ut_check(!movie_note_publish_state(&said),
             "a name with no terminator inside its field is refused rather than cut");
    memcpy(said.stem, "sc\x01ne", 6u);
    ut_check(!movie_note_publish_state(&said), "and so is one with an unprintable byte");

    memset(&heard, 0, sizeof heard);
    ut_check(movie_note_read_state(&heard) && strcmp(heard.stem, "scene2") == 0 &&
             heard.state == MOVIE_STATE_PLAYING,
             "and every refusal left the record before it standing");
}

static void check_the_gate_round_trip(void)
{
    movie_gate_t said;
    movie_gate_t heard;

    ut_section("what the multiplayer files is what the movie player reads");

    memset(&said, 0, sizeof said);
    said.running        = true;
    said.client         = true;
    said.host_connected = true;
    said.host_moving    = false;
    said.generation     = 7u;
    said.language       = (uint8_t)LANGUAGE_DE;
    said.connection     = 0x0123456789ABCDEFull;
    said.host_payloads  = 0xFFFFFFF0u;
    said.published      = 41u;
    ut_check(movie_note_publish_gate(&said), "a client's gate is filed");
    memset(&heard, 0, sizeof heard);
    ut_check(movie_note_read_gate(&heard), "and read back");
    ut_check(heard.running && heard.client && heard.host_connected && !heard.host_moving,
             "the four answers as they were said");
    ut_check(heard.connection == 0x0123456789ABCDEFull,
             "the connection whole, both halves of it: a movie ends only on the one it began on");
    ut_check(heard.host_payloads == 0xFFFFFFF0u && heard.published == 41u &&
             heard.generation == 7u && heard.language == (uint8_t)LANGUAGE_DE,
             "and the counts, the generation and the language");

    ut_section("the gate comes back in its one consistent shape");

    said.running = false;
    ut_check(movie_note_publish_gate(&said) && movie_note_read_gate(&heard),
             "a gate whose session does not run is filed");
    ut_check(!heard.running && !heard.client && !heard.host_connected && !heard.host_moving,
             "and nobody is a client of a session that does not run, whatever the caller said");

    said.running        = true;
    said.client         = false;
    said.host_connected = true;
    said.host_moving    = true;
    ut_check(movie_note_publish_gate(&said) && movie_note_read_gate(&heard),
             "a host's gate is filed");
    ut_check(heard.running && !heard.client && !heard.host_connected && !heard.host_moving,
             "and a host has no host of its own to be connected to or to see moving");

    said.language = (uint8_t)LANGUAGE_COUNT;
    ut_check(!movie_note_publish_gate(&said), "a language past the five is refused");
}

static void check_another_shape_is_refused(void)
{
    movie_state_t  state;
    movie_gate_t   gate;
    uint8_t        foreign[36];
    uint32_t       version_two = 2u;

    ut_section("a record this build does not know is refused, not read field by field");

    memset(foreign, 0xAB, sizeof foreign);
    ut_check(shared_note_publish(MOVIE_STATE_NOTE_NAME, foreign, 20u),
             "something shorter is filed under the state's name");
    memset(&state, 0, sizeof state);
    ut_check(!movie_note_read_state(&state), "and the reader refuses it");
    ut_check(state.movies_begun == 0u, "without writing into the caller's record");

    memset(foreign, 0, sizeof foreign);
    memcpy(foreign, &version_two, sizeof version_two);
    ut_check(shared_note_publish(MOVIE_STATE_NOTE_NAME, foreign, sizeof foreign),
             "a state of the right length and another version is filed");
    ut_check(!movie_note_read_state(&state), "and the reader refuses the version");

    memset(foreign, 0, sizeof foreign);
    foreign[0] = 1u;    /* version 1, little endian */
    foreign[4] = 2u;    /* running = 2, which no publisher writes */
    ut_check(shared_note_publish(MOVIE_GATE_NOTE_NAME, foreign, 28u),
             "a gate of the right length with a flag of two is filed");
    memset(&gate, 0, sizeof gate);
    ut_check(!movie_note_read_gate(&gate),
             "and the reader refuses it rather than taking two for true");
    foreign[4] = 1u;
    ut_check(shared_note_publish(MOVIE_GATE_NOTE_NAME, foreign, 28u) &&
             movie_note_read_gate(&gate) && gate.running,
             "the same bytes with a one are read, so it was the flag that was refused");
}

/* A state record of the right length and version, laid by hand, so each field the reader checks
 * can be made the one thing wrong with it. Offsets as the record is filed: locked at 9, the name
 * at 12. */
static void lay_a_state(uint8_t *record, uint8_t locked, uint8_t stem_byte)
{
    memset(record, 0, 36u);
    record[0]  = 1u;                    /* version 1 */
    record[6]  = MOVIE_STATE_PLAYING;
    record[8]  = MOVIE_PATH_VLC;
    record[9]  = locked;
    record[12] = 's';
    record[13] = stem_byte;
}

/* A gate record the same way: the language at 9. */
static void lay_a_gate(uint8_t *record, uint8_t language)
{
    memset(record, 0, 28u);
    record[0] = 1u;                     /* version 1 */
    record[4] = 1u;                     /* running */
    record[9] = language;
}

static void check_the_read_side_field_by_field(void)
{
    movie_state_t state;
    movie_gate_t  gate;
    movie_state_t said;
    uint8_t       record[36];

    ut_section("every field the reader checks is refused on its own");

    lay_a_state(record, 2u, 'c');
    ut_check(shared_note_publish(MOVIE_STATE_NOTE_NAME, record, 36u),
             "a state whose held flag is two is filed");
    memset(&state, 0, sizeof state);
    ut_check(!movie_note_read_state(&state), "and refused, not read as held");
    lay_a_state(record, 1u, 'c');
    ut_check(shared_note_publish(MOVIE_STATE_NOTE_NAME, record, 36u) &&
             movie_note_read_state(&state) && state.locked && strcmp(state.stem, "sc") == 0,
             "the same record with a one is read, so it was the flag that was refused");

    lay_a_state(record, 1u, 0x7F);
    ut_check(shared_note_publish(MOVIE_STATE_NOTE_NAME, record, 36u),
             "a state whose name carries DEL is filed");
    ut_check(!movie_note_read_state(&state), "and refused: DEL is not a printable character");
    lay_a_state(record, 1u, 0x7E);
    ut_check(shared_note_publish(MOVIE_STATE_NOTE_NAME, record, 36u) &&
             movie_note_read_state(&state), "with a tilde, the last printable one, it is read");
    memset(&said, 0, sizeof said);
    said.state = MOVIE_STATE_PLAYING;
    memcpy(said.stem, "sc\x7F", 4u);
    ut_check(!movie_note_publish_state(&said), "and the publisher refuses DEL as well");

    lay_a_gate(record, (uint8_t)LANGUAGE_COUNT);
    ut_check(shared_note_publish(MOVIE_GATE_NOTE_NAME, record, 28u),
             "a gate with a language past the five is filed");
    memset(&gate, 0, sizeof gate);
    ut_check(!movie_note_read_gate(&gate), "and refused, not read as some language");
    lay_a_gate(record, (uint8_t)(LANGUAGE_COUNT - 1));
    ut_check(shared_note_publish(MOVIE_GATE_NOTE_NAME, record, 28u) &&
             movie_note_read_gate(&gate) && gate.language == (uint8_t)(LANGUAGE_COUNT - 1),
             "the last of the five is read");
}

int main(void)
{
    check_nothing_published_yet();
    check_the_state_round_trip();
    check_an_unsound_state_is_refused();
    check_the_gate_round_trip();
    check_another_shape_is_refused();
    check_the_read_side_field_by_field();
    return ut_summary("movie_note");
}
