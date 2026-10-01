/* mp_roster.c: the roster codec, the name rule it shares with the handshake, and the asset rule it
 * shares with the appearance event.
 *
 * What would be silent if it were wrong: a name with a byte the engine's font has no glyph for,
 * drawn into a screen; a count past the table, walking a decoder off it; a roster that changed
 * and was not sent because two tables compared equal when they were not; an asset name that got
 * through and ended the program on the machine that tried to load it.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_roster.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Where an entry's asset field starts, counted rather than remembered: slot, team, ready, hero,
 * a two byte round trip and the name come before it. */
#define ENTRY_AT(index)  (2u + (index) * MP_ROSTER_ENTRY_BYTES)
#define ASSET_IN_ENTRY   (4u + 2u + MP_ROSTER_NAME_MAX)

/* The length a sender must quote, and the reason this test exists.
 *
 * The note became variable length, the buffer that holds it stayed at its full size, and the one
 * sender went on quoting `sizeof note`. A table of two players therefore left the host declaring
 * two and measuring eight hundred and sixty six, and the receiver's recogniser, which tests the
 * length against the declared count, exactly, as every recogniser on this channel does, refused
 * it. It was not even counted as torn, because it was never recognised as a roster at all.
 *
 * In the field that was a host sending fourteen tables and a client taking none: a lobby list with
 * nobody in it, and a far body wearing the wrong player's hero. */
static void check_the_padded_buffer_is_not_a_roster(void)
{
    mp_roster_t table;
    uint8_t     note[MP_ROSTER_BYTES];
    size_t      bytes;

    memset(&table, 0, sizeof table);
    table.count = 2u;
    table.entry[0].slot = 0u;
    table.entry[1].slot = 1u;
    memcpy(table.entry[0].name, "host", 5u);
    memcpy(table.entry[1].name, "peer", 5u);

    bytes = mp_roster_encode(&table, note, sizeof note);

    ut_check(bytes == MP_ROSTER_BYTES_FOR(2u),
             "two entries encode to the length two entries take");
    ut_check(bytes < sizeof note,
             "which is SHORTER than the buffer that holds them, and that gap is the trap");
    ut_check(mp_roster_is_roster(note, bytes),
             "the encoded length is recognised");
    ut_check(!mp_roster_is_roster(note, sizeof note),
             "the whole buffer is not: a sender that quotes sizeof sends a note nobody takes");
    ut_check(!mp_roster_is_roster(note, bytes - 1u), "nor is one byte short");
    ut_check(!mp_roster_is_roster(note, bytes + 1u), "nor one byte long");
}


/* The size a player is drawn at, in the table the authority repeats.
 *
 * The event is an edge and the table is the state: a player who joins after somebody has become a
 * giant learns it here or not at all, which is the same reason the asset rides in the table.
 */
static void check_the_scale_in_the_table(void)
{
    mp_roster_t sent;
    mp_roster_t got;
    uint8_t     buffer[MP_ROSTER_BYTES];
    size_t      bytes;

    ut_section("the repeated table carries the scale, so a late joiner is not the only one who "
               "sees an ordinary player");

    memset(&sent, 0, sizeof sent);
    sent.count = 1u;
    sent.entry[0].slot  = 1u;
    sent.entry[0].ready = 1u;
    sent.entry[0].scale = 35u;   /* the doll */
    memcpy(sent.entry[0].name, "Anakin", 6u);
    memcpy(sent.entry[0].asset, "obiwan.baf", 10u);

    bytes = mp_roster_encode(&sent, buffer, sizeof buffer);
    ut_checkf(bytes == MP_ROSTER_BYTES_FOR(1u), "one entry weighs %u bytes", (unsigned)bytes);
    ut_check(mp_roster_decode(buffer, bytes, &got), "and comes back");
    ut_checkf(got.entry[0].scale == 35u, "with the scale it was given (%u)",
              (unsigned)got.entry[0].scale);

    sent.entry[0].scale = 1u;
    ut_check(mp_roster_encode(&sent, buffer, sizeof buffer) == 0u,
             "a factor outside the bounds is refused here too, by the same numbers");
}

