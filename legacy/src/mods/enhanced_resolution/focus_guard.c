/* focus_guard.c: the window changes hands and the engine is never told.
 *
 * ==============================================================================================
 * BYTE BASIS 1. what the engine does on a focus change, which is almost nothing
 *
 * The window procedure is 0x0049905E, `C7 45 DC 5E 90 49 00` at 0x00498F6D writes it into the
 * `lpfnWndProc` field of the WNDCLASSA that RegisterClassA at 0x00498FD0 is handed. It switches on
 * `msg - 2`
 * through the byte table at 0x004991EB and the address table at 0x004991D7, and exactly four
 * messages have a case. WM_ACTIVATEAPP (0x1C) is one of them:
 *
 *   004990E5  8B 45 10              mov  eax,[ebp+0x10]        ; wParam: 1 = activated
 *   004990E8  A3 A0 A5 4B 00        mov  [004BA5A0], eax
 *   ...                             SystemParametersInfoA(0x61 SPI_SETSCREENSAVERRUNNING, ...)
 *   ...                             falls through to 00499131
 *
 * 0x00499131 walks the engine's own message-handler chain, `call [edx*8 + 00862160]` while
 * `[edx*8 + 00862164]` is non-zero, for `[008621E8]` entries. Two handlers are ever registered,
 * both through stdWin_addMessageHandler 0x00498D5E:
 *
 *   0043EB5B  68 03 F6 43 00        push 0043F603     ; the control filter
 *   0046BAB0  68 C6 BA 46 00        push 0046BAC6     ; the graphics hot keys, WM_KEYDOWN only
 *
 * and 0x0043F603 hands everything that is not WM_KEYDOWN to control_windowFilter 0x0046A189:
 *
 *   0046A193  cmp [ebp-4],0x1C / je 0046A1B2          ; WM_ACTIVATEAPP
 *   0046A1B2  cmp [ebp+0x0C],1 / jne 0046A1BF
 *   0046A1B8  call 0046A155                           ; activated   -> SetCapture + a warp
 *   0046A1BF  call 0046A17E                           ; deactivated -> ReleaseCapture
 *
 * So the engine's ENTIRE response to a focus change is SetCapture/ReleaseCapture plus a screen
 * saver flag. It does not pause, it does not touch the display mode, and it does not release the
 * device: graphics_shutdownMode 0x0046C4E8 has one caller, 0x0043F5CD, on the shutdown path.
 *
 * And the message pump 0x00498BCA does not block. It calls PeekMessageA with PM_NOREMOVE, returns
 * 0 when the queue is empty, and the frame limiter around it spins on `Sleep(0)`:
 *
 *   00475BC9  6A 00 / FF 15 <Sleep> / EB BF            ; push 0, call, jmp back to the pump
 *
 * The game therefore runs at full speed while it is in the background. That is engine behaviour,
 * not a defect this file repairs, and it is named here so nobody looks for it later.
 *
 * ----------------------------------------------------------------------------------------------
 * BYTE BASIS 2. the input devices are acquired once and never again
 *
 * stdControl_openMouse 0x0048DA7C sets the cooperative level and nothing else does:
 *
 *   0048DAEA  6A 06                 push 6                     ; NONEXCLUSIVE | FOREGROUND
 *   0048DAEC  E8 <rel32>            call 00498967              ; the engine's window
 *   0048DB00  FF 52 34              call [edx+0x34]            ; SetCooperativeLevel
 *
 * stdControl_openKeyboard 0x0048D9E8 creates the device (`call [edx+0x0C]`), sets the data format
 * (`call [edx+0x2C]`) and the buffer size; there is no `call [edx+0x34]` in it at all, so the
 * keyboard runs at whatever DirectInput's default is.
 *
 * The one function that acquires is stdControl_setFocus 0x0048D719:
 *
 *   0048D71D  83 3D B4 19 86 00 00  cmp  [008619B4],0          ; bOpen; 0 -> return
 *   0048D72B  83 7D 08 00           cmp  [ebp+8],0
 *   0048D743  A1 C0 19 86 00 ...    mov  eax,[pMouseDevice]
 *   0048D751  FF 52 1C              call [edx+0x1C]            ; Acquire
 *   0048D76E  FF 52 1C              call [edx+0x1C]            ; Acquire (keyboard)
 *   0048D774  C7 05 C8 19 86 00 ..  mov  [008619C8],1          ; bAcquired, gate 1 of every read
 *   0048D797  FF 52 20              call [edx+0x20]            ; Unacquire, on the zero branch
 *
 * It has five callers and NOT ONE of them is reachable from a focus change:
 *
 *   0048D195  stdControl_open   (module message 3)
 *   0048D1B6  stdControl_close  (module message 4)
 *   00464A8E  the Control module's case for message 0x12 -> setFocus(0)
 *   00464AA1  the Control module's case for message 0x13 -> setFocus(1), then resync
 *   0048D656  inside 0x0048D629, which handles WM_ACTIVATE(6) -> setFocus(LOWORD(wParam) != 0)
 *
 * The last two are the interesting ones, and both are dead:
 *
 *   * 0x0048D629 IS NEVER REGISTERED. The little-endian dword `29 D6 48 00` does not occur once
 *     in the whole 829,952-byte image, and no `call`/`jmp rel32` targets it either. So WM_ACTIVATE
 *     reaches nothing.
 *   * MESSAGES 0x12 AND 0x13 ARE NEVER SENT. All 24 call sites of module_broadcast 0x0046F3C3 and
 *     all 14 of module_broadcastDt 0x0046F4A9 push their message id as an immediate, and the
 *     complete set is {3,4,5,6,7,0x10,0x16,0x17,0x18,0x19} and {8,9,0x0C,0x0D,0x0E,0x11,0x15}.
 *     0x12 and 0x13 are in neither.
 *
 * The engine therefore AUTHORED a suspend/resume pair for its input and shipped without a sender.
 * Windows, meanwhile, unacquires a FOREGROUND device by itself when the window goes to the
 * background, so after the first Alt-Tab the devices are unacquired and the retail build has no
 * code that can ever acquire them again. This file sends the two messages the engine is missing,
 * by calling exactly the two leaves its own case 0x13 calls, in the same order.
 *
 * ----------------------------------------------------------------------------------------------
 * BYTE BASIS 3. why the pointer still leaves, and why capture does not save it
 *
 * The confinement is warp-on-every-WM_MOUSEMOVE and nothing else: the import table has ClipCursor
 * nowhere, and the whole USER32 cursor vocabulary is SetCapture, ReleaseCapture, SetCursor and
 * SetCursorPos. A warp only happens when a message arrives, and a message only arrives while the
 * pointer is over the window, or while a button is down, which is the only case in which a
 * captured window still gets mouse input once the pointer is over another thread's window. Move
 * the pointer across the edge with no button held and the loop stops feeding itself.
 *
 * That was harmless in the shape the engine ships in: CreateWindowExA at 0x00499019 is called with
 * X = 0, Y = 0, GetSystemMetrics(SM_CXSCREEN) x GetSystemMetrics(SM_CYSCREEN) and style
 * 0x80000000, and both SetWindowPos sites (0x00498CD1 and 0x00498D19) push uFlags = 6, i.e.
 * SWP_NOMOVE | SWP_NOZORDER. The window covered the whole screen and the edge was the edge of the
 * desktop. It is not harmless now: window_fit.c sizes the window to the display mode, and even at
 * full size on one monitor the warp parks the pointer at client (320,240), 320 pixels from the
 * left edge, with a second monitor beginning one pixel further left.
 *
 * And the capture the engine takes on WM_ACTIVATEAPP is not reaching it either. A graphics
 * wrapper in front of the window procedure logs, in the same session as the field report:
 *     WndProc::Handler Warning: filtering WM_ACTIVATEAPP: 0
 *     WndProc::Handler Warning: filtering WM_ACTIVATEAPP: 1
 * so 0x0046A155 and 0x0046A17E never run there. That is why the focus signal used here is polled
 * from the frame hook instead of taken from a message: a message can be filtered, GetForegroundWindow
 * cannot.
 *
 * SIZE NOTE (rule 9): the file is over the preferred 400 lines and the excess is the three
 * listings above. They are what make a feature that holds process-global OS state reviewable:
 * without them "we clip the cursor" is a decision with no stated cause, and the claim that the
 * engine has no resume path is a claim about ABSENCE, which cannot be checked from the C at all.
 * ============================================================================================ */
