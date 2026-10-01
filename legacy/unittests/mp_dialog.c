/* unittests/mp_dialog.c: one spoken line on the wire, and the host's pick.
 *
 * The notes are small and so is the surface, but two of the refusals are the only thing standing
 * between a stranger's bytes and a read off the end of the dialogue book, so those are what this
 * file is about. How far a line carries is judged by mp_voice_rule, and its edges are tested in
 * unittests/mp_voice_rule.c.
 */
#include "unittest.h"

#include "mp_dialog.h"

#include <math.h>
#include <string.h>

static void fill(mp_dialog_line_t *line)
{
    memset(line, 0, sizeof *line);
    line->level        = 0x1234u;
    line->line         = 1000u;
    line->has_position = true;
    line->position[0]  = 51.5f;
    line->position[1]  = -22.25f;
    line->position[2]  = 26.0f;
}

static void check_the_note_crosses(void)
{
    mp_dialog_line_t sent;
    mp_dialog_line_t back;
    uint8_t          note[MP_DIALOG_BYTES];
    size_t           bytes;

    ut_section("a line with a place");

    fill(&sent);
    bytes = mp_dialog_encode(&sent, note, sizeof note);
    ut_check(bytes == MP_DIALOG_BYTES, "eighteen bytes");
    ut_check(mp_dialog_is(note, bytes), "recognised by tag and exact length");
    ut_check(mp_dialog_decode(note, bytes, &back), "and decoded");
    ut_check(back.level == sent.level, "the level survives");
    ut_check(back.line == sent.line, "the line id survives");
    ut_check(back.has_position, "and it still carries a place");

    /* The wire's position is 1/256 of a unit, so the round trip is exact for these and never
     * worse than half a step for anything. A sound anchor does not hear the difference. */
    ut_check(fabs((double)(back.position[0] - sent.position[0])) < 0.01, "x comes back");
    ut_check(fabs((double)(back.position[1] - sent.position[1])) < 0.01, "y comes back");
    ut_check(fabs((double)(back.position[2] - sent.position[2])) < 0.01, "z comes back");

    ut_section("a line with no place, which is legal and is not the same as a place of nought");

    fill(&sent);
    sent.has_position = false;
    bytes = mp_dialog_encode(&sent, note, sizeof note);
    ut_check(bytes == MP_DIALOG_BYTES, "the same length either way, so the note has one shape");
    ut_check(mp_dialog_decode(note, bytes, &back), "decoded");
    ut_check(!back.has_position, "and it says it has no place");
}

static void check_what_is_refused(void)
{
    mp_dialog_line_t sent;
    mp_dialog_line_t back;
    uint8_t          note[MP_DIALOG_BYTES];
    size_t           bytes;

    ut_section("a line id past the book is refused BOTH ways");

    /* `DLG_LineText` and `DLG_TextLength` both compare against 0x27FF before an assert that the
     * retail build does not carry, so what an id past the end actually does is read off the end of
     * the book. It is refused by the encoder so it never travels, and again by the decoder because
     * the sender is another machine. */
    fill(&sent);
    sent.line = (uint16_t)MP_DIALOG_MAX_ID;
    ut_check(mp_dialog_encode(&sent, note, sizeof note) == 0u, "the encoder writes nothing");

    fill(&sent);
    sent.line = (uint16_t)(MP_DIALOG_MAX_ID - 1u);
    bytes = mp_dialog_encode(&sent, note, sizeof note);
    ut_check(bytes == MP_DIALOG_BYTES, "the last legal id does travel");
    /* Now bend the id on the wire, the way a stranger would. */
    note[3] = 0x00u;
    note[4] = 0x28u;   /* 0x2800, the first id past the book */
    ut_check(mp_dialog_is(note, bytes), "it still looks like one of ours");
    ut_check(!mp_dialog_decode(note, bytes, &back), "and the decoder refuses it anyway");

    ut_section("the place flag has two states and a byte has two hundred and fifty six");

    fill(&sent);
    bytes   = mp_dialog_encode(&sent, note, sizeof note);
    note[5] = 2u;
    ut_check(!mp_dialog_decode(note, bytes, &back), "anything but nought or one is refused");

    ut_section("a wrong length or a wrong tag is not ours");

    fill(&sent);
    bytes = mp_dialog_encode(&sent, note, sizeof note);
    ut_check(!mp_dialog_is(note, bytes - 1u), "one byte short");
    ut_check(!mp_dialog_is(note, bytes + 1u), "one byte long");
    note[0] = (uint8_t)(MP_DIALOG_TAG + 1u);
    ut_check(!mp_dialog_is(note, bytes), "and somebody else's tag");

    ut_section("a buffer too small produces nothing rather than half a note");

    fill(&sent);
    ut_check(mp_dialog_encode(&sent, note, MP_DIALOG_BYTES - 1u) == 0u, "no bytes written");
    ut_check(mp_dialog_encode(NULL, note, sizeof note) == 0u, "no line, no note");
    ut_check(mp_dialog_encode(&sent, NULL, sizeof note) == 0u, "no buffer, no note");
    ut_check(!mp_dialog_decode(note, sizeof note, NULL), "and nowhere to decode to");
}

