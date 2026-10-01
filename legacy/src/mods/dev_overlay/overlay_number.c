/* overlay_number.c: see overlay_number.h. */
#include "overlay_number.h"

#include <stdbool.h>
#include <stdint.h>

static float on_the_track(float fraction)
{
    if (fraction < 0.0f) {
        return 0.0f;
    }
    if (fraction > 1.0f) {
        return 1.0f;
    }
    return fraction;
}

bool overlay_number_usable(const overlay_number_t *n)
{
    return n != NULL && n->maximum > n->minimum;
}

bool overlay_number_fraction_of(const overlay_number_t *n, float value, float *out)
{
    if (!overlay_number_usable(n) || out == NULL) {
        return false;
    }
    *out = on_the_track((value - n->minimum) / (n->maximum - n->minimum));
    return true;
}

bool overlay_number_value_at(const overlay_number_t *n, float fraction, float *out)
{
    if (!overlay_number_usable(n) || out == NULL) {
        return false;
    }
    *out = n->minimum + on_the_track(fraction) * (n->maximum - n->minimum);
    return true;
}

bool overlay_number_stepped(const overlay_number_t *n, float fraction, int32_t by, bool coarse,
                            float *out)
{
    const float amount = coarse ? n->coarse : n->step;
    float       value;

    if (!overlay_number_usable(n) || out == NULL || !(amount > 0.0f) || by == 0) {
        return false;
    }
    (void)overlay_number_value_at(n, fraction, &value);
    /* Clamped as a VALUE and not as a fraction, so a press at an end lands exactly on that end
     * rather than a press-width past it and then back. The row above shows the end it reached. */
    value += (by > 0) ? amount : -amount;
    if (value < n->minimum) {
        value = n->minimum;
    }
    if (value > n->maximum) {
        value = n->maximum;
    }
    return overlay_number_fraction_of(n, value, out);
}

bool overlay_number_standard(const overlay_number_t *n, float *out)
{
    if (!overlay_number_usable(n) || !n->has_standard) {
        return false;
    }
    return overlay_number_fraction_of(n, n->standard, out);
}

overlay_number_fit_t overlay_number_fit(float room, float least, float value_w, float default_w,
                                        float gap, bool have_default)
{
    if (have_default && least + gap + value_w + gap + default_w <= room) {
        return OVERLAY_NUMBER_FIT_BOTH;
    }
    if (least + gap + value_w <= room) {
        return OVERLAY_NUMBER_FIT_VALUE;
    }
    return OVERLAY_NUMBER_FIT_NONE;
}

bool overlay_number_columns(float x0, float edge, float least, float number_w, float default_w,
                            float gap, bool have_default, overlay_number_columns_t *out)
{
    overlay_number_columns_t c;

    if (out == NULL || !(edge > x0)) {
        return false;
    }
    c.fit = overlay_number_fit(edge - x0, least, number_w, default_w, gap, have_default);
    /* Everything ends at the margin and nothing is drawn: that is the track-alone case, and
     * writing it down first means the two cases below each say only what they add. */
    c.track_x1   = edge;
    c.number_x1  = edge;
    c.default_x0 = edge;
    c.default_x1 = edge;
    if (c.fit == OVERLAY_NUMBER_FIT_BOTH) {
        c.default_x0 = edge - default_w;
        c.number_x1  = c.default_x0 - gap;
        c.track_x1   = c.number_x1 - number_w - gap;
    } else if (c.fit == OVERLAY_NUMBER_FIT_VALUE) {
        c.track_x1 = edge - number_w - gap;
    }
    if (!(c.track_x1 > x0)) {
        return false;
    }
    *out = c;
    return true;
}

bool overlay_number_grabs(float x, float x0, float x1, float text_h, float gap)
{
    /* Behind the track is the row's own indent and nothing else, so the whole text height is
     * free there. Ahead of it is `gap` and then the number, so the slack stops where the number
     * begins: it reached a text height that way too, and a press on the number's first half
     * height took hold of the track instead. A grab writes at once and is not throttled, so what
     * that press did was put the row at its MAXIMUM. */
    return x >= x0 - text_h && x <= x1 + gap;
}
