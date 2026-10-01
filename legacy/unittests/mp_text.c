/* The multiplayer's own texts in the five languages the game shipped in.
 *
 * Every row has a text in all five languages, none of them empty, because an empty cell shows the
 * English one. Every cell is printable ASCII, every cell has the conversions of its English one in
 * the same order, because a translation is handed to the same printf, and every row with a width
 * fits it in every language, measured with the fonts' own advance tables. Then the choice: a tag
 * picks a language, a cell past the table answers "", and the language stays chosen.
 */
#include "unittest.h"

#include "mp_menu_metrics.h"
#include "mp_text.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The next printf conversion at or after `at`, its length in `*length`; NULL for none. "%%" is not
 * a conversion and is stepped over. */
static const char *next_conversion(const char *at, size_t *length)
{
    while (at != NULL && (at = strchr(at, '%')) != NULL) {
        const char *end = at + 1;

        if (*end == '%') {
            at = end + 1;
            continue;
        }
        while (*end != '\0' && strchr("-+ #0123456789.", *end) != NULL) {
            ++end;
        }
        while (*end == 'h' || *end == 'l' || *end == 'z') {
            ++end;
        }
        if (*end != '\0') {
            ++end;
        }
        *length = (size_t)(end - at);
        return at;
    }
    return NULL;
}

static bool same_conversions(const char *a, const char *b)
{
    size_t la = 0;
    size_t lb = 0;

    for (;;) {
        a = next_conversion(a, &la);
        b = next_conversion(b, &lb);
        if (a == NULL || b == NULL) {
            return a == NULL && b == NULL;
        }
        if (la != lb || memcmp(a, b, la) != 0) {
            return false;
        }
        a += la;
        b += lb;
    }
}

static const uint8_t *advance_of(uint8_t font)
{
    switch (font) {
    case MP_TEXT_FONT_INDUST:  return MP_MENU_ADVANCE_INDUST;
    case MP_TEXT_FONT_SYSFONT: return MP_MENU_ADVANCE_SYSFONT;
    case MP_TEXT_FONT_COURIER: return MP_MENU_ADVANCE_COURIER;
    default:                   return NULL;
    }
}

static uint32_t width_of(const uint8_t *advance, const char *text)
{
    uint32_t width = 0;

    for (; *text != '\0'; ++text) {
        width += advance[(unsigned char)*text & 0x7Fu];
    }
    return width;
}

static bool printable(const char *text)
{
    for (; *text != '\0'; ++text) {
        if ((unsigned char)*text < 0x20u || (unsigned char)*text > 0x7Eu) {
            return false;
        }
    }
    return true;
}

static void check_every_row(void)
{
    size_t   id;
    unsigned no_english = 0u;
    unsigned empty      = 0u;
    unsigned not_ascii  = 0u;
    unsigned converts   = 0u;
    unsigned too_wide   = 0u;

    ut_section("every row, in every language");
    for (id = 0; id < (size_t)MP_TEXT_COUNT; ++id) {
        const mp_text_row_t *row     = mp_text_row((mp_text_id_t)id);
        const uint8_t       *advance = advance_of(row->font);
        size_t               language;

        if (row->text[LANGUAGE_EN] == NULL || row->text[LANGUAGE_EN][0] == '\0') {
            ++no_english;
            continue;
        }
        for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
            const char *text = row->text[language];

            /* An empty cell falls back to English on the screen, so a translation nobody wrote
             * would pass every other check here and show up as English in the game. */
            if (text == NULL || text[0] == '\0') {
                ++empty;
                ut_checkf(false, "row %u in %s has a text of its own", (unsigned)id,
                          language_tag((language_t)language));
                continue;
            }
            if (!printable(text)) {
                ++not_ascii;
                ut_checkf(false, "row %u in %s is printable ASCII", (unsigned)id,
                          language_tag((language_t)language));
            }
            if (!same_conversions(text, row->text[LANGUAGE_EN])) {
                ++converts;
                ut_checkf(false, "row %u in %s has the conversions of its English text: %s",
                          (unsigned)id, language_tag((language_t)language), text);
            }
            if (advance != NULL && width_of(advance, text) > row->width) {
                ++too_wide;
                ut_checkf(false, "row %u in %s fits %u pixels: %s is %u", (unsigned)id,
                          language_tag((language_t)language), (unsigned)row->width, text,
                          (unsigned)width_of(advance, text));
            }
        }
    }
    ut_checkf(no_english == 0u, "every one of %u rows has an English text: %u have not",
              (unsigned)MP_TEXT_COUNT, no_english);
    ut_checkf(empty == 0u, "and a text in every other language: %u cell(s) are empty", empty);
    ut_check(not_ascii == 0u && converts == 0u && too_wide == 0u,
             "and every cell is ASCII, converts like its English one, and fits its width");
}