#include "focus_guard.h"

#include "window_fit.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0048D719  stdControl_setFocus ---------------------------------------------------------
 * The four absolute .data operands are wildcarded, so the pattern survives a rebased image; what
 * carries it is the shape, the bOpen gate, the three short branches 4F/1D/14 and the two
 * `call [edx+0x1C]` slots. Unique in both retail builds and in the recompiled obi.exe, where
 * it sits 0x60 lower at 0x0048D6B9. */
static const uint8_t SIG_CONTROL_SET_FOCUS[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x05,
    0xE9, 0x93, 0x00, 0x00, 0x00,
    0x83, 0x7D, 0x08, 0x00,
    0x74, 0x4F,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x1D,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x14,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x11,
    0xFF, 0x52, 0x1C
};
static const uint8_t MSK_CONTROL_SET_FOCUS[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CONTROL_SET_FOCUS) == sizeof(MSK_CONTROL_SET_FOCUS),
               "the setFocus pattern and its mask are different lengths");

/* --- 0x0048D1CF  stdControl_resync -----------------------------------------------------------
 * `now = timer(); if (keyboard) flushKeyboard(); if (mouse) flushMouse();`; three calls and two
 * absolute reads, all five operands wildcarded. Also unique in all three builds (0x0048D16F on
 * obi.exe). It is what the engine's own case 0x13 runs after setFocus(1), and it is why a key held
 * across an Alt-Tab does not come back as a fresh press. */
