/* mp_host_settings_rule.c: the note that carries the host's world settings, and what the host
 * reads out of its own configuration.
 *
 * The codec is held to both directions of its bounds against the table in common/
 * host_settings_note: every value the encoder writes the decoder takes, and every note the decoder
 * refuses is one the encoder would not have written. The fuzz over random and mutated notes is in
 * mp_fuzz_notes with the other codecs; this file names the cases one by one. The host's own reading
 * is held against what each mod does with the same ini line.
 */
#include "unittest.h"

#include "mp_host_settings_rule.h"

#include "common/host_settings_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_host_settings_note_t a_note(void)
{
    mp_host_settings_note_t note;

    memset(&note, 0, sizeof note);
    note.cheats  = (uint8_t)MP_HOST_SETTINGS_CHEAT_HAPPY;
    note.present = 0x000Fu;
    note.values[HOST_SETTING_VIEW_RANGE_SCALE]   = 1.5f;
    note.values[HOST_SETTING_FOG_BAND_SCALE]     = 0.25f;
    note.values[HOST_SETTING_AUTHORED_FOG_BAND]  = 1.0f;
    note.values[HOST_SETTING_DISMEMBERMENT_MODE] = 2.0f;
    return note;
}

static void check_the_round_trip(void)
{
    mp_host_settings_note_t said = a_note();
    mp_host_settings_note_t heard;
    uint8_t                 bytes[MP_HOST_SETTINGS_BYTES + 4u];
    size_t                  length;

    ut_section("a note the host writes is the note a client reads");
    ut_check(MP_HOST_SETTINGS_TAG == 0xABu && MP_HOST_SETTINGS_BYTES == 13u,
             "tag 0xAB, thirteen bytes for four settings");
    length = mp_host_settings_encode(&said, bytes, sizeof bytes);
    ut_check(length == MP_HOST_SETTINGS_BYTES && bytes[0] == 0xABu && bytes[1] == 1u,
             "it encodes with its tag and version 1");
    memset(&heard, 0x5A, sizeof heard);
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_TAKEN,
             "and decodes");
    ut_check(heard.cheats == said.cheats && heard.present == said.present &&
                 heard.values[HOST_SETTING_VIEW_RANGE_SCALE] == 1.5f &&
                 heard.values[HOST_SETTING_FOG_BAND_SCALE] == 0.25f &&
                 heard.values[HOST_SETTING_AUTHORED_FOG_BAND] == 1.0f &&
                 heard.values[HOST_SETTING_DISMEMBERMENT_MODE] == 2.0f,
             "with every value and both cheat bits as they were said");

    said.values[HOST_SETTING_VIEW_RANGE_SCALE] = 1.234f;
    length = mp_host_settings_encode(&said, bytes, sizeof bytes);
    ut_check(length != 0u && mp_host_settings_decode(bytes, length, &heard) ==
                                 MP_HOST_SETTINGS_TAKEN &&
                 heard.values[HOST_SETTING_VIEW_RANGE_SCALE] == 1.23f,
             "a value travels in hundredths: 1.234 arrives as 1.23");

    memset(&said, 0, sizeof said);
    length = mp_host_settings_encode(&said, bytes, sizeof bytes);
    ut_check(length == MP_HOST_SETTINGS_BYTES &&
                 mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_TAKEN &&
                 heard.present == 0u && heard.cheats == 0u,
             "a host whose mods are none of these says nothing, and that is a note too");
    ut_check(mp_host_settings_encode(&said, bytes, MP_HOST_SETTINGS_BYTES - 1u) == 0u &&
                 mp_host_settings_encode(NULL, bytes, sizeof bytes) == 0u,
             "no room, or no note, writes nothing");
}

static void check_the_encoder_refuses(void)
{
    mp_host_settings_note_t note;
    uint8_t                 bytes[MP_HOST_SETTINGS_BYTES];

    ut_section("the encoder refuses what the table does not admit");
    note = a_note();
    note.values[HOST_SETTING_VIEW_RANGE_SCALE] = 2.51f;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u,
             "a draw distance past 2.5");
    note = a_note();
    note.values[HOST_SETTING_FOG_BAND_SCALE] = 0.2f;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u,
             "a fog band under a quarter");
    note = a_note();
    note.values[HOST_SETTING_DISMEMBERMENT_MODE] = 1.5f;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u,
             "a dismemberment mode of 1.5, because the mode is a whole number");
    note = a_note();
    note.values[HOST_SETTING_AUTHORED_FOG_BAND] = NAN;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u, "a NaN");
    note = a_note();
    note.present = 0x0010u;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u,
             "a fifth setting this build does not know");
    note = a_note();
    note.cheats = 0x04u;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u, "a third cheat bit");
    note = a_note();
    note.present = 0x0001u;
    note.values[HOST_SETTING_DISMEMBERMENT_MODE] = 7.0f;
    ut_check(mp_host_settings_encode(&note, bytes, sizeof bytes) == MP_HOST_SETTINGS_BYTES &&
                 bytes[MP_HOST_SETTINGS_HEAD_BYTES + 6u] == 0u &&
                 bytes[MP_HOST_SETTINGS_HEAD_BYTES + 7u] == 0u,
             "and a value the host did not name is not judged, it is written as 0");
}