/* The table is two files split by id, and the walk above reads every row through mp_text_row
 * without asking which file answered. This asks it: every id before the marker belongs to the
 * first table and every id from it on to the second, the second refuses the first's ids rather than
 * answering them out of its own rows at a wrong index, and the marker stands inside the table, so
 * both halves hold rows and the switch between them is taken. */
static void check_the_two_tables(void)
{
    size_t   id;
    unsigned wrong = 0u;

    ut_section("two tables, split by id");
    for (id = 0; id < (size_t)MP_TEXT_COUNT; ++id) {
        const mp_text_row_t *second    = mp_text_second_row((mp_text_id_t)id);
        const bool           in_second = id >= (size_t)MP_TEXT_SECOND_TABLE_FIRST;

        if ((second != NULL) != in_second ||
            (in_second && second != mp_text_row((mp_text_id_t)id))) {
            ++wrong;
            ut_checkf(false, "id %u is answered by the table it belongs to", (unsigned)id);
        }
    }
    ut_checkf(wrong == 0u, "every one of %u ids is answered by exactly one table: %u are not",
              (unsigned)MP_TEXT_COUNT, wrong);
    ut_check(mp_text_second_row(MP_TEXT_COUNT) == NULL, "an id past the count is neither's");
    ut_check((size_t)MP_TEXT_SECOND_TABLE_FIRST > 0u &&
                 (size_t)MP_TEXT_SECOND_TABLE_FIRST < (size_t)MP_TEXT_COUNT,
             "and the marker stands inside the table, so both halves hold rows");
    ut_check(strcmp(mp_text_in(MP_TEXT_CHAT_SAY, LANGUAGE_DE), "Sagen:") == 0 &&
                 strcmp(mp_text_in(MP_TEXT_SAVE_FALLBACK_NAME, LANGUAGE_EN), "Saved game %u") == 0,
             "the last row of the first table and a row of the second both read through the "
             "ordinary lookup");
}

/* The band of a client whose lobby opened on a running session. The band is drawn in sysfont and
 * cut at 400 pixels, and the walk above measures a row against the width the row names, so a row
 * that named a wider one, or no font, would pass there and be cut on screen. They are handed to
 * the band as they stand and never to printf, so a conversion in one would be printed as it is
 * written. */
static void check_the_running_session_band(void)
{
    static const mp_text_id_t IDS[] = { MP_TEXT_BAND_RUNNING_PICK, MP_TEXT_BAND_RUNNING_READY };
    unsigned                  wrong = 0u;
    size_t                    i;
    size_t                    language;

    ut_section("the band's two sentences for a session that already runs");
    for (i = 0; i < sizeof IDS / sizeof IDS[0]; ++i) {
        const mp_text_row_t *row = mp_text_row(IDS[i]);

        if (row == NULL || row->font != (uint8_t)MP_TEXT_FONT_SYSFONT || row->width != 400u) {
            ++wrong;
            ut_checkf(false, "row %u is measured in sysfont against the band's 400 pixels",
                      (unsigned)IDS[i]);
            continue;
        }
        for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
            const char *text = row->text[language];

            if (text == NULL || strchr(text, '%') != NULL ||
                width_of(MP_MENU_ADVANCE_SYSFONT, text) > 400u) {
                ++wrong;
                ut_checkf(false, "row %u in %s is a whole sentence of at most 400 pixels: %s",
                          (unsigned)IDS[i], language_tag((language_t)language),
                          text != NULL ? text : "(none)");
            }
        }
    }
    ut_checkf(wrong == 0u, "both are sysfont sentences that fit the band whole in all five "
              "languages, with no conversion in them: %u are not", wrong);
    ut_check(strcmp(mp_text_in(MP_TEXT_BAND_RUNNING_PICK, LANGUAGE_DE),
                    "Die Sitzung laeuft. Held waehlen, dann BEREIT.") == 0 &&
                 strcmp(mp_text_in(MP_TEXT_BAND_RUNNING_READY, LANGUAGE_DE),
                        "Bereit, es geht los.") == 0,
             "and the German reads exactly as written here");
}