static const uint8_t SIG_CONTROL_RESYNC[] = {
    0x55, 0x8B, 0xEC,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x05,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x05,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x5D, 0xC3
};
static const uint8_t MSK_CONTROL_RESYNC[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CONTROL_RESYNC) == sizeof(MSK_CONTROL_RESYNC),
               "the resync pattern and its mask are different lengths");

enum {
    FOCUS_SITE_SET_FOCUS,
    FOCUS_SITE_RESYNC,
    FOCUS_SITE_COUNT
};

static signature_t sites[FOCUS_SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("control_set_focus", SIG_CONTROL_SET_FOCUS, MSK_CONTROL_SET_FOCUS),
    SIGNATURE_ENTRY_MASKED("control_resync",    SIG_CONTROL_RESYNC,    MSK_CONTROL_RESYNC)
};

/* Both __cdecl: `6A 01 / E8 <rel32>` at 0x0048D193 with `83 C4 04` behind it, and a bare
 * `call 0x0048D1CF` with no cleanup at all at 0x00464AA9.
 * setFocus leaves nothing meaningful in eax, its two branches fall into a shared epilogue, so
 * it is typed void rather than inventing a result for it. */
typedef void (__cdecl *control_set_focus_fn_t)(int32_t has_focus);
typedef void (__cdecl *control_resync_fn_t)(void);

/* A focus change is a human action. Sixteen logged transitions is more than a session produces and
 * far less than a fight between two windows would, so the cap both keeps the log readable and
 * makes such a fight visible as the line that announces the cap. */
#define FOCUS_LOG_LIMIT 16

typedef struct focus_guard_state {
    bool installed;
    focus_guard_config_t config;

    bool state_known;
    bool had_focus;
    bool confining;
    RECT confined_to;

    control_set_focus_fn_t engine_set_focus;
    control_resync_fn_t    engine_resync;

    unsigned transitions;
    bool     warned_clip_failed;
} focus_guard_state_t;

static focus_guard_state_t focus_state;