/* Where the kind sits: the last byte of an entry, behind the scale. The offsets are fixed wire
 * layout, and a round trip alone would not see both ends move together. */
#define KIND_IN_ENTRY 56u

/* The kind of the asset. A model worn over a hero and an actor the player embodies are both a
 * name in this field, and a late joiner that read a model as an actor built the far body out of
 * the model's own file, whose clip table is not the one the wire's ordinals index. */
static void check_the_kind_in_the_table(void)
{
    mp_roster_t sent;
    mp_roster_t got;
    mp_roster_t other;
    uint8_t     buffer[MP_ROSTER_BYTES];
    size_t      bytes;

    ut_section("the repeated table says whether the asset is an actor or a model");

    memset(&sent, 0, sizeof sent);
    sent.count = 1u;
    sent.entry[0].slot       = 1u;
    sent.entry[0].ready      = 1u;
    sent.entry[0].hero       = 2u;
    sent.entry[0].asset_kind = MP_SKIN_MODEL;
    memcpy(sent.entry[0].name, "Anakin", 6u);
    memcpy(sent.entry[0].asset, "anakin.baf", 10u);

    bytes = mp_roster_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_ROSTER_BYTES_FOR(1u) && mp_roster_decode(buffer, bytes, &got),
             "an entry wearing a model encodes and comes back");
    ut_checkf(got.entry[0].asset_kind == MP_SKIN_MODEL, "still a model (%u)",
              (unsigned)got.entry[0].asset_kind);
    ut_check(buffer[ENTRY_AT(0u) + KIND_IN_ENTRY] == MP_SKIN_MODEL &&
                 KIND_IN_ENTRY + 1u == MP_ROSTER_ENTRY_BYTES,
             "and the kind is the last byte of the entry, behind the scale");

    other = sent;
    other.entry[0].asset_kind = MP_SKIN_CHARACTER;
    ut_check(mp_roster_differs_to_a_player(&sent, &other) && !mp_roster_equal(&sent, &other),
             "two tables that differ only in the kind are different to a player");
    other = sent;
    other.entry[0].scale = 250u;
    ut_check(mp_roster_differs_to_a_player(&sent, &other),
             "and so are two that differ only in the size");

    buffer[ENTRY_AT(0u) + KIND_IN_ENTRY] = (uint8_t)(MP_SKIN_KIND_MAX + 1u);
    ut_check(!mp_roster_decode(buffer, bytes, &got),
             "a kind this build does not know is refused off the wire");
    sent.entry[0].asset_kind = (uint8_t)(MP_SKIN_KIND_MAX + 1u);
    ut_check(mp_roster_encode(&sent, buffer, sizeof buffer) == 0u, "and refused going out");

    sent.entry[0].asset_kind = MP_SKIN_MODEL;
    memset(sent.entry[0].asset, 0, sizeof sent.entry[0].asset);
    ut_check(mp_roster_encode(&sent, buffer, sizeof buffer) == 0u,
             "a model with no name is refused, so an asset that is not known has one encoding");
    sent.entry[0].asset_kind = MP_SKIN_CHARACTER;
    bytes = mp_roster_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes != 0u, "and the same line with kind nought is the ordinary one");
    buffer[ENTRY_AT(0u) + KIND_IN_ENTRY] = MP_SKIN_MODEL;
    ut_check(!mp_roster_decode(buffer, bytes, &got), "which the decoder holds to as well");
}

