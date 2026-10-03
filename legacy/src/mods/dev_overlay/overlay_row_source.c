/* overlay_row_source.c: see overlay_row_source.h. */
#include "overlay_row_source.h"

#include "overlay_edit.h"
#include "overlay_reason.h"
#include "overlay_row_fill.h"

#include "overlay_cheats.h"
#include "overlay_controls.h"
#include "overlay_dismember.h"
#include "overlay_fog.h"
#include "overlay_framerate.h"
#include "overlay_freecam.h"
#include "overlay_levels.h"
#include "overlay_menu_extras.h"
#include "overlay_modelswap.h"
#include "overlay_multiplayer.h"
#include "overlay_picture.h"
#include "overlay_row_ids.h"
#include "overlay_spawn.h"
#include "overlay_utilities.h"
#include "overlay_window.h"

#include "cheats_original.h"
#include "cheats_original_actions.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many rows a group's source holds, and what one of them looks like. The six sources differ
 * in everything except this shape, so the rest of the file does not care which group is open. */
uint32_t overlay_row_source_count(overlay_group_t group)
{
    switch (group) {
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        return cheats_original_count();
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        return (uint32_t)CHEATS_ACTION_COUNT;
    case OVERLAY_GROUP_OPENPHANTOM_UTILITIES:
        return OVERLAY_UTILITIES_ROW_COUNT;
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
        /* Three groups count their own table rather than answering with the constant beside it;
         * see overlay_kit.h. The constant is still what the row ids are budgeted against in
         * overlay_row_ids.h, and unittests/overlay_groups.c is what holds the two together. */
        return overlay_picture_row_count();
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
        return overlay_fog_row_count();
    case OVERLAY_GROUP_OPENPHANTOM_CONTROLS:
        return overlay_controls_row_count();   /* eight, and the fold's lines while open */
    case OVERLAY_GROUP_OPENPHANTOM_WINDOW:
        return overlay_window_row_count();
    case OVERLAY_GROUP_OPENPHANTOM_FRAMERATE:
        return OVERLAY_FRAMERATE_ROW_COUNT;
    case OVERLAY_GROUP_OPENPHANTOM_FREECAM:
        return overlay_freecam_row_count();   /* five, and the fold's lines while it is open */
    case OVERLAY_GROUP_OPENPHANTOM_LEVELS:
        return overlay_levels_row_count();    /* two, and the list while it is open */
    case OVERLAY_GROUP_OPENPHANTOM_SPAWN:
        return overlay_spawn_row_count();     /* two, and the level's actors while open */
    case OVERLAY_GROUP_OPENPHANTOM_DISMEMBER:
        return overlay_dismember_row_count();
    case OVERLAY_GROUP_OPENPHANTOM_MODELSWAP:
        return overlay_modelswap_row_count();   /* what the roster offers on this installation */
    case OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS:
        return overlay_menu_extras_row_count();   /* two, and the fold's lines while open */
    case OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER:
        return overlay_multiplayer_row_count();   /* three, and the line after a press */
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* Every cheat but free camera and the jump-boost scale; the numbering is in
         * overlay_row_ids.h. */
        return OVERLAY_CHEATS_ROW_COUNT;
    }
}

