#include "fov_strings.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct fov_string_row {
    fov_string_id_t id;
    /* Column order must match enum fov_language. NULL = fall back to English. */
    const char     *text[FOV_LANGUAGE_COUNT];
} fov_string_row_t;

/* The FORMAT belongs in the table, not around it. A translation that sets the number differently
 * or writes the unit differently has to be able to do that in the same string, otherwise one
 * assembles captions out of fragments and ends up translating half of them. The placeholder order
 * is therefore part of the contract and is stated in the comment. */
static const fov_string_row_t string_rows[] = {
    { FOV_STRING_HORIZONTAL_AND_VERTICAL,
      { "FIELD OF VIEW  H %.0f  V %.0f",
        "SICHTFELD  H %.0f  V %.0f",
        "CHAMP DE VISION  H %.0f  V %.0f",
        "CAMPO VISIVO  H %.0f  V %.0f",
        "CAMPO DE VISION  H %.0f  V %.0f" } },

    { FOV_STRING_HORIZONTAL_ONLY,
      { "FIELD OF VIEW  H %.0f",
        "SICHTFELD  H %.0f",
        "CHAMP DE VISION  H %.0f",
        "CAMPO VISIVO  H %.0f",
        "CAMPO DE VISION  H %.0f" } },

    { FOV_STRING_NO_PROJECTION,
      { "FIELD OF VIEW",
        "SICHTFELD",
        "CHAMP DE VISION",
        "CAMPO VISIVO",
        "CAMPO DE VISION" } }
};

static fov_language_t active_language = FOV_LANGUAGE_EN;
static bool           language_resolved;

static int language_from_tag(const char *tag)
{
    if (tag == NULL || tag[0] == '\0') {
        return -1;
    }
    if (_strnicmp(tag, "en", 2) == 0) { return FOV_LANGUAGE_EN; }
    if (_strnicmp(tag, "de", 2) == 0) { return FOV_LANGUAGE_DE; }
    if (_strnicmp(tag, "fr", 2) == 0) { return FOV_LANGUAGE_FR; }
    if (_strnicmp(tag, "it", 2) == 0) { return FOV_LANGUAGE_IT; }
    if (_strnicmp(tag, "es", 2) == 0) { return FOV_LANGUAGE_ES; }
    return -1;
}

static const char *language_name(fov_language_t language)
{
    switch (language) {
    case FOV_LANGUAGE_DE: return "de";
    case FOV_LANGUAGE_FR: return "fr";
    case FOV_LANGUAGE_IT: return "it";
    case FOV_LANGUAGE_ES: return "es";
    default:              return "en";
    }
}

/* The primary language identifier of a LANGID, per the Windows LANGID layout. */
#define PRIMARY_LANGUAGE_MASK 0x3FFu
#define LANG_PRIMARY_GERMAN   0x07u
#define LANG_PRIMARY_FRENCH   0x0Cu
#define LANG_PRIMARY_ITALIAN  0x10u
#define LANG_PRIMARY_SPANISH  0x0Au

void fov_strings_init(const char *language_tag)
{
    int from_ini = language_from_tag(language_tag);
    LANGID ui_language;

    if (from_ini >= 0) {
        active_language = (fov_language_t)from_ini;
        language_resolved = true;
        log_info("caption language '%s' from the ini", language_name(active_language));
        return;
    }

    /* An approximation, and it is named as one: the game has no language selector to ask. */
    ui_language = GetUserDefaultUILanguage();
    switch (ui_language & PRIMARY_LANGUAGE_MASK) {
    case LANG_PRIMARY_GERMAN:  active_language = FOV_LANGUAGE_DE; break;
    case LANG_PRIMARY_FRENCH:  active_language = FOV_LANGUAGE_FR; break;
    case LANG_PRIMARY_ITALIAN: active_language = FOV_LANGUAGE_IT; break;
    case LANG_PRIMARY_SPANISH: active_language = FOV_LANGUAGE_ES; break;
    default:                   active_language = FOV_LANGUAGE_EN; break;
    }
    language_resolved = true;

    log_info("caption language '%s' from the Windows UI (LANGID %04X). The game itself has no "
             "language selector, every language was a separate release with its own "
             "LOCALIZE.LAB. Language=en|de|fr|it|es in the ini overrides this.",
             language_name(active_language), (unsigned)ui_language);
}

const char *fov_string(fov_string_id_t id)
{
    size_t index;

    if (!language_resolved) {
        fov_strings_init(NULL);
    }

    for (index = 0; index < sizeof(string_rows) / sizeof(string_rows[0]); ++index) {
        if (string_rows[index].id != id) {
            continue;
        }
        if (string_rows[index].text[active_language] != NULL) {
            return string_rows[index].text[active_language];
        }
        return string_rows[index].text[FOV_LANGUAGE_EN];   /* fallback per ROW, not per language */
    }

    return "";
}