/* ============================================================================================
 * The decision, and the one property that matters
 *
 * RELEASE is produced whenever a confinement is held and either the foreground is not ours or the
 * feature has been switched off. It is written before CONFINE and does not depend on the previous
 * state, so there is no sequence of ticks that can leave a clip standing while the game is not in
 * front, not a missed transition, not a first tick, not a configuration change.
 *
 * CONFINE and RELEASE are mutually exclusive by construction: the first needs
 * (has_focus && confine_wanted), the second needs its negation.
 *
 * ACQUIRE and UNACQUIRE are the only actions that reach into the engine, and they fire on an
 * observed CHANGE only. The first tick deliberately produces neither: at that point nothing has
 * gone wrong yet, and sending the engine a resume it did not ask for would flush the input buffers
 * for no reason.
 * ============================================================================================ */
unsigned focus_guard_actions(const focus_guard_inputs_t *inputs)
{
    unsigned actions = FOCUS_ACTION_NONE;

    if (inputs == NULL) {
        return FOCUS_ACTION_NONE;
    }

    if (inputs->confining && (!inputs->has_focus || !inputs->confine_wanted)) {
        actions |= FOCUS_ACTION_RELEASE;
    }
    if (inputs->has_focus && inputs->confine_wanted) {
        actions |= FOCUS_ACTION_CONFINE;
    }

    if (inputs->reacquire_wanted && inputs->state_known &&
        inputs->has_focus != inputs->had_focus) {
        actions |= inputs->has_focus ? FOCUS_ACTION_ACQUIRE : FOCUS_ACTION_UNACQUIRE;
    }

    return actions;
}

/* ============================================================================================ */
static bool rects_equal(const RECT *a, const RECT *b)
{
    return a->left == b->left && a->top == b->top &&
           a->right == b->right && a->bottom == b->bottom;
}

/* The client area in screen coordinates. False when the window cannot be asked or the area is
 * degenerate, a zero-width clip rectangle would pin the pointer to a single column. */
static bool client_area_on_screen(HWND window, RECT *out)
{
    RECT  client;
    POINT top_left;
    POINT bottom_right;

    if (!GetClientRect(window, &client)) {
        return false;
    }
    if (client.right - client.left < 1 || client.bottom - client.top < 1) {
        return false;
    }

    top_left.x     = client.left;
    top_left.y     = client.top;
    bottom_right.x = client.right;
    bottom_right.y = client.bottom;
    if (!ClientToScreen(window, &top_left) || !ClientToScreen(window, &bottom_right)) {
        return false;
    }

    out->left   = top_left.x;
    out->top    = top_left.y;
    out->right  = bottom_right.x;
    out->bottom = bottom_right.y;
    return true;
}

static void release_confinement(void)
{
    if (!focus_state.confining) {
        return;
    }
    focus_state.confining = false;
    ClipCursor(NULL);
}

/* Applying the same rectangle again every frame would be a wasted call, and never re-applying it
 * would be wrong: Windows drops the clip whenever the foreground changes, and a window that
 * regains the foreground inside one frame would come back unconfined without anything here
 * noticing. So the OS is asked what it currently holds and we only write when it disagrees. */
static void apply_confinement(HWND window)
{
    RECT wanted;
    RECT current = { 0, 0, 0, 0 };

    if (!client_area_on_screen(window, &wanted)) {
        release_confinement();
        return;
    }

    if (focus_state.confining && rects_equal(&focus_state.confined_to, &wanted) &&
        GetClipCursor(&current) && rects_equal(&current, &wanted)) {
        return;
    }

    if (!ClipCursor(&wanted)) {
        release_confinement();
        if (!focus_state.warned_clip_failed) {
            focus_state.warned_clip_failed = true;
            log_warning("the pointer could not be confined to the window (ClipCursor refused). "
                        "The pointer keeps the engine's own behaviour, which holds it only while "
                        "the window covers the whole screen.");
        }
        return;
    }

    focus_state.confining  = true;
    focus_state.confined_to = wanted;
}

