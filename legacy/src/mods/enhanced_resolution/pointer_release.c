#include "pointer_release.h"

#include "common/frame_hook.h"
#include "common/import_patch.h"
#include "common/logging.h"

#include <windows.h>

typedef HCURSOR(WINAPI *set_cursor_fn)(HCURSOR);

static struct {
    int32_t       key;
    bool          armed;
    bool          released;
    bool          key_was_down;
    set_cursor_fn original_set_cursor;
    HCURSOR       arrow;
} release_state;

/* Which cursor the frame under the pointer calls for, worked out from the window's own rectangles
 * rather than from a hit test.
 *
 * Asking Windows would mean sending WM_NCHITTEST, and this runs inside the window procedure's own
 * answer to WM_SETCURSOR, so that would be re-entrant. The geometry is not hard: anything outside
 * the client area but inside the window is frame, and which edge it is decides the cursor. The
 * corners are given a wider reach than the edges, exactly as Windows does, because a corner that is
 * only as thick as the border is nearly impossible to hit.
 *
 * Returns NULL when the pointer is over the picture, where the plain arrow is right. */
static HCURSOR frame_cursor(void)
{
    RECT  window_rect;
    RECT  client_rect;
    POINT client_origin = { 0, 0 };
    POINT at;
    /* The thread's own active window, which inside the engine's answer to WM_SETCURSOR is the
     * window the message is about. cursor_anchor.c reaches for the same thing for the same
     * reason. Asking window_fit instead would tie this file to that one, and with it two unit
     * tests that have no business linking the window plumbing. */
    HWND  window = GetActiveWindow();
    bool  left, right, above, below;
    long  corner;

    if (window == NULL || !GetCursorPos(&at) ||
        !GetWindowRect(window, &window_rect) || !GetClientRect(window, &client_rect) ||
        !ClientToScreen(window, &client_origin)) {
        return NULL;
    }

    OffsetRect(&client_rect, client_origin.x, client_origin.y);
    if (PtInRect(&client_rect, at) || !PtInRect(&window_rect, at)) {
        return NULL;                              /* over the picture, or not over this window */
    }

    /* The reach of a corner, in the same spirit as SM_CXSIZEFRAME but larger, so the diagonal grips
     * can actually be hit. Taken from the frame's own thickness so it follows the system metrics
     * and the display's scaling rather than being a number picked here. */
    corner = (client_rect.left - window_rect.left) * 2 + 8;

    left  = at.x < client_rect.left + corner;
    right = at.x > client_rect.right - corner;
    above = at.y < client_rect.top;
    below = at.y > client_rect.bottom - corner;

    if (above && at.y >= window_rect.top + (client_rect.left - window_rect.left)) {
        above = false;                            /* the caption, not the top border */
    }
    if ((above && left) || (below && right)) {
        return LoadCursorA(NULL, IDC_SIZENWSE);
    }
    if ((above && right) || (below && left)) {
        return LoadCursorA(NULL, IDC_SIZENESW);
    }
    if (above || below) {
        return LoadCursorA(NULL, IDC_SIZENS);
    }
    if (at.x < client_rect.left || at.x > client_rect.right) {
        return LoadCursorA(NULL, IDC_SIZEWE);
    }
    return NULL;                                  /* the caption bar */
}

/* The engine answers WM_SETCURSOR by calling SetCursor(NULL) and returning 1, which tells Windows
 * that the cursor has been dealt with and to leave it alone. It does that for the WHOLE window and
 * not only for the picture, so a released pointer is invisible over the title bar too, which is
 * exactly where it has to be seen to be used.
 *
 * Rather than argue with the message, the argument is changed: while the pointer is released a
 * NULL becomes the ordinary arrow, and the engine's own "I handled it" then makes that arrow
 * stick. Everything else about the call is left alone, including the return value, so a build that
 * one day passes a real cursor here keeps whatever it passed. When the pointer is given back, the
 * next WM_SETCURSOR hides it again with no help from us. */
static HCURSOR WINAPI substituted_set_cursor(HCURSOR cursor)
{
    if (cursor == NULL && release_state.released && release_state.arrow != NULL) {
        HCURSOR frame = frame_cursor();

        cursor = (frame != NULL) ? frame : release_state.arrow;
    }
    return release_state.original_set_cursor(cursor);
}

bool pointer_release_is_active(void)
{
    return release_state.armed && release_state.released;
}

/* Polled once a frame rather than hooked into the window procedure, because the pointer has to be
 * recoverable from the state where the game is not receiving mouse messages at all, and a key
 * pressed while the window is merely active is the one input that still arrives there. */