static void check_the_shape(void)
{
    ut_section("the shape");
    ut_check(MP_ROSTER_ENTRY_BYTES == 57u,
             "an entry is slot, team, ready, hero, a round trip, sixteen name bytes, thirty two "
             "asset bytes, the scale the body is drawn at and the kind of the asset");
    ut_check(MP_ROSTER_BYTES == 914u, "sixteen of those behind a tag and a count");
    ut_check(MP_ROSTER_BYTES_FOR(2u) == 116u,
             "but two players weigh a hundred and sixteen, because the message is as long as its "
             "own count says and not as long as the table could be");
    ut_check(MP_ROSTER_TAG == 0x8Fu,
             "the tag is the first free one after the events and the scratchpad");

    ut_section("the channel carries it, and the number that matters is the ordinary one");

    /* A reliable message larger than this is refused outright by the channel, silently, at the
     * point where the caller who owns it still exists. Even the largest possible roster fits. */
    ut_checkf(MP_ROSTER_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
              "a full roster of %u bytes fits the channel's %u byte maximum",
              (unsigned)MP_ROSTER_BYTES, (unsigned)MP_CHANNEL_MESSAGE_BYTES);

    /* A packet reserves its unreliable payload first and seats reliable messages in what is left,
     * so a roster rides a packet only while that packet's payload leaves room for it. That is a
     * condition on the payload, and it is the reason this message is as long as its count rather
     * than as long as its table: at sixteen entries the condition leaves room for a small
     * payload only, which in a level where the enemy block runs to 974 can be no packet at all
     * for a long stretch, and nothing counts a message that found no seat. */
    /* 974 is the largest payload measured in a level. Three players are seated beside it; four
     * already wait for a quieter packet, and a full sixteen waits longer still. */
    ut_checkf(MP_ROSTER_BYTES_FOR(3u) + MP_CHANNEL_MESSAGE_HEADER_BYTES <=
                  MP_CHANNEL_PAYLOAD_BYTES - 974u,
              "three players take %u seatable bytes and ride a packet whose payload is the "
              "largest measured 974, out of %u",
              (unsigned)(MP_ROSTER_BYTES_FOR(3u) + MP_CHANNEL_MESSAGE_HEADER_BYTES),
              (unsigned)MP_CHANNEL_PAYLOAD_BYTES);
    ut_check(MP_ROSTER_BYTES_FOR(4u) + MP_CHANNEL_MESSAGE_HEADER_BYTES >
                 MP_CHANNEL_PAYLOAD_BYTES - 974u,
             "four already wait for a quieter packet, which is worth knowing rather than "
             "rounding up: the co-op ceiling is exactly where this message starts having to "
             "queue behind a busy level");
    ut_check(MP_ROSTER_BYTES + MP_CHANNEL_MESSAGE_HEADER_BYTES >
                 MP_CHANNEL_PAYLOAD_BYTES - 974u,
             "and a full sixteen still does not, which is why the length is the count: the "
             "padded message would have waited behind every busy packet in a level");
    ut_check(MP_ROSTER_BYTES_FOR(2u) > MP_CHANNEL_EAGER_BYTES,
             "it is still past the eager size, so it rests between copies rather than riding "
             "every packet");
}

static void check_the_name_rule(void)
{
    char name[MP_ROSTER_NAME_MAX];

    ut_section("the name rule");
    ut_check(mp_roster_name_is_sound("Padme"), "a plain name is sound");
    ut_check(!mp_roster_name_is_sound(""), "an empty name is not");
    ut_check(!mp_roster_name_is_sound(NULL), "and neither is none");
    ut_check(!mp_roster_name_is_sound("a\tb"), "a control character is refused");
    ut_check(!mp_roster_name_is_sound("a\\nb"),
             "and so is a backslash: the engine's text drawer unescapes before it measures, so a "
             "name carrying one draws as two lines in a list whose rows are one line each");
    ut_check(!mp_roster_name_is_sound("Gr\xfc\xdf"),
             "and so is anything past ASCII, which the font has no glyph for");
    memset(name, 'x', sizeof name);
    ut_check(!mp_roster_name_is_sound(name), "a field with no terminator inside it is refused");

    mp_roster_name_clean("  Obi Wan  ", name);
    ut_check(strcmp(name, "Obi Wan") == 0, "cleaning trims the blanks at both ends");
    mp_roster_name_clean("Mace\\nWindu", name);
    ut_check(strcmp(name, "Mace?nWindu") == 0,
             "a backslash in a typed name becomes a question mark rather than an escape");
    mp_roster_name_clean("Gr\xfc\xdf", name);
    ut_check(strcmp(name, "Gr??") == 0, "and replaces what is not printable");
    mp_roster_name_clean("", name);
    ut_check(strcmp(name, "Player") == 0, "and an empty name becomes the default");
    mp_roster_name_clean("abcdefghijklmnopqrstuvwxyz", name);
    ut_check(strlen(name) == MP_ROSTER_NAME_MAX - 1u && mp_roster_name_is_sound(name),
             "and a long one is cut to fit with its terminator");
}