static void check_a_place_the_fixed_point_cannot_hold(void)
{
    mp_dialog_line_t sent;
    uint8_t          note[MP_DIALOG_BYTES];

    ut_section("a place beyond the wire's reach refuses the WHOLE note");

    /* A voice anchored at the wrong end of the level is worse than a voice with no place, and the
     * receiver already has a rule for no place. So the encoder refuses rather than clamping. */
    fill(&sent);
    sent.position[1] = 1.0e12f;
    ut_check(mp_dialog_encode(&sent, note, sizeof note) == 0u, "nothing is written");

    ut_section("and so does a place that is not a number at all");

    fill(&sent);
    /* A NaN without a header. */
    sent.position[2] = (float)(1.0 / 0.0) - (float)(1.0 / 0.0);
    ut_check(mp_dialog_encode(&sent, note, sizeof note) == 0u, "nothing is written");
}

/* ==============================================================================================
 * The host's pick.
 * ============================================================================================ */

static void check_the_pick(void)
{
    uint8_t  note[MP_DIALOG_PICK_BYTES];
    uint16_t level = 0;
    uint16_t line  = 0;
    size_t   bytes;

    ut_section("the answer the host gave");

    bytes = mp_dialog_encode_pick(0x0304u, 1234u, note, sizeof note);
    ut_check(bytes == MP_DIALOG_PICK_BYTES, "five bytes");
    ut_check(mp_dialog_is_pick(note, bytes), "recognised");
    ut_check(mp_dialog_decode_pick(note, bytes, &level, &line), "and decoded");
    ut_check(level == 0x0304u && line == 1234u, "to the level and the line");

    ut_section("it carries the LINE and not a row, because a client has no menu to index");

    ut_check(MP_DIALOG_PICK_BYTES == 5u, "tag, level and line, and no row index anywhere in it");

    ut_section("a line past the book is refused both ways");

    ut_check(mp_dialog_encode_pick(0u, (uint16_t)MP_DIALOG_MAX_ID, note, sizeof note) == 0u,
             "nothing written");
    bytes   = mp_dialog_encode_pick(0u, 5u, note, sizeof note);
    note[3] = 0x00u;
    note[4] = 0x28u;
    ut_check(!mp_dialog_decode_pick(note, bytes, &level, &line), "and a bent one is refused");

    ut_section("the tags cannot be confused with one another, the retired one included");

    ut_check(MP_DIALOG_TAG != MP_DIALOG_PICK_TAG && MP_DIALOG_TAG != MP_DIALOG_CHOICE_TAG &&
                 MP_DIALOG_PICK_TAG != MP_DIALOG_CHOICE_TAG,
             "all three differ, so a retired tag can never be read as a live one");
    bytes = mp_dialog_encode_pick(0u, 5u, note, sizeof note);
    ut_check(!mp_dialog_is(note, bytes), "a pick is not a spoken line");
}

int main(void)
{
    check_the_note_crosses();
    check_what_is_refused();
    check_a_place_the_fixed_point_cannot_hold();
    check_the_pick();
    return ut_summary("mp_dialog");
}