/* One text with one value put in, against the pixels of the font it is drawn in and the band's
 * 64 characters. Answers whether it fits, and says which does not. */
static bool fits_with(mp_text_id_t id, const char *value, const uint8_t *advance, uint32_t pixels,
                      size_t language)
{
    char text[128];

    text_format(text, sizeof text, mp_text_in(id, (language_t)language), value);
    if (width_of(advance, text) <= pixels && strlen(text) < 64u) {
        return true;
    }
    ut_checkf(false, "in %s '%s' fits %u pixels: it is %u", language_tag((language_t)language),
              text, (unsigned)pixels, (unsigned)width_of(advance, text));
    return false;
}

/* A refusal for a required mod or the game data, with the longest cases put in. The walk above
 * measures each format with its %s standing for two characters, and a player reads the name of a
 * file there: the longest of this release's mods, enhanced_resolution.dll, and characters.ini on
 * the band in sysfont at 400 pixels; on the player list, in courier at 288, a release number and a
 * build date as the log writes it, the id of a build whose stamp is no date, a data file's
 * fingerprint and the word for a side that lacks the mod. */
static void check_the_refusal_with_its_values(void)
{
    static const char *const VALUES[] = {
        "0.4.4, 2026-09-29 04:27", "2026-09-29 04:27", "id 6ABB2222", "84273DBB"
    };
    unsigned too_wide = 0u;
    size_t   language;
    size_t   v;

    ut_section("a refusal's sentence and its two rows, with the longest cases in them");
    for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
        const char *missing = mp_text_in(MP_TEXT_REFUSED_MISSING, (language_t)language);

        too_wide += !fits_with(MP_TEXT_REFUSED_OTHER, "enhanced_resolution.dll",
                               MP_MENU_ADVANCE_SYSFONT, 400u, language);
        too_wide += !fits_with(MP_TEXT_REFUSED_OTHER, "characters.ini", MP_MENU_ADVANCE_SYSFONT,
                               400u, language);
        too_wide += !fits_with(MP_TEXT_REFUSED_MISSING_AT_HOST, "enhanced_resolution.dll",
                               MP_MENU_ADVANCE_SYSFONT, 400u, language);
        too_wide += !fits_with(MP_TEXT_REFUSED_MISSING_HERE, "enhanced_resolution.dll",
                               MP_MENU_ADVANCE_SYSFONT, 400u, language);
        for (v = 0; v < sizeof VALUES / sizeof VALUES[0]; ++v) {
            too_wide += !fits_with(MP_TEXT_REFUSED_HOST_ROW, VALUES[v], MP_MENU_ADVANCE_COURIER,
                                   288u, language);
            too_wide += !fits_with(MP_TEXT_REFUSED_HERE_ROW, VALUES[v], MP_MENU_ADVANCE_COURIER,
                                   288u, language);
        }
        too_wide += !fits_with(MP_TEXT_REFUSED_HOST_ROW, missing, MP_MENU_ADVANCE_COURIER, 288u,
                               language);
        too_wide += !fits_with(MP_TEXT_REFUSED_HERE_ROW, missing, MP_MENU_ADVANCE_COURIER, 288u,
                               language);
    }
    ut_checkf(too_wide == 0u, "every sentence and every row fits whole in all five languages with "
              "the longest name, number and date in it: %u do not", too_wide);
}

/* How many characters of `text` a line of `pixels` shows: the lobby's fit copies a text up to the
 * first character that would take it past the width. */
static size_t shown_in(const uint8_t *advance, const char *text, uint32_t pixels)
{
    uint32_t width = 0;
    size_t   i;

    for (i = 0; text[i] != '\0'; ++i) {
        width += advance[(unsigned char)text[i] & 0x7Fu];
        if (width > pixels) {
            break;
        }
    }
    return i;
}