static void check_the_asset_rule(void)
{
    char asset[MP_EVENT_ASSET_MAX];

    ut_section("the asset rule, which is the ENGINE'S 8.3 gate and not this file's choice");

    /* The same table the appearance event's test drives, because the two rules are separate copies
     * in two modules that link independently of each other, and the obligation the copy creates is
     * that they agree. A change made to one and not the other fails here or there.
     *
     * The one deliberate difference is the empty field: sound here, where it means the appearance
     * is not known, and refused in the event, which exists only to name something. */
    {
        static const struct { const char *asset; bool sound; } SHAPES[] = {
            { "obiwan.baf",     true  },
            { "MACE.BAF",       true  },
            { "mace_2.baf",     true  },
            { "abcdefgh.baf",   true  },
            { "abcdefghi.baf",  false },   /* a stem of nine */
            { "mace.ba",        true  },
            { "mace.",          true  },
            { "mace.abcd",      false },   /* an extension of four */
            { "ma.ce.baf",      false },   /* two dots */
            { "stnd-wp1.baf",   false },   /* a hyphen, which is not in the engine's alphabet */
            { "mace baf",       false },
            { ".baf",           true  },   /* a leading dot, which the engine's gate takes */
            { "obiwan",         true  },   /* no dot at all, which it never length checks */
            { "mace\nbaf",      false },
            { "..\\..\\windows", false },
            { "",               true  },   /* empty is "not known", and only in the roster */
        };
        size_t i;

        for (i = 0; i < sizeof SHAPES / sizeof SHAPES[0]; ++i) {
            memset(asset, 0, sizeof asset);
            memcpy(asset, SHAPES[i].asset, strlen(SHAPES[i].asset));
            ut_checkf(mp_roster_asset_is_sound(asset) == SHAPES[i].sound, "\"%s\" is %s",
                      SHAPES[i].asset, SHAPES[i].sound ? "sound" : "refused");
        }
    }

    memset(asset, 'x', sizeof asset);
    ut_check(!mp_roster_asset_is_sound(asset), "a field with no terminator inside it is refused");
    memset(asset, 0, sizeof asset);
    memcpy(asset, "mace.baf", 8u);
    asset[MP_EVENT_ASSET_MAX - 1u] = 'x';
    ut_check(!mp_roster_asset_is_sound(asset),
             "and so is padding that carries a byte, so the encoding stays canonical");
    ut_check(!mp_roster_asset_is_sound(NULL), "and none is not a field at all");

    ut_section("cleaning an asset, so one bad name cannot cost everybody the player list");

    mp_roster_asset_clean("quigon.baf", asset);
    ut_check(strcmp(asset, "quigon.baf") == 0, "a sound name is kept");
    mp_roster_asset_clean("stnd-wp1.baf", asset);
    ut_check(asset[0] == '\0', "one the engine could never load becomes not known");
    mp_roster_asset_clean("abcdefghijklmnopqrstuvwxyz0123456789", asset);
    ut_check(asset[0] == '\0',
             "and so does one too long for the field, because cutting an asset name renames it");
    mp_roster_asset_clean("abcdefghijklmnopqrstuvwxyz01234", asset);
    ut_check(strlen(asset) == MP_EVENT_ASSET_MAX - 1u && mp_roster_asset_is_sound(asset),
             "while the longest one that does fit is kept whole");
    mp_roster_asset_clean(NULL, asset);
    ut_check(asset[0] == '\0', "and so does none");
}

