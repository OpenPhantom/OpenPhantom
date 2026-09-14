/* overlay_controls.c: see overlay_controls.h. */
#include "overlay_controls.h"

#include "air_control_row.h"
#include "camera_follow_row.h"
#include "controller_mode_row.h"
#include "free_look_row.h"
#include "overlay_row_fill.h"
#include "sensitivity_row.h"
#include "strafe_row.h"

#include "common/text.h"

/* The fold's own text, one row per line: what each row above does, in the order they are drawn,
 * in the words the settings file uses for them. */
static const char *const CONTROLS_LINES[OVERLAY_CONTROLS_LINE_COUNT] = {
    "Controller mode: the best modern feel",
    "  on a pad, all four below switched on",
    "Free look: the mouse aims the camera",
    "  and the body turns to where you walk;",
    "  standing still, it looks around you",
    "Strafe: walk in any direction, the",
    "  full 360, with the turn keys or stick",
    "Camera follows you: swings gently in",
    "  behind you, not locked to your",
    "  heading like the original's follow",
    "Steer a jump: change direction in",
    "  the air, as the game did before",
    "Mouse speed: how far a mouse move turns"
};

static bool fold_open;   /* the "what these do" row is showing its lines */

/* The slots, in drawn order. The one-click switch first, so a reader meets it before the four
 * rows whose names give no hint that a pad wants all of them; then the two the scheme is built
 * on, then the two built on them, each directly under what it depends on; then the mouse speed,
 * the one number here, with its slider on its own line so the handle never covers it; then the
 * fold, last, so its lines are the tail of the group. */
typedef enum controls_slot {
    CONTROLS_CONTROLLER_MODE = 0,
    CONTROLS_FREE_LOOK,
    CONTROLS_STRAFE,
    CONTROLS_CAMERA_FOLLOW,
    CONTROLS_AIR_CONTROL,
    CONTROLS_SENSITIVITY,
    CONTROLS_SENSITIVITY_TRACK,
    CONTROLS_SUMMARY
} controls_slot_t;

