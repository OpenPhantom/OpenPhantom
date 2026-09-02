/* overlay_input.c: one detour that opens the panel, drives it, and locks the game while it is up.
 *
 * ==============================================================================================
 * THE SITE
 *
 * The engine registers one function to see its window messages and hands it every one of them. It
 * is where the shipped game opens its own cheat console, on backspace. Retail WMAIN.EXE, 829,952
 * bytes, ImageBase 0x400000, at 0x0043F603:
 *
 *   0043F603  55 8B EC                 push ebp; mov ebp,esp
 *   0043F606  83 EC 08                 sub esp,8
 *   0043F609  C7 45 FC 00 00 00 00     the result, cleared
 *   0043F610  83 3D 3C A4 86 00 00     cmp [0086A43C],0     ; a movie is playing
 *   0043F617  74 07 / 33 C0 / E9 ..    if so, answer zero and do nothing
 *   0043F620  81 7D 0C 00 01 00 00     cmp [ebp+0x0C],0x100 ; the message, against WM_KEYDOWN
 *   0043F641  83 3D B4 CF 6C 00 00     cmp [006CCFB4],0     ; THE MODAL CELL
 *   0043F648  74 04                    a RAISED cell jumps past the result and answers 0
 *
 * Two things come out of that listing and both are load bearing.
 *
 * The arguments are at [ebp+8], [ebp+0x0C], [ebp+0x10] and [ebp+0x14], and the function ends in a
 * plain `ret` rather than `ret 0x10`, so it is cdecl and the caller cleans. It also answers a
 * value, in eax, out of [ebp-4], and a detour that dropped that answer would hand the engine
 * whatever happened to be in the register.
 *
 * The dispatcher that calls it pushes FIVE arguments and cleans 0x14, not four: ahead of these
 * four it pushes the address of a local that carries the result back when the hook answers non
 * zero. Reading only four is safe under cdecl, since the caller cleans and the fifth simply is not
 * looked at, but the fifth exists and this used to claim it did not.
 *
 * The modal cell is right there in the body, so its address is read out of the matched bytes rather
 * than written down. It is NOT raised: see the note beside set_open for the census and the branch
 * that show why raising it would do the opposite of what it looks like. It is resolved because a
 * pattern that finds it has landed on the right function, which is worth having on its own.
 *
 * ==============================================================================================
 * WHAT THIS DOES AND DOES NOT STOP, BECAUSE THE FIRST VERSION CLAIMED TOO MUCH
 *
 * Answering a message here instead of passing it on stops everything the engine drives FROM window
 * messages. That is the cheat console, the pause, and the menu keys.
 *
 * It does NOT stop gameplay. Movement, turning and firing are polled from DirectInput, in a layer
 * that never looks at the message queue, so the player keeps moving with the panel open. The game's
 * own cheat console does not stop it either: it pumps whole frames inside its own loop and the
 * world runs on behind the box. There is no ready made input lock in this engine to borrow, and
 * pretending otherwise in a comment is worse than saying so.
 *
 * THE POINTER comes from the system cursor, which is where the game's own menu pointer comes from,
 * and is mapped from the window's client pixels into the picture the engine draws. Those two are
 * usually the same size and the mapping is then the identity.
 * ============================================================================================ */
#include "overlay_input.h"

#include "cheats_openphantom.h"
#include "cheats_original_actions.h"
#include "input_freeze.h"
#include "sim_pause.h"
#include "overlay_draw.h"
#include "overlay_layout.h"
#include "overlay_model.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0043F603, sixty nine bytes, reaching as far as the modal cell so that one match yields both
 * the function to sit in front of and the cell to raise. The four address bearing fields are
 * masked. */