/* `bytes` is the three entries of `roster` as the round trip left them on the wire. */
static void check_change_detection(const uint8_t *bytes, mp_roster_t roster)
{
    mp_roster_t back;

    ut_section("change detection");
    ut_check(mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back) &&
                 mp_roster_equal(&roster, &back),
             "the same table compares equal");
    back.entry[1].rtt_ms += 1u;
    ut_check(!mp_roster_equal(&roster, &back), "a changed round trip is a change");
    back = roster;
    memcpy(back.entry[2].asset, "queen.baf", 10u);
    ut_check(!mp_roster_equal(&roster, &back),
             "a player who changed appearance is a change, or the table would never be resent");
    back = roster;
    back.count = 2u;
    ut_check(!mp_roster_equal(&roster, &back), "and so is a peer fewer");

    /* What the sender gates on, and it is deliberately not the comparison above.
     *
     * The round trip is a live measurement: it moves almost every substep, so a sender that asks
     * "is this table equal to the last" is told no almost every substep and offers a note meant
     * for once a second thirty two times a second. That flooded the reliable channel and took the
     * slots a savegame transfer needed. Everything a player can SEE still counts. */
    ut_section("what a sender gates on leaves the round trip out");
    back = roster;
    back.entry[1].rtt_ms += 7u;
    ut_check(!mp_roster_differs_to_a_player(&roster, &back),
             "a table that differs only in a ping is not worth a note of its own");
    ut_check(!mp_roster_equal(&roster, &back),
             "though it is still not equal, so a decoder test keeps its full comparison");
    back.entry[1].ready = (uint8_t)!back.entry[1].ready;
    ut_check(mp_roster_differs_to_a_player(&roster, &back),
             "a ready flag beside that ping is worth one at once");
    back = roster;
    memcpy(back.entry[2].asset, "queen.baf", 10u);
    ut_check(mp_roster_differs_to_a_player(&roster, &back), "and so is an appearance");
    back = roster;
    back.count = 2u;
    ut_check(mp_roster_differs_to_a_player(&roster, &back), "and a peer fewer");
}

