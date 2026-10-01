/* overlay_edit.c: see overlay_edit.h. */
#include "overlay_edit.h"

#include "overlay_model.h"
#include "overlay_notice.h"

#include "overlay_cheats.h"
#include "overlay_controls.h"
#include "overlay_fog.h"
#include "overlay_framerate.h"
#include "overlay_freecam.h"
#include "overlay_multiplayer.h"
#include "overlay_picture.h"
#include "overlay_row_ids.h"
#include "overlay_spawn.h"
#include "overlay_utilities.h"
#include "overlay_window.h"

#include "cheats_openphantom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/text.h"

#include <string.h>

typedef struct overlay_edit_state {
    bool     capturing;      /* a key row is waiting for a keypress */
    uint32_t capture_row;    /* which one; several rows here bind a key */
    bool     typing;         /* a value row is waiting for typed digits */
    uint32_t value_row;      /* which one, a row id and not a position */
    /* What has been typed so far: six usable characters plus the terminator, which is longer
     * than any value this panel accepts. The row appends its own cursor to make "<typed>_", and
     * the row's own value[16] holds that with room to spare. */
    char     typed[7];
} overlay_edit_state_t;

static overlay_edit_state_t edit;

void overlay_edit_forget(void)
{
    edit.capturing = false;
    edit.typing = false;
    edit.typed[0] = '\0';
}

void overlay_edit_start_capture(uint32_t row_id)
{
    /* Does not bind anything itself. The panel's keyboard handling routes the next key press to
     * overlay_model_capture_hotkey() while this is true, rather than that key reaching its usual
     * handling. The row is remembered because there is more than one that binds a key, and the
     * capture has to land on the one that was clicked. */
    edit.capturing = true;
    edit.capture_row = row_id;
    edit.typing = false;
}

void overlay_edit_start_value(uint32_t row_id)
{
    /* A fresh number, discarding anything left over from an edit that was never committed, the
     * same "click it again to redo it" shape the key row above has. Does not touch the stored
     * value itself; only overlay_model_value_commit() does. */
    edit.typing = true;
    edit.value_row = row_id;
    edit.typed[0] = '\0';
    edit.capturing = false;
}

const char *overlay_edit_text_for(uint32_t row_id)
{
    return (edit.typing && edit.value_row == row_id) ? edit.typed : NULL;
}

bool overlay_edit_capturing_row(uint32_t row_id)
{
    return edit.capturing && edit.capture_row == row_id;
}

bool overlay_model_is_capturing_hotkey(void)
{
    return edit.capturing;
}

void overlay_model_capture_hotkey(int32_t virtual_key)
{
    uint32_t row;

    if (!edit.capturing) {
        return;
    }
    row = edit.capture_row;
    edit.capturing = false;
    /* Tested from the HIGHEST base downwards. These are open-ended ranges, so asking about
     * Utilities first would answer yes for a Window row as well and bind the wrong setting. The
     * multiplayer's block is the highest, and without this arm its key row would fall into the
     * model swap's below it and be dropped. */
    if (row >= MULTIPLAYER_FIRST_ID) {
        (void)overlay_multiplayer_bind(row - MULTIPLAYER_FIRST_ID, virtual_key);
        return;
    }
    if (row >= MODELSWAP_FIRST_ID) {
        return;     /* nothing there binds a key */
    }
    if (row >= SPAWN_FIRST_ID) {
        (void)overlay_spawn_bind(row - SPAWN_FIRST_ID, virtual_key);
        return;
    }
    if (row >= LEVELS_FIRST_ID) {
        return;     /* nothing there binds a key */
    }
    if (row >= FREECAM_FIRST_ID) {
        cheats_openphantom_freecam_set_hotkey(virtual_key);
        return;
    }
    if (row >= WINDOW_FIRST_ID) {
        (void)overlay_window_bind(row - WINDOW_FIRST_ID, virtual_key);
        return;
    }
    if (row >= UTILITIES_FIRST_ID) {
        /* A refusal leaves the binding alone and the row shows the key it still has, which is the
         * same shape the value rows use for text that is not a number. Each key row says which
         * keys it refuses; see open_key_row.c here and chat_key_row.c for the chat's. */
        (void)overlay_utilities_bind(row - UTILITIES_FIRST_ID, virtual_key);
    }
}

bool overlay_model_is_editing_value(void)
{
    return edit.typing;
}

void overlay_model_value_append(char digit)
{
    size_t length;

    if (!edit.typing) {
        return;
    }
    /* Only what a positive decimal number can contain, and only one point. Anything else is
     * refused outright rather than accepted and left to fail atof() later, the same "do not accept
     * what cannot mean anything" reasoning overlay_model_search_append() above applies to its own,
     * much wider, set of allowed characters. */
    if (digit != '.' && (digit < '0' || digit > '9')) {
        return;
    }
    if (digit == '.' && strchr(edit.typed, '.') != NULL) {
        return;
    }
    length = strlen(edit.typed);
    if (length + 1u >= sizeof edit.typed) {
        return;
    }
    edit.typed[length] = digit;
    edit.typed[length + 1u] = '\0';
}

void overlay_model_value_backspace(void)
{
    size_t length;

    if (!edit.typing) {
        return;
    }
    length = strlen(edit.typed);
    if (length > 0u) {
        edit.typed[length - 1u] = '\0';
    }
}

/* Which group a typed row belongs to, and what it made of the text. Every arm answers false for
 * a refusal, and every one of them used to be thrown away right here: the setting was left alone,
 * the row went on showing the value it already had, and that is EXACTLY what the panel does when
 * the write worked and the value happened to be the same. Seven silent takers, not one.
 *
 * Highest base first, for the reason given at the matching test in the hotkey path. */
static bool take_the_number(uint32_t row, const char *typed)
{
    if (row >= LEVELS_FIRST_ID) {
        return true;    /* nothing there is typed into, so nothing was refused either */
    }
    if (row >= FOG_FIRST_ID) {
        return overlay_fog_commit(row - FOG_FIRST_ID, typed);
    }
    if (row >= PICTURE_FIRST_ID) {
        return overlay_picture_commit(row - PICTURE_FIRST_ID, typed);
    }
    if (row >= CONTROLS_FIRST_ID) {
        return overlay_controls_commit(row - CONTROLS_FIRST_ID, typed);
    }
    if (row >= FRAMERATE_FIRST_ID) {
        return overlay_framerate_accept_value(row - FRAMERATE_FIRST_ID, typed);
    }
    if (row >= WINDOW_FIRST_ID) {
        return overlay_window_commit(row - WINDOW_FIRST_ID, typed);
    }
    if (row >= UTILITIES_FIRST_ID) {
        return overlay_utilities_commit(row - UTILITIES_FIRST_ID, typed);
    }
    /* The cheats group's two typed rows, the jump boost scale and the super run speed. */
    return overlay_cheats_commit(row, typed);
}

void overlay_model_value_commit(void)
{
    uint32_t row;

    if (!edit.typing) {
        return;
    }
    row = edit.value_row;
    edit.typing = false;
    if (edit.typed[0] == 0) {
        return;      /* nothing was typed, leave whatever value was already set alone */
    }
    if (!take_the_number(row, edit.typed)) {
        /* One sentence for all seven, written once. What the row's own ends are is on the row's
         * label, which is still on screen under the band; what is NOT anywhere else is that the
         * number was refused at all. */
        char line[OVERLAY_NOTICE_MAX];

        text_format(line, sizeof line, "Refused: %s is not a value that row takes", edit.typed);
        overlay_notice_say(line);
    }
}

void overlay_model_value_cancel(void)
{
    edit.typing = false;
}

