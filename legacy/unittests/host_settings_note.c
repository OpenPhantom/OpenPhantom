/* host_settings_note.c: the host's world settings and the consumers' acknowledgements, published
 * and read in one process through the real shared_note.
 *
 * Both halves are DLLs of one process in the game, so a test that publishes and then reads is the
 * real case with one process doing both. What is worth pinning is what decides whether a machine
 * plays with the host's value or its own: no record and a record of another shape both mean its
 * own, a record that does not run names nothing, a value outside the table is never filed or
 * read, a reader that has found nothing asks the channel at most once a second, and every name an
 * acknowledgement is filed under is one the channel accepts.
 *
 * The sections run in order and share the process's one channel, so the first one runs before
 * anything has been published.
 */
#include "unittest.h"

#include "common/host_settings_note.h"
#include "common/shared_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static host_settings_t a_record(bool running, uint16_t present, float range, float band,
                                float authored, float mode)
{
    host_settings_t record;

    memset(&record, 0, sizeof record);
    record.running    = running;
    record.generation = 1u;
    record.present    = present;
    record.values[HOST_SETTING_VIEW_RANGE_SCALE]  = range;
    record.values[HOST_SETTING_FOG_BAND_SCALE]    = band;
    record.values[HOST_SETTING_AUTHORED_FOG_BAND] = authored;
    record.values[HOST_SETTING_DISMEMBERMENT_MODE] = mode;
    record.published  = 1u;
    return record;
}

#define ALL_FOUR 0x000Fu

static void check_no_record_and_the_retry(void)
{
    host_settings_t record;
    float           value = 7.0f;

    ut_section("with no record a consumer keeps its own value, and asks once a second at most");
    memset(&record, 0x5A, sizeof record);
    ut_check(!host_settings_read(&record), "nobody has filed the host's settings yet");
    ut_check(record.published == 0x5A5A5A5Au,
             "and the read wrote nothing into the caller's record");
    ut_check(!host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 1000u) && value == 7.0f,
             "so the question a consumer asks answers no, which is: use your own");

    record = a_record(true, ALL_FOUR, 1.5f, 0.5f, 1.0f, 2.0f);
    ut_check(host_settings_publish(&record), "a session's multiplayer then files a record");
    ut_check(!host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 1500u),
             "and a consumer that missed half a second ago does not ask again yet");
    ut_check(host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 2000u) && value == 1.5f,
             "a second after its miss it asks, and has the host's 1.50");

    record.running = false;
    record.present = 0u;
    ut_check(host_settings_publish(&record), "the session ends and the record says so");
    ut_check(!host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 2001u),
             "once found, the record is read at every question: the next one hears the end");
    record = a_record(true, ALL_FOUR, 2.0f, 0.5f, 1.0f, 2.0f);
    ut_check(host_settings_publish(&record) &&
                 host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 2002u) &&
                 value == 2.0f,
             "and a new session is heard at the question after, with no second of waiting");
}

