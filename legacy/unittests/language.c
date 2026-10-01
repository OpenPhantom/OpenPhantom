/* The five languages and how one of them is chosen: a tag names one by its first two letters in
 * either case, a Windows LANGID names one by its primary part, anything else is English, and a
 * named tag wins over Windows. */
#include "unittest.h"

#include "common/language.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    language_t language = LANGUAGE_EN;
    bool       from_tag = false;

    ut_section("a tag names one of the five");
    ut_check(language_from_tag("de", &language) && language == LANGUAGE_DE, "de is German");
    ut_check(language_from_tag("FR", &language) && language == LANGUAGE_FR, "FR is French");
    ut_check(language_from_tag("it", &language) && language == LANGUAGE_IT, "it is Italian");
    ut_check(language_from_tag("es-ES", &language) && language == LANGUAGE_ES,
             "es-ES is Spanish, read by its first two letters");
    ut_check(language_from_tag("en", &language) && language == LANGUAGE_EN, "en is English");
    ut_check(!language_from_tag("", &language) && !language_from_tag("pt", &language) &&
                 !language_from_tag("d", &language) && !language_from_tag(NULL, &language),
             "an empty tag, a sixth language, one letter and no tag name none");

    ut_section("Windows names one by the primary part of its LANGID");
    ut_check(language_from_langid(0x0407u) == LANGUAGE_DE, "0x0407 is German");
    ut_check(language_from_langid(0x0807u) == LANGUAGE_DE, "and so is Swiss German, 0x0807");
    ut_check(language_from_langid(0x040Cu) == LANGUAGE_FR, "0x040C is French");
    ut_check(language_from_langid(0x0410u) == LANGUAGE_IT, "0x0410 is Italian");
    ut_check(language_from_langid(0x0C0Au) == LANGUAGE_ES, "0x0C0A is Spanish");
    ut_check(language_from_langid(0x0409u) == LANGUAGE_EN, "0x0409 is English");
    ut_check(language_from_langid(0x0416u) == LANGUAGE_EN,
             "and Portuguese, which the game never shipped in, is English");

    ut_section("the ini's tag wins over Windows");
    ut_check(language_choose("it", &from_tag) == LANGUAGE_IT && from_tag,
             "a tag that names a language is taken, and said to be the tag");
    (void)language_choose("", &from_tag);
    ut_check(!from_tag, "an empty tag leaves it to Windows");
    ut_check(language_tag(LANGUAGE_ES)[0] == 'e' && language_tag(LANGUAGE_ES)[1] == 's' &&
                 language_tag(LANGUAGE_COUNT)[0] == 'e' && language_tag(LANGUAGE_COUNT)[1] == 'n',
             "the tags read back, and one past the table is English");
    return ut_summary("language");
}