static const uint8_t SIG_KEY_HOOK[] = {
    0x55, 0x8B, 0xEC,                                      /* push ebp; mov ebp,esp   */
    0x83, 0xEC, 0x08,                                      /* sub esp,8               */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,              /* result = 0              */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,              /* cmp [movie],0           */
    0x74, 0x07,                                            /* jz  +7                  */
    0x33, 0xC0,                                            /* xor eax,eax             */
    0xE9, 0x00, 0x00, 0x00, 0x00,                          /* jmp out                 */
    0x81, 0x7D, 0x0C, 0x00, 0x01, 0x00, 0x00,              /* cmp [ebp+0x0C],0x100    */
    0x74, 0x18,                                            /* jz  +0x18               */
    0x8B, 0x45, 0x14, 0x50,                                /* push lParam             */
    0x8B, 0x4D, 0x10, 0x51,                                /* push wParam             */
    0x8B, 0x55, 0x0C, 0x52,                                /* push msg                */
    0xE8, 0x00, 0x00, 0x00, 0x00,                          /* call the default path   */
    0x83, 0xC4, 0x0C,                                      /* add esp,0x0C            */
    0x33, 0xC0,                                            /* xor eax,eax             */
    0xEB, 0x6F,                                            /* jmp out                 */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00               /* cmp [THE MODAL CELL],0  */
};
static const uint8_t MSK_KEY_HOOK[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
_Static_assert(sizeof(SIG_KEY_HOOK) == sizeof(MSK_KEY_HOOK),
               "the key hook pattern and its mask are different lengths");
#define OFFSET_MODAL_CELL   64u
#define KEY_HOOK_PROLOGUE    6u

/* The window messages this reads. Spelled out rather than pulled from a system header, because the
 * engine compares against the same literals and the pattern above carries 0x100 as bytes. */
#define MSG_KEY_DOWN        0x0100
#define MSG_SYS_KEY_DOWN    0x0104
#define MSG_CHAR            0x0102
#define MSG_MOUSE_MOVE      0x0200
#define MSG_LEFT_BUTTON     0x0201
#define MSG_MOUSE_WHEEL     0x020A
#define KEY_ESCAPE          0x1B
#define KEY_BACKSPACE       0x08
#define KEY_RETURN          0x0D
#define KEY_PAGE_UP         0x21
#define KEY_PAGE_DOWN       0x22
#define KEY_UP              0x26
#define KEY_DOWN            0x28

/* THE KEY THAT OPENS THE PANEL, AND WHY TWO OF THEM ARE ACCEPTED BY DEFAULT.
 *
 * The key wanted is the one directly below Escape, where developer consoles have lived for
 * decades. Which virtual key that is depends on the keyboard: on a German layout it is the caret
 * key and reports as 0xDC, on a British or American one the same physical key carries a backtick
 * and reports as 0xC0. Neither is more correct than the other, so both open the panel unless the
 * ini names one.
 *
 * They cannot collide with anything: the shipped game binds no key in that range. */
#define KEY_BELOW_ESCAPE_DE 0xDC   /* caret, German and most continental layouts */
#define KEY_BELOW_ESCAPE_US 0xC0   /* backtick, British and American layouts      */

/* BOTH KEY MESSAGES, BECAUSE OPENKEY CAN NAME A SYSTEM KEY.
 *
 * Windows sends the function keys as WM_SYSKEYDOWN rather than WM_KEYDOWN, on their own and with no
 * modifier held, because they can reach for a window's menu bar. A handler watching only WM_KEYDOWN
 * never sees them and says nothing, because every other part of the install succeeded. That cost
 * this feature a round, back when the default key was F10.
 *
 * The key below Escape needs only WM_KEYDOWN, but OpenKey takes any virtual key, so both are
 * accepted. The engine's own handler compares against WM_KEYDOWN alone. */
static bool is_key_down(int32_t message)
{
    return message == MSG_KEY_DOWN || message == MSG_SYS_KEY_DOWN;
}

/* Zero means "whatever sits below Escape on this keyboard", which is the default. */
static int32_t configured_key;

static bool is_open_key(int32_t wparam)
{
    if (configured_key != 0) {
        return wparam == configured_key;
    }
    return wparam == KEY_BELOW_ESCAPE_DE || wparam == KEY_BELOW_ESCAPE_US;
}

void overlay_input_set_key(int32_t virtual_key)
{
    configured_key = virtual_key;
}

/* The engine answers zero for a message it did not act on and non-zero for one it consumed. The
 * panel answers consumed for everything it sees while it is open, which is what stops the game
 * from ever seeing it. */
#define HANDLED             1

typedef int32_t (__cdecl *key_hook_fn_t)(uint32_t window, int32_t message,
                                         int32_t wparam, uint32_t lparam);

typedef struct overlay_input_state {
    bool              installed;
    bool              open;
    bool              saw_a_message;
    bool              search_focused;    /* whether a click has landed in the search field yet */
    detour_t          detour;
    key_hook_fn_t     original;
    HWND              window;
    float             pointer_x;
    float             pointer_y;
} overlay_input_state_t;

static overlay_input_state_t input_state;

/* ============================================================================================ */

/* THE MODAL CELL IS NOT TOUCHED, AND THAT IS A CORRECTION.
 *
 * It was raised here on the belief that it is what makes the engine stop listening. A byte census
 * says otherwise on both counts. The cell at the operand this file resolves has exactly three
 * references in the whole code section: a store of literal 1 and a store of literal 0, both inside
 * the game's own cheat console, and the one read in the hook. So it is a flag, not a count, and the
 * console is its only writer. Menus do not raise it.
 *
 * And raising it does the opposite of what was assumed. The read is
 * `cmp [cell],0 / jz +4 / xor eax,eax / jmp` and that jump lands one instruction PAST the load of
 * the result, so a raised cell makes the hook answer zero, which in this hook's contract means
 * "not handled, pass it on". Raising it swallows nothing; all it does is stop backspace opening
 * the console and Escape pausing.
 *
 * Worse, the console pumps frames inside its own loop, so the panel can be opened while the console
 * is up. Dropping the cell back to zero on close would then clear a flag the console had raised and
 * still needs.
 *
 * The panel swallows the messages it wants by answering them itself, which needs no cell at all.
 * The address is still resolved, because it is what proves the pattern landed on the right
 * function, and the log still names it. */
/* The panel is held open while the camera is flying, and this is a repair, not a policy.
 *
 * Free camera runs on the panel being up: closing it releases SIM_PAUSE_PANEL and the input freeze
 * out from under a camera that is still detoured and still flying, and the camera is left broken.
 * Field confirmed.
 *
 * So the two ways a PERSON closes the panel, Escape and the key that opened it, are refused while
 * the camera is on. Nothing is taken away: free camera cannot be switched on at all without a key
 * bound (see cheats_openphantom_toggle), and F4 is always there besides, so the flight can always
 * be ended and the panel closes normally the moment it is. Ending the flight is the way out, and
 * it is the ONLY thing that was ever going to leave both halves consistent.
 *
 * The programmatic closes are deliberately NOT gated: overlay_input_close() is what a level skip
 * and a failed paint use, and both of those are the panel getting out of the way of something
 * worse. Free camera's own teleport calls it too, on a frame where it has already switched
 * itself off. */
static bool free_camera_holds_panel(void)
{
    return cheats_openphantom_is_on(CHEATS_OWN_FREECAM);
}

static void set_open(bool open)
{
    input_state.open = open;
    input_state.search_focused = false;   /* every open starts unfocused; see the header comment
                                            * on overlay_input_search_focused() for why */
    input_freeze_set(open);
    /* The freeze stops the player being given orders; this stops the world carrying on regardless.
       Both, because either alone leaves half the game running: without the freeze the player moves
       behind the panel, and without this the NPCs, movers and timers do. */
    sim_pause_hold(SIM_PAUSE_PANEL, open);
    if (!open) {
        /* AFTER input_freeze_set(false), not before: a queued play-as swap has its own precondition
         * that reads as unmet while the player is suspended, which is exactly what closing this
         * call just ended. See cheats_original_actions.h's note on invoke() for why the swap is
         * queued rather than run the moment its row is pressed. */
        cheats_original_actions_apply_pending();
        overlay_model_reset();
        return;
    }
    overlay_input_update_pointer();     /* start where the cursor already is */
}

void overlay_input_close(void)
{
    if (input_state.open) {
        set_open(false);
    }
}

bool overlay_input_is_open(void)
{
    return input_state.open;
}

/* THE POINTER COMES FROM THE SYSTEM CURSOR, WHICH IS WHERE THE GAME'S OWN MENU POINTER COMES FROM.
 *
 * The first attempt integrated the device deltas the game was being denied. It moved, but it was
 * not usable: a relative stream has no home position, it drifts, and its speed is a number somebody
 * has to guess. The engine does not do that for its own menu pointer either. It takes the system
 * cursor's position and works from that, which is why the blue pointer in the front end behaves
 * like a pointer and not like a stick.
 *
 * So this does the same. The cursor keeps moving while the panel is open, because the panel is not
 * a window and takes nothing away from it, and the position is simply mapped into the panel's own
 * space. There is no speed to tune and nothing to drift. */
/* The wheel, once a frame, and only while the panel is open.
 *
 * It consumes the same accumulator the free camera's fly speed reads. That is not a clash: the
 * camera needs the wheel while FLYING, which is exactly when the panel is closed and this does not
 * run. Whichever of the two is on screen owns the wheel, and neither ever sees the other's notches.
 *
 * One notch is WHEEL_DELTA, 120. Three rows a notch is the shape Windows itself uses by default and
 * it reads about right against a row this tall. */
#define WHEEL_NOTCH   120
#define ROWS_PER_NOTCH  3

void overlay_input_update_scroll(void)
{
    const int32_t delta = overlay_input_take_wheel_delta();

    if (delta != 0) {
        /* Away from the player scrolls UP the list, which is the direction every other list on the
         * machine moves. */
        overlay_model_scroll_by(-(delta / WHEEL_NOTCH) * ROWS_PER_NOTCH);
    }
}

void overlay_input_update_pointer(void)
{
    POINT cursor;
    RECT  client;
    float screen_w = 0.0f;
    float screen_h = 0.0f;
    if (!input_state.open || input_state.window == NULL) {
        return;
    }
    if (!GetCursorPos(&cursor) || !ScreenToClient(input_state.window, &cursor)) {
        return;
    }
    if (!GetClientRect(input_state.window, &client) ||
        client.right <= client.left || client.bottom <= client.top) {
        return;
    }

    /* Two rectangles, and they are not the same one. The window's client area is in desktop pixels
     * and can be any size; the engine draws into a picture of its own chosen size. The panel lives
     * in the second, so the pointer is mapped from the first into it. They are usually equal and
     * the mapping is then the identity, which is exactly why getting this wrong was invisible until
     * the panel moved out of its old fixed size box and the pointer stayed behind in it. */
    if (!overlay_draw_screen(&screen_w, &screen_h)) {
        return;
    }
    input_state.pointer_x = (float)(cursor.x - client.left)
                          * (screen_w / (float)(client.right - client.left));
    input_state.pointer_y = (float)(cursor.y - client.top)
                          * (screen_h / (float)(client.bottom - client.top));

    if (input_state.pointer_x < 0.0f) { input_state.pointer_x = 0.0f; }
    if (input_state.pointer_y < 0.0f) { input_state.pointer_y = 0.0f; }
    if (input_state.pointer_x > screen_w) { input_state.pointer_x = screen_w; }
    if (input_state.pointer_y > screen_h) { input_state.pointer_y = screen_h; }
}

void overlay_input_pointer(float *out_x, float *out_y)
{
    if (out_x != NULL) { *out_x = input_state.pointer_x; }
    if (out_y != NULL) { *out_y = input_state.pointer_y; }
}

bool overlay_input_search_focused(void)
{
    return input_state.search_focused;
}

/* ============================================================================================ */

static void click(float x, float y)
{
    int32_t tab;
    int32_t row;

    /* A click anywhere ends the search box's focus except a click ON it, which is what starts it.
     * That is the whole of the click-to-type rule: typing goes into the box only between these two
     * moments, never just because the panel is open. The jump-scale edit follows the same rule via
     * the unconditional cancel below - it is cancelled on every click, then immediately re-armed by
     * overlay_model_activate() a few lines down if and only if the click actually landed back on
     * its own row, which is exactly the "click it again to redo it" shape that row already has. */
    if (overlay_draw_search_at(x, y)) {
        input_state.search_focused = true;
        overlay_model_value_cancel();
        return;
    }
    input_state.search_focused = false;
    overlay_model_value_cancel();

    tab = overlay_draw_tab_at(x, y);
    if (tab >= 0) {
        overlay_model_set_tab((overlay_tab_t)tab);
        overlay_model_rebuild();
        return;
    }
    row = overlay_draw_row_at(x, y);
    if (row >= 0) {
        (void)overlay_model_activate((uint32_t)row);
        overlay_model_rebuild();
    }
}

static bool handle(int32_t message, int32_t wparam, uint32_t lparam)
{
    /* The pointer no longer comes out of the message, so its coordinates are not read. It stays in
     * the signature because the caller has it and a later reader of this file would otherwise
     * wonder where it went. */
    (void)lparam;

    if (is_key_down(message)) {
        /* WHAT IS DELIBERATELY NOT SWALLOWED. Alt+F4 and Alt+Tab belong to the system and to the
         * person at the keyboard, not to a panel: a modal overlay that can trap somebody in a full
         * screen game is a worse defect than any it fixes. Both arrive with the alt bit set in the
         * message's own context, bit 29 of the flags, which is what MSG_SYS_KEY_DOWN means, so the
         * test is simply to hand a system key combination back untouched. Escape closes the panel
         * and is handled below rather than passed on, which is the same thing from the user's side.
         */
        if (message == MSG_SYS_KEY_DOWN && !is_open_key(wparam)) {
            return false;
        }
        /* A hotkey row is waiting for exactly this. Checked before Escape and everything else
         * below, on purpose: whatever key arrives while capturing IS the binding, including
         * Escape itself, rather than closing the panel or being swallowed as ordinary navigation.
         * Predictable this way - no separate list of keys a capture refuses to accept - and a
         * player who binds something inconvenient can simply click the row again to rebind it. */
        if (overlay_model_is_capturing_hotkey()) {
            overlay_model_capture_hotkey(wparam);
            overlay_model_rebuild();
            return true;
        }
        /* Checked before Escape and everything else below, same priority as the hotkey capture
         * just above and for the same reason: while a value is being typed, Enter/Escape/Backspace
         * belong to THAT field (commit/cancel/delete a digit), not to the panel (close) or the
         * search box (whose own backspace arm is further down and gated on a focus this is not). */
        if (overlay_model_is_editing_value()) {
            if (wparam == KEY_RETURN) {
                overlay_model_value_commit();
                overlay_model_rebuild();
                return true;
            }
            if (wparam == KEY_ESCAPE) {
                overlay_model_value_cancel();
                overlay_model_rebuild();
                return true;
            }
            if (wparam == KEY_BACKSPACE) {
                overlay_model_value_backspace();
                overlay_model_rebuild();
                return true;
            }
            return true;      /* everything else is swallowed, same as the panel-wide lock below */
        }
        /* SCROLLING BY KEY, AND NOT ONLY BY WHEEL. The machine that needs a scrolling list most is
         * the one with the smallest screen, and on a Steam Deck there is no wheel at all unless
         * somebody has bound one in Steam Input. These four are otherwise unused by the panel.
         *
         * Placed after the hotkey capture and the value editor above, so binding an arrow to a
         * cheat still works and typing into a field is undisturbed. */
        if (wparam == KEY_UP || wparam == KEY_DOWN) {
            overlay_model_scroll_by((wparam == KEY_UP) ? -1 : 1);
            return true;
        }
        if (wparam == KEY_PAGE_UP || wparam == KEY_PAGE_DOWN) {
            /* A page is what is on screen less one row, so the row you were reading stays in
             * view and there is no gap to lose your place in. */
            const uint32_t visible = overlay_layout()->visible_rows;
            const int32_t  page = (visible > 1u) ? (int32_t)(visible - 1u) : 1;

            overlay_model_scroll_by((wparam == KEY_PAGE_UP) ? -page : page);
            return true;
        }
        if (wparam == KEY_ESCAPE) {
            if (free_camera_holds_panel()) {
                return true;    /* swallowed, not acted on; see free_camera_holds_panel */
            }
            set_open(false);
            return true;
        }
        if (wparam == KEY_BACKSPACE && input_state.search_focused) {
            overlay_model_search_backspace();
            overlay_model_rebuild();
            return true;
        }
        return true;                       /* everything else is swallowed, which is the lock */
    }

    switch (message) {
    case MSG_CHAR:
        /* Backspace also arrives as a character and would otherwise be appended as one - it is
         * handled above, as a key-down, for both fields, so it is only ever excluded here. Typing
         * reaches either field only once a click has landed on it - see click() - which is also
         * what stops the open key itself leaking in as a character the instant the panel opens: the
         * key-down that opens the panel is handled above and never reaches here, but Windows still
         * queues the matching WM_CHAR right behind it, and without the focus gate that character
         * had nowhere else to go but into a box nobody had clicked on yet. The value row is checked
         * first because a click that starts editing it also leaves search_focused false, never
         * both true at once, so the order between these two only matters for readability. */
        if (wparam != KEY_BACKSPACE) {
            if (overlay_model_is_editing_value()) {
                overlay_model_value_append((char)wparam);
                overlay_model_rebuild();
            } else if (input_state.search_focused) {
                overlay_model_search_append((char)wparam);
                overlay_model_rebuild();
            }
        }
        return true;

    case MSG_MOUSE_MOVE:
        return true;               /* swallowed; the pointer comes from the system cursor */

    case MSG_LEFT_BUTTON:
        /* The panel's own position, not the one in the message: the game recentres the system
         * pointer while it plays, so a click's coordinates say nothing about where the panel's
         * pointer was drawn. */
        click(input_state.pointer_x, input_state.pointer_y);
        return true;

    default:
        break;
    }
    return false;
}

/* Notches scrolled since the last take, positive away from the player. Free camera's own fly
 * speed (see cheats_openphantom.c) is the one consumer, and it needs the wheel while FLYING, which
 * is exactly when the panel is closed - unlike everything else in this file, gated on
 * input_state.open, this has to be observed unconditionally, in hook_key() itself rather than in
 * handle() below. It is never consumed: the message still reaches input_state.original()
 * afterwards exactly as if this were not here, since nothing here is known to need the wheel for
 * anything of its own and there is no reason to find out by swallowing it.
 *
 * No locking: hook_key() and the consumer both run on the single game thread (a chained window-
 * message dispatch and a chained per-frame camera update respectively), never concurrently, the
 * same reasoning that lets the rest of this file's state go unguarded too. */
static int32_t wheel_delta_accum;

static void observe_wheel(int32_t message, int32_t wparam)
{
    if (message != MSG_MOUSE_WHEEL) {
        return;
    }
    /* WHEEL_DELTA notches live in the high word of wParam, signed. */
    wheel_delta_accum += (int32_t)(int16_t)((uint32_t)wparam >> 16);
}

int32_t overlay_input_take_wheel_delta(void)
{
    int32_t delta = wheel_delta_accum;

    wheel_delta_accum = 0;
    return delta;
}

static int32_t __cdecl hook_key(uint32_t window, int32_t message, int32_t wparam, uint32_t lparam)
{
    /* The game's own window, taken from the first message rather than searched for. */
    if (input_state.window == NULL) {
        input_state.window = (HWND)(uintptr_t)window;
    }

    /* Said once, on the first message of the session. It is the only thing that separates "the
     * detour is not being called at all" from "it runs and the key never arrives", and those
     * two need completely different repairs. */
    if (!input_state.saw_a_message) {
        input_state.saw_a_message = true;
        log_info("the message hook is live, first message %04X. Reported once.",
                 (unsigned)message);
    }

    observe_wheel(message, wparam);

    if (is_key_down(message) && is_open_key(wparam)) {
        if (input_state.open && free_camera_holds_panel()) {
            /* Said once per attempt rather than silently swallowed: a key that does nothing needs
             * to say why, and the panel itself cannot say it while the camera owns the screen. */
            log_info("the overlay stayed open: the free camera is flying, and closing the panel "
                     "would break it. End the flight first with your teleport key or F4");
            return HANDLED;
        }
        log_info("the overlay was %s with key %02X",
                 input_state.open ? "closed" : "opened", (unsigned)wparam);
        set_open(!input_state.open);
        if (input_state.open) {
            overlay_model_rebuild();
        }
        return HANDLED;
    }

    if (input_state.open && handle(message, wparam, lparam)) {
        return HANDLED;
    }

    /* Not ours: hand it on exactly as it arrived, and hand the answer back. Another feature may be
     * chained in front of the original and its answer is the one the engine has to see. */
    return input_state.original(window, message, wparam, lparam);
}

/* ============================================================================================ */

bool overlay_input_install(void)
{
    uintptr_t site;
    uint32_t  modal = 0;

    if (input_state.installed) {
        return true;
    }

    site = signature_find_detour_target(SIG_KEY_HOOK, MSK_KEY_HOOK, sizeof SIG_KEY_HOOK,
                                        KEY_HOOK_PROLOGUE);
    if (site == 0) {
        log_warning("the engine's window message hook did not resolve, so the overlay has no way "
                    "to be opened and nothing is patched");
        return false;
    }
    if (!memory_read_u32(site + OFFSET_MODAL_CELL, &modal) ||
        !memory_is_inside_image(modal, sizeof(int32_t))) {
        log_warning("the modal cell operand at %08X is not an address inside the image, refused",
                    (unsigned)(site + OFFSET_MODAL_CELL));
        return false;
    }
    if (!detour_install(&input_state.detour, site, (const void *)&hook_key, KEY_HOOK_PROLOGUE)) {
        log_warning("the window message hook at %08X could not be detoured", (unsigned)site);
        return false;
    }

    input_state.original = (key_hook_fn_t)input_state.detour.original;
    input_state.installed = true;

    log_info("the key below Escape opens the overlay, hooked at %08X. While it is open "
             "every message is answered here instead of being passed on, and Alt "
             "combinations are handed back so the game can still be closed or switched away "
             "from. The modal cell at %08X is read as proof this is the right function and "
             "is deliberately not written.",
             (unsigned)site, (unsigned)modal);
    return true;
}
