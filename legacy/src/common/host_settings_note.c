/* common/host_settings_note.c: the host's world settings and each consumer's acknowledgement,
 * filed through common/shared_note. The contract is in the header.
 */
#include "host_settings_note.h"

#include "shared_note.h"
#include "text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The ranges and defaults are the consumers' own. The draw distance is held to 1.0 to 2.5 and
 * starts at 1.0; the fog band to 0.25 to 1.0 and starts at 1.0; the authored band is a switch that
 * starts off; the dismemberment mode is 0 (off), 1 (the node only) or 2 (on the killing blow as
 * well) and starts off. The unit tests of those mods keep these numbers against their clamps,
 * because a copy of a bound is how two places stop agreeing. */
static const host_setting_key_t KEYS[HOST_SETTING_COUNT] = {
    { "view_distance_fix", "ViewRangeScale",  1.0f,  2.5f, 1.0f, false },
    { "view_distance_fix", "FogBandScale",    0.25f, 1.0f, 1.0f, false },
    { "view_distance_fix", "AuthoredFogBand", 0.0f,  1.0f, 0.0f, true },
    { "dismemberment",     "Mode",            0.0f,  2.0f, 0.0f, true },
};

/* One bit per setting the table has. A record naming any other bit names a setting this build
 * does not know. */
#define KNOWN_BITS ((1u << HOST_SETTING_COUNT) - 1u)

/* The running flag is the one byte of the record whose other values are not a legal value of its
 * type, so a reader looks at it as a byte before it copies the record into a bool. */
_Static_assert(offsetof(host_settings_t, running) == 0u, "running is the record's first byte");

/* Whether a read has ever found the record in this module, and when the last miss was. Each DLL
 * links its own copy of this file, so each keeps its own. */
typedef struct host_settings_reader {
    bool     found_once;
    bool     missed_once;
    uint32_t missed_at_ms;
} host_settings_reader_t;

static host_settings_reader_t reader;

const host_setting_key_t *host_settings_keys(size_t *count)
{
    if (count != NULL) {
        *count = HOST_SETTING_COUNT;
    }
    return KEYS;
}

bool host_settings_value_is_admitted(host_setting_id_t id, float value)
{
    if ((unsigned)id >= (unsigned)HOST_SETTING_COUNT || !isfinite(value)) {
        return false;
    }
    return value >= KEYS[id].minimum && value <= KEYS[id].maximum;
}

/* A record that does not run names nothing: a consumer asked about it has to fall back to its own
 * value whatever the bits say, and a record that says both would be read two ways. */
static bool present_is_sound(bool running, unsigned present)
{
    return (present & ~KNOWN_BITS) == 0u && (running || present == 0u);
}

/* The note filed under `name`, when it has exactly the size of the record a reader expects. The
 * size is the shape: a record of another size was written by another build. */
static bool read_whole(const char *name, uint8_t *raw, size_t size)
{
    size_t count = 0;

    return shared_note_read(name, raw, SHARED_NOTE_BYTES, &count, NULL) && count == size;
}

bool host_settings_publish(const host_settings_t *settings)
{
    host_settings_t record;
    size_t          id;

    if (settings == NULL || !present_is_sound(settings->running, settings->present)) {
        return false;
    }
    /* A value the host did not name is filed as 0, and the padding-free record is written whole,
     * so two publications that say the same thing are the same bytes. */
    memset(&record, 0, sizeof record);
    record.running    = settings->running ? true : false;
    record.generation = settings->generation;
    record.present    = settings->present;
    record.published  = settings->published;
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((settings->present & (1u << id)) == 0u) {
            continue;
        }
        if (!host_settings_value_is_admitted((host_setting_id_t)id, settings->values[id])) {
            return false;
        }
        record.values[id] = settings->values[id];
    }
    return shared_note_publish(HOST_SETTINGS_NOTE_NAME, &record, sizeof record);
}