/* ============================================================================================
 * The engine's own suspend and resume, sent by us because nothing in the retail build sends them.
 * The order inside the resume is the engine's: acquire first, then drain what queued up while the
 * devices were gone. Doing it the other way round would drain a device that is still unacquired
 * and then let the stale events through afterwards.
 * ============================================================================================ */
static void engine_resume_input(void)
{
    if (focus_state.engine_set_focus == NULL) {
        return;
    }
    focus_state.engine_set_focus(1);
    if (focus_state.engine_resync != NULL) {
        focus_state.engine_resync();
    }
}

static void engine_suspend_input(void)
{
    if (focus_state.engine_set_focus == NULL) {
        return;
    }
    focus_state.engine_set_focus(0);
}

/* ============================================================================================ */
static bool window_has_foreground(HWND window)
{
    /* Deliberately the WINDOW and not the process. An error box put up by the engine belongs to
     * this process too, and confining the pointer to the game's client area while a modal dialog
     * asks for a click somewhere else is exactly the bug this feature must not create. */
    return window != NULL && GetForegroundWindow() == window && !IsIconic(window);
}

/* Every clause below is written from what actually happened this tick, not from the intent: the
 * pointer clause reads the flag AFTER the clip was applied or dropped, and the input clause is
 * driven by the action bit rather than by the configuration key, so a run in which the engine's
 * functions did not resolve cannot produce a line claiming they were called. */
static void log_transition(bool has_focus, unsigned actions)
{
    if (focus_state.transitions > FOCUS_LOG_LIMIT) {
        return;
    }
    if (focus_state.transitions == FOCUS_LOG_LIMIT) {
        log_info("further focus changes are handled without a log line");
        return;
    }

    if (has_focus) {
        log_info("the game window is in front again%s%s",
                 ((actions & FOCUS_ACTION_ACQUIRE) != 0)
                     ? ", the input devices are re-acquired and their buffers drained, which is "
                       "the engine's own message 0x13 and nothing in the retail build sends it"
                     : "",
                 focus_state.confining ? "; the pointer is held in the client area" : "");
    } else {
        log_info("the game window lost the foreground%s%s",
                 ((actions & FOCUS_ACTION_UNACQUIRE) != 0)
                     ? ", the input devices are released" : "",
                 focus_state.confining ? "" : "; the pointer is free");
    }
}

static void focus_guard_on_frame(void)
{
    HWND                  window = window_fit_game_window();
    focus_guard_inputs_t  inputs;
    unsigned              actions;
    bool                  has_focus = window_has_foreground(window);

    inputs.state_known      = focus_state.state_known;
    inputs.had_focus        = focus_state.had_focus;
    inputs.has_focus        = has_focus;
    inputs.confining        = focus_state.confining;
    inputs.confine_wanted   = focus_state.config.confine_pointer;
    inputs.reacquire_wanted = focus_state.config.reacquire_input;

    actions = focus_guard_actions(&inputs);

    if ((actions & FOCUS_ACTION_RELEASE) != 0) {
        release_confinement();
    }
    if ((actions & FOCUS_ACTION_CONFINE) != 0) {
        apply_confinement(window);
    }
    if ((actions & FOCUS_ACTION_UNACQUIRE) != 0) {
        engine_suspend_input();
    }
    if ((actions & FOCUS_ACTION_ACQUIRE) != 0) {
        engine_resume_input();
    }

    if (focus_state.state_known && has_focus != focus_state.had_focus) {
        log_transition(has_focus, actions);
        ++focus_state.transitions;
    }

    focus_state.had_focus   = has_focus;
    focus_state.state_known = true;
}

