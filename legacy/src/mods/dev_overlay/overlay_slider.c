/* overlay_slider.c: see the header. */
#include "overlay_slider.h"

#include "overlay_model.h"
#include "overlay_notice.h"
#include "overlay_number.h"

/* The two write intervals, in one place. They were the same pair of numbers in overlay_input.c and
 * in pad_panel.c, each with its own name, and the header says what they are for. */
#define WRITE_MS       250u
#define WRITE_FULL_MS   33u

static struct {
    int32_t               row;        /* the track held, -1 for none */
    overlay_slider_hand_t hand;
    bool                  driving;    /* whether that hand is MOVING it or only resting on it */
    float                 fraction;   /* where the hand has it, every frame */
    float                 written;    /* the last fraction that reached the file */
    uint32_t              every_ms;   /* this track's own write interval */
    uint32_t              last_ms;
} held = { -1, OVERLAY_SLIDER_POINTER, false, 0.0f, 0.0f, 0u, 0u };

static float on_the_track(float fraction)
{
    if (fraction < 0.0f) {
        return 0.0f;
    }
    if (fraction > 1.0f) {
        return 1.0f;
    }
    return fraction;
}

static bool this_hand_holds_it(overlay_slider_hand_t hand)
{
    return held.row >= 0 && held.hand == hand;
}

/* The one write. A rebuild only when the write landed: a refused write changed nothing, so there
 * is nothing to build again.
 *
 * And the one place a refused write is SAID. Every hand on every track in this panel comes through
 * here: the drag, the click, the release, the pad's trigger, its D-pad, the sideways keys and
 * Default. All seven threw the answer away with a (void). A write that did not land leaves
 * the handle where it was, which is exactly the picture a write that landed on the value the row
 * already had leaves, so there was no telling a read-only settings file from having changed
 * nothing. Said here and not at the seven callers for the reason the write itself is here: seven
 * copies of one sentence are seven chances for six of them to be missing. */
static bool into_the_file(float fraction)
{
    if (!overlay_model_slider_set((uint32_t)held.row, fraction)) {
        overlay_notice_say("That setting could not be saved to the settings file");
        return false;
    }
    held.written = fraction;
    overlay_model_rebuild();
    return true;
}

void overlay_slider_take(overlay_slider_hand_t hand, int32_t index, float fraction)
{
    if (index < 0) {
        overlay_slider_let_go(hand);   /* a hand on no track holds none */
        return;
    }
    /* Driving, not merely holding: see the header. A hand that has only noticed a track gives
     * it up to one that wants to change it. */
    if (held.row >= 0 && held.hand != hand && held.driving) {
        return;
    }
    held.row      = index;
    held.hand     = hand;
    held.driving  = false;
    held.fraction = on_the_track(fraction);
    held.written  = held.fraction;
    /* Asked once, here, rather than every frame: the track cannot change while it is held. */
    held.every_ms = overlay_model_slider_wants_full_rate((uint32_t)index) ? WRITE_FULL_MS
                                                                         : WRITE_MS;
}

void overlay_slider_grab(overlay_slider_hand_t hand, int32_t index, float fraction, uint32_t now)
{
    overlay_slider_take(hand, index, fraction);
    if (!this_hand_holds_it(hand) || held.row != index) {
        return;
    }
    held.driving = true;   /* a click is a change, so this hand is on it from the first frame */
    held.last_ms = now;
    into_the_file(held.fraction);
}

void overlay_slider_move(overlay_slider_hand_t hand, float fraction, uint32_t now)
{
    if (!this_hand_holds_it(hand)) {
        return;
    }
    held.driving = true;   /* it is moving the track now, whatever it was doing before */
    /* Recorded before the throttle and read by the drawing, so the HANDLE follows the hand at the
     * full frame rate while the WRITE is throttled. Drawing the handle from the value read back
     * out of the file was the first version, and it moved in thirty steps a second against a
     * pointer moving in sixty, which a slider must not do. */
    held.fraction = on_the_track(fraction);
    if (now - held.last_ms < held.every_ms) {
        return;
    }
    /* Stamped whether or not the write lands, so a refused write costs one interval and not one
     * frame. */
    held.last_ms = now;
    into_the_file(held.fraction);
}