bool host_settings_read(host_settings_t *out)
{
    uint8_t         raw[SHARED_NOTE_BYTES];
    host_settings_t record;
    size_t          id;

    if (out == NULL || !read_whole(HOST_SETTINGS_NOTE_NAME, raw, sizeof record) ||
        raw[offsetof(host_settings_t, running)] > 1u) {
        return false;
    }
    memcpy(&record, raw, sizeof record);
    if (!present_is_sound(record.running, record.present)) {
        return false;
    }
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((record.present & (1u << id)) == 0u) {
            record.values[id] = 0.0f;
        } else if (!host_settings_value_is_admitted((host_setting_id_t)id, record.values[id])) {
            return false;
        }
    }
    *out = record;
    return true;
}

bool host_settings_value(host_setting_id_t id, float *value, uint32_t now_ms)
{
    host_settings_t record;

    if (value == NULL || (unsigned)id >= (unsigned)HOST_SETTING_COUNT) {
        return false;
    }
    /* A name nobody filed is a failed lookup in the operating system, and a consumer asks from its
     * poll. Once a read has found the record the mapping stays open and a read is a copy, so only
     * the misses before that first find are spaced out. */
    if (!reader.found_once && reader.missed_once &&
        now_ms - reader.missed_at_ms < HOST_SETTINGS_RETRY_MS) {
        return false;
    }
    if (!host_settings_read(&record)) {
        if (!reader.found_once) {
            reader.missed_once  = true;
            reader.missed_at_ms = now_ms;
        }
        return false;
    }
    reader.found_once = true;
    if (!record.running || (record.present & (1u << id)) == 0u) {
        return false;
    }
    *value = record.values[id];
    return true;
}

/* The acknowledgement's name: the prefix, then the consumer's own. False when the two do not fit
 * the channel's limit together or the result is not a name the channel files. */
static bool taken_name(const char *mod, char *out, size_t out_size)
{
    size_t prefix = sizeof HOST_SETTINGS_TAKEN_PREFIX - 1u;
    size_t length;

    if (mod == NULL) {
        return false;
    }
    length = strlen(mod);
    if (length == 0u || prefix + length + 1u > out_size) {
        return false;
    }
    text_format(out, out_size, "%s%s", HOST_SETTINGS_TAKEN_PREFIX, mod);
    return shared_note_name_is_sound(out);
}

/* Whether an acknowledgement says only what one may: bits of known settings, and a finite value
 * for each of them. */
static bool taken_is_sound(const host_settings_taken_t *taken)
{
    size_t id;

    if ((taken->in_force & ~KNOWN_BITS) != 0u) {
        return false;
    }
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((taken->in_force & (1u << id)) != 0u && !isfinite(taken->effective[id])) {
            return false;
        }
    }
    return true;
}

bool host_settings_publish_taken(const char *mod, const host_settings_taken_t *taken)
{
    char                  name[SHARED_NOTE_NAME_MAX];
    host_settings_taken_t record;
    size_t                id;

    if (taken == NULL || !taken_name(mod, name, sizeof name) || !taken_is_sound(taken)) {
        return false;
    }
    /* Zero for a setting not in force, as the header promises, and zero in the padding byte, so a
     * reader never sees the caller's leftovers. */
    memset(&record, 0, sizeof record);
    record.in_force   = taken->in_force;
    record.generation = taken->generation;
    record.published  = taken->published;
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((taken->in_force & (1u << id)) != 0u) {
            record.effective[id] = taken->effective[id];
        }
    }
    return shared_note_publish(name, &record, sizeof record);
}

bool host_settings_read_taken(const char *mod, host_settings_taken_t *out)
{
    char                  name[SHARED_NOTE_NAME_MAX];
    uint8_t               raw[SHARED_NOTE_BYTES];
    host_settings_taken_t record;
    size_t                id;

    if (out == NULL || !taken_name(mod, name, sizeof name) ||
        !read_whole(name, raw, sizeof record)) {
        return false;
    }
    memcpy(&record, raw, sizeof record);
    if (!taken_is_sound(&record)) {
        return false;
    }
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        if ((record.in_force & (1u << id)) == 0u) {
            record.effective[id] = 0.0f;
        }
    }
    *out = record;
    return true;
}
