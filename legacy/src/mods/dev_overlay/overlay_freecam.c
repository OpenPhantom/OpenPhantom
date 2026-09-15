/* overlay_freecam.c: see overlay_freecam.h. */
#include "overlay_freecam.h"

#include "cheats_openphantom.h"
#include "freecam_world.h"
#include "freeze_anim_row.h"
#include "overlay_key_name.h"
#include "overlay_row_fill.h"

#include "common/text.h"

/* The fold's own text, one row per line. The count is in the header with the slots; only the
 * words are here. */
static const char *const FREECAM_INFO_LINES[OVERLAY_FREECAM_LINE_COUNT] = {
    "Needs a teleport key set first",
    "WASD to move",
    "Mouse to look",
    "E / Q for up and down",
    "Scroll wheel changes speed",
    "Your Cheatmenu open key or Escape",
    "  hides the panel and shows it again",
    "Your teleport key ends the flight",
    "  and brings the player here",
    "F4 ends the flight and leaves",
    "  the player where they were"
};

static struct {
    bool fold_open;   /* the "how to fly" row is showing its lines */
    bool was_on;      /* last-seen CHEATS_OWN_FREECAM state, to catch the edge */
} freecam;

uint32_t overlay_freecam_row_count(void)
{
    return OVERLAY_FREECAM_LINE_FIRST + (freecam.fold_open ? OVERLAY_FREECAM_LINE_COUNT : 0u);
}

void overlay_freecam_reset(void)
{
    freecam.fold_open = false;   /* folds closed on every open, same as the groups do */
    freecam.was_on = false;      /* re-synced against the real state on the very next rebuild */
}

void overlay_freecam_sync(void)
{
    bool on = cheats_openphantom_is_on(CHEATS_OWN_FREECAM);

    if (on != freecam.was_on) {
        /* The mouse is fully claimed for as long as free camera flies, so this is the only way
         * the fold could open at all without a click reaching it, and forcing it shut again the
         * instant free camera turns off is what keeps an old reading list from lingering once
         * there is nothing left it is explaining. It fires on the flip only, so a manual click
         * in between still wins. */
        freecam.fold_open = on;
        freecam.was_on = on;
    }
}

void overlay_freecam_row(uint32_t slot, bool capturing, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    switch (slot) {
    case OVERLAY_FREECAM_HOTKEY_SLOT:
        out->kind = OVERLAY_ROW_HOTKEY;
        overlay_row_label(out->label, "Free camera teleport key");
        out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
        /* Always populated, never left for the drawer's own ACTION/CHEAT fallback word to
         * guess at; "RUN" and "OFF" are both wrong for a key binding. */
        if (capturing) {
            text_format(out->value, sizeof out->value, "...");
        } else {
            int32_t vk = cheats_openphantom_freecam_hotkey();

            if (vk != 0) {
                overlay_key_name(vk, out->value, sizeof out->value);
            } else {
                text_format(out->value, sizeof out->value, "Set");
            }
        }
        out->value[sizeof out->value - 1] = '\0';
        return;

    case OVERLAY_FREECAM_TOGGLE_SLOT:
        overlay_row_label(out->label, cheats_openphantom_name(CHEATS_OWN_FREECAM));
        out->on = cheats_openphantom_is_on(CHEATS_OWN_FREECAM);
        out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
        /* Free camera also needs a teleport key bound before it can be switched ON, and
         * cheats_openphantom_toggle() enforces this too, so this is display honesty rather
         * than the only gate: a row that looked clickable but silently refused every click
         * would be worse than one that shows why. Once it IS on, availability no longer
         * depends on this: the row is unreachable anyway with the mouse claimed, and the
         * hotkey is how it actually turns back off. */
        if (!out->on && cheats_openphantom_freecam_hotkey() == 0) {
            out->available = false;
        }
        return;

    case OVERLAY_FREECAM_FREEZE_SLOT:
        /* The pause's own look: on, the animations hold with it; off, as shipped, they run on
         * the spot as they did. A setting, so never gated on the camera being on. */
        overlay_row_label(out->label, "Animations freeze while paused");
        out->on = freeze_anim_row_get();
        out->available = freeze_anim_row_available();
        return;

    case OVERLAY_FREECAM_WORLD_SLOT:
        /* A setting, not a cheat: it says what the next flight does, so it is never gated on
         * the camera being on, and it writes the settings file like the Utilities rows do. */
        overlay_row_label(out->label, "World runs while flying");
        out->on = freecam_world_runs();
        out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
        return;

    case OVERLAY_FREECAM_SUMMARY_SLOT:
        /* A fold on one row, the same shape as a group's own expand/collapse but scoped to the
         * lines under it. The marker is a character in the label, so the drawer needs nothing
         * new to draw it. */
        out->kind = OVERLAY_ROW_INFO;
        overlay_row_label(out->label, freecam.fold_open ? "- How free camera flies"
                                                        : "+ How free camera flies");
        out->expanded = freecam.fold_open;
        return;

    default:
        if (freecam.fold_open && slot >= OVERLAY_FREECAM_LINE_FIRST &&
            slot < OVERLAY_FREECAM_LINE_FIRST + OVERLAY_FREECAM_LINE_COUNT) {
            char line[OVERLAY_LABEL_MAX];

            out->kind = OVERLAY_ROW_INFO;
            text_format(line, sizeof line, "    %s",
                        FREECAM_INFO_LINES[slot - OVERLAY_FREECAM_LINE_FIRST]);
            overlay_row_label(out->label, line);
            return;    /* a nested line, not a gate; never clicked either way */
        }
        overlay_row_label(out->label, "");
        out->available = false;
        return;
    }
}

bool overlay_freecam_row_is_key(uint32_t slot)
{
    return slot == OVERLAY_FREECAM_HOTKEY_SLOT;
}

bool overlay_freecam_toggle(uint32_t slot)
{
    switch (slot) {
    case OVERLAY_FREECAM_TOGGLE_SLOT:
        return cheats_openphantom_toggle(CHEATS_OWN_FREECAM);
    case OVERLAY_FREECAM_FREEZE_SLOT:
        /* One or the other: a world that runs under the camera is not paused, so there is
         * nothing for the freeze to hold, and a row reading ON with no effect is worse than a
         * row that goes off. Switching either on switches the other off; off leaves both off. */
        if (!freeze_anim_row_get()) {
            freecam_world_set_runs(false);
        }
        return freeze_anim_row_set(!freeze_anim_row_get());
    case OVERLAY_FREECAM_WORLD_SLOT:
        if (!freecam_world_runs()) {
            (void)freeze_anim_row_set(false);
        }
        freecam_world_set_runs(!freecam_world_runs());
        return true;
    case OVERLAY_FREECAM_SUMMARY_SLOT:
        freecam.fold_open = !freecam.fold_open;
        return true;
    default:
        return false;    /* the key row is a capture the model starts itself; a line is a note */
    }
}