void overlay_slider_let_go(overlay_slider_hand_t hand)
{
    if (!this_hand_holds_it(hand)) {
        return;
    }
    if (held.fraction != held.written) {
        into_the_file(held.fraction);
    }
    held.row = -1;
}

int32_t overlay_slider_row(overlay_slider_hand_t hand)
{
    return this_hand_holds_it(hand) ? held.row : -1;
}

bool overlay_slider_held(int32_t *index, float *fraction)
{
    if (held.row < 0) {
        return false;
    }
    if (index != NULL) {
        *index = held.row;
    }
    if (fraction != NULL) {
        *fraction = held.fraction;
    }
    return true;
}

void overlay_slider_number_on(uint32_t index, overlay_row_t *on_it)
{
    int32_t  at = -1;
    float    fraction = 0.0f;
    uint32_t track;

    if (on_it == NULL || !overlay_slider_held(&at, &fraction)) {
        return;
    }
    /* A track sits on the line under the value it drives, so a value row asks about the row
     * below it and a track row about itself. */
    if (on_it->kind == OVERLAY_ROW_VALUE) {
        track = index + 1u;
    } else if (on_it->kind == OVERLAY_ROW_SLIDER) {
        track = index;
    } else {
        return;
    }
    if (at != (int32_t)track) {
        return;
    }
    (void)overlay_model_slider_value(track, fraction, on_it->value, sizeof on_it->value);
}

/* ============================================================================================
 * One discrete change: a press of a sideways key, or the Default button. See the header for why
 * neither holds the track between calls and why neither is throttled.
 * ========================================================================================== */

/* Where the handle on `index` is right now: from the hand while one is on that track, and from
 * the row otherwise. One fraction, so a press during a drag steps from where the hand has the
 * handle and not from what the file last got, which is up to a quarter of a second behind it. */
static bool the_handle_on(int32_t index, float *fraction)
{
    overlay_row_t on_it;
    int32_t       at = -1;

    if (overlay_slider_held(&at, fraction) && at == index) {
        return true;
    }
    if (!overlay_model_row((uint32_t)index, &on_it)) {
        return false;
    }
    *fraction = on_it.fraction;
    return true;
}

static bool one_change(int32_t index, float fraction)
{
    bool borrowed = false;
    bool landed;

    if (index < 0) {
        return false;
    }
    if (held.row != index) {
        if (held.row >= 0) {
            if (held.driving) {
                return false;   /* another track is being driven; this is not the one to move */
            }
            /* A hand resting on another track is not a drag to protect, and leaving it there
             * would refuse every press on every other row for as long as it rested. */
            overlay_slider_let_go(held.hand);
        }
        overlay_slider_take(OVERLAY_SLIDER_KEYS, index, fraction);
        if (held.row != index) {
            return false;
        }
        borrowed = true;
    }
    /* A track a hand already has is moved where it stands rather than taken from it: "the value
     * is now this" and "the handle is now there" are one sentence, and the hand that has it goes
     * on from the new place. */
    held.fraction = on_the_track(fraction);
    landed = into_the_file(held.fraction);
    if (borrowed) {
        held.row = -1;
    }
    return landed;
}

bool overlay_slider_nudge(int32_t index, int32_t by, bool coarse)
{
    overlay_number_t limits;
    float            fraction;
    float            to;

    if (index < 0 || !overlay_model_slider_limits((uint32_t)index, &limits) ||
        !the_handle_on(index, &fraction) ||
        !overlay_number_stepped(&limits, fraction, by, coarse, &to)) {
        return false;
    }
    return one_change(index, to);
}

bool overlay_slider_to_standard(int32_t index)
{
    overlay_number_t limits;
    float            fraction;

    if (index < 0 || !overlay_model_slider_limits((uint32_t)index, &limits) ||
        !overlay_number_standard(&limits, &fraction)) {
        return false;
    }
    return one_change(index, fraction);
}