/* A refusal for a DLL outside this release, and a host that cannot host for one of its own, name
 * a file that is not this release's: up to 31 characters, the most a refusal carries, and of any
 * width. So in every language the name stands last, with nothing after it, and the band's cut at
 * its end shortens the name and never a word in front of it: a word after the name would be cut
 * first, and a lost "not" turns the sentence into its opposite. With the longest name of this
 * release the whole sentence fits the band, and with a name of 31 characters every word stands and
 * at least the start of the name follows. The host's row carries the word for a DLL it does not
 * allow, this side's row another release's number and date. */
static void check_a_dll_outside_this_release(void)
{
    static const mp_text_id_t NAMED[] = {
        MP_TEXT_REFUSED_NOT_ALLOWED, MP_TEXT_REFUSED_TOO_MANY, MP_TEXT_ARM_FOREIGN_DLL
    };
    static const char *const LONG_NAMES[] = {
        "ReShade32_addon_screenshots.dll", "WWWWWWWWWWWWWWWWWWWWWWWWWWW.dll"
    };
    unsigned not_last = 0u;
    unsigned too_wide = 0u;
    unsigned cut_wrong = 0u;
    size_t   language;
    size_t   i;
    size_t   n;

    ut_section("a refusal for a DLL outside this release: its words first, the name last");
    for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
        for (i = 0; i < sizeof NAMED / sizeof NAMED[0]; ++i) {
            const char *format = mp_text_in(NAMED[i], (language_t)language);
            const char *slot   = strstr(format, "%s");
            size_t      words  = slot != NULL ? (size_t)(slot - format) : 0u;

            if (slot == NULL || strcmp(slot, "%s") != 0) {
                ++not_last;
                ut_checkf(false, "in %s the name stands last in '%s'",
                          language_tag((language_t)language), format);
                continue;
            }
            too_wide += !fits_with(NAMED[i], "enhanced_resolution.dll", MP_MENU_ADVANCE_SYSFONT,
                                   400u, language);
            for (n = 0; n < sizeof LONG_NAMES / sizeof LONG_NAMES[0]; ++n) {
                char   text[128];
                size_t shown;

                text_format(text, sizeof text, format, LONG_NAMES[n]);
                shown = shown_in(MP_MENU_ADVANCE_SYSFONT, text, 400u);
                if (strlen(text) >= 64u || shown <= words) {
                    ++cut_wrong;
                    ut_checkf(false, "in %s the band shows '%.*s' of '%s': every word and at "
                              "least the start of the name", language_tag((language_t)language),
                              (int)shown, text, text);
                }
            }
        }
        too_wide += !fits_with(MP_TEXT_REFUSED_HOST_ROW,
                               mp_text_in(MP_TEXT_REFUSED_NOT_ALLOWED_WORD, (language_t)language),
                               MP_MENU_ADVANCE_COURIER, 288u, language);
        too_wide += !fits_with(MP_TEXT_REFUSED_HERE_ROW, "0.4.10, 2026-09-29 04:27",
                               MP_MENU_ADVANCE_COURIER, 288u, language);
    }
    ut_checkf(not_last == 0u, "in all five languages the name is the last thing of the band's "
              "three sentences that carry one: %u are not", not_last);
    ut_checkf(too_wide == 0u, "each fits whole with enhanced_resolution.dll, and the host's row "
              "with its word and this side's with another release's number and date: %u do not",
              too_wide);
    ut_checkf(cut_wrong == 0u, "with a name of 31 characters, a real one and one of the widest "
              "letter, the band keeps every word and cuts only the name: %u do not", cut_wrong);
}