/* A sound note in `bytes`, for the decoder's refusals to be made from. */
static size_t sound_note(uint8_t *bytes)
{
    mp_host_settings_note_t note = a_note();

    return mp_host_settings_encode(&note, bytes, MP_HOST_SETTINGS_BYTES);
}

static void put_hundredths(uint8_t *bytes, size_t id, uint16_t hundredths)
{
    bytes[MP_HOST_SETTINGS_HEAD_BYTES + 2u * id]      = (uint8_t)(hundredths & 0xFFu);
    bytes[MP_HOST_SETTINGS_HEAD_BYTES + 2u * id + 1u] = (uint8_t)(hundredths >> 8);
}

static void check_the_decoder_refuses(void)
{
    uint8_t                 bytes[MP_HOST_SETTINGS_BYTES + 1u];
    mp_host_settings_note_t heard;
    mp_host_settings_note_t untouched;
    size_t                  length;

    ut_section("the decoder refuses what no encoder here would have written, and says why");
    memset(&untouched, 0x5A, sizeof untouched);
    heard  = untouched;
    length = sound_note(bytes);
    bytes[0] = 0xAAu;
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_NOT_THIS_NOTE &&
                 mp_host_settings_decode(NULL, 0u, &heard) == MP_HOST_SETTINGS_NOT_THIS_NOTE,
             "a note of another tag is somebody else's to read");
    length = sound_note(bytes);
    ut_check(mp_host_settings_decode(bytes, length - 1u, &heard) == MP_HOST_SETTINGS_WRONG_SHAPE &&
                 mp_host_settings_decode(bytes, length + 1u, &heard) ==
                     MP_HOST_SETTINGS_WRONG_SHAPE,
             "a byte short or a byte long is another shape");
    bytes[1] = 2u;
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_WRONG_SHAPE,
             "and so is another version");
    length   = sound_note(bytes);
    bytes[2] = 0x07u;
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_UNKNOWN_BIT,
             "a third cheat bit is one this build does not know");
    length   = sound_note(bytes);
    bytes[4] = 0x01u;
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_UNKNOWN_BIT,
             "and so is a setting past the fourth");
    length = sound_note(bytes);
    put_hundredths(bytes, HOST_SETTING_VIEW_RANGE_SCALE, 251u);
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_OUT_OF_RANGE,
             "a draw distance of 2.51 is out of range");
    length = sound_note(bytes);
    put_hundredths(bytes, HOST_SETTING_VIEW_RANGE_SCALE, 99u);
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_OUT_OF_RANGE,
             "and so is 0.99");
    length = sound_note(bytes);
    put_hundredths(bytes, HOST_SETTING_AUTHORED_FOG_BAND, 50u);
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_OUT_OF_RANGE,
             "a switch of 0.50 is not a switch");
    length = sound_note(bytes);
    put_hundredths(bytes, HOST_SETTING_DISMEMBERMENT_MODE, 300u);
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_OUT_OF_RANGE,
             "a fourth dismemberment mode does not exist");
    length   = sound_note(bytes);
    bytes[3] = 0x0Eu;   /* the draw distance not named, its value still 1.50 */
    ut_check(mp_host_settings_decode(bytes, length, &heard) == MP_HOST_SETTINGS_OUT_OF_RANGE,
             "a value for a setting the host did not name is refused");
    ut_check(memcmp(&heard, &untouched, sizeof heard) == 0,
             "and not one of the refusals wrote into the caller's note");
    length = sound_note(bytes);
    ut_check(mp_host_settings_decode(bytes, length, NULL) == MP_HOST_SETTINGS_TAKEN,
             "a caller that only asks what the note is may pass nowhere to put it");
}

static bool taken(const uint8_t *bytes, mp_host_settings_note_t *heard)
{
    return mp_host_settings_decode(bytes, MP_HOST_SETTINGS_BYTES, heard) == MP_HOST_SETTINGS_TAKEN;
}

