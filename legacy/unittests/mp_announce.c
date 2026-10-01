/* What a host says about itself to a room that has not asked, driven with no game and no socket.
 *
 * The announce is read by a machine that has never spoken to the sender, so every field is a claim
 * from a stranger and every one of them has to be refused when it is nonsense. The two that are
 * NOT refused are the interesting ones: an unknown wire version and an unknown flag bit both mean
 * a sender newer than this build, and the whole reason those fields exist is to be able to show
 * that in the list rather than let the player walk into a refused handshake.
 */
#include "unittest.h"

#include "mp_announce.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void seed(mp_announce_t *announce)
{
    memset(announce, 0, sizeof *announce);
    announce->wire        = 14u;
    announce->fingerprint = 0xABCD1234u;
    announce->game_port   = 27960u;
    announce->players     = 2u;
    announce->slots       = 4u;
    announce->flags       = 0u;
    mp_announce_name_clean("Naboo Hangar", announce->name);
}

static bool round_trip(const mp_announce_t *in, mp_announce_t *out)
{
    uint8_t buffer[MP_ANNOUNCE_BYTES];

    if (mp_announce_encode(in, buffer, sizeof buffer) != MP_ANNOUNCE_BYTES) {
        return false;
    }
    return mp_announce_decode(buffer, sizeof buffer, out);
}

static void check_the_round_trip(void)
{
    mp_announce_t in;
    mp_announce_t out;

    ut_section("every field survives the wire");
    seed(&in);
    in.flags = MP_ANNOUNCE_F_PASSWORD | MP_ANNOUNCE_F_TDM;
    ut_check(round_trip(&in, &out), "a sound announce encodes and decodes");
    ut_check(out.wire == in.wire && out.fingerprint == in.fingerprint,
             "the wire version and the fingerprint come back, which is what decides whether the "
             "row is joinable at all");
    ut_check(out.game_port == in.game_port,
             "and the GAME port, which is not the port this datagram arrived from");
    ut_check(out.players == 2u && out.slots == 4u, "and the two numbers of the players column");
    ut_check(out.flags == in.flags, "and the flags, password and mode together in one byte");
    ut_check(strcmp(out.name, "Naboo Hangar") == 0, "and the name the browser sorts by");
    ut_check(out.version == MP_ANNOUNCE_VERSION,
             "and the announce version is reported, not hidden");
}

static void check_what_is_refused(void)
{
    mp_announce_t in;
    mp_announce_t out;
    uint8_t       buffer[MP_ANNOUNCE_BYTES];

    ut_section("a stranger's nonsense is refused rather than shown");
    seed(&in);

    in.game_port = 0u;
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == 0u,
             "port zero is refused at the sender: it means 'any' to a socket and a row nobody can "
             "join in a list");

    seed(&in);
    in.players = 5u;
    in.slots   = 4u;
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == 0u,
             "more players than slots is refused: the column would read 5/4");

    seed(&in);
    in.slots = 0u;
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == 0u,
             "and a session with no slots is not a session");

    seed(&in);
    in.name[0] = '\0';
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == 0u,
             "an empty name is refused: a blank row is one nobody clicks");

    seed(&in);
    in.name[3] = (char)0x01;
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == 0u,
             "and a name with a byte the engine's font has no glyph for");

    seed(&in);
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer - 1u) == 0u,
             "a buffer one byte short writes nothing rather than a truncated announce");

    ut_check(!mp_announce_decode(buffer, MP_ANNOUNCE_BYTES - 1u, &out),
             "a datagram of the wrong length is not an announce, whatever it holds");
}

static void check_the_magic(void)
{
    mp_announce_t in;
    mp_announce_t out;
    uint8_t       buffer[MP_ANNOUNCE_BYTES];

    ut_section("a stray datagram on the same port is dropped on its first four bytes");
    seed(&in);
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == MP_ANNOUNCE_BYTES, "encoded");
    ut_check(mp_announce_is_announce(buffer, sizeof buffer), "our own datagram is recognised");

    buffer[0] = 'X';
    ut_check(!mp_announce_is_announce(buffer, sizeof buffer),
             "one wrong magic byte is enough, without decoding the rest");
    ut_check(!mp_announce_decode(buffer, sizeof buffer, &out),
             "and the decode refuses it too rather than trusting the cheap test to have run");
}

