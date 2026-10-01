/* local_look.c: see the header. */
#include "local_look.h"

#include "common/appearance_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The whole of this machine's look, as the note last took it. An empty model is what a player
 * who has swapped nothing wears, so the record starts right. */
static struct {
    float scale;
    char  model[APPEARANCE_MODEL_MAX];
} look = { 1.0f, { 0 } };

static void say_it(void)
{
    (void)appearance_note_publish(look.model, look.scale);
}

bool local_look_set_model(const char *model)
{
    char   wanted[APPEARANCE_MODEL_MAX];
    size_t length = 0u;

    while (model != NULL && model[length] != '\0') {
        ++length;
    }
    if (length + 1u > sizeof wanted) {
        return false;   /* a name this long is not one the resource layer would have loaded */
    }
    if (length == strlen(look.model) && (length == 0u || memcmp(look.model, model, length) == 0)) {
        return true;
    }
    memset(wanted, 0, sizeof wanted);
    if (length != 0u) {
        memcpy(wanted, model, length);
    }
    /* Kept only once the note took it: a model kept before would read as said, and the next
     * call with the same name would then say nothing. */
    if (!appearance_note_publish(wanted, look.scale)) {
        return false;
    }
    memcpy(look.model, wanted, sizeof look.model);
    return true;
}

void local_look_set_scale(float scale)
{
    if (!(scale >= APPEARANCE_SCALE_MIN) || !(scale <= APPEARANCE_SCALE_MAX)) {
        return;
    }
    if (scale == look.scale) {
        return;   /* the note bumps its serial on every publication, so a repeat is not free */
    }
    /* Kept only once the note took it, as the model is: a scale kept before would read as said,
     * and the next press of the same row would say nothing. */
    if (appearance_note_publish(look.model, scale)) {
        look.scale = scale;
    }
}