static void check_the_table(void)
{
    const host_setting_key_t *keys;
    size_t                    count = 0;

    ut_section("the table names the four keys with their consumers' ranges and defaults");
    keys = host_settings_keys(&count);
    ut_check(keys != NULL && count == (size_t)HOST_SETTING_COUNT && count == 4u,
             "four settings, in the order of the ids, which is the wire's order");
    ut_check(strcmp(keys[HOST_SETTING_VIEW_RANGE_SCALE].section, "view_distance_fix") == 0 &&
                 strcmp(keys[HOST_SETTING_VIEW_RANGE_SCALE].key, "ViewRangeScale") == 0 &&
                 keys[HOST_SETTING_VIEW_RANGE_SCALE].minimum == 1.0f &&
                 keys[HOST_SETTING_VIEW_RANGE_SCALE].maximum == 2.5f &&
                 keys[HOST_SETTING_VIEW_RANGE_SCALE].default_value == 1.0f &&
                 !keys[HOST_SETTING_VIEW_RANGE_SCALE].whole_numbers,
             "the draw distance: [view_distance_fix] ViewRangeScale, 1.0 to 2.5, default 1.0");
    ut_check(strcmp(keys[HOST_SETTING_FOG_BAND_SCALE].key, "FogBandScale") == 0 &&
                 keys[HOST_SETTING_FOG_BAND_SCALE].minimum == 0.25f &&
                 keys[HOST_SETTING_FOG_BAND_SCALE].maximum == 1.0f &&
                 keys[HOST_SETTING_FOG_BAND_SCALE].default_value == 1.0f &&
                 !keys[HOST_SETTING_FOG_BAND_SCALE].whole_numbers,
             "the fog band: FogBandScale, 0.25 to 1.0, default 1.0");
    ut_check(strcmp(keys[HOST_SETTING_AUTHORED_FOG_BAND].key, "AuthoredFogBand") == 0 &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].minimum == 0.0f &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].maximum == 1.0f &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].default_value == 0.0f &&
                 keys[HOST_SETTING_AUTHORED_FOG_BAND].whole_numbers,
             "the authored band: AuthoredFogBand, a switch that starts off");
    ut_check(strcmp(keys[HOST_SETTING_DISMEMBERMENT_MODE].section, "dismemberment") == 0 &&
                 strcmp(keys[HOST_SETTING_DISMEMBERMENT_MODE].key, "Mode") == 0 &&
                 keys[HOST_SETTING_DISMEMBERMENT_MODE].minimum == 0.0f &&
                 keys[HOST_SETTING_DISMEMBERMENT_MODE].maximum == 2.0f &&
                 keys[HOST_SETTING_DISMEMBERMENT_MODE].default_value == 0.0f &&
                 keys[HOST_SETTING_DISMEMBERMENT_MODE].whole_numbers,
             "the dismemberment: [dismemberment] Mode, 0 to 2, whole, default off");
    ut_check(host_settings_keys(NULL) == keys, "and a caller that needs no count still gets it");
}

static void check_what_is_admitted(void)
{
    ut_section("a value is admitted when it is finite and inside its key's range");
    ut_check(host_settings_value_is_admitted(HOST_SETTING_VIEW_RANGE_SCALE, 1.0f) &&
                 host_settings_value_is_admitted(HOST_SETTING_VIEW_RANGE_SCALE, 2.5f),
             "both ends of the draw distance");
    ut_check(!host_settings_value_is_admitted(HOST_SETTING_VIEW_RANGE_SCALE, 0.99f) &&
                 !host_settings_value_is_admitted(HOST_SETTING_VIEW_RANGE_SCALE, 2.51f),
             "and nothing just past either");
    ut_check(host_settings_value_is_admitted(HOST_SETTING_FOG_BAND_SCALE, 0.25f) &&
                 !host_settings_value_is_admitted(HOST_SETTING_FOG_BAND_SCALE, 0.24f),
             "the fog band starts at a quarter");
    ut_check(!host_settings_value_is_admitted(HOST_SETTING_DISMEMBERMENT_MODE, 3.0f) &&
                 !host_settings_value_is_admitted(HOST_SETTING_DISMEMBERMENT_MODE, -1.0f),
             "no fourth dismemberment mode and no negative one");
    ut_check(!host_settings_value_is_admitted(HOST_SETTING_VIEW_RANGE_SCALE, NAN) &&
                 !host_settings_value_is_admitted(HOST_SETTING_VIEW_RANGE_SCALE, INFINITY),
             "NaN and infinity are never a setting");
    ut_check(!host_settings_value_is_admitted(HOST_SETTING_COUNT, 1.0f) &&
                 !host_settings_value_is_admitted((host_setting_id_t)99, 1.0f),
             "and a setting the table does not have admits nothing");
}

