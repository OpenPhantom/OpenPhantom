#include "ini.h"

#include "host_image.h"
#include "logging.h"
#include "text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_FILE_NAME "engine_fixes.ini"
#define INI_VALUE_MAX 128

static char ini_file_path[MAX_PATH];

/* --- The read cache ---------------------------------------------------------------------------
 *
 * Every read here used to be one call into the profile API, and on Windows that is microseconds:
 * the system keeps the file mapped and answers from it. Under Wine the same call opens the file,
 * asks for its write time, parses it if that moved and closes it again, two round trips to the
 * server for one key. The developer panel rebuilds every visible row every frame and each row asks
 * for its key, so sixty rows were sixty of those a frame, and a Steam Deck fell to seven frames a
 * second with a few groups open. This remembers what the file answered, per key, and asks again
 * only when the file's write time has moved or this DLL wrote it itself.
 *
 * The write time is asked for at most every INI_RECHECK_MS through ini_generation, one attribute
 * query and no open, so an edit made with the game up is noticed within that and the once-a-second
 * polls the features run see it as they always did. A write through this file drops the whole
 * cache at once, since the profile API may reflow more than the one key.
 *
 * Direct mapped on a hash of section and key: a collision is a refetch, because the slot's own
 * names are compared before it is believed. The value is kept exactly as
 * the profile API returned it, ABSENT sentinel included, so presence is remembered too. A value
 * longer than the slot holds, or a caller with a buffer bigger than the slot, goes to the API
 * directly and is not cached; no key this project ships is that long. */
#define INI_CACHE_SLOTS      256u
#define INI_CACHE_NAME_MAX   48u
#define INI_RECHECK_MS       100u

/* A default no settings file can hold, so its arrival means the key was not there. */
static const char ABSENT[] = "\001\002absent\002\001";

typedef struct ini_cache_slot {
    char     section[INI_CACHE_NAME_MAX];
    char     key[INI_CACHE_NAME_MAX];
    char     value[INI_VALUE_MAX];
    uint64_t generation;
    bool     used;
} ini_cache_slot_t;

static struct {
    ini_cache_slot_t slots[INI_CACHE_SLOTS];
    uint64_t         generation;         /* the file's write time as last checked */
    DWORD            checked_ms;         /* when it was last checked, GetTickCount */
    bool             checked_once;
} ini_cache;

static uint32_t cache_hash(const char *section, const char *key)
{
    uint32_t h = 2166136261u;

    for (; *section != '\0'; ++section) {
        h = (h ^ (uint8_t)*section) * 16777619u;
    }
    h = (h ^ (uint8_t)'/') * 16777619u;
    for (; *key != '\0'; ++key) {
        h = (h ^ (uint8_t)*key) * 16777619u;
    }
    return h;
}

/* The file's write time, re-read at most every INI_RECHECK_MS. A caller that asks for
 * ini_generation itself refreshes this on the way, so a feature that polls the write time every
 * frame and reads on a change is never handed a value from before the change. */
static uint64_t cache_generation(void)
{
    DWORD now = GetTickCount();

    if (!ini_cache.checked_once || (uint32_t)(now - ini_cache.checked_ms) >= INI_RECHECK_MS) {
        (void)ini_generation();
    }
    return ini_cache.generation;
}

static void cache_drop(void)
{
    uint32_t i;

    for (i = 0; i < INI_CACHE_SLOTS; ++i) {
        ini_cache.slots[i].used = false;
    }
    ini_cache.checked_once = false;      /* the next read asks the file system again */
}

/* The raw answer of the profile API for one key, ABSENT when the key is not there. Returns false
 * only when the names do not fit a slot, and then `value` has not been written. */
static bool cache_fetch(const char *section, const char *key, char value[INI_VALUE_MAX])
{
    ini_cache_slot_t *slot;
    uint64_t          generation;

    if (section == NULL || key == NULL || strlen(section) >= INI_CACHE_NAME_MAX ||
        strlen(key) >= INI_CACHE_NAME_MAX) {
        return false;
    }
    generation = cache_generation();
    slot = &ini_cache.slots[cache_hash(section, key) % INI_CACHE_SLOTS];
    if (slot->used && slot->generation == generation && strcmp(slot->section, section) == 0 &&
        strcmp(slot->key, key) == 0) {
        memcpy(value, slot->value, INI_VALUE_MAX);
        return true;
    }

    (void)GetPrivateProfileStringA(section, key, ABSENT, value, (DWORD)INI_VALUE_MAX, ini_path());
    value[INI_VALUE_MAX - 1] = '\0';

    strncpy(slot->section, section, INI_CACHE_NAME_MAX - 1);
    slot->section[INI_CACHE_NAME_MAX - 1] = '\0';
    strncpy(slot->key, key, INI_CACHE_NAME_MAX - 1);
    slot->key[INI_CACHE_NAME_MAX - 1] = '\0';
    memcpy(slot->value, value, INI_VALUE_MAX);
    slot->generation = generation;
    slot->used       = true;
    return true;
}