/* ============================================================================================ */
static bool resolve_input_sites(void)
{
    signature_resolve_table(sites, FOCUS_SITE_COUNT);

    if (sites[FOCUS_SITE_SET_FOCUS].address == 0) {
        log_warning("stdControl_setFocus did not resolve, the input devices are NOT re-acquired "
                    "after an Alt-Tab, which is the engine's own behaviour and means the keyboard "
                    "and mouse stay dead until the game is restarted");
        return false;
    }
    focus_state.engine_set_focus =
        (control_set_focus_fn_t)sites[FOCUS_SITE_SET_FOCUS].address;

    if (sites[FOCUS_SITE_RESYNC].address == 0) {
        log_warning("stdControl_resync did not resolve, the devices are still re-acquired, but "
                    "the events that queued up while the game was in the background are not "
                    "drained, so a key held across the switch can arrive as a fresh press");
    } else {
        focus_state.engine_resync = (control_resync_fn_t)sites[FOCUS_SITE_RESYNC].address;
    }
    return true;
}

bool focus_guard_install(const focus_guard_config_t *config)
{
    bool reacquire_possible = false;

    if (focus_state.installed || config == NULL) {
        return false;
    }
    focus_state.installed = true;

    if (!config->confine_pointer && !config->reacquire_input) {
        log_info("ClipPointerToWindow=0 and ReacquireInputOnFocus=0, nothing watches the "
                 "foreground; the pointer can leave the window and the input devices stay "
                 "unacquired after an Alt-Tab");
        return false;
    }

    if (config->reacquire_input) {
        reacquire_possible = resolve_input_sites();
    }

    focus_state.config.confine_pointer = config->confine_pointer;
    focus_state.config.reacquire_input = config->reacquire_input && reacquire_possible;

    if (!focus_state.config.confine_pointer && !focus_state.config.reacquire_input) {
        return false;
    }

    /* THE FALLBACK IS "DO NOTHING", and it has to be. Without a per-frame tick nothing would ever
     * observe the foreground being lost, so a confinement applied once would be held for the
     * rest of the session, across every Alt-Tab, which is a worse bug than the one being fixed.
     * The re-acquire has the same single source of truth and goes with it. */
    if (!frame_hook_add(focus_guard_on_frame)) {
        focus_state.config.confine_pointer = false;
        focus_state.config.reacquire_input = false;
        log_warning("no per-frame hook, the pointer is NOT confined and the input devices are "
                    "NOT re-acquired after an Alt-Tab. Both need one signal, and it is the only "
                    "signal a graphics wrapper cannot filter away.");
        return false;
    }

    log_info("the foreground is watched once per frame (GetForegroundWindow, not WM_ACTIVATEAPP - "
             "a graphics wrapper in front of the window procedure can filter that message, and one "
             "was measured doing exactly that). Pointer "
             "confinement %s, input re-acquire %s. The engine's own confinement is a warp on every "
             "mouse message, which stops working the moment the pointer crosses the window edge "
             "between two of them; its input is opened FOREGROUND and never acquired again, "
             "because the two module messages that would do it have a receiver and no sender.",
             focus_state.config.confine_pointer ? "ON" : "off",
             focus_state.config.reacquire_input ? "ON" : "off");

    /* The honest half of the line above, and it applies to the confinement only: the re-acquire is
     * load-bearing in every session, because the engine has no resume path at all. */
    if (focus_state.config.confine_pointer) {
        if (config->window_is_moved) {
            log_info("the pointer confinement is LOAD-BEARING in this session: FitWindowToMode is "
                     "on, so the window is moved and sized away from the desktop edge and the "
                     "pointer really can walk off it onto another monitor.");
        } else {
            log_info("the pointer confinement is BELT-AND-BRACES in this session: nothing moves the "
                     "window, so it stays at screen 0,0 at the size of the desktop and its client "
                     "edge IS the desktop edge, which the pointer cannot cross anyway. It is left "
                     "on because it costs nothing there, the rectangle is the whole desktop, "
                     "and it is what makes a setup where something else repositions the window "
                     "survivable.");
        }
    }
    return true;
}

void focus_guard_shutdown(void)
{
    /* Reached from DLL_PROCESS_DETACH, so it must stay a single USER32 call with no logging, no
     * allocation and no engine memory: whatever is holding the loader lock is still holding it. */
    release_confinement();
}