static void check_what_is_refused(void)
{
    host_settings_t good = a_record(true, ALL_FOUR, 1.25f, 0.75f, 0.0f, 1.0f);
    host_settings_t bad;
    host_settings_t heard;

    ut_section("a record the table would not admit is not filed, and the one before it stands");
    ut_check(host_settings_publish(&good), "a sound record is filed");

    bad = good;
    bad.values[HOST_SETTING_VIEW_RANGE_SCALE] = 3.0f;
    ut_check(!host_settings_publish(&bad), "a draw distance of 3.0 is refused");
    bad = good;
    bad.values[HOST_SETTING_FOG_BAND_SCALE] = NAN;
    ut_check(!host_settings_publish(&bad), "a fog band of NaN is refused");
    bad = good;
    bad.present = 0x0010u;
    ut_check(!host_settings_publish(&bad),
             "a bit for a setting this build does not know is refused");
    bad = good;
    bad.running = false;
    ut_check(!host_settings_publish(&bad), "a record that does not run and still names something");
    ut_check(!host_settings_publish(NULL), "and no record at all");

    memset(&heard, 0, sizeof heard);
    ut_check(host_settings_read(&heard) && heard.running && heard.present == ALL_FOUR &&
                 heard.values[HOST_SETTING_VIEW_RANGE_SCALE] == 1.25f &&
                 heard.values[HOST_SETTING_DISMEMBERMENT_MODE] == 1.0f,
             "after every refusal the reader still sees the sound record");

    bad = good;
    bad.present = (uint16_t)(1u << HOST_SETTING_VIEW_RANGE_SCALE);
    bad.values[HOST_SETTING_DISMEMBERMENT_MODE] = 99.0f;   /* not named, so not judged */
    ut_check(host_settings_publish(&bad) && host_settings_read(&heard) &&
                 heard.present == bad.present &&
                 heard.values[HOST_SETTING_DISMEMBERMENT_MODE] == 0.0f,
             "a value the host did not name is not judged, and is filed and read as 0");
}

static void check_a_record_of_another_shape(void)
{
    host_settings_t good = a_record(true, ALL_FOUR, 1.5f, 1.0f, 1.0f, 2.0f);
    uint8_t         raw[sizeof(host_settings_t) + 4u];
    host_settings_t heard;
    float           value = 0.0f;

    ut_section("a record of another shape is read as no record, which means the consumer's own");
    ut_check(host_settings_publish(&good), "a sound record first");

    memset(raw, 0, sizeof raw);
    memcpy(raw, &good, sizeof good);
    ut_check(shared_note_publish(HOST_SETTINGS_NOTE_NAME, raw, sizeof raw),
             "another build files four bytes more under the same name");
    ut_check(!host_settings_read(&heard) &&
                 !host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 5000u),
             "and a reader refuses it whole, so the consumer uses its own value");

    memcpy(raw, &good, sizeof good);
    raw[offsetof(host_settings_t, running)] = 2u;
    ut_check(shared_note_publish(HOST_SETTINGS_NOTE_NAME, raw, sizeof good) &&
                 !host_settings_read(&heard),
             "a running byte that is neither 0 nor 1 is refused");

    memcpy(raw, &good, sizeof good);
    raw[offsetof(host_settings_t, present)] = 0x30u;
    ut_check(shared_note_publish(HOST_SETTINGS_NOTE_NAME, raw, sizeof good) &&
                 !host_settings_read(&heard),
             "a bit for a setting this build does not know is refused on the way in as well");

    good.values[HOST_SETTING_VIEW_RANGE_SCALE] = 9.0f;
    memcpy(raw, &good, sizeof good);
    ut_check(shared_note_publish(HOST_SETTINGS_NOTE_NAME, raw, sizeof good) &&
                 !host_settings_read(&heard) &&
                 !host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 5001u),
             "and so is a draw distance of 9.0 that no publisher here would have filed");
}

static void check_the_question(void)
{
    host_settings_t record = a_record(true, (uint16_t)(1u << HOST_SETTING_FOG_BAND_SCALE), 1.0f,
                                      0.5f, 0.0f, 0.0f);
    float           value = 0.0f;

    ut_section("the consumer's question: the host's value while a session runs and names it");
    ut_check(host_settings_publish(&record), "a host that names only the fog band");
    ut_check(host_settings_value(HOST_SETTING_FOG_BAND_SCALE, &value, 6000u) && value == 0.5f,
             "the fog band is the host's 0.50");
    ut_check(!host_settings_value(HOST_SETTING_VIEW_RANGE_SCALE, &value, 6000u),
             "the draw distance it did not name stays this machine's own");
    ut_check(!host_settings_value(HOST_SETTING_COUNT, &value, 6000u) &&
                 !host_settings_value(HOST_SETTING_FOG_BAND_SCALE, NULL, 6000u),
             "a setting that does not exist, or nowhere to put it, is no");

    record.running = false;
    record.present = 0u;
    ut_check(host_settings_publish(&record) &&
                 !host_settings_value(HOST_SETTING_FOG_BAND_SCALE, &value, 6001u),
             "a session that is over names nothing, and every consumer is back on its own");
}

