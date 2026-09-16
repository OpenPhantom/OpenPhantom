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
#include "overlay_layout.h"
#include "overlay_model.h"
#include "pad_input.h"

#include <windows.h>

#include <string.h>

#define SCROLL_ROWS_PER_S 12.0f    /* the right stick fully over scrolls this many rows a second */
#define TRIGGER_PER_S     0.5f     /* a trigger fully in moves a slider this far a second */
#define TRIGGER_WRITE_MS  250u     /* the drag's own write throttle; the file is the cost */
#define TRIGGER_WRITE_FULL_MS 33u
#define ROW_X_FRACTION    0.3f     /* where across a row the D-pad lands, on the label */
#define REPEAT_AFTER_S    0.4f     /* a held D-pad steps again after this */
#define REPEAT_EVERY_S    0.1f     /* and then this often */

static struct {
    float    scroll_carry;   /* rows not yet scrolled, the fraction of a row carried over */
    int32_t  trigger_row;    /* the slider the triggers are moving, -1 for none */
    float    trigger_fraction;
    float    trigger_written;
    uint32_t trigger_last_ms;
    float    dpad_held;      /* how long the D-pad has been down, up or down */
    float    dpad_repeat;    /* time to the next repeated step */
    bool     pad_owns;       /* the pad has been touched since the panel opened */
} st = { 0.0f, -1, 0.0f, 0.0f, 0u, 0.0f, 0.0f, false };

#define SIDEWAYS_NOTICE  0.05f   /* a stick this far over sideways, before any deadzone, counts */

/* Leaving a slider writes what it was left at, throttle or not. */
static void flush_trigger(void)
{
    if (st.trigger_row >= 0 && st.trigger_fraction != st.trigger_written) {
        (void)overlay_model_slider_set((uint32_t)st.trigger_row, st.trigger_fraction);
        overlay_model_rebuild();
    }
    st.trigger_row = -1;
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

/* The left stick: the pointer moved up and down by a fraction of the screen's width a second,
 * and never sideways. Sideways it walked the pointer off the label the D-pad had put it on,
 * and nothing in the panel wants it there: the tabs are the D-pad's, the sliders are the
 * triggers' (played 2026-09-16). The stick's sideways half is the free camera's strafe, which
 * reads the frame itself. */
static void glide_pointer(float down)
{
    float x;
    float y;
    float screen_w = 0.0f;
    float screen_h = 0.0f;

    if (!overlay_draw_screen(&screen_w, &screen_h)) {
        return;
    }
    overlay_input_pointer(&x, &y);
    place_pointer(x, y + down * screen_w);
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
 * frame; the file is written at the drag's own rate and once more when the triggers let go. */
static void trigger_slider(float amount, float dt)
{
    overlay_row_t row;
    float         x;
    float         y;
    int32_t       index;
    uint32_t      now;
    uint32_t      every;

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
    if (index != st.trigger_row) {
        flush_trigger();
        st.trigger_row = index;
        if (index >= 0) {
            st.trigger_fraction = row.fraction;
            st.trigger_written  = row.fraction;
        }
    }
    if (index < 0 || amount == 0.0f) {
        return;
    }
    st.trigger_fraction += amount * TRIGGER_PER_S * dt;
    if (st.trigger_fraction < 0.0f) { st.trigger_fraction = 0.0f; }
    if (st.trigger_fraction > 1.0f) { st.trigger_fraction = 1.0f; }
    now   = (uint32_t)GetTickCount();
    every = overlay_model_slider_wants_full_rate((uint32_t)index) ? TRIGGER_WRITE_FULL_MS
                                                                   : TRIGGER_WRITE_MS;
    if (now - st.trigger_last_ms < every) {
        return;
    }
    st.trigger_last_ms = now;
    if (overlay_model_slider_set((uint32_t)index, st.trigger_fraction)) {
        st.trigger_written = st.trigger_fraction;
        overlay_model_rebuild();
    }
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

/* The pointer's x on the label column, every frame, once the pad has been touched while the
 * panel is open. The sideways half of the right stick reaches the system cursor as mouse
 * motion, faked by controller_input for the game's camera, on that DLL's own thread and at its
 * own rate, and the cursor is this panel's pointer; nothing here can stop that DLL. Putting the
 * pointer back where it was let every bump creep, and clamping only while a stick read as over
 * still let a wiggle through, since the counts land between frames and after the stick reads
 * centred (played 2026-09-16, four times). So the first press or push of the pad hands the
 * panel's sideways to the pad for as long as it stays open: the pointer sits on the column the
 * D-pad uses, whatever the cursor did, and the mouse is free again the next time the panel
 * opens without the pad being touched. */
static void clamp_pointer_x(const pad_state_t *pad)
{
    const layout_t *lay = overlay_layout();
    float           x;
    float           y;
    float           column;

    if (pad->raw_x > SIDEWAYS_NOTICE || pad->left_y != 0.0f || pad->right_y != 0.0f ||
        pad->pressed != 0u || pad->trigger_left > 0.0f || pad->trigger_right > 0.0f) {
        st.pad_owns = true;
    }
    if (!st.pad_owns) {
        return;
    }
    overlay_input_pointer(&x, &y);
    column = lay->left + lay->width * ROW_X_FRACTION;
    if (x != column) {
        place_pointer(column, y);
    }
}

/* The panel from the pad, one frame. */
static void drive_panel(float dt)
{
    const pad_state_t *pad = pad_input_state();
    const layout_t    *lay = overlay_layout();
    float              speed = pad_input_pointer_speed();

    clamp_pointer_x(pad);
    if (pad->left_y != 0.0f) {
        glide_pointer(-pad->left_y * speed * dt);
    }
    step_rows(pad, dt);
    if (pad->pressed & (PAD_BUTTON_DPAD_LEFT | PAD_BUTTON_DPAD_RIGHT)) {
        /* The other tab; there are two, so left and right are the same step. */
        overlay_model_set_tab((overlay_model_tab() == OVERLAY_TAB_ORIGINAL)
                                  ? OVERLAY_TAB_OPENPHANTOM : OVERLAY_TAB_ORIGINAL);
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
        st.pad_owns = false;
    }
}
