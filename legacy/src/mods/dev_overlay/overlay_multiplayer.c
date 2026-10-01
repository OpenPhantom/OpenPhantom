/* overlay_multiplayer.c: see overlay_multiplayer.h. */
#include "overlay_multiplayer.h"

#include "chat_key_row.h"
#include "overlay_row_fill.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots, in drawn order. Named rather than numbered at the use sites, so that a second row
 * changes this list and nothing that reads it. */
typedef enum multiplayer_slot {
    MULTIPLAYER_CHAT_KEY = 0
} multiplayer_slot_t;

_Static_assert((uint32_t)MULTIPLAYER_CHAT_KEY + 1u == OVERLAY_MULTIPLAYER_ROW_COUNT,
               "the slot enum and OVERLAY_MULTIPLAYER_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

void overlay_multiplayer_row(uint32_t slot, bool capturing, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);

    switch ((multiplayer_slot_t)slot) {
    case MULTIPLAYER_CHAT_KEY:
        /* The label stays what it was under Menus, so the search for "chat" or "key" finds it
         * where it finds it now, and the settings file's comment names it the same way. */
        out->kind = OVERLAY_ROW_HOTKEY;
        overlay_row_label(out->label, "Key that opens the chat");
        if (capturing) {
            text_format(out->value, sizeof out->value, "...");
        } else {
            (void)chat_key_row_name(chat_key_row_get(), out->value, sizeof out->value);
        }
        out->value[sizeof out->value - 1] = '\0';
        return;

    default:
        /* Past the end. An empty unavailable row rather than whatever the caller's struct held,
         * so a caller asking for a slot that does not exist shows a blank line and not stale
         * text. */
        overlay_row_label(out->label, "");
        out->available = false;
        return;
    }
}

bool overlay_multiplayer_bind(uint32_t slot, int32_t virtual_key)
{
    if ((multiplayer_slot_t)slot != MULTIPLAYER_CHAT_KEY) {
        return false;
    }
    return chat_key_row_set(virtual_key);
}
