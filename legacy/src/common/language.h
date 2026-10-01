/* common/language.h: which language a mod draws its own text in.
 *
 * The game has no language setting to read. It shipped in five languages, English, German,
 * French, Italian and Spanish, each as a release of its own with its own LOCALIZE.LAB, and nothing
 * in the process says which one is installed. So a mod that draws text picks one: the ini's
 * Language= when it names one of the five, otherwise the Windows UI language, otherwise English.
 *
 * A table of texts keeps one column per language in the order below and may leave a cell empty;
 * whoever reads it then shows the English cell, so a row added in one language is readable in all
 * of them. Every cell is printable ASCII: the menu fonts are bitmap fonts whose width tables stop
 * at 0x7F, so umlauts and accents are written out (ZURUECK, and the plain vowel for an accented
 * one).
 */
#ifndef COMMON_LANGUAGE_H
#define COMMON_LANGUAGE_H

#include <stdbool.h>
#include <stdint.h>

/* The column order of every table, and the order the releases are usually listed in. */
typedef enum language {
    LANGUAGE_EN = 0,
    LANGUAGE_DE,
    LANGUAGE_FR,
    LANGUAGE_IT,
    LANGUAGE_ES,
    LANGUAGE_COUNT
} language_t;

/* The language a tag names, "en", "de", "fr", "it" or "es" in either case, read by its first two
 * letters so that "de-DE" names German as well. False for anything else, an empty tag included. */
bool language_from_tag(const char *tag, language_t *out);

/* The language of a Windows LANGID, by its primary part; English for one this does not carry. */
language_t language_from_langid(uint16_t langid);

/* The language to draw in: the tag when it names one, otherwise the Windows UI language.
 * `from_tag`, when given, says which of the two it was, for the line a caller logs. */
language_t language_choose(const char *tag, bool *from_tag);

/* The tag of a language, "en" for anything past the table. */
const char *language_tag(language_t language);

#endif /* COMMON_LANGUAGE_H */
