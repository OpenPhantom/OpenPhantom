/* overlay_utilities.c: see overlay_utilities.h. */
#include "overlay_utilities.h"

#include "dev_menu_size_row.h"
#include "open_key_row.h"
#include "overlay_key_name.h"
#include "overlay_row_fill.h"

#include "common/text.h"

/* The slots, in drawn order. Named rather than numbered at the use sites, because the order is a
 * reading decision and whoever changes it should have to change one list. */
typedef enum utilities_slot {
    UTILITIES_DEV_MENU_SIZE = 0,
    UTILITIES_OPEN_KEY
} utilities_slot_t;

_Static_assert((uint32_t)UTILITIES_OPEN_KEY + 1u == OVERLAY_UTILITIES_ROW_COUNT,
               "the slot enum and OVERLAY_UTILITIES_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

void overlay_utilities_row(uint32_t slot, const char *editing_text, bool capturing,
                           overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);

    switch ((utilities_slot_t)slot) {
    case UTILITIES_DEV_MENU_SIZE:
        out->kind = OVERLAY_ROW_VALUE;
        overlay_row_label(out->label, "Cheatmenu size (0.33 to 4.0)");
        overlay_row_typed(out, editing_text, dev_menu_size_row_format, dev_menu_size_row_get());
        return;

    case UTILITIES_OPEN_KEY:
        out->kind = OVERLAY_ROW_HOTKEY;
        overlay_row_label(out->label, "Key that opens this menu");
        if (capturing) {
            text_format(out->value, sizeof out->value, "...");
        } else {
            int32_t vk = open_key_row_get();

            if (vk != 0) {
                overlay_key_name(vk, out->value, sizeof out->value);
            } else {
                /* The default accepts three keys, so naming one would be a lie about the other
                   two. F6 is the one every keyboard has in the same place, so it is the one worth
                   telling a player about. */
                text_format(out->value, sizeof out->value, "F6 or ~");
            }
        }
        out->value[sizeof out->value - 1] = '\0';
        return;

    default:
        /* Past the end. Answered as an empty unavailable row rather than left as whatever the
         * caller's struct held: a caller asking for a slot that does not exist has a bug, and a
         * blank row makes that bug visible instead of showing stale text. */
        overlay_row_label(out->label, "");
        out->available = false;
        return;
    }
}

bool overlay_utilities_row_is_key(uint32_t slot)
{
    return slot == (uint32_t)UTILITIES_OPEN_KEY;
}

bool overlay_utilities_toggle(uint32_t slot)
{
    (void)slot;
    return false;    /* neither row is a switch: one is typed, the other binds a key */
}

bool overlay_utilities_commit(uint32_t slot, const char *text)
{
    float parsed;

    if (text == NULL || text[0] == '\0' || (utilities_slot_t)slot != UTILITIES_DEV_MENU_SIZE) {
        return false;
    }
    /* Refused rather than clamped when the text is not a number, because a typing mistake would
     * otherwise become the smallest panel, which shrinks the thing being typed into. */
    return dev_menu_size_row_parse(text, &parsed) && dev_menu_size_row_set(parsed);
}

bool overlay_utilities_bind(uint32_t slot, int32_t virtual_key)
{
    if (!overlay_utilities_row_is_key(slot)) {
        return false;
    }
    return open_key_row_set(virtual_key);
}
