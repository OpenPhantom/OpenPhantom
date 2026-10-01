/* mp_mod_allow.c: [multiplayer] AllowMods, and whether this machine may host. See the header.
 */
#include "mp_mod_allow.h"

#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "multiplayer.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/mod_identity.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The key's value as the ini gives it, and a list of names as a log line prints it. */
#define ALLOW_LIST_BYTES 512u
#define NAMES_BYTES      256u
#define NAME_BYTES       260u

static struct {
    bool read;
    char list[ALLOW_LIST_BYTES];
    bool hosting_asked;
    bool hosting_refused;
    char blocked[NAME_BYTES];
} allow;

/* One name of the list, from `*at` to the next comma, with the blanks around it dropped, into
 * `name`. False at the end of the list. */
static bool next_name(const char *list, size_t *at, char *name, size_t capacity)
{
    size_t start;
    size_t end;

    while (list[*at] != '\0') {
        while (list[*at] == ' ' || list[*at] == '\t') {
            ++*at;
        }
        start = *at;
        while (list[*at] != '\0' && list[*at] != ',') {
            ++*at;
        }
        end = *at;
        while (end > start && (list[end - 1u] == ' ' || list[end - 1u] == '\t')) {
            --end;
        }
        if (list[*at] == ',') {
            ++*at;
        }
        if (end > start) {
            (void)text_format(name, capacity, "%.*s", (int)(end - start), list + start);
            return true;
        }
    }
    return false;
}

/* Every name the list holds, each with whether a DLL of that name is loaded here. */
static void say_the_list(void)
{
    const char *loaded[MP_MOD_CENSUS_FOREIGN_MAX];
    size_t      held = mp_mod_census_foreign_names(loaded, MP_MOD_CENSUS_FOREIGN_MAX, NULL, NULL);
    char        marked[MP_MOD_FOREIGN_NAMES_MAX][NAME_BYTES + 24u];
    const char *names[MP_MOD_FOREIGN_NAMES_MAX];
    char        name[NAME_BYTES];
    char        listed[ALLOW_LIST_BYTES];
    size_t      count = 0u;
    size_t      at = 0u;

    while (next_name(allow.list, &at, name, sizeof name)) {
        if (count < MP_MOD_FOREIGN_NAMES_MAX) {
            bool   here = false;
            size_t i;

            for (i = 0; i < held && !here; ++i) {
                here = mod_identity_name_listed(name, loaded[i]);
            }
            (void)text_format(marked[count], sizeof marked[count], "%s (%s)", name,
                              here ? "loaded here" : "not loaded here");
            names[count] = marked[count];
        }
        ++count;
    }
    if (count == 0u) {
        log_info("AllowMods in [multiplayer] names none: a session hosted here accepts this "
                 "release's DLLs only");
        return;
    }
    log_info("AllowMods in [multiplayer] names %u DLL(s) that a session hosted here accepts "
             "although they are not of this release: %s", (unsigned)count,
             mp_mod_foreign_list(names, count, listed, sizeof listed));
}

const char *mp_mod_allow_read(void)
{
    char now[ALLOW_LIST_BYTES];

    (void)ini_read_string(MULTIPLAYER_SECTION, "AllowMods", "", now, sizeof now);
    if (!allow.read || strcmp(now, allow.list) != 0) {
        allow.read = true;
        (void)text_format(allow.list, sizeof allow.list, "%s", now);
        say_the_list();
    }
    return allow.list;
}

const char *mp_mod_allow_list(void)
{
    return allow.list;
}

/* The refusal, naming the first DLL the list does not name and every other like it. */
static void refuse(const char *const *names, size_t count, size_t first)
{
    const char *others[MP_MOD_CENSUS_FOREIGN_MAX];
    char        listed[NAMES_BYTES];
    char        more[NAMES_BYTES + 32u];
    size_t      unnamed = 0u;
    size_t      i;

    for (i = first + 1u; i < count; ++i) {
        if (!mod_identity_name_listed(allow.list, names[i])) {
            others[unnamed++] = names[i];
        }
    }
    more[0] = '\0';
    if (unnamed != 0u) {
        (void)text_format(more, sizeof more, "; %u more like it: %s", (unsigned)unnamed,
                          mp_mod_foreign_list(others, unnamed, listed, sizeof listed));
    }
    allow.hosting_refused = true;
    (void)text_format(allow.blocked, sizeof allow.blocked, "%s", names[first]);
    log_warning("hosting is refused: %s is loaded from the mods folder, is not of this release "
                "(%s) and [multiplayer] AllowMods does not name it; remove it and start the game "
                "again, or name it there%s", names[first], mp_mod_census_class_of(names[first]),
                more);
}

