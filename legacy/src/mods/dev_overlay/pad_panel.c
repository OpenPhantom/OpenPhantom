/* pad_panel.c: see pad_panel.h.
 *
 * The pointer is the system cursor, as the mouse's is (overlay_input.c reads it back every
 * frame), so the stick moves the cursor and everything that follows the cursor follows the
 * stick: the hover, the click, the drag. Nothing in the panel's model knows a pad exists. The
 * D-pad is the exception in feel, not in kind: it puts the cursor on the next row's centre, so a
 * press is a step from row to row with the hover as the mark, as a pad expects of a list
 * (played 2026-09-16: the first cut nudged the pointer by a row's height and felt like a slow
 * mouse).
 */
#include "pad_panel.h"

#include "cheats_openphantom.h"
#include "overlay_draw.h"
#include "overlay_input.h"
#include "overlay_keys.h"
#include "overlay_notice.h"
#include "overlay_layout.h"
#include "overlay_model.h"
#include "overlay_slider.h"
#include "pad_input.h"

#include <windows.h>

#include <string.h>

#define SCROLL_ROWS_PER_S 12.0f    /* the right stick fully over scrolls this many rows a second */
#define TRIGGER_PER_S     0.5f     /* a trigger fully in moves a slider this far a second */
#define ROW_X_FRACTION    0.3f     /* where across a row the D-pad lands, on the label */
#define REPEAT_AFTER_S    0.4f     /* a held D-pad steps again after this */
#define REPEAT_EVERY_S    0.1f     /* and then this often */
#define STICK_NOTICE      0.05f    /* the right stick this far over, before any deadzone, counts */
#define HIDE_HOLD_S       0.3f     /* the pointer stays hidden this long after the stick centres */

static struct {
    float    scroll_carry;   /* rows not yet scrolled, the fraction of a row carried over */
    float    dpad_held;      /* how long the D-pad has been down, up or down */
    float    dpad_repeat;    /* time to the next repeated step */
    float    hidden_for;     /* seconds left of hiding the pointer */
} st = { 0.0f, 0.0f, 0.0f, 0.0f };

/* Which track the triggers are on, where the hand has it and what the file last got is
 * overlay_slider.c's, together with the throttle: the mouse's drag kept the same four things
 * beside the same two interval numbers, and two copies of one state are two answers. */

/* The pointer hidden while the right stick is over, and for a moment after: its sideways half
 * reaches the cursor as mouse motion faked by controller_input, so the pointer wanders while
 * the stick scrolls, and a wandering pointer with a hover under it reads as a choice being
 * made (Chip's own idea, 2026-09-17, after holding the stick still and pinning the pointer had
 * both cost the mouse more than they gave the pad). A D-pad step shows it again at once, on
 * the row it lands on; the mouse shows it again the moment the stick has been centred for the
 * hold. */
static void hide_pointer_under_stick(const pad_state_t *pad, float dt)
{
    if (pad->raw_right_x > STICK_NOTICE || pad->right_y != 0.0f) {
        st.hidden_for = HIDE_HOLD_S;
    } else if (st.hidden_for > 0.0f) {
        st.hidden_for -= dt;
    }
    overlay_input_set_pointer_hidden(st.hidden_for > 0.0f);
}

/* Leaving a slider writes what it was left at, throttle or not. */
static void flush_trigger(void)
{
    overlay_slider_let_go(OVERLAY_SLIDER_TRIGGER);
}

/* The system cursor put at a point of the picture, in the panel's own units, and kept inside
 * the game window's client area, since the pointer is clamped to the picture on the way back
 * in and a cursor left outside would stick at an edge. The pointer is re-read at once so a
 * press on this frame lands where the cursor now is. */
static void place_pointer(float x, float y)
{
    HWND  window = (HWND)overlay_input_window();
    RECT  client;
    POINT origin;
    POINT cursor;
    float screen_w = 0.0f;
    float screen_h = 0.0f;

    if (window == NULL || !GetClientRect(window, &client) ||
        !overlay_draw_screen(&screen_w, &screen_h) || screen_w <= 0.0f || screen_h <= 0.0f) {
        return;
    }
    origin.x = client.left;
    origin.y = client.top;
    if (!ClientToScreen(window, &origin)) {
        return;
    }
    cursor.x = origin.x + (LONG)((x / screen_w) * (float)(client.right - client.left));
    cursor.y = origin.y + (LONG)((y / screen_h) * (float)(client.bottom - client.top));
    if (cursor.x < origin.x) { cursor.x = origin.x; }
    if (cursor.y < origin.y) { cursor.y = origin.y; }
    if (cursor.x > origin.x + client.right - client.left - 1) {
        cursor.x = origin.x + client.right - client.left - 1;
    }
    if (cursor.y > origin.y + client.bottom - client.top - 1) {
        cursor.y = origin.y + client.bottom - client.top - 1;
    }
    SetCursorPos(cursor.x, cursor.y);
    overlay_input_update_pointer();
}