static void check_the_edges_of_each_range(void)
{
    const host_setting_key_t *keys;
    size_t                    count = 0;
    size_t                    id;
    unsigned                  wrong = 0u;

    ut_section("both ends of every range travel, and one hundredth past either does not");
    keys = host_settings_keys(&count);
    for (id = 0; id < count; ++id) {
        mp_host_settings_note_t note;
        uint8_t                 bytes[MP_HOST_SETTINGS_BYTES];
        uint16_t                low  = (uint16_t)(keys[id].minimum * 100.0f + 0.5f);
        uint16_t                high = (uint16_t)(keys[id].maximum * 100.0f + 0.5f);
        mp_host_settings_note_t heard;

        memset(&note, 0, sizeof note);
        note.present    = (uint16_t)(1u << id);
        note.values[id] = keys[id].minimum;
        wrong += mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u ? 1u : 0u;
        note.values[id] = keys[id].maximum;
        wrong += mp_host_settings_encode(&note, bytes, sizeof bytes) == 0u ? 1u : 0u;
        put_hundredths(bytes, id, high);
        wrong += taken(bytes, &heard) ? 0u : 1u;
        put_hundredths(bytes, id, (uint16_t)(high + 1u));
        wrong += taken(bytes, &heard) ? 1u : 0u;
        put_hundredths(bytes, id, low);
        wrong += taken(bytes, &heard) ? 0u : 1u;
        if (low > 0u) {
            put_hundredths(bytes, id, (uint16_t)(low - 1u));
            wrong += taken(bytes, &heard) ? 1u : 0u;
        }
    }
    ut_checkf(wrong == 0u && count == 4u, "four settings, %u edge(s) answered wrongly", wrong);
}

static void check_what_the_host_says_of_its_own(void)
{
    ut_section("the host says the value its own mod runs, read the way that mod reads it");
    ut_check(mp_host_settings_own_value(HOST_SETTING_VIEW_RANGE_SCALE, 3.0f) == 2.5f,
             "ViewRangeScale=3.0 draws at 2.5, and 2.5 is what the host says");
    ut_check(mp_host_settings_own_value(HOST_SETTING_VIEW_RANGE_SCALE, 0.5f) == 1.0f &&
                 mp_host_settings_own_value(HOST_SETTING_VIEW_RANGE_SCALE, 1.75f) == 1.75f,
             "0.5 draws at 1.0, and 1.75 at 1.75");
    ut_check(mp_host_settings_own_value(HOST_SETTING_FOG_BAND_SCALE, 2.0f) == 1.0f &&
                 mp_host_settings_own_value(HOST_SETTING_FOG_BAND_SCALE, 0.1f) == 0.25f,
             "the fog band is clamped to a quarter and one");
    ut_check(mp_host_settings_own_value(HOST_SETTING_VIEW_RANGE_SCALE, NAN) == 1.0f &&
                 mp_host_settings_own_value(HOST_SETTING_FOG_BAND_SCALE, INFINITY) == 1.0f,
             "a number that is not one is the key's default");
    ut_check(mp_host_settings_own_value(HOST_SETTING_AUTHORED_FOG_BAND, 2.0f) == 1.0f &&
                 mp_host_settings_own_value(HOST_SETTING_AUTHORED_FOG_BAND, -1.0f) == 1.0f &&
                 mp_host_settings_own_value(HOST_SETTING_AUTHORED_FOG_BAND, 0.5f) == 0.0f &&
                 mp_host_settings_own_value(HOST_SETTING_AUTHORED_FOG_BAND, 0.0f) == 0.0f,
             "the authored band is a switch read as an integer: anything but 0 is on, 0.5 is 0");
    ut_check(mp_host_settings_own_value(HOST_SETTING_DISMEMBERMENT_MODE, 1.0f) == 1.0f &&
                 mp_host_settings_own_value(HOST_SETTING_DISMEMBERMENT_MODE, 2.7f) == 2.0f,
             "a mode is read as an integer, so 2.7 is 2");
    ut_check(mp_host_settings_own_value(HOST_SETTING_DISMEMBERMENT_MODE, 5.0f) == 0.0f &&
                 mp_host_settings_own_value(HOST_SETTING_DISMEMBERMENT_MODE, -1.0f) == 0.0f,
             "and a mode the mod does not know is its default, off, as its own load has it");
    ut_check(mp_host_settings_own_value(HOST_SETTING_COUNT, 1.0f) == 0.0f,
             "a setting the table does not have is 0");
}

int main(void)
{
    check_the_round_trip();
    check_the_encoder_refuses();
    check_the_decoder_refuses();
    check_the_edges_of_each_range();
    check_what_the_host_says_of_its_own();
    return ut_summary("mp_host_settings_rule");
}