const char *ini_path(void)
{
    if (ini_file_path[0] == '\0') {
        text_format(ini_file_path, sizeof(ini_file_path), "%s%s", host_directory(), INI_FILE_NAME);
    }
    return ini_file_path;
}

int32_t ini_read_int(const char *section, const char *key, int32_t default_value)
{
    char value[INI_VALUE_MAX];

    if (!cache_fetch(section, key, value)) {
        return (int32_t)GetPrivateProfileIntA(section, key, (INT)default_value, ini_path());
    }
    if (strcmp(value, ABSENT) == 0) {
        return default_value;
    }
    /* What the profile API's own integer reader does with the text: a leading sign, decimal
     * digits, and anything that is not a number reads as zero, not as the default. */
    return (int32_t)strtol(value, NULL, 10);
}

bool ini_read_bool(const char *section, const char *key, bool default_value)
{
    return ini_read_int(section, key, default_value ? 1 : 0) != 0;
}

float ini_read_float(const char *section, const char *key, float default_value)
{
    char   written_default[INI_VALUE_MAX];
    char   value[INI_VALUE_MAX];
    char  *end;
    double parsed;

    text_format(written_default, sizeof(written_default), "%.6f", (double)default_value);

    if (!cache_fetch(section, key, value)) {
        GetPrivateProfileStringA(section, key, written_default, value, (DWORD)sizeof(value),
                                 ini_path());
        value[sizeof(value) - 1] = '\0';
    } else if (strcmp(value, ABSENT) == 0) {
        return default_value;
    }

    /* strtod and not atof, so a value that is not a number is told apart from a zero. Trailing
     * text is allowed, as the integer reader allows it, so "1.5x" reads as 1.5; a value with no
     * digits at all reads as the default and is named once, because a silent zero from a typo
     * looks like a decision. */
    parsed = strtod(value, &end);
    if (end == value) {
        log_warning("[%s] %s=%s is not a number, the default %.6g is in force",
                    section, key, value, (double)default_value);
        return default_value;
    }
    return (float)parsed;
}

bool ini_read_string(const char *section, const char *key, const char *default_value,
                     char *buffer, size_t buffer_size)
{
    DWORD copied;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    /* Absent and empty are different, and the platform call cannot tell them apart on its
       own. It answers with the number of characters it copied, and it copies the default when
       the key is missing, so a non-empty default always came back looking present. Every
       caller in this project happens to pass an empty default, where a count of zero means
       absent by luck; one passes a real one, and its absent branch could never run.

       So the question is asked with a default no settings file can hold, a value carrying
       control characters. Its arrival means the key was not there, and the caller's own
       default is copied in afterwards. */
    if (buffer_size > sizeof ABSENT) {
        char cached[INI_VALUE_MAX];

        if (buffer_size <= INI_VALUE_MAX && cache_fetch(section, key, cached)) {
            strncpy(buffer, cached, buffer_size - 1);
        } else {
            (void)GetPrivateProfileStringA(section, key, ABSENT, buffer, (DWORD)buffer_size,
                                           ini_path());
        }
        buffer[buffer_size - 1] = '\0';

        if (strcmp(buffer, ABSENT) == 0) {
            strncpy(buffer, (default_value != NULL) ? default_value : "", buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return false;
        }
        return true;
    }

    /* Too small to hold the sentinel, so the question cannot be put that way. The count is the
       only signal left, and it is right whenever the default is empty. */
    copied = GetPrivateProfileStringA(section, key, (default_value != NULL) ? default_value : "",
                                      buffer, (DWORD)buffer_size, ini_path());
    buffer[buffer_size - 1] = '\0';

    return copied != 0;
}

bool ini_write_float(const char *section, const char *key, float value, int decimal_places)
{
    char format[16];
    char text[INI_VALUE_MAX];

    if (decimal_places < 0) {
        decimal_places = 0;
    }
    if (decimal_places > 6) {
        decimal_places = 6;
    }

    text_format(format, sizeof(format), "%%.%df", decimal_places);

    text_format(text, sizeof(text), format, (double)value);

    cache_drop();
    return WritePrivateProfileStringA(section, key, text, ini_path()) != 0;
}

bool ini_write_int(const char *section, const char *key, int32_t value)
{
    char text[INI_VALUE_MAX];

    text_format(text, sizeof(text), "%d", (int)value);

    cache_drop();
    return WritePrivateProfileStringA(section, key, text, ini_path()) != 0;
}

uint64_t ini_generation(void)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    uint64_t                  generation = 0u;   /* unreadable reads as unchanged; see the header */

    if (GetFileAttributesExA(ini_path(), GetFileExInfoStandard, &attributes)) {
        /* The two halves are one FILETIME, which is a 64-bit count of 100 nanosecond ticks. Joined
         * here rather than compared as a structure so a caller can hold it in one variable and
         * compare it with one test. */
        generation = ((uint64_t)attributes.ftLastWriteTime.dwHighDateTime << 32) |
                     (uint64_t)attributes.ftLastWriteTime.dwLowDateTime;
    }
    ini_cache.generation   = generation;
    ini_cache.checked_ms   = GetTickCount();
    ini_cache.checked_once = true;
    return generation;
}
