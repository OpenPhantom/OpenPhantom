/* common/language.c: the five languages and how one of them is chosen. See the header. */
#include "common/language.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The primary language identifier of a LANGID, per the Windows LANGID layout. */
#define PRIMARY_LANGUAGE_MASK 0x3FFu
#define PRIMARY_GERMAN        0x07u
#define PRIMARY_SPANISH       0x0Au
#define PRIMARY_FRENCH        0x0Cu
#define PRIMARY_ITALIAN       0x10u

static const char *const TAGS[LANGUAGE_COUNT] = { "en", "de", "fr", "it", "es" };

bool language_from_tag(const char *tag, language_t *out)
{
    size_t index;

    if (tag == NULL || out == NULL) {
        return false;
    }
    for (index = 0; index < (size_t)LANGUAGE_COUNT; ++index) {
        if (_strnicmp(tag, TAGS[index], 2) == 0) {
            *out = (language_t)index;
            return true;
        }
    }
    return false;
}

language_t language_from_langid(uint16_t langid)
{
    switch (langid & PRIMARY_LANGUAGE_MASK) {
    case PRIMARY_GERMAN:  return LANGUAGE_DE;
    case PRIMARY_FRENCH:  return LANGUAGE_FR;
    case PRIMARY_ITALIAN: return LANGUAGE_IT;
    case PRIMARY_SPANISH: return LANGUAGE_ES;
    default:              return LANGUAGE_EN;
    }
}

language_t language_choose(const char *tag, bool *from_tag)
{
    language_t chosen = LANGUAGE_EN;
    bool       named  = language_from_tag(tag, &chosen);

    if (from_tag != NULL) {
        *from_tag = named;
    }
    return named ? chosen : language_from_langid((uint16_t)GetUserDefaultUILanguage());
}

const char *language_tag(language_t language)
{
    return (size_t)language < (size_t)LANGUAGE_COUNT ? TAGS[language] : TAGS[LANGUAGE_EN];
}