static void check_a_newer_sender_is_shown_not_hidden(void)
{
    mp_announce_t in;
    mp_announce_t out;
    uint8_t       buffer[MP_ANNOUNCE_BYTES];

    ut_section("a sender this build cannot join still makes a row");
    seed(&in);
    in.wire = 99u;
    ut_check(round_trip(&in, &out),
             "an unknown WIRE version decodes: the player has to be told the server is newer, "
             "and a refused decode would show them nothing at all");
    ut_check(!mp_announce_joinable(&out, 14u, 0xABCD1234u),
             "and it is reported as not joinable");

    seed(&in);
    in.flags = 0x80u;   /* a bit no version of this build knows */
    ut_check(round_trip(&in, &out) && out.flags == 0x80u,
             "an unknown FLAG bit decodes and is kept, for the same reason");

    /* A newer sender cannot be built by setting a field: the encoder stamps its OWN version,
     * because that is a fact about the sender and not a parameter. So the byte is poked, which is
     * exactly what such a datagram looks like coming off the wire. */
    seed(&in);
    ut_check(mp_announce_encode(&in, buffer, sizeof buffer) == MP_ANNOUNCE_BYTES, "encoded");
    ut_check(buffer[4] == MP_ANNOUNCE_VERSION,
             "the encoder stamped its own announce version rather than the caller's");
    buffer[4] = 200u;
    ut_check(!mp_announce_decode(buffer, sizeof buffer, &out),
             "but an unknown ANNOUNCE version IS refused, because a later one may lay its fields "
             "out differently and everything read would come from the wrong offsets");
}

static void check_joinable_and_full(void)
{
    mp_announce_t in;

    ut_section("what the browser greys out, and why");
    seed(&in);
    ut_check(mp_announce_joinable(&in, 14u, 0xABCD1234u), "a matching build is joinable");
    ut_check(!mp_announce_joinable(&in, 13u, 0xABCD1234u), "a different wire version is not");
    ut_check(!mp_announce_joinable(&in, 14u, 0u),
             "and neither is a listener with no fingerprint yet, which is every listener sitting "
             "in the main menu");

    in.flags = MP_ANNOUNCE_F_LOCKED;
    ut_check(!mp_announce_joinable(&in, 14u, 0xABCD1234u),
             "a locked session refuses everybody, however well it matches");

    seed(&in);
    ut_check(!mp_announce_full(&in), "two of four is not full");
    in.players = 4u;
    ut_check(mp_announce_full(&in),
             "four of four is, and that is a different answer from not joinable: a full server is "
             "one to wait for, a mismatched one is one to give up on");
}

static void check_the_name_cleaner(void)
{
    char out[MP_ANNOUNCE_NAME_MAX];

    ut_section("a name off a keyboard is made sound rather than refused");
    mp_announce_name_clean("  Naboo Hangar  ", out);
    ut_check(strcmp(out, "Naboo Hangar") == 0,
             "blanks at both ends go, because the person who typed them cannot see them");
    ut_check(mp_announce_name_is_sound(out), "and what comes out always passes the sound test");

    mp_announce_name_clean("", out);
    ut_check(out[0] != '\0' && mp_announce_name_is_sound(out),
             "an empty name becomes the default rather than a blank row");

    mp_announce_name_clean("   ", out);
    ut_check(out[0] != '\0' && mp_announce_name_is_sound(out),
             "and so does a name that is only blanks");

    mp_announce_name_clean("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", out);
    ut_check(mp_announce_name_is_sound(out),
             "a name longer than the field is cut with a terminator inside it, not run over");

    mp_announce_name_clean("Naboo\x01Hangar", out);
    ut_check(mp_announce_name_is_sound(out) && strchr(out, '?') != NULL,
             "and a byte with no glyph becomes a question mark rather than losing the name");
}

int main(void)
{
    check_the_round_trip();
    check_what_is_refused();
    check_the_magic();
    check_a_newer_sender_is_shown_not_hidden();
    check_joinable_and_full();
    check_the_name_cleaner();

    return ut_summary("the session announce");
}