static void check_the_choice(void)
{
    ut_section("a language is chosen, and stays chosen");
    ut_check(mp_text_language() == LANGUAGE_EN, "English before anything is chosen");
    ut_check(strcmp(mp_text(MP_TEXT_BACK), "BACK") == 0, "so the red button says BACK");
    mp_text_choose("fr");
    ut_check(mp_text_language() == LANGUAGE_FR && strcmp(mp_text(MP_TEXT_BACK), "RETOUR") == 0,
             "fr in the ini makes it RETOUR");
    mp_text_set_language(LANGUAGE_DE);
    ut_check(strcmp(mp_text(MP_TEXT_BACK), "ZURUECK") == 0, "and German makes it ZURUECK");
    ut_check(strcmp(mp_text_in(MP_TEXT_BACK, LANGUAGE_IT), "INDIETRO") == 0 &&
                 strcmp(mp_text_in(MP_TEXT_BACK, LANGUAGE_ES), "VOLVER") == 0,
             "Italian and Spanish have their own, asked for directly");
    ut_check(strcmp(mp_text_in(MP_TEXT_BACK, LANGUAGE_COUNT), "BACK") == 0,
             "a language past the table answers English");
    ut_check(strcmp(mp_text(MP_TEXT_COUNT), "") == 0 && mp_text_row(MP_TEXT_COUNT) == NULL,
             "and an id past the table answers nothing rather than something else");
    mp_text_set_language(LANGUAGE_COUNT);
    ut_check(mp_text_language() == LANGUAGE_EN, "a language that is none of the five is English");
    ut_check(strstr(mp_text_in(MP_TEXT_OVER_BEHIND, LANGUAGE_DE),
                    "Der Host hat die Verbindung getrennt:") != NULL,
             "the German notice for a player sent away says what the host cut: the connection");
}

/* A row with a conversion is measured above as it stands, with the conversion in it, which says
 * nothing about the text a player reads. The lobby's hero row is the one whose argument is not a
 * player's: it is one of four heroes, so its widest form is known and has to fit the slot, or the
 * row shows a name without its last letters. The names are the four the lobby draws
 * (mp_screens_hero_name in mp_menu_screens.c); the slot is the row's own width. */
static void check_the_hero_row(void)
{
    static const char *const HEROES[] = {
        "Obi-Wan Kenobi", "Qui-Gon Jinn", "Panaka", "Amidala"
    };
    const mp_text_row_t *row = mp_text_row(MP_TEXT_ROW_HERO);
    const uint8_t       *advance;
    unsigned             too_wide = 0u;
    size_t               language;
    size_t               hero;

    ut_section("the lobby's hero row, with every hero in it");
    if (row == NULL || (advance = advance_of(row->font)) == NULL || row->width == 0u) {
        ut_check(false, "the hero row names the font and the slot it is drawn in");
        return;
    }
    for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
        for (hero = 0; hero < sizeof HEROES / sizeof HEROES[0]; ++hero) {
            char text[64];

            text_format(text, sizeof text, row->text[language], HEROES[hero]);
            if (width_of(advance, text) > row->width) {
                ++too_wide;
                ut_checkf(false, "in %s '%s' fits the %u pixel slot: it is %u",
                          language_tag((language_t)language), text, (unsigned)row->width,
                          (unsigned)width_of(advance, text));
            }
        }
    }
    ut_checkf(too_wide == 0u,
              "every hero's name fits the lobby row whole in all five languages: %u do not",
              too_wide);
}

/* The friendly fire row's argument is not a player's either: it is one of two words, and both of
 * them are measured into the row rather than the conversion that stands there in the table. */
static void check_the_friendly_fire_row(void)
{
    const mp_text_row_t *row = mp_text_row(MP_TEXT_ROW_FRIENDLY_FIRE);
    const uint8_t       *advance;
    unsigned             too_wide = 0u;
    size_t               language;
    size_t               value;

    ut_section("the lobby's friendly fire row, with both of its values in it");
    if (row == NULL || (advance = advance_of(row->font)) == NULL || row->width == 0u) {
        ut_check(false, "the row names the font and the slot it is drawn in");
        return;
    }
    for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
        for (value = 0; value < 2u; ++value) {
            char text[64];

            text_format(text, sizeof text, row->text[language],
                        mp_text_in(value != 0u ? MP_TEXT_RULES_ON : MP_TEXT_RULES_OFF,
                                   (language_t)language));
            if (width_of(advance, text) > row->width) {
                ++too_wide;
                ut_checkf(false, "in %s '%s' fits the %u pixel slot: it is %u",
                          language_tag((language_t)language), text, (unsigned)row->width,
                          (unsigned)width_of(advance, text));
            }
        }
    }
    ut_checkf(too_wide == 0u, "on and off both fit the row in all five languages: %u do not",
              too_wide);
}

int main(void)
{
    check_every_row();
    check_the_two_tables();
    check_the_running_session_band();
    check_the_refusal_with_its_values();
    check_a_dll_outside_this_release();
    check_the_hero_row();
    check_the_friendly_fire_row();
    check_the_choice();
    return ut_summary("mp_text");
}
