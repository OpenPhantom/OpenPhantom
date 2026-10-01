/* mp_mod_census.c: every DLL of the mods folder that runs in this process, and what it is to a
 * session. See the header.
 */
#include "mp_mod_census.h"

#include "mp_mod_folder.h"
#include "mp_mod_manifest_rule.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/mod_identity.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A list of names as a log line prints it. */
#define NAMES_BYTES 256u

/* 1970 in hundred nanosecond units since 1601, the units a FILETIME counts. */
#define UNIX_EPOCH_AS_FILETIME    116444736000000000ULL
#define FILETIME_UNITS_PER_SECOND 10000000ULL

/* A link time from 2004 to 2038. A reproducible build writes a hash into the same field, which
 * lands outside this range as often as not and is then no date at all. */
#define PLAUSIBLE_STAMP_MIN 0x40000000u
#define PLAUSIBLE_STAMP_MAX 0x80000000u

typedef struct census_build {
    bool     loaded;
    uint32_t stamp;
    uint32_t image;
    char     version[MOD_IDENTITY_VERSION_MAX];
} census_build_t;

/* One DLL outside this release, as it runs here. */
typedef struct census_foreign {
    char                name[MP_MOD_FOLDER_PATH_MAX];   /* its file name as it was loaded */
    mod_identity_kind_t kind;
    uint32_t            stamp;
    char                internal_name[MOD_IDENTITY_NAME_MAX];
    char                version[MOD_IDENTITY_VERSION_MAX];
} census_foreign_t;

static struct {
    bool                 taken;
    bool                 judged;          /* this build knows a release number of its own */
    char                 own_version[MOD_IDENTITY_VERSION_MAX];
    mp_mod_folder_walk_t walk;
    unsigned             loaded;          /* modules directly in the folder */
    unsigned             by_class[MP_MOD_CLASS_COUNT];
    unsigned             unlisted;        /* this release's own, and not in this build's table */
    unsigned             foreign_count;   /* outside this release, also past the table */
    unsigned             foreign_listed;  /* of those, held in the table */
    census_foreign_t     foreign[MP_MOD_CENSUS_FOREIGN_MAX];
    char                 unheld[MP_MOD_FOLDER_PATH_MAX];   /* the first past the table */
    double               ms;
    census_build_t       required[MP_MOD_MANIFEST_MAX_MODS];
} census;

static void copy_text(char *out, size_t capacity, const char *text)
{
    if (out != NULL && capacity != 0u) {
        (void)text_format(out, capacity, "%s", text != NULL ? text : "");
    }
}

/* The file name without ".dll", the form the classification compares. */
static void stem_of(const char *name, char *out, size_t capacity)
{
    size_t length;

    copy_text(out, capacity, name);
    length = strlen(out);
    if (length > 4u && _stricmp(out + length - 4u, ".dll") == 0) {
        out[length - 4u] = '\0';
    }
}

static void take_required(void)
{
    size_t                   count = 0;
    const mp_mod_required_t *table = mp_mod_manifest_required(&count);
    size_t                   i;

    for (i = 0; i < count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        HMODULE        module = NULL;
        mod_identity_t identity;
        char           stem[MOD_IDENTITY_NAME_MAX];

        /* The multiplayer is the module this code runs in, whatever its file is called. */
        if (table[i].id == MP_WIRE_MOD_MULTIPLAYER) {
            (void)GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     (LPCSTR)(uintptr_t)&mp_mod_census_take, &module);
        } else {
            module = GetModuleHandleA(table[i].dll);
        }
        stem_of(table[i].dll, stem, sizeof stem);
        if (module == NULL ||
            !mod_identity_of_module(module, stem, census.own_version, &identity)) {
            continue;
        }
        census.required[i].loaded = true;
        census.required[i].stamp  = identity.time_stamp;
        census.required[i].image  = identity.image_size;
        copy_text(census.required[i].version, sizeof census.required[i].version,
                  identity.file_version);
    }
}

/* One module the folder walk found directly in the mods folder, under its file name `name`. */
static void judge_module(void *module, const char *name)
{
    mod_identity_t        identity;
    char                  stem[MP_MOD_FOLDER_PATH_MAX];
    const mp_mod_known_t *known;
    census_foreign_t     *entry;

    ++census.loaded;
    if (!census.judged) {
        return;
    }
    stem_of(name, stem, sizeof stem);
    (void)mod_identity_of_module(module, stem, census.own_version, &identity);
    if (identity.kind == MOD_IDENTITY_OWN) {
        known = mp_mod_manifest_find_known(stem);
        if (known != NULL) {
            ++census.by_class[known->mod_class];
        } else {
            ++census.unlisted;
        }
        return;
    }
    ++census.foreign_count;
    if (census.foreign_listed == MP_MOD_CENSUS_FOREIGN_MAX) {
        if (census.unheld[0] == '\0') {
            copy_text(census.unheld, sizeof census.unheld, name);
        }
        return;
    }
    entry = &census.foreign[census.foreign_listed++];
    copy_text(entry->name, sizeof entry->name, name);
    entry->kind  = identity.kind;
    entry->stamp = identity.time_stamp;
    copy_text(entry->internal_name, sizeof entry->internal_name, identity.internal_name);
    copy_text(entry->version, sizeof entry->version, identity.file_version);
}