bool mp_mod_allow_may_host(void)
{
    const char *names[MP_MOD_CENSUS_FOREIGN_MAX];
    char        listed[NAMES_BYTES];
    unsigned    all = 0u;
    bool        judged = false;
    size_t      count = mp_mod_census_foreign_names(names, MP_MOD_CENSUS_FOREIGN_MAX, &all,
                                                    &judged);
    size_t      first;

    (void)mp_mod_allow_read();
    allow.hosting_asked   = true;
    allow.hosting_refused = false;
    allow.blocked[0]      = '\0';
    if (!judged) {
        log_info("hosting is allowed without a judgement: this build has no release number of "
                 "its own");
        return true;
    }
    first = mp_mod_foreign_first_refused(names, count, allow.list);
    if (first < count) {
        refuse(names, count, first);
        return false;
    }
    if (all > count) {
        allow.hosting_refused = true;
        (void)text_format(allow.blocked, sizeof allow.blocked, "%s", mp_mod_census_first_unheld());
        log_warning("hosting is refused: %u DLL(s) outside this release are loaded from the mods "
                    "folder, more than the %u the census holds, so %s and the ones after it "
                    "cannot be held against [multiplayer] AllowMods; remove them and start the "
                    "game again", all, MP_MOD_CENSUS_FOREIGN_MAX, allow.blocked);
        return false;
    }
    log_info("hosting is allowed: every DLL loaded from the mods folder is this release's own or "
             "named in [multiplayer] AllowMods (%u named: %s)", (unsigned)count,
             mp_mod_foreign_list(names, count, listed, sizeof listed));
    return true;
}

/* The list as a log line prints it: its first names, then how many more, and never the key's raw
 * value, which can run to the whole length the key is read with. Answers how many names it holds.
 */
static size_t allowed_names(char *out, size_t capacity)
{
    char        held[MP_MOD_FOREIGN_NAMES_MAX][NAME_BYTES];
    const char *names[MP_MOD_FOREIGN_NAMES_MAX];
    char        name[NAME_BYTES];
    size_t      count = 0u;
    size_t      at = 0u;

    while (next_name(allow.list, &at, name, sizeof name)) {
        if (count < MP_MOD_FOREIGN_NAMES_MAX) {
            (void)text_format(held[count], sizeof held[count], "%s", name);
            names[count] = held[count];
        }
        ++count;
    }
    (void)mp_mod_foreign_list(names, count, out, capacity);
    return count;
}

bool mp_mod_allow_hosting_blocked(char *name, size_t capacity)
{
    if (name != NULL && capacity != 0u) {
        (void)text_format(name, capacity, "%s", allow.hosting_refused ? allow.blocked : "");
    }
    return allow.hosting_refused;
}

void mp_mod_allow_report(void)
{
    const char *names[MP_MOD_CENSUS_FOREIGN_MAX];
    char        listed[NAMES_BYTES];
    char        allowed[NAMES_BYTES];
    char        blocked[NAME_BYTES];
    char        hosting[NAME_BYTES + 32u];
    unsigned    all = 0u;
    size_t      named;

    /* Only once the list was read, which took the census: a report takes nothing itself. */
    if (!allow.read) {
        log_info("  [multiplayer] AllowMods was not read, since this process never hosted");
        return;
    }
    (void)mp_mod_census_foreign_names(names, MP_MOD_CENSUS_FOREIGN_MAX, &all, NULL);
    (void)mp_mod_foreign_list(names, all, listed, sizeof listed);
    named = allowed_names(allowed, sizeof allowed);
    if (mp_mod_allow_hosting_blocked(blocked, sizeof blocked)) {
        (void)text_format(hosting, sizeof hosting, "refused for %s", blocked);
    } else {
        (void)text_format(hosting, sizeof hosting, "%s",
                          allow.hosting_asked ? "allowed"
                                              : "not asked, the transport came from NetRole or "
                                                "OBI_NET_ROLE");
    }
    log_info("  the DLLs outside this release in this process: %u (%s); [multiplayer] AllowMods at "
             "the last hosting: %u (%s); hosting from the menu was %s", all, listed,
             (unsigned)named, allowed, hosting);
}
