/* overlay_row_fill.c: see overlay_row_fill.h. */
#include "overlay_row_fill.h"

#include "common/text.h"

void overlay_row_defaults(overlay_row_t *out)
{
    out->kind      = OVERLAY_ROW_CHEAT;
    out->on        = false;
    out->available = true;
    out->value[0]  = '\0';
    out->expanded  = false;
    out->pending   = false;
    out->fraction  = 0.0f;
}

void overlay_row_label(char *out, const char *text)
{
    size_t i;

    if (text == NULL) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < OVERLAY_LABEL_MAX && text[i] != '\0'; ++i) {
        out[i] = text[i];
    }
    out[i] = '\0';
}

void overlay_row_typed(overlay_row_t *out, const char *editing_text,
                       void (*format)(float, char *, size_t), float value)
{
    if (editing_text != NULL) {
        text_format(out->value, sizeof out->value, "%s_", editing_text);
    } else {
        format(value, out->value, sizeof out->value);
    }
    out->value[sizeof out->value - 1] = '\0';
}

void overlay_row_clamp_fraction(overlay_row_t *out)
{
    if (out->fraction < 0.0f) {
        out->fraction = 0.0f;
    }
    if (out->fraction > 1.0f) {
        out->fraction = 1.0f;
    }
}
