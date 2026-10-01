/* overlay_modelswap.c: see overlay_modelswap.h. */
#include "overlay_modelswap.h"

#include "character_model.h"
#include "overlay_row_fill.h"

uint32_t overlay_modelswap_row_count(void)
{
    uint32_t count = character_model_count();

    return count > OVERLAY_MODELSWAP_ROWS_MAX ? OVERLAY_MODELSWAP_ROWS_MAX : count;
}

void overlay_modelswap_row(uint32_t slot, overlay_row_t *out)
{
    const char *name;

    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    name = character_model_name(slot);
    if (name == NULL) {
        overlay_row_label(out->label, "");   /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
    overlay_row_label(out->label, name);
    /* One entry of a list: the player wears one of these and the mark says which. It was a switch
     * per model, so a roster of twenty read as twenty things that could be on at once.
     *
     * Asked of the engine on every repaint rather than remembered: a level change builds a fresh
     * body in the player's own model, and nothing tells this file that it happened. */
    out->kind      = OVERLAY_ROW_CHOICE;
    out->on        = character_model_is_current(slot);
    out->available = character_model_is_available(slot);
}

bool overlay_modelswap_toggle(uint32_t slot)
{
    /* It takes effect under the click: nothing about it needs the engine's permission the way a
     * respawn does, it rebinds the body that is already standing there. */
    return character_model_select(slot);
}
