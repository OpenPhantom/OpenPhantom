/* overlay_cheats.c: see overlay_cheats.h. */
#include "overlay_cheats.h"

#include "cheats_openphantom.h"
#include "overlay_row_fill.h"
#include "overlay_row_ids.h"

#include "common/text.h"

/* The cheats group: the rows that line up with cheats_own_id_t, and the two that do not (the
 * jump boost scale and the level skip). A slot here is its id. */
void overlay_cheats_row(uint32_t id, const char *editing_text, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);

    if (id == JUMP_SCALE_ROW_ID) {
        out->kind = OVERLAY_ROW_VALUE;
        overlay_row_label(out->label, "Jump boost scale");
        out->available = cheats_openphantom_is_available(CHEATS_OWN_JUMP_BOOST);
        /* Read live either way: mid-edit shows exactly what has been typed with a trailing
         * cursor, otherwise the value this cheat would multiply by right now if switched on,
         * formatted the same "1.30x" way its own chip is meant to be typed back in. */
        if (editing_text != NULL) {
            text_format(out->value, sizeof out->value, "%s_", editing_text);
        } else {
            text_format(out->value, sizeof out->value, "%.2fx",
                        (double)cheats_openphantom_jump_boost_scale());
        }
        out->value[sizeof out->value - 1] = '\0';
        return;
    }
    if (id == END_LEVEL_ROW_ID) {
        out->kind = OVERLAY_ROW_ACTION;
        /* It HAS been played since this row was added, so the label no longer says it
           has not. It stays marked as a debug tool, which is still true: it is a jump to the
           next level with none of the bookkeeping the game itself does on the way out of
           one. */
        overlay_row_label(out->label, "Skip to next level (debug)");
        out->available = cheats_openphantom_end_level_is_available();
        return;
    }
    if (id >= OVERLAY_CHEATS_ROW_COUNT) {
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
    /* Everything left is one of the cheats (ammunition, health, invincible NPCs, one-shot NPCs,
     * giant player, tiny player, no clip, jump boost's own toggle), whose ids line up 1:1 with
     * cheats_own_id_t. Free camera, the last id of that enum, is drawn by its own group and
     * never reaches here: the scale row above holds its number. */
    overlay_row_label(out->label, cheats_openphantom_name((cheats_own_id_t)id));
    out->on = cheats_openphantom_is_on((cheats_own_id_t)id);
    out->available = cheats_openphantom_is_available((cheats_own_id_t)id);
}

bool overlay_cheats_toggle(uint32_t id)
{
    if (id == END_LEVEL_ROW_ID) {
        return cheats_openphantom_end_level_invoke();
    }
    if (id == JUMP_SCALE_ROW_ID || id >= OVERLAY_CHEATS_ROW_COUNT) {
        return false;    /* the typed row is the model's own business; past the end is nothing */
    }
    (void)cheats_openphantom_toggle((cheats_own_id_t)id);
    return true;
}