static int by_name(const void *a, const void *b)
{
    return _stricmp(((const census_foreign_t *)a)->name, ((const census_foreign_t *)b)->name);
}

static size_t held_names(const char **names)
{
    size_t i;

    for (i = 0; i < census.foreign_listed; ++i) {
        names[i] = census.foreign[i].name;
    }
    return i;
}

static const char *class_text(mod_identity_kind_t kind)
{
    switch (kind) {
    case MOD_IDENTITY_FOREIGN:
        return "foreign, no version resource of this project";
    case MOD_IDENTITY_MISNAMED:
        return "misnamed, this project's resource under another file name";
    case MOD_IDENTITY_OTHER_RELEASE:
        return "another release";
    default:
        return "unreadable, the image did not parse";
    }
}

/* The census as one line, then the first eight DLLs outside this release one line each. */
static void say_the_census(const char *indent, const char *when)
{
    const char *names[MP_MOD_CENSUS_FOREIGN_MAX];
    char        listed[NAMES_BYTES];
    char        source[96];
    size_t      known = 0;
    unsigned    own = census.unlisted;
    size_t      i;

    (void)mp_mod_manifest_known(&known);
    for (i = 0; i < MP_MOD_CLASS_COUNT; ++i) {
        own += census.by_class[i];
    }
    if (census.walk.from_list) {
        copy_text(source, sizeof source, "the process's module list");
    } else if (census.walk.list_asked) {
        (void)text_format(source, sizeof source, "the folder, because the module list could not "
                          "be read (error %lu)", census.walk.list_error);
    } else {
        copy_text(source, sizeof source, "the folder, as the caller asked");
    }
    (void)held_names(names);
    (void)mp_mod_foreign_list(names, census.foreign_count, listed, sizeof listed);
    log_info("%sthe mods of this process (%s): %u loaded from the mods folder, read from %s in "
             "%.1f ms: %u of this release, of the %u this build knows (%u required, %u with host "
             "settings, %u acting on the host alone, %u local, %u not in this build's table), %u "
             "outside this release (%s); %u more module(s) from its subfolders, not judged (a "
             "mod's own files); %u DLL(s) in the folder not loaded", indent, when, census.loaded,
             source, census.ms, own, (unsigned)known, census.by_class[MP_MOD_CLASS_REQUIRED],
             census.by_class[MP_MOD_CLASS_HOST_SETTINGS], census.by_class[MP_MOD_CLASS_HOST_ONLY],
             census.by_class[MP_MOD_CLASS_LOCAL], census.unlisted, census.foreign_count, listed,
             census.walk.below, census.walk.not_loaded);
    for (i = 0; i < census.foreign_listed && i < MP_MOD_FOREIGN_NAMES_MAX; ++i) {
        const census_foreign_t *entry = &census.foreign[i];
        char                    built[48];

        mp_mod_census_describe_stamp(entry->stamp, built, sizeof built);
        log_info("%s  outside this release: %s, %s (internal name '%s', version '%s' against this "
                 "release's '%s'), built %s (%08X)", indent, entry->name, class_text(entry->kind),
                 entry->internal_name, entry->version, census.own_version, built,
                 (unsigned)entry->stamp);
    }
}

/* `indent` is "" when the census is taken and the report's two blanks in the report. */
static void say(const char *indent, const char *when)
{
    size_t                   count = 0;
    const mp_mod_required_t *table = mp_mod_manifest_required(&count);
    size_t                   i;

    if (!census.walk.folder_found && census.loaded == 0u) {
        log_info("%sthe mods of this process (%s): no mods folder at %s, in %.1f ms", indent,
                 when, census.walk.folder, census.ms);
    } else if (!census.judged) {
        log_info("%sthe mods of this process (%s): %u loaded from the mods folder in %.1f ms: no "
                 "own release to compare (a build without a version resource), so none is judged "
                 "and none stands in the way of a session", indent, when, census.loaded,
                 census.ms);
    } else {
        say_the_census(indent, when);
    }
    for (i = 0; i < count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        const census_build_t *build = &census.required[i];
        char                  built[48];

        if (!build->loaded) {
            log_info("%s  required: %s is not loaded in this process", indent, table[i].dll);
            continue;
        }
        mp_mod_census_describe_stamp(build->stamp, built, sizeof built);
        log_info("%s  required: %s %s, built %s (%08X, image %08X)", indent, table[i].dll,
                 build->version[0] != '\0' ? build->version : "without a release number", built,
                 (unsigned)build->stamp, (unsigned)build->image);
    }
}

