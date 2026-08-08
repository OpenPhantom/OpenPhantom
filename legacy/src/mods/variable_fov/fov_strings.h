/* fov_strings.h: the lookup table for this feature's own menu captions.
 *
 * Why this cannot come out of the engine
 *
 * The original has NO language selector. Every language was a separate release with its own
 * LOCALIZE.LAB (125 info records + 424 strings, identically shaped in every language). There is
 * no variable in the image that holds "the language", and nothing to read out: the language IS
 * the file that is installed. And our captions are not in there anyway, "FIELD OF VIEW" with a
 * degree readout is new.
 *
 * So this does not pretend to read the game's language. It does what is honestly possible:
 *   1. `Language` from the ini, when set, the truth the user knows themselves;
 *   2. otherwise the Windows UI language, as an approximation;
 *   3. otherwise English.
 * English is additionally the fallback for each individual row, so a missing translation is an
 * untranslated caption rather than an empty one.
 *
 * CHARACTER SET: letters, digits and spaces, nothing else. rdFont substitutes '?' for a glyph the
 * font does not carry (0x479492), and no shipped menu string proves that "sysfont" carries a '+'
 * or a '%'.
 */
#ifndef FOV_STRINGS_H
#define FOV_STRINGS_H

typedef enum fov_language {
    FOV_LANGUAGE_EN = 0,
    FOV_LANGUAGE_DE,
    FOV_LANGUAGE_FR,
    FOV_LANGUAGE_IT,
    FOV_LANGUAGE_ES,
    FOV_LANGUAGE_COUNT
} fov_language_t;

typedef enum fov_string_id {
    FOV_STRING_HORIZONTAL_AND_VERTICAL = 1,  /* %.0f horizontal, %.0f the vertical that falls out */
    FOV_STRING_HORIZONTAL_ONLY,              /* %.0f horizontal; the vertical is not known yet    */
    FOV_STRING_NO_PROJECTION                 /* no placeholders: nothing has been built yet       */
} fov_string_id_t;

/* `language_tag` may be NULL or "" to auto-detect. Accepts en|de|fr|it|es. */
void fov_strings_init(const char *language_tag);

/* Never NULL; an unknown id yields "". */
const char *fov_string(fov_string_id_t id);

#endif /* FOV_STRINGS_H */