_Static_assert((uint32_t)CONTROLS_SUMMARY == OVERLAY_CONTROLS_SUMMARY_SLOT &&
               (uint32_t)CONTROLS_SUMMARY + 1u == OVERLAY_CONTROLS_FIXED_ROWS,
               "the slot enum and OVERLAY_CONTROLS_FIXED_ROWS have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

uint32_t overlay_controls_row_count(void)
{
    return OVERLAY_CONTROLS_FIXED_ROWS + (fold_open ? OVERLAY_CONTROLS_LINE_COUNT : 0u);
}

void overlay_controls_reset(void)
{
    fold_open = false;   /* folds closed on every open, same as the groups do */
}

void overlay_controls_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    switch ((controls_slot_t)slot) {
    case CONTROLS_CONTROLLER_MODE:
        /* Reads ON only while all four below do, and holds no key of its own, so any of them can
         * still be switched off alone and this row then reads OFF, which is the truth. */
        overlay_row_label(out->label, "Enhanced controller mode (the four below)");
        out->on = controller_mode_row_get();
        return;

    case CONTROLS_FREE_LOOK:
        /* The name the game's own controls screen gave it, so a reader who has seen that screen
         * recognises the row; the same for strafe and the mouse speed. */
        overlay_row_label(out->label, "Free look");
        out->on = free_look_row_get();
        return;

    case CONTROLS_STRAFE:
        overlay_row_label(out->label, "Strafe");
        out->on = strafe_row_get();
        return;

    case CONTROLS_CAMERA_FOLLOW:
        /* Directly under strafe, because it is the only row here whose availability depends on
         * another row rather than on an engine site. Unavailable rather than hidden while
         * strafe is off: the walk never leaves the heading then, so there is nothing to follow,
         * and a reader hunting for it should find out why instead of wondering if it exists. */
        overlay_row_label(out->label, "Camera follows you (turns on free look)");
        out->on = camera_follow_row_get();
        out->available = camera_follow_row_available();
        return;

    case CONTROLS_AIR_CONTROL:
        /* Next to the camera follow because it shares its dependency: both are built on free
         * look and both switch it on. It is a key as well, but a key alone was no use to the
         * player who wanted it, since a pad on a handheld has no comfortable way to open an
         * ini. */
        overlay_row_label(out->label, "Steer a jump in the air (free look)");
        out->on = air_control_row_get();
        out->available = air_control_row_available();
        return;

    case CONTROLS_SENSITIVITY:
        out->kind = OVERLAY_ROW_VALUE;
        overlay_row_label(out->label, "Mouse speed");
        overlay_row_typed(out, editing_text, sensitivity_row_format, sensitivity_row_get());
        return;

    case CONTROLS_SENSITIVITY_TRACK:
        out->kind = OVERLAY_ROW_SLIDER;
        overlay_row_label(out->label, "");
        /* No availability test, unlike the field of view: both ends of this one are fixed, so
         * there is nothing to wait for another DLL to publish. */
        out->fraction = (sensitivity_row_get() - SENSITIVITY_MIN) /
                        (SENSITIVITY_MAX - SENSITIVITY_MIN);
        overlay_row_clamp_fraction(out);
        return;

    case CONTROLS_SUMMARY:
        /* A fold on one row, the same shape as the free camera's "how to fly": the marker is a
         * character in the label, so the drawer needs nothing new to draw it. */
        out->kind = OVERLAY_ROW_INFO;
        overlay_row_label(out->label, fold_open ? "- What these do" : "+ What these do");
        out->expanded = fold_open;
        return;

    default:
        if (fold_open && slot >= OVERLAY_CONTROLS_FIXED_ROWS &&
            slot < OVERLAY_CONTROLS_FIXED_ROWS + OVERLAY_CONTROLS_LINE_COUNT) {
            char line[OVERLAY_LABEL_MAX];

            out->kind = OVERLAY_ROW_INFO;
            text_format(line, sizeof line, "    %s",
                        CONTROLS_LINES[slot - OVERLAY_CONTROLS_FIXED_ROWS]);
            overlay_row_label(out->label, line);
            return;    /* a nested line, not a gate; never clicked either way */
        }
        /* Past the end, answered as an empty unavailable row so the caller's bug shows as a blank
         * and not as stale text. */
        overlay_row_label(out->label, "");
        out->available = false;
        return;
    }
}

bool overlay_controls_toggle(uint32_t slot)
{
    switch ((controls_slot_t)slot) {
    case CONTROLS_CONTROLLER_MODE:
        return controller_mode_row_set(!controller_mode_row_get());
    case CONTROLS_FREE_LOOK:
        return free_look_row_set(!free_look_row_get());
    case CONTROLS_STRAFE:
        return strafe_row_set(!strafe_row_get());
    case CONTROLS_CAMERA_FOLLOW:
        if (!camera_follow_row_available()) {
            return false;      /* nothing to follow without strafe; the row already says so */
        }
        return camera_follow_row_set(!camera_follow_row_get());
    case CONTROLS_AIR_CONTROL:
        if (!air_control_row_available()) {
            return false;      /* nothing to steer by without strafe; the row already says so */
        }
        return air_control_row_set(!air_control_row_get());
    case CONTROLS_SUMMARY:
        fold_open = !fold_open;
        return true;
    default:
        return false;
    }
}

bool overlay_controls_commit(uint32_t slot, const char *text)
{
    float parsed;

    if (text == NULL || text[0] == '\0' || (controls_slot_t)slot != CONTROLS_SENSITIVITY) {
        return false;
    }
    /* Refused rather than clamped when the text is not a number: a typing mistake would otherwise
     * become the slowest mouse the band allows. */
    return sensitivity_row_parse(text, &parsed) && sensitivity_row_set(parsed);
}

bool overlay_controls_slider_set(uint32_t slot, float fraction)
{
    if ((controls_slot_t)slot != CONTROLS_SENSITIVITY_TRACK) {
        return false;
    }
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    /* Not rounded to anything, unlike the picture group's sliders: the band is a tenth of a degree
     * wide and the row shows three decimals, so every position along the track is a value somebody
     * can tell apart from the one beside it. */
    return sensitivity_row_set(SENSITIVITY_MIN + fraction * (SENSITIVITY_MAX - SENSITIVITY_MIN));
}
