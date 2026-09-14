/* overlay_dismember.c: see overlay_dismember.h. */
#include "overlay_dismember.h"

#include "dismemberment_row.h"
#include "overlay_row_fill.h"

void overlay_dismember_row(uint32_t slot, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    if (slot != 0u) {
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
    /* Named for the thing itself: a reader looking for it is looking for the word, not for the
     * node correction underneath it. Always available, since it only writes the file. */
    overlay_row_label(out->label, "Lightsaber dismemberment");
    out->on = dismemberment_row_get();
}

bool overlay_dismember_toggle(uint32_t slot)
{
    if (slot != 0u) {
        return false;
    }
    return dismemberment_row_set(!dismemberment_row_get());
}