/* The left stick: the pointer moved by fractions of the screen's width a second, as fast up
 * as across, a mouse on a stick. Holding it still sideways, and then pinning it to the label
 * column once the pad was touched, were both tried against the sideways drift the right stick
 * puts into the cursor through controller_input; each cost the mouse more than it gave the
 * pad, since a pinned pointer is a mouse that cannot move (played 2026-09-16 and 2026-09-17).
 * The D-pad steps are the pad's own way to a row, and land on the column regardless. */
static void glide_pointer(float across, float down)
{
    float x;
    float y;
    float screen_w = 0.0f;
    float screen_h = 0.0f;

    if (!overlay_draw_screen(&screen_w, &screen_h)) {
        return;
    }
    overlay_input_pointer(&x, &y);
    place_pointer(x + across * screen_w, y + down * screen_w);
}

/* The D-pad, up or down: the cursor onto the centre of the row above or below the one it is on,
 * scrolling the list by one when that row is off the screen, and onto the first row on screen
 * when it is on none. Across, it lands on the label column, where a press activates the row. */
static void step_row(int32_t by)
{
    const layout_t *lay = overlay_layout();
    float           x;
    float           y;
    int32_t         count = (int32_t)overlay_model_row_count();
    int32_t         first;
    int32_t         target;

    if (lay->visible_rows == 0u || count == 0) {
        return;
    }
    overlay_input_pointer(&x, &y);
    first  = (int32_t)overlay_model_scroll(lay->visible_rows);
    target = overlay_draw_row_at(x, y);
    target = (target < 0) ? first : target + by;
    if (target < 0) {
        target = 0;
    }
    if (target >= count) {
        target = count - 1;
    }
    if (target < first || target >= first + (int32_t)lay->visible_rows) {
        overlay_model_scroll_by(by);
        first = (int32_t)overlay_model_scroll(lay->visible_rows);
    }
    x = lay->left + lay->width * ROW_X_FRACTION;
    y = lay->top + lay->rows_top + ((float)(target - first) + 0.5f) * lay->row_h;
    place_pointer(x, y);
}

/* The triggers: the slider on the row under the pointer, or the one under a value row, moved
 * by how far the triggers are in, right raising and left lowering. The handle follows every
 * frame; the file is written at the drag's own rate and once more when the triggers let go.
 * They take the track only while they are in, and give it back the moment they come out or
 * the pointer leaves the row. */
static void trigger_slider(float amount, float dt)
{
    overlay_row_t row;
    float         x;
    float         y;
    float         fraction = 0.0f;
    int32_t       index;

    memset(&row, 0, sizeof row);
    overlay_input_pointer(&x, &y);
    index = overlay_draw_row_at(x, y);
    if (index >= 0 && overlay_model_row((uint32_t)index, &row) &&
        row.kind != OVERLAY_ROW_SLIDER && (uint32_t)index + 1u < overlay_model_row_count() &&
        overlay_model_row((uint32_t)index + 1u, &row) && row.kind == OVERLAY_ROW_SLIDER) {
        index += 1;
    }
    if (index < 0 || !overlay_model_row((uint32_t)index, &row) ||
        row.kind != OVERLAY_ROW_SLIDER) {
        index = -1;
    }
    /* Only while they are pushed in. This took the track under the pointer every frame, pushed
     * in or not, and this function runs every frame a pad is plugged in. Every track the pointer
     * crossed was therefore held by a hand that was not touching it, and the panel's own pointer,
     * its sideways keys and its Default button were each refused the track they were pointing at:
     * no slider in the panel could be moved by anything at all (field, 2026-09-24).
     *
     * A hand commands only the hold it has: a track the pointer is dragging is not taken from it
     * here, and letting go asks for the triggers' own hold and not for whatever is held. Without
     * that, a mouse drag whose hand wandered off its row would be ended by the triggers noticing
     * a different row under the pointer. */
    if (amount == 0.0f || index != overlay_slider_row(OVERLAY_SLIDER_TRIGGER)) {
        flush_trigger();
    }
    if (amount == 0.0f || index < 0) {
        return;
    }
    if (overlay_slider_row(OVERLAY_SLIDER_TRIGGER) != index) {
        overlay_slider_take(OVERLAY_SLIDER_TRIGGER, index, row.fraction);
        if (overlay_slider_row(OVERLAY_SLIDER_TRIGGER) != index) {
            return;   /* another hand is driving that track */
        }
    }
    if (!overlay_slider_held(NULL, &fraction)) {
        return;
    }
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, fraction + amount * TRIGGER_PER_S * dt,
                        (uint32_t)GetTickCount());
}

/* The D-pad up or down, on the press and then again while held: after REPEAT_AFTER_S, every
 * REPEAT_EVERY_S, so a long list is walked by holding it. */
