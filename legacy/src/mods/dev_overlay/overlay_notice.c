/* overlay_notice.c: see overlay_notice.h. */
#include "overlay_notice.h"

#include <stdbool.h>
#include <stddef.h>

static char said[OVERLAY_NOTICE_MAX];

/* What kind of sentence `said` holds, written in the same breath as the sentence so the two can
 * never describe different sentences. Read only while one stands. */
static bool confirmed;

static void stand(const char *text, bool confirmation)
{
    size_t i;

    if (text == NULL || text[0] == '\0') {
        return;
    }
    for (i = 0; i + 1u < sizeof said && text[i] != '\0'; ++i) {
        said[i] = text[i];
    }
    said[i] = '\0';
    /* Cut the way the drawing cuts a label that will not fit, two dots and no more, so a sentence
     * written too long says it was shortened instead of ending mid word. */
    if (text[i] != '\0') {
        said[OVERLAY_NOTICE_CHARS - 2u] = '.';
        said[OVERLAY_NOTICE_CHARS - 1u] = '.';
        said[OVERLAY_NOTICE_CHARS]      = '\0';
    }
    confirmed = confirmation;
}

void overlay_notice_say(const char *text)
{
    stand(text, false);
}

void overlay_notice_confirm(const char *text)
{
    stand(text, true);
}

bool overlay_notice_confirms(void)
{
    return said[0] != '\0' && confirmed;
}

const char *overlay_notice_text(void)
{
    return (said[0] != '\0') ? said : NULL;
}

void overlay_notice_act(void)
{
    said[0] = '\0';
}

void overlay_notice_forget(void)
{
    said[0] = '\0';
}
