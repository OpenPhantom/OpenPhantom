/* unittests/mp_taken.c: the note that says which pickups are gone.
 *
 * The bug it exists for is a client joining a session in progress and healing itself on every
 * medipack the host used up an hour ago, so the cases that matter are the ones a joiner meets: a
 * full list, an empty one, a repeat, and a note that arrived torn or from the wrong level.
 */
#include "unittest.h"

#include "mp_taken.h"

#include <string.h>

static void check_the_list_keeps_itself_in_order(void)
{
    mp_taken_t taken;
    unsigned   i;

    memset(&taken, 0, sizeof taken);

    ut_section("out of order in, sorted out");

    ut_check(mp_taken_add(&taken, 40u), "an index goes in");
    ut_check(mp_taken_add(&taken, 3u), "one below it goes in");
    ut_check(mp_taken_add(&taken, 17u), "and one between them");
    ut_check(taken.count == 3u, "three of them");
    ut_check(taken.index[0] == 3u && taken.index[1] == 17u && taken.index[2] == 40u,
             "and they are in order, which is what makes two hosts describing the same set "
             "produce the same bytes");

    ut_section("saying the same thing twice is not a failure");

    ut_check(mp_taken_add(&taken, 17u), "an index already named is accepted");
    ut_check(taken.count == 3u, "and does not grow the list");

    ut_section("and the list refuses to overflow rather than wrapping");

    memset(&taken, 0, sizeof taken);
    for (i = 0; i < MP_TAKEN_MAX; ++i) {
        ut_checkf(mp_taken_add(&taken, (uint8_t)i), "index %u fits", i);
    }
    ut_check(!mp_taken_add(&taken, 200u), "the one past the end is refused");
    ut_check(taken.count == MP_TAKEN_MAX, "and the list is unchanged by the refusal");
}

static void check_the_note_crosses(void)
{
    mp_taken_t sent;
    mp_taken_t heard;
    uint8_t    note[MP_TAKEN_MAX_BYTES];
    size_t     bytes;

    memset(&sent, 0, sizeof sent);
    sent.level = 528u;
    (void)mp_taken_add(&sent, 7u);
    (void)mp_taken_add(&sent, 12u);

    ut_section("a note with entries");

    bytes = mp_taken_encode(&sent, note, sizeof note);
    ut_check(bytes == MP_TAKEN_BYTES_FOR(2u), "two entries take six bytes");
    ut_check(mp_taken_is(note, bytes), "and the note recognises itself");
    ut_check(mp_taken_decode(note, bytes, &heard), "it decodes");
    ut_check(mp_taken_equal(&sent, &heard), "and it says what was sent");

    ut_section("an EMPTY note is a statement, not an absence");

    /* This is the case a joiner meets in a level where nothing has been taken yet, and it has to
     * be sent and believed: an absent note and a note saying nothing is gone would otherwise look
     * the same, and the second is the one that means the host is answering. */
    memset(&sent, 0, sizeof sent);
    sent.level = 528u;
    bytes = mp_taken_encode(&sent, note, sizeof note);
    ut_check(bytes == MP_TAKEN_HEADER_BYTES, "an empty list still encodes");
    ut_check(mp_taken_is(note, bytes), "and is still recognised");
    ut_check(mp_taken_decode(note, bytes, &heard) && heard.count == 0u,
             "and decodes to an empty list rather than to a refusal");

    ut_section("the level travels with it");

    memset(&sent, 0, sizeof sent);
    sent.level = 100u;
    (void)mp_taken_add(&sent, 7u);
    memset(&heard, 0, sizeof heard);
    heard.level = 55u;
    (void)mp_taken_add(&heard, 7u);
    ut_check(!mp_taken_equal(&sent, &heard),
             "the same set in a different level is a different statement");
}

static void check_a_torn_note_is_refused(void)
{
    mp_taken_t sent;
    mp_taken_t heard;
    uint8_t    note[MP_TAKEN_MAX_BYTES];
    size_t     bytes;

    memset(&sent, 0, sizeof sent);
    sent.level = 528u;
    (void)mp_taken_add(&sent, 7u);
    (void)mp_taken_add(&sent, 12u);
    (void)mp_taken_add(&sent, 30u);
    bytes = mp_taken_encode(&sent, note, sizeof note);

    ut_section("length and count have to agree");

    /* The roster has the same trap: a note recognised by its tag alone, arriving one byte short,
     * seats whatever was after it in the buffer as a placement index and buries something nobody
     * took. */
    ut_check(!mp_taken_is(note, bytes - 1u), "a note one byte short is not one of ours");
    ut_check(!mp_taken_is(note, bytes + 1u), "and neither is one a byte too long");
    ut_check(!mp_taken_decode(note, bytes - 1u, &heard), "so it does not decode either");

    ut_section("and so does the tag");

    note[0] = (uint8_t)(MP_TAKEN_TAG + 1u);
    ut_check(!mp_taken_is(note, bytes), "a different tag is somebody else's message");

    ut_section("nothing shorter than a header can be one");

    ut_check(!mp_taken_is(note, 0u), "no bytes at all");
    ut_check(!mp_taken_is(NULL, bytes), "and no buffer at all");
    ut_check(!mp_taken_is(note, MP_TAKEN_HEADER_BYTES - 1u), "nor a header one byte short");
}

static void check_the_encoder_refuses_what_it_cannot_hold(void)
{
    mp_taken_t sent;
    uint8_t    small[8];

    ut_section("a buffer too small gets nothing, not a truncation");

    memset(&sent, 0, sizeof sent);
    sent.level = 528u;
    (void)mp_taken_add(&sent, 1u);
    (void)mp_taken_add(&sent, 2u);
    (void)mp_taken_add(&sent, 3u);
    (void)mp_taken_add(&sent, 4u);
    /* Four bytes of header and one per entry, so eight bytes hold four of them. */
    ut_check(mp_taken_encode(&sent, small, sizeof small) == MP_TAKEN_BYTES_FOR(4u),
             "four fit in eight bytes, exactly");
    (void)mp_taken_add(&sent, 5u);
    ut_check(mp_taken_encode(&sent, small, sizeof small) == 0u,
             "five do not, and the encoder refuses rather than sending a list of what is LEFT");
    ut_check(mp_taken_encode(&sent, NULL, sizeof small) == 0u, "no buffer, no note");
    ut_check(mp_taken_encode(NULL, small, sizeof small) == 0u, "no list, no note");
}

int main(void)
{
    check_the_list_keeps_itself_in_order();
    check_the_note_crosses();
    check_a_torn_note_is_refused();
    check_the_encoder_refuses_what_it_cannot_hold();
    return ut_summary("mp_taken");
}
