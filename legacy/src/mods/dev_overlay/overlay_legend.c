/* overlay_legend.c: see overlay_legend.h. */
#include "overlay_legend.h"

#include "session_lock.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>

/* Four keys, and they are the four that do something everywhere in the panel, except while a key
 * row waits for its key, when the prompt below stands in their place. Escape is last because it
 * is the one a player already half expects, and it used to be the only one the panel said
 * anything about: it stood on the right of the title band, and it came off there when this line
 * arrived, because one sentence in two places is the pair that drifts apart.
 *
 * The words are short on purpose. They are the first thing dropped when the panel is narrow, and
 * the panel is only ever as wide as its longest row needs. */
static const overlay_legend_key_t KEYS[OVERLAY_LEGEND_KEYS] = {
    { "Up/Down",    "move" },
    { "Left/Right", "fold, set, tab" },
    { "Return",     "act" },
    { "Esc",        "close" }
};

const overlay_legend_key_t *overlay_legend_key(uint32_t index)
{
    return (index < OVERLAY_LEGEND_KEYS) ? &KEYS[index] : NULL;
}

/* Lower case like the grey words beside the caps, because it is the line's own voice and not a
 * key's name. */
const char *overlay_legend_prompt(bool capturing)
{
    return capturing ? "press a key" : NULL;
}

void overlay_legend_right(char *out, size_t size, uint32_t headings, uint32_t rows)
{
    if (out == NULL || size == 0u) {
        return;
    }
    if (session_lock_running()) {
        /* Which end of the session this machine is, and no more than that. The note says whether
         * one runs and who hosts it; it carries no count of the players, so neither does this. */
        text_format(out, size, "session - %s", session_lock_is_host() ? "host" : "client");
        return;
    }
    text_format(out, size, "%u groups - %u rows", (unsigned)headings, (unsigned)rows);
}

overlay_legend_fit_t overlay_legend_fit(float room, float caps, float words, float right)
{
    /* One gap between the caps and the right hand text, so that the two never touch. It is the
     * only spacing decided here; where each piece lands is the painter's. */
    const float gap = (caps > 0.0f) ? (caps / (float)OVERLAY_LEGEND_KEYS) : 0.0f;

    if (caps + words + gap + right <= room) {
        return OVERLAY_LEGEND_FULL;
    }
    if (caps + gap + right <= room) {
        return OVERLAY_LEGEND_CAPS;
    }
    return OVERLAY_LEGEND_BARE;
}

uint32_t overlay_legend_caps_that_fit(float room, float gap, const float *cap_widths)
{
    float    used = 0.0f;
    uint32_t fits = 0u;
    uint32_t i;

    if (cap_widths == NULL) {
        return 0u;
    }
    for (i = 0; i < OVERLAY_LEGEND_KEYS; ++i) {
        const float box = (cap_widths[i] > 0.0f) ? cap_widths[i] : 0.0f;

        if (used + box > room) {
            break;
        }
        used += box + gap;
        ++fits;
    }
    return fits;
}
