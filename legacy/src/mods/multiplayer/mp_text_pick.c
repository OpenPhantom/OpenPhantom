/* mp_text_pick.c: which language this process draws in, and reading one row out of the table.
 *
 * The table is mp_text.c. The seam is between the rows and the questions asked of them: the rows
 * grow with every screen this feature gains and these five functions do not, and a table file at
 * its size limit stops a screen from gaining a text at all.
 */
#include "mp_text.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>

/* Module state because a process draws in one language, chosen once as the ini is read. */
static language_t current = LANGUAGE_EN;

void mp_text_set_language(language_t language)
{
    current = (size_t)language < (size_t)LANGUAGE_COUNT ? language : LANGUAGE_EN;
}

language_t mp_text_language(void)
{
    return current;
}

void mp_text_choose(const char *tag)
{
    bool from_tag = false;

    mp_text_set_language(language_choose(tag, &from_tag));
    log_info("the multiplayer's own texts are in '%s', %s", language_tag(mp_text_language()),
             from_tag ? "as Language= in the ini says"
                      : "after the Windows UI language; Language=en|de|fr|it|es in the ini "
                        "chooses one");
}

const char *mp_text_in(mp_text_id_t id, language_t language)
{
    const mp_text_row_t *row = mp_text_row(id);

    if (row == NULL) {
        return "";
    }
    if ((size_t)language < (size_t)LANGUAGE_COUNT && row->text[language] != NULL) {
        return row->text[language];
    }
    return row->text[LANGUAGE_EN] != NULL ? row->text[LANGUAGE_EN] : "";
}

const char *mp_text(mp_text_id_t id)
{
    return mp_text_in(id, current);
}
