/* overlay_multiplayer.c: see overlay_multiplayer.h. */
#include "overlay_multiplayer.h"

#include "chat_key_row.h"
#include "overlay_row_fill.h"
#include "player_help_row.h"

#include "common/player_help_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>

_Static_assert((uint32_t)OVERLAY_MULTIPLAYER_LAST + 1u == OVERLAY_MULTIPLAYER_ROW_COUNT,
               "the slot enum and OVERLAY_MULTIPLAYER_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

/* What stands in front of the sentence on the line under the buttons. Indented like every note
 * that belongs to the rows above it. */
#define LAST_PREFIX "    Last: "

_Static_assert(sizeof(LAST_PREFIX) - 1u + PLAYER_HELP_ROW_WORDS_MAX <= OVERLAY_LABEL_MAX,
               "a sentence of the full width and its prefix have to fit one label, or the "
               "sentence is cut without a word");

uint32_t overlay_multiplayer_row_count(void)
{
    /* The line is the last slot, so the count is the slots before it and the line itself while
     * there is something to say. */
    return (uint32_t)OVERLAY_MULTIPLAYER_LAST +
           (player_help_row_last(NULL, 0u, NULL) ? 1u : 0u);
}

/* One of the two buttons. Whether it can be pressed, and the word it shows when it cannot, are
 * the reading player_help_row.c took for this picture. */
static void button_row(uint8_t kind, const char *label, overlay_row_t *out)
{
    out->kind = OVERLAY_ROW_ACTION;
    overlay_row_label(out->label, label);
    out->available = player_help_row_offered(kind, &out->reason);
}

/* What the last press came to, as a note under the buttons. It is a row and not the band above
 * the footer, because the press closed the panel and the band goes with every close: the answer
 * has to be standing here when the panel is opened again. */
static void last_row(overlay_row_t *out)
{
    char words[PLAYER_HELP_ROW_WORDS_MAX];
    char line[OVERLAY_LABEL_MAX];
    bool refused = false;

    out->kind      = OVERLAY_ROW_INFO;
    out->available = false;   /* a note, never clicked */
    if (!player_help_row_last(words, sizeof words, &refused)) {
        overlay_row_label(out->label, "");
        return;
    }
    text_format(line, sizeof line, LAST_PREFIX "%s", words);
    overlay_row_label(out->label, line);
    out->warn = refused;      /* a refusal is drawn as one, like the spawner's */
}

void overlay_multiplayer_row(uint32_t slot, bool capturing, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);

    switch ((overlay_multiplayer_slot_t)slot) {
    case OVERLAY_MULTIPLAYER_CHAT_KEY:
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

    case OVERLAY_MULTIPLAYER_REPAIR:
        button_row(PLAYER_HELP_KIND_REPAIR, "Repair lock", out);
        return;

    case OVERLAY_MULTIPLAYER_TELEPORT:
        button_row(PLAYER_HELP_KIND_TELEPORT, "Teleport to host", out);
        return;

    case OVERLAY_MULTIPLAYER_LAST:
        last_row(out);
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
    if ((overlay_multiplayer_slot_t)slot != OVERLAY_MULTIPLAYER_CHAT_KEY) {
        return false;
    }
    return chat_key_row_set(virtual_key);
}

bool overlay_multiplayer_toggle(uint32_t slot)
{
    switch ((overlay_multiplayer_slot_t)slot) {
    case OVERLAY_MULTIPLAYER_REPAIR:
        return player_help_row_press(PLAYER_HELP_KIND_REPAIR);
    case OVERLAY_MULTIPLAYER_TELEPORT:
        return player_help_row_press(PLAYER_HELP_KIND_TELEPORT);
    case OVERLAY_MULTIPLAYER_CHAT_KEY:
    case OVERLAY_MULTIPLAYER_LAST:
    default:
        return false;   /* the key, whose capture the model starts itself, and the note */
    }
}