static void step_rows(const pad_state_t *pad, float dt)
{
    const uint32_t down = pad->held & (PAD_BUTTON_DPAD_UP | PAD_BUTTON_DPAD_DOWN);
    int32_t        by = (down & PAD_BUTTON_DPAD_UP) ? -1 : 1;

    if (down == 0) {
        st.dpad_held   = 0.0f;
        st.dpad_repeat = 0.0f;
        return;
    }
    if (pad->pressed & down) {
        step_row(by);
        st.hidden_for  = 0.0f;   /* the step puts the pointer where it belongs: shown again */
        overlay_input_set_pointer_hidden(false);
        st.dpad_held   = 0.0f;
        st.dpad_repeat = REPEAT_AFTER_S;
        return;
    }
    st.dpad_held   += dt;
    st.dpad_repeat -= dt;
    if (st.dpad_repeat <= 0.0f) {
        step_row(by);
        st.dpad_repeat = REPEAT_EVERY_S;
    }
}

/* The panel from the pad, one frame. */
static void drive_panel(float dt)
{
    const pad_state_t *pad = pad_input_state();
    const layout_t    *lay = overlay_layout();
    float              speed = pad_input_pointer_speed();

    hide_pointer_under_stick(pad, dt);
    if (pad->left_x != 0.0f || pad->left_y != 0.0f) {
        glide_pointer(pad->left_x * speed * dt, -pad->left_y * speed * dt);
    }
    step_rows(pad, dt);
    if (pad->pressed & (PAD_BUTTON_DPAD_LEFT | PAD_BUTTON_DPAD_RIGHT)) {
        /* The same rule the keyboard's sideways keys follow, asked of the row under the
         * pointer, which is how this file addresses a row everywhere else: a heading folds,
         * a row of words walks them, a number changes by one press, and anything else is
         * the other tab. It used to be the tab unconditionally, which would have left the
         * pad with no way to fold a heading and the two halves of one rule drifting apart
         * in two files.
         *
         * Always the fine step: a pad has no modifier to hold, and the triggers are its own
         * way to cross a track quickly. There are two tabs, so left and right are the same
         * step between them. */
        float x;
        float y;

        overlay_input_pointer(&x, &y);
        overlay_notice_act();      /* the pad's own door; see overlay_notice.h */
        if (!overlay_keys_sideways(overlay_draw_row_at(x, y),
                                   (pad->pressed & PAD_BUTTON_DPAD_LEFT) ? -1 : 1, false)) {
            overlay_model_set_tab((overlay_model_tab() == OVERLAY_TAB_ORIGINAL)
                                      ? OVERLAY_TAB_OPENPHANTOM : OVERLAY_TAB_ORIGINAL);
        }
        overlay_model_rebuild();
    }
    if (pad->pressed & PAD_BUTTON_A) {
        overlay_input_pad_press(true);
    } else if (!(pad->held & PAD_BUTTON_A)) {
        overlay_input_pad_press(false);
    }
    if (pad->pressed & PAD_BUTTON_B) {
        overlay_input_pad_escape();
    }
    trigger_slider(pad->trigger_right - pad->trigger_left, dt);
    /* The right stick scrolls as the wheel does, up the list with the stick up; the fraction of
     * a row left over is carried to the next frame so a gentle push still moves. */
    st.scroll_carry += -pad->right_y * SCROLL_ROWS_PER_S * dt;
    if (st.scroll_carry >= 1.0f || st.scroll_carry <= -1.0f) {
        int32_t rows = (int32_t)st.scroll_carry;

        st.scroll_carry -= (float)rows;
        overlay_model_scroll_by(rows);
    }
    if (pad->pressed & (PAD_BUTTON_LEFT_SHOULDER | PAD_BUTTON_RIGHT_SHOULDER)) {
        const uint32_t visible = lay->visible_rows;
        const int32_t  page = (visible > 1u) ? (int32_t)(visible - 1u) : 1;

        overlay_model_scroll_by((pad->pressed & PAD_BUTTON_LEFT_SHOULDER) ? -page : page);
    }
}

void pad_panel_tick(void)
{
    const pad_state_t *pad;
    float              dt = pad_input_poll();

    pad = pad_input_state();
    if (!pad->present) {
        overlay_input_pad_press(false);   /* a pad that went while A was down lets go */
        return;
    }
    /* The opening buttons: the frame the last of them goes down with the rest already held,
     * so a chord fires once and a single button fires on its press. A hold was tried first and
     * taken out: the game's own joystick reading has the button for the length of the hold. */
    {
        const uint32_t open = pad_input_open_buttons();

        if (open != 0 && (pad->pressed & open) != 0 && (pad->held & open) == open) {
            overlay_input_pad_toggle_open();
        }
    }
    /* Flying, the camera reads the frame's state itself from its own hook; the panel is either
     * hidden or under a look that owns the cursor, so it takes nothing. */
    if (cheats_openphantom_is_on(CHEATS_OWN_FREECAM)) {
        overlay_input_pad_press(false);
        flush_trigger();
        return;
    }
    if (overlay_input_is_open()) {
        drive_panel(dt);
    } else {
        overlay_input_pad_press(false);
        flush_trigger();
        st.hidden_for = 0.0f;
        overlay_input_set_pointer_hidden(false);
    }
}