static void check_the_acknowledgements(void)
{
    static const char *const CONSUMERS[] = { "view_distance_fix", "dismemberment" };
    host_settings_taken_t    said;
    host_settings_taken_t    heard;
    char                     name[SHARED_NOTE_NAME_MAX + 8u];
    size_t                   i;

    ut_section("each consumer files what it applies under a name the channel accepts");
    for (i = 0; i < sizeof CONSUMERS / sizeof CONSUMERS[0]; ++i) {
        size_t prefix = strlen(HOST_SETTINGS_TAKEN_PREFIX);
        size_t length = strlen(CONSUMERS[i]);

        memcpy(name, HOST_SETTINGS_TAKEN_PREFIX, prefix);
        memcpy(name + prefix, CONSUMERS[i], length + 1u);
        ut_checkf(shared_note_name_is_sound(name), "%s is a name the channel files", name);

        memset(&said, 0, sizeof said);
        said.in_force   = (uint16_t)(1u << HOST_SETTING_VIEW_RANGE_SCALE);
        said.generation = 7u;
        said.effective[HOST_SETTING_VIEW_RANGE_SCALE] = 1.21f;
        said.effective[HOST_SETTING_FOG_BAND_SCALE]   = 0.8f;   /* not in force */
        said.published  = (uint32_t)(i + 1u);
        memset(&heard, 0x5A, sizeof heard);
        ut_checkf(host_settings_publish_taken(CONSUMERS[i], &said) &&
                      host_settings_read_taken(CONSUMERS[i], &heard) &&
                      heard.in_force == said.in_force && heard.generation == 7u &&
                      heard.effective[HOST_SETTING_VIEW_RANGE_SCALE] == 1.21f &&
                      heard.effective[HOST_SETTING_FOG_BAND_SCALE] == 0.0f &&
                      heard.published == said.published,
                  "%s: filed and read back, the value not in force as 0", CONSUMERS[i]);
    }

    memset(&said, 0, sizeof said);
    ut_check(!host_settings_publish_taken("abcdefghijklmnopqrstu", &said) &&
                 host_settings_publish_taken("abcdefghijklmnopqrst", &said),
             "a consumer name of 21 characters does not fit behind the prefix, one of 20 does");
    ut_check(!host_settings_publish_taken("view:distance", &said) &&
                 !host_settings_publish_taken("", &said) &&
                 !host_settings_publish_taken(NULL, &said),
             "a name with a colon, an empty one and none are refused");
    said.in_force = 0x0100u;
    ut_check(!host_settings_publish_taken("dismemberment", &said),
             "a bit for a setting this build does not know is refused");
    said.in_force = (uint16_t)(1u << HOST_SETTING_DISMEMBERMENT_MODE);
    said.effective[HOST_SETTING_DISMEMBERMENT_MODE] = INFINITY;
    ut_check(!host_settings_publish_taken("dismemberment", &said),
             "and so is an effective value that is not finite");
    ut_check(host_settings_read_taken("dismemberment", &heard) && heard.published == 2u,
             "the acknowledgement before the refusals still stands");
    ut_check(!host_settings_read_taken("variable_fov", &heard),
             "and a consumer that never answered has none to read");
}

int main(void)
{
    check_no_record_and_the_retry();
    check_the_table();
    check_what_is_admitted();
    check_what_is_refused();
    check_a_record_of_another_shape();
    check_the_question();
    check_the_acknowledgements();
    return ut_summary("host_settings_note");
}