void mp_mod_census_take_against(const char *own_version, bool module_list)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER before;
    LARGE_INTEGER after;

    if (census.taken) {
        return;
    }
    census.taken = true;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&before);
    copy_text(census.own_version, sizeof census.own_version, own_version);
    census.judged = census.own_version[0] != '\0';
    take_required();
    mp_mod_folder_walk(module_list, judge_module, &census.walk);
    /* The loader's order, so a statement names the same DLLs first on every machine. */
    qsort(census.foreign, census.foreign_listed, sizeof census.foreign[0], by_name);
    QueryPerformanceCounter(&after);
    census.ms = frequency.QuadPart > 0
                    ? (double)(after.QuadPart - before.QuadPart) * 1000.0 /
                          (double)frequency.QuadPart
                    : 0.0;
    say("", "once per process");
}

void mp_mod_census_take(void)
{
    char own[MOD_IDENTITY_VERSION_MAX];

    if (census.taken) {
        return;
    }
    (void)mod_identity_own_version((const void *)(uintptr_t)&mp_mod_census_take, own, sizeof own);
    mp_mod_census_take_against(own, true);
}

size_t mp_mod_census_foreign_names(const char **names, size_t capacity, unsigned *count,
                                   bool *judged)
{
    size_t i;

    mp_mod_census_take();
    for (i = 0; names != NULL && i < capacity && i < census.foreign_listed; ++i) {
        names[i] = census.foreign[i].name;
    }
    if (count != NULL) {
        *count = census.foreign_count;
    }
    if (judged != NULL) {
        *judged = census.judged;
    }
    return i;
}

static const census_foreign_t *held(const char *name)
{
    size_t i;

    for (i = 0; name != NULL && i < census.foreign_listed; ++i) {
        if (_stricmp(census.foreign[i].name, name) == 0) {
            return &census.foreign[i];
        }
    }
    return NULL;
}

bool mp_mod_census_foreign_by_name(const char *name, uint32_t *stamp, char *version,
                                   size_t capacity)
{
    const census_foreign_t *entry = held(name);

    *stamp = entry != NULL ? entry->stamp : 0u;
    copy_text(version, capacity,
              entry != NULL && entry->kind == MOD_IDENTITY_OTHER_RELEASE ? entry->version : "");
    return entry != NULL;
}

const char *mp_mod_census_class_of(const char *name)
{
    const census_foreign_t *entry = held(name);

    return entry != NULL ? class_text(entry->kind) : "not held by the census";
}

const char *mp_mod_census_first_unheld(void)
{
    return census.unheld;
}

void mp_mod_census_required_builds(mp_mod_manifest_t *out)
{
    size_t                   count = 0;
    const mp_mod_required_t *table = mp_mod_manifest_required(&count);
    size_t                   i;

    if (out == NULL) {
        return;
    }
    mp_mod_census_take();
    out->count = 0u;
    for (i = 0; i < count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        if (census.required[i].loaded) {
            out->mods[out->count].id    = table[i].id;
            out->mods[out->count].stamp = census.required[i].stamp;
            out->mods[out->count].image = census.required[i].image;
            ++out->count;
        }
    }
}

bool mp_mod_census_build_of(uint8_t id, uint32_t *stamp, uint32_t *image, char *version,
                            size_t capacity)
{
    size_t                   count = 0;
    const mp_mod_required_t *table = mp_mod_manifest_required(&count);
    size_t                   i;

    *stamp = 0u;
    *image = 0u;
    copy_text(version, capacity, "");
    for (i = 0; i < count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        if (table[i].id == id && census.required[i].loaded) {
            *stamp = census.required[i].stamp;
            *image = census.required[i].image;
            copy_text(version, capacity, census.required[i].version);
            return true;
        }
    }
    return false;
}

/* The link time as the date a person compares against a build they have. A reproducible build
 * writes a hash rather than a time, which reads as a date far outside any plausible range; such a
 * stamp is given as the number it is. */
void mp_mod_census_describe_stamp(uint32_t stamp, char *out, size_t capacity)
{
    ULONGLONG  hundred_ns = UNIX_EPOCH_AS_FILETIME + (ULONGLONG)stamp * FILETIME_UNITS_PER_SECOND;
    FILETIME   utc;
    FILETIME   local;
    SYSTEMTIME when;

    if (out == NULL || capacity == 0u) {
        return;
    }
    utc.dwLowDateTime  = (DWORD)hundred_ns;
    utc.dwHighDateTime = (DWORD)(hundred_ns >> 32);
    if (stamp > PLAUSIBLE_STAMP_MIN && stamp < PLAUSIBLE_STAMP_MAX &&
        FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &when)) {
        text_format(out, capacity, "%04u-%02u-%02u %02u:%02u", (unsigned)when.wYear,
                    (unsigned)when.wMonth, (unsigned)when.wDay, (unsigned)when.wHour,
                    (unsigned)when.wMinute);
    } else {
        text_format(out, capacity, "id %08X", (unsigned)stamp);
    }
}

void mp_mod_census_report(void)
{
    if (!census.taken) {
        log_info("  the mods of this process: no census: this process made no statement");
        return;
    }
    say("  ", "taken once per process");
}