void overlay_row_source_fill(overlay_group_t group, uint32_t id, overlay_row_t *out)
{
    out->group = (uint32_t)group;
    out->id = id;
    out->expanded = false;
    out->pending = false;      /* only the actions group's own play-as rows ever set this */
    out->value[0] = '\0';      /* only graphics detail, among all the actions, ever sets this */
    out->fraction = 0.0f;      /* only a SLIDER row ever sets this */
    out->chosen = 0u;          /* only a SEGMENT row ever sets this */
    out->host_value = false;   /* only a row the host of a running session decides sets this */
    out->reason = (uint32_t)OVERLAY_REASON_NONE;   /* only an unavailable row ever sets this */

    switch (group) {
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        out->kind = OVERLAY_ROW_CHEAT;
        cheats_original_label(id, out->label, sizeof out->label);   /* what it does, and the code */
        out->on = cheats_original_is_on(id);
        out->available = true;         /* a row only exists here once its table resolved */
        return;
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        out->kind = OVERLAY_ROW_ACTION;
        overlay_row_label(out->label, cheats_original_actions_name((cheats_action_id_t)id));
        out->on = false;               /* meaningless for an action; never read by the drawer */
        out->available = cheats_original_actions_is_available((cheats_action_id_t)id);
        out->reason = cheats_original_actions_reason((cheats_action_id_t)id);
        out->pending = cheats_original_actions_is_pending((cheats_action_id_t)id);
        /* Read live on every rebuild, not cached from the press that set it: the retail console can
         * also cycle this, and a chip showing a level nobody is at any more would be lying about
         * the one thing this row exists to show. */
        if ((cheats_action_id_t)id == CHEATS_ACTION_GRAPHICS_DETAIL) {
            int32_t level = cheats_original_actions_graphics_level();

            if (level > 0) {
                text_format(out->value, sizeof out->value, "%d", (int)level);
            }
        }
        return;
    case OVERLAY_GROUP_OPENPHANTOM_UTILITIES: {
        /* A slot here IS its position in the list, with none of the arithmetic the cheats group
         * below needs: nothing in this group is positioned relative to a cheat enum and nothing in
         * it folds. The id carries the base so it cannot be confused with a cheats-group id; see
         * UTILITIES_FIRST_ID. */
        const char *editing = NULL;
        bool        capturing = false;

        out->id = UTILITIES_FIRST_ID + id;
        editing = overlay_edit_text_for(out->id);
        capturing = overlay_edit_capturing_row(out->id);
        overlay_utilities_row(id, editing, capturing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE: {
        /* The same shape as the utilities: a slot is its position, the id carries a base, and
         * only the typed rows need to know what is being typed. No key rows here. */
        const char *editing = NULL;

        out->id = PICTURE_FIRST_ID + id;
        editing = overlay_edit_text_for(out->id);
        overlay_picture_row(id, editing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM_FOG: {
        const char *editing = NULL;

        out->id = FOG_FIRST_ID + id;
        editing = overlay_edit_text_for(out->id);
        overlay_fog_row(id, editing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM_DISMEMBER:
        out->id = DISMEMBER_FIRST_ID + id;
        overlay_dismember_row(id, out);
        break;
    case OVERLAY_GROUP_OPENPHANTOM_MODELSWAP:
        out->id = MODELSWAP_FIRST_ID + id;
        overlay_modelswap_row(id, out);
        return;
    case OVERLAY_GROUP_OPENPHANTOM_LEVELS:
        out->id = LEVELS_FIRST_ID + id;
        overlay_levels_row(id, out);
        return;
    case OVERLAY_GROUP_OPENPHANTOM_SPAWN:
        /* Two key rows here; their slots never move, so the capture lands on the one clicked. */
        out->id = SPAWN_FIRST_ID + id;
        overlay_spawn_row(id, overlay_edit_capturing_row(out->id),
                          out);
        return;
    case OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS:
        out->id = MENU_EXTRAS_FIRST_ID + id;
        overlay_menu_extras_row(id, out);
        return;
    case OVERLAY_GROUP_OPENPHANTOM_FREECAM:
        /* The key row is the one capture this group holds; the fold's state is the group's own. */
        out->id = FREECAM_FIRST_ID + id;
        overlay_freecam_row(id, overlay_edit_capturing_row(out->id),
                            out);
        return;
    case OVERLAY_GROUP_OPENPHANTOM_CONTROLS: {
        const char *editing = NULL;

        out->id = CONTROLS_FIRST_ID + id;
        editing = overlay_edit_text_for(out->id);
        overlay_controls_row(id, editing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM_FRAMERATE: {
        /* Same shape again: the id carries a base so no two groups' ids can be confused. */
        const char *editing = NULL;

        out->id = FRAMERATE_FIRST_ID + id;
        editing = overlay_edit_text_for(out->id);
        overlay_framerate_row(id, editing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM_WINDOW: {
        /* The same shape as the group above, and for the same reason: a slot here is its own
         * position and the id carries a base so the two groups' ids cannot be confused. */
        const char *editing = NULL;
        bool        capturing = false;

        out->id = WINDOW_FIRST_ID + id;
        editing = overlay_edit_text_for(out->id);
        capturing = overlay_edit_capturing_row(out->id);
        overlay_window_row(id, editing, capturing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER:
        /* One key row, the first, and the capture lands on it by its id, which carries the
         * group's base. The rows under it bind nothing. */
        out->id = MULTIPLAYER_FIRST_ID + id;
        overlay_multiplayer_row(id, overlay_edit_capturing_row(out->id), out);
        return;
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* The group's own order, slot to id, so a typed row sits under its toggle. */
        out->id = overlay_cheats_id_at(id);
        overlay_cheats_row(out->id, overlay_edit_text_for(out->id), out);
        return;
    }
}


/* ============================================================================================
 * The track on a row, asked of the group that owns it.
 *
 * Four questions with one switch each, and the same switch four times over: which group, then
 * that group's own slot. They were in overlay_model.c, which is a file about the panel's state
 * and held no state at all for these; this file is where a per-group difference is absorbed, and
 * they were the last dispatch left outside it.
 *
 * The caller has already established that the row is a track and can be used. `row` is passed
 * whole rather than by index because the index belongs to the model's list and the group and id
 * are all that is needed here.
 * ========================================================================================== */
bool overlay_row_source_slider_set(const overlay_row_t *row, float fraction)
{
    switch ((overlay_group_t)row->group) {
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
        return overlay_picture_slider_set(row->id - PICTURE_FIRST_ID, fraction);
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
        return overlay_fog_slider_set(row->id - FOG_FIRST_ID, fraction);
    case OVERLAY_GROUP_OPENPHANTOM_CONTROLS:
        return overlay_controls_slider_set(row->id - CONTROLS_FIRST_ID, fraction);
    case OVERLAY_GROUP_OPENPHANTOM:
        return overlay_cheats_slider_set(row->id, fraction);
    default:
        return false;              /* nothing else offers one */
    }
}

bool overlay_row_source_slider_value(const overlay_row_t *row, float fraction, char *out,
                                     size_t size)
{
    switch ((overlay_group_t)row->group) {
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
        return overlay_picture_slider_value(row->id - PICTURE_FIRST_ID, fraction, out, size);
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
        return overlay_fog_slider_value(row->id - FOG_FIRST_ID, fraction, out, size);
    case OVERLAY_GROUP_OPENPHANTOM_CONTROLS:
        return overlay_controls_slider_value(row->id - CONTROLS_FIRST_ID, fraction, out, size);
    case OVERLAY_GROUP_OPENPHANTOM:
        return overlay_cheats_slider_value(row->id, fraction, out, size);
    default:
        return false;              /* nothing else offers one */
    }
}

bool overlay_row_source_slider_limits(const overlay_row_t *row, overlay_number_t *out)
{
    switch ((overlay_group_t)row->group) {
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
        return overlay_picture_slider_limits(row->id - PICTURE_FIRST_ID, out);
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
        return overlay_fog_slider_limits(row->id - FOG_FIRST_ID, out);
    case OVERLAY_GROUP_OPENPHANTOM_CONTROLS:
        return overlay_controls_slider_limits(row->id - CONTROLS_FIRST_ID, out);
    case OVERLAY_GROUP_OPENPHANTOM:
        return overlay_cheats_slider_limits(row->id, out);
    default:
        return false;              /* nothing else offers one */
    }
}
