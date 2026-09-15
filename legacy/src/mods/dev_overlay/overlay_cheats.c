/* overlay_cheats.c: see overlay_cheats.h. */
#include "overlay_cheats.h"

#include "cheats_openphantom.h"
#include "overlay_row_fill.h"
#include "overlay_row_ids.h"
#include "view_range_row.h"

#include "common/text.h"

/* The order the group draws, slot by slot. The toggles carry their cheats_own_id_t as their id;
 * the three typed and slider rows carry ids past that enum, and each sits directly under the
 * toggle it belongs to, which is the one thing a plain 1:1 numbering could not give super run
 * without moving jump boost off the slot its own scale row is defined against. */
static const uint32_t SLOT_IDS[] = {
    (uint32_t)CHEATS_OWN_UNLIMITED_AMMO,
    (uint32_t)CHEATS_OWN_UNLIMITED_HEALTH,
    (uint32_t)CHEATS_OWN_INVINCIBLE_NPCS,
    (uint32_t)CHEATS_OWN_ONE_SHOT_NPCS,
    (uint32_t)CHEATS_OWN_GIANT_PLAYER,
    (uint32_t)CHEATS_OWN_TINY_PLAYER,
    (uint32_t)CHEATS_OWN_NOCLIP,
    (uint32_t)CHEATS_OWN_SUPER_RUN,
    SUPER_RUN_SPEED_ROW_ID,
    SUPER_RUN_TRACK_ROW_ID,
    (uint32_t)CHEATS_OWN_JUMP_BOOST,
    JUMP_SCALE_ROW_ID
};
_Static_assert(sizeof SLOT_IDS / sizeof SLOT_IDS[0] == OVERLAY_CHEATS_ROW_COUNT,
               "the cheats group's slot table and its row count disagree");
/* OVERLAY_CHEATS_SUPER_RUN_SPEED_SLOT and OVERLAY_CHEATS_JUMP_SCALE_SLOT name two entries of
 * this table for the tests; an array element is not a constant expression, so the tests are
 * what hold them to it. */

uint32_t overlay_cheats_id_at(uint32_t slot)
{
    return (slot < OVERLAY_CHEATS_ROW_COUNT) ? SLOT_IDS[slot] : OVERLAY_CHEATS_ROW_COUNT;
}

static void format_scale(float scale, char *out, size_t size)
{
    text_format(out, size, "%.2fx", (double)scale);
}

/* The same hundredths grid the picture group's sliders write on, for the same reason recorded
 * there: a drag that lands between two hundredths shows a number the row cannot be typed back
 * to. */
static float on_hundredths(float low, float high, float fraction)
{
    float value = low + fraction * (high - low);

    return (float)((int)(value * 100.0f + 0.5f)) / 100.0f;
}

void overlay_cheats_row(uint32_t id, const char *editing_text, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);

    switch (id) {
    case JUMP_SCALE_ROW_ID:
        out->kind = OVERLAY_ROW_VALUE;
        overlay_row_label(out->label, "Jump boost scale");
        out->available = cheats_openphantom_is_available(CHEATS_OWN_JUMP_BOOST);
        /* Read live either way: mid-edit shows what has been typed with a trailing cursor,
         * otherwise the value this cheat would multiply by right now if switched on, formatted
         * the same "1.30x" way its own chip is meant to be typed back in. */
        overlay_row_typed(out, editing_text, format_scale, cheats_openphantom_jump_boost_scale());
        return;

    case SUPER_RUN_SPEED_ROW_ID:
        out->kind = OVERLAY_ROW_VALUE;
        overlay_row_label(out->label, "Super run speed (1.1 to 4.0x)");
        out->available = cheats_openphantom_is_available(CHEATS_OWN_SUPER_RUN);
        overlay_row_typed(out, editing_text, format_scale, cheats_openphantom_super_run_scale());
        return;

    case SUPER_RUN_TRACK_ROW_ID:
        out->kind = OVERLAY_ROW_SLIDER;
        overlay_row_label(out->label, "");
        out->available = cheats_openphantom_is_available(CHEATS_OWN_SUPER_RUN);
        out->fraction  = (cheats_openphantom_super_run_scale() - SUPER_RUN_SCALE_MIN) /
                         (SUPER_RUN_SCALE_MAX - SUPER_RUN_SCALE_MIN);
        overlay_row_clamp_fraction(out);
        return;

    default:
        break;
    }
    if (id >= (uint32_t)CHEATS_OWN_FREECAM) {
        overlay_row_label(out->label, "");    /* past the group; a blank shows the caller's bug */
        out->available = false;
        return;
    }
    /* Everything left is one of the cheats (ammunition, health, invincible NPCs, one-shot NPCs,
     * giant player, tiny player, no clip, super run, jump boost's own toggle), whose ids are
     * their cheats_own_id_t. Free camera, the last id of that enum, is drawn by its own group and
     * never reaches here. */
    overlay_row_label(out->label, cheats_openphantom_name((cheats_own_id_t)id));
    out->on = cheats_openphantom_is_on((cheats_own_id_t)id);
    out->available = cheats_openphantom_is_available((cheats_own_id_t)id);
}

bool overlay_cheats_toggle(uint32_t id)
{
    if (id >= (uint32_t)CHEATS_OWN_FREECAM) {
        return false;    /* the typed rows and the track are not switches; past the end, nothing */
    }
    (void)cheats_openphantom_toggle((cheats_own_id_t)id);
    return true;
}

bool overlay_cheats_commit(uint32_t id, const char *text)
{
    float parsed;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    /* Refused when the text is not a number, and refused at zero or below: atof answers 0 for a
     * lone full stop, and a setter's clamp would otherwise turn a typing mistake into the floor.
     * The hand written parser is the one that keeps reading a full stop as a decimal point on a
     * machine whose language does not agree. */
    if (!view_range_row_parse(text, &parsed) || !(parsed > 0.0f)) {
        return false;
    }
    switch (id) {
    case JUMP_SCALE_ROW_ID:
        cheats_openphantom_jump_boost_set_scale(parsed);
        return true;
    case SUPER_RUN_SPEED_ROW_ID:
        return cheats_openphantom_super_run_set_scale(parsed);
    default:
        return false;
    }
}

bool overlay_cheats_slider_set(uint32_t id, float fraction)
{
    if (id != SUPER_RUN_TRACK_ROW_ID) {
        return false;
    }
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    return cheats_openphantom_super_run_set_scale(on_hundredths(SUPER_RUN_SCALE_MIN,
                                                                SUPER_RUN_SCALE_MAX, fraction));
}