static void check_the_round_trip(void)
{
    uint8_t     bytes[MP_ROSTER_BYTES + 8u];
    mp_roster_t roster;
    mp_roster_t back;

    ut_section("a round trip");
    memset(&roster, 0, sizeof roster);
    roster.count = 3u;
    roster.entry[0].slot = 0u;
    memcpy(roster.entry[0].name, "Host", 5u);
    roster.entry[1].slot = 1u;
    roster.entry[1].rtt_ms = 23u;
    roster.entry[1].ready = 1u;
    memcpy(roster.entry[1].name, "Guest One", 10u);
    roster.entry[2].slot = 2u;
    roster.entry[2].team = 1u;
    roster.entry[2].rtt_ms = 65535u;
    memcpy(roster.entry[2].name, "Guest Two", 10u);
    /* Entry 1 has changed appearance, entry 0 and entry 2 have not. That is the ordinary state of
     * a table: an empty asset is a line that has nothing to say about how its player looks. */
    memcpy(roster.entry[1].asset, "mace.baf", 9u);
    ut_check(mp_roster_encode(&roster, bytes, sizeof bytes) == MP_ROSTER_BYTES_FOR(3u),
             "three entries encode to the length three entries take");
    ut_check(mp_roster_is_roster(bytes, MP_ROSTER_BYTES_FOR(3u)),
             "and are recognised by tag and length");
    ut_check(!mp_roster_is_roster(bytes, MP_ROSTER_BYTES_FOR(3u) - 1u),
             "a byte short is not a roster");
    ut_check(!mp_roster_is_roster(bytes, MP_ROSTER_BYTES_FOR(3u) + 1u),
             "and neither is a byte long: the count in the message decides the length exactly, "
             "so this is not a range that a longer message could slip through");
    ut_check(!mp_roster_is_roster(bytes, MP_ROSTER_BYTES),
             "the message a padded encoder would have produced is refused, because its own count "
             "says three");
    ut_check(mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back), "they decode");
    ut_check(mp_roster_equal(&roster, &back), "field for field");
    ut_check(back.entry[2].rtt_ms == 65535u && back.entry[1].ready == 1u,
             "the round trip and the ready flag survive");

    ut_section("the appearance rides the roster, which is what a late joiner reads");

    ut_check(strcmp(back.entry[1].asset, "mace.baf") == 0,
             "the asset name of the player who changed came back");
    ut_check(back.entry[0].asset[0] == '\0' && back.entry[2].asset[0] == '\0',
             "and the two who did not change carry an empty field, which means not known");

    ut_section("what is refused");
    roster.count = 9u;
    ut_check(mp_roster_encode(&roster, bytes, sizeof bytes) == 0u, "a count past the table");
    roster.count = 3u;
    ut_check(mp_roster_encode(&roster, bytes, MP_ROSTER_BYTES_FOR(3u) - 1u) == 0u,
             "a buffer too small");
    roster.entry[1].ready = 2u;
    ut_check(mp_roster_encode(&roster, bytes, sizeof bytes) == 0u,
             "a ready that is neither 0 nor 1");
    roster.entry[1].ready = 1u;
    roster.entry[0].name[0] = '\0';
    ut_check(mp_roster_encode(&roster, bytes, sizeof bytes) == 0u, "an empty name");
    memcpy(roster.entry[0].name, "Host", 5u);
    memcpy(roster.entry[0].asset, "stnd-wp1.baf", 13u);
    ut_check(mp_roster_encode(&roster, bytes, sizeof bytes) == 0u,
             "an asset name the engine's own gate would refuse");
    memset(roster.entry[0].asset, 0, sizeof roster.entry[0].asset);
    ut_check(mp_roster_encode(&roster, bytes, sizeof bytes) == MP_ROSTER_BYTES_FOR(3u),
             "and all of that healed");
    bytes[1] = 17u;
    ut_check(!mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back),
             "a decoded count past the table is refused");
    bytes[1] = 3u;
    bytes[2u + 6u] = 0x01u;   /* the first byte of the first name: slot, team, ready, hero, rtt */
    ut_check(!mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back),
             "and so is a name with a control character in it");
    bytes[2u + 6u] = 'H';

    ut_section("an asset off the wire is refused the same way, because a stranger wrote it");

    /* This is the one field on this message whose validation is not tidiness. A receiver hands the
     * name to the engine's resource loader; a miss there is a message box and an exit, so a
     * stranger who could choose the name could end everybody's game. */
    {
        static const struct { const char *asset; bool sound; } FORGED[] = {
            { "mace-baf",         false },   /* a hyphen, which the engine's alphabet has not */
            { "ma.ce.baf",        false },   /* a second dot */
            { "aaaaaaaaaaaa.baf", false },   /* a stem of twelve, refused from nine up */
            { "mace.abcd",        false },   /* an extension of four */
            { "mace baf",         false },
            { "mace.baf",         true  },   /* and the sound one, taken again */
        };
        uint8_t *field = &bytes[ENTRY_AT(1u) + ASSET_IN_ENTRY];
        size_t   i;

        for (i = 0; i < sizeof FORGED / sizeof FORGED[0]; ++i) {
            memset(field, 0, MP_EVENT_ASSET_MAX);
            memcpy(field, FORGED[i].asset, strlen(FORGED[i].asset));
            ut_checkf(mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back) == FORGED[i].sound,
                      "the decoder %s \"%s\"", FORGED[i].sound ? "takes" : "refuses",
                      FORGED[i].asset);
        }
        ut_check(strcmp(back.entry[1].asset, "mace.baf") == 0,
                 "and the last of those, the sound one, arrived as itself");

        field[MP_EVENT_ASSET_MAX - 1u] = 'x';
        ut_check(!mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back),
                 "padding that carries something is refused, so one name has one encoding");
        field[MP_EVENT_ASSET_MAX - 1u] = '\0';
        memset(field, 'a', MP_EVENT_ASSET_MAX);
        ut_check(!mp_roster_decode(bytes, MP_ROSTER_BYTES_FOR(3u), &back),
                 "and a field with no terminator is refused rather than read off the end");
        memset(field, 0, MP_EVENT_ASSET_MAX);
        memcpy(field, "mace.baf", 8u);
    }
    check_change_detection(bytes, roster);
}

int main(void)
{
    check_the_shape();
    check_the_name_rule();
    check_the_asset_rule();
    check_the_round_trip();
    check_the_padded_buffer_is_not_a_roster();
    check_the_scale_in_the_table();
    check_the_kind_in_the_table();

    return ut_summary("mp_roster");
}
