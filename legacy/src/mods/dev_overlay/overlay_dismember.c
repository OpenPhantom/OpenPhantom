/* overlay_dismember.c: see overlay_dismember.h. */
#include "overlay_dismember.h"

#include "dismemberment_row.h"
#include "overlay_host_value.h"
#include "overlay_kit.h"

/* On a client of a running session the host decides the mode, and the row the session has taken
 * says it in its chip. ON for either mode that is not off, the way the row reads this machine's
 * own key: 1 and 2 are both the feature switched on. */
static bool dismemberment_host_word(char *out, size_t size, bool *on)
{
    float mode;

    if (!overlay_host_value(HOST_SETTING_DISMEMBERMENT_MODE, &mode)) {
        return false;
    }
    *on = mode != 0.0f;
    return overlay_host_switch_word(*on, out, size);
}

/* The rows, in drawn order. Named for the thing itself: a reader looking for it is looking for the
 * word, not for the node correction underneath it. Always available, since it only writes the
 * settings file. */
static const overlay_kit_entry_t ROWS[] = {
    { .type      = OVERLAY_KIT_TOGGLE,
      .label     = "Lightsaber dismemberment",
      .get_on    = dismemberment_row_get,
      .set_on    = dismemberment_row_set,
      .host_word = dismemberment_host_word }
};

#define ROW_ENTRIES ((uint32_t)(sizeof ROWS / sizeof ROWS[0]))

void overlay_dismember_row(uint32_t slot, overlay_row_t *out)
{
    overlay_kit_fill(ROWS, ROW_ENTRIES, slot, NULL, out);
}

bool overlay_dismember_toggle(uint32_t slot)
{
    return overlay_kit_activate(ROWS, ROW_ENTRIES, slot);
}

uint32_t overlay_dismember_row_count(void)
{
    return overlay_kit_count(ROWS, ROW_ENTRIES);
}