static void poll_release_key(void)
{
    bool down;

    if (!release_state.armed) {
        return;
    }

    /* Checked every frame and not only on the press, because the engine takes the capture back
     * whenever it handles an activation, and coming back from minimised is an activation. Dropping
     * it once at the press was enough to move the window until the first time it was minimised, and
     * never again after that: the capture was back and every click went to the game instead of to
     * the title bar under the pointer. Anything else that re-captures is covered by the same line,
     * which is why it asks the OS rather than tracking what the engine did. */
    if (release_state.released && GetCapture() != NULL) {
        (void)ReleaseCapture();
    }

    down = (GetAsyncKeyState(release_state.key) & 0x8000) != 0;
    if (down == release_state.key_was_down) {
        return;
    }
    release_state.key_was_down = down;
    if (!down) {
        return;                                  /* act on the press, not on the release */
    }

    release_state.released = !release_state.released;
    if (release_state.released) {
        /* Dropped so that a click on the title bar reaches the title bar. The engine takes it back
         * for itself the next time it handles an activation, which is exactly when we want it to. */
        (void)ReleaseCapture();
        log_info("the pointer has been handed back to Windows: it can leave the window now, and "
                 "the title bar and its buttons are clickable. Press the same key to give it back "
                 "to the game.");
    } else {
        /* Hidden here rather than left to the engine's next WM_SETCURSOR. That message only
         * arrives on a mouse movement, so leaving it to the engine meant the arrow this module
         * substituted stayed on screen until the pointer was moved, sitting wherever the engine
         * had re-centred it. The engine keeps hiding it after this; this only makes the moment of
         * handing it back immediate rather than eventual. */
        if (release_state.original_set_cursor != NULL) {
            (void)release_state.original_set_cursor(NULL);
        }
        log_info("the pointer belongs to the game again");
    }
}

bool pointer_release_set_key(int32_t virtual_key)
{
    if (!release_state.armed || virtual_key < 0 || virtual_key > 0xFF) {
        return false;
    }
    if (virtual_key == 0 && release_state.released) {
        release_state.released = false;    /* never leave it out with no way to bring it back */
    }
    release_state.key = virtual_key;

    /* Seeded from the new key's CURRENT state, because the press that chose it in the dev panel
     * is very likely still down, and without this that same press would read as a toggle the
     * moment it was bound. */
    release_state.key_was_down = virtual_key != 0 &&
                                 (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
    return true;
}

bool pointer_release_install(const pointer_release_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (config->key == 0) {
        log_info("PointerReleaseKey=0, so the pointer is never handed back to Windows. In a window "
                 "with a frame that means the title bar cannot be reached with the mouse, because "
                 "the engine re-centres the pointer on every mouse message.");
        return false;
    }
    if (config->key < 0 || config->key > 0xFF) {
        log_warning("PointerReleaseKey=%d is not a virtual key code, so the pointer is never "
                    "handed back to Windows", (int)config->key);
        return false;
    }

    release_state.key   = config->key;
    release_state.armed = true;

    /* Seeded from the CURRENT state of the key so that a key already held while the game starts,
     * which is easy to do when the same key launched it, does not read as a press on frame one. */
    release_state.key_was_down = (GetAsyncKeyState(config->key) & 0x8000) != 0;

    /* Optional. Without it the feature still works and the pointer is still usable, it is just
     * invisible until it leaves the game's window altogether, so a failure here is worth a line
     * and is not worth refusing the whole feature over. */
    release_state.arrow = LoadCursorA(NULL, IDC_ARROW);
    if (release_state.arrow == NULL ||
        !import_patch_replace(NULL, "user32.dll", "SetCursor",
                              (void *)substituted_set_cursor,
                              (void **)&release_state.original_set_cursor)) {
        release_state.original_set_cursor = NULL;
        log_warning("the pointer will be invisible while it is over the game's own window, "
                    "including its title bar, because the engine's SetCursor could not be "
                    "intercepted. It still moves and still clicks.");
    }

    if (!frame_hook_add(poll_release_key)) {
        release_state.armed = false;
        log_warning("the frame hook was refused, so the pointer release key will not work");
        return false;
    }

    log_info("pointer release armed on virtual key 0x%02X: pressing it lets the pointer leave the "
             "window so the frame can be used, and pressing it again gives it back to the game. "
             "While it is out, the engine is sent no mouse movement at all, so the view does not "
             "jump when it comes back.", (unsigned)config->key);
    return true;
}
