#include "window_close.h"

#include "window_fit.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdint.h>

#include <windows.h>

/* --- 0x0043E5E0  sys_main ---------------------------------------------------------------
 *
 *   55                 push ebp
 *   8B EC              mov  ebp,esp
 *   E8 2B 00 00 00     call 0x0043E613          sys_startup
 *   85 C0              test eax,eax
 *   75 09              jnz  +9
 *   E8 C3 04 00 00     call 0x0043EAB4          sys_shutdown        <- the one this reads
 *   33 C0              xor  eax,eax
 *   EB 1C              jmp  +0x1C
 *   83 3D 0C D0 6C 00 01   cmp dword [0x006CD00C],1
 *   75 07              jnz  +7
 *
 * Resolved rather than hard-coded, and the shutdown's own address is READ OUT of the call above
 * instead of being matched for. Its own first bytes are push ebp / mov ebp,esp / push 0 / call,
 * which is the shape of a great many functions in this image; sys_main's is not, because of the
 * test-and-branch pair and the compare against a global immediately after.
 *
 * Checked in every build present: one match in WMAIN.EXE and one in GAMEDATA\BIN\WMAIN.EXE, both at
 * 0x0043E5E0. TPM.EXE is a different size and does not contain it, so this refuses there, which is
 * the intended behaviour rather than a gap.
 */
static const uint8_t SIG_SYS_MAIN[] = {
    0x55, 0x8B, 0xEC,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xC0,
    0x75, 0x09,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x33, 0xC0,
    0xEB, 0x1C,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x75, 0x07
};
static const uint8_t MASK_SYS_MAIN[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_SYS_MAIN == sizeof MASK_SYS_MAIN,
               "the sys_main pattern and its mask are different lengths");

/* Where the shutdown call sits inside that pattern, and how long it is. Written as offsets into the
 * pattern so the two cannot drift apart. */
#define SHUTDOWN_CALL_OFFSET 12u
#define CALL_LENGTH          5u

enum {
    SITE_SYS_MAIN,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("sys_main", SIG_SYS_MAIN, MASK_SYS_MAIN)
};

typedef void(__cdecl *shutdown_fn)(void);

static LRESULT CALLBACK close_window_proc(HWND window, UINT message, WPARAM w, LPARAM l);

static struct {
    shutdown_fn shutdown;
    WNDPROC     original_proc;
    HWND        window;
    bool        quit_requested;
    bool        armed;
    bool        gave_up;
} close_state;

/* Wrapping the window procedure is left until a window EXISTS. Everything in this patch installs
 * at the host's entry point, which is before WinMain and therefore before wkernel_createWindow, so
 * asking for the window at install time asks too early and always answers nothing. That is exactly
 * what the first build of this file did, and the log said so on every run.
 *
 * Retried on each poll rather than once, because the window is not the only thing that has to be
 * ready, and given up on after a while so that a machine where it never appears is not asking the
 * OS for it once a second for the rest of the session. */
#define WRAP_ATTEMPT_LIMIT 600u

static void wrap_window_when_it_exists(void)
{
    static uint32_t attempts;
    HWND            window;

    if (close_state.armed || close_state.gave_up || close_state.shutdown == NULL) {
        return;
    }
    window = window_fit_game_window();
    if (window == NULL) {
        if (++attempts >= WRAP_ATTEMPT_LIMIT) {
            close_state.gave_up = true;
            log_warning("the game window never turned up, so the close box will do nothing. That "
                        "is what the engine does with it in every shipped build.");
        }
        return;
    }

    close_state.original_proc =
        (WNDPROC)(uintptr_t)SetWindowLongA(window, GWL_WNDPROC, (LONG)(uintptr_t)close_window_proc);
    if (close_state.original_proc == NULL) {
        close_state.gave_up = true;
        log_warning("the window procedure could not be wrapped, so the close box will do nothing");
        return;
    }
    close_state.window = window;
    close_state.armed  = true;
    log_info("the window's close box now quits: the engine swallows WM_CLOSE by design, because "
             "the window it gives itself has no close box, so this answers it and runs the "
             "engine's own shutdown at %08X a frame later. It applies only while the window has a "
             "frame with a close box on it.",
             (unsigned)(uintptr_t)close_state.shutdown);
}

/* The close box is answered by REQUESTING rather than by acting, and the request is served a frame
 * later from window_close_poll. Tearing the game down from inside its own window procedure would
 * mean freeing the world while a message is being dispatched from inside a frame that is still
 * running, which is a crash on exit waiting to happen. */
static LRESULT CALLBACK close_window_proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_CLOSE &&
        (GetWindowLongA(window, GWL_STYLE) & WS_SYSMENU) != 0) {
        /* Only when the window actually HAS a close box. In the shape the engine gives itself
         * there is none, and Alt+F4 doing nothing is then the behaviour every release has had. */
        close_state.quit_requested = true;
        return 0;
    }
    return CallWindowProcA(close_state.original_proc, window, message, w, l);
}

void window_close_poll(void)
{
    wrap_window_when_it_exists();

    if (!close_state.armed || !close_state.quit_requested) {
        return;
    }
    close_state.quit_requested = false;

    log_info("the window's close box was used, so the engine's own shutdown is running now");

    /* The engine's teardown, the same function sys_main calls when the game exits normally: it
     * shuts every module down, frees the level, closes the graphics, destroys the window and then
     * pumps messages until its own WM_DESTROY answers.
     *
     * The process then ends here rather than returning, and it cannot: the engine only ever
     * calls this from sys_main, with nothing above it but WinMain, whereas this is reached from
     * inside a frame, so returning would carry on running a level that has just been freed. The
     * teardown itself is complete before this line, so what is skipped by leaving this way is the C
     * runtime's own exit handlers and nothing of the game's. */
    close_state.shutdown();
    ExitProcess(0u);
}

bool window_close_install(void)
{
    uintptr_t site;
    uintptr_t call_at;
    int32_t   displacement;

    signature_resolve_table(sites, SITE_COUNT);
    site = sites[SITE_SYS_MAIN].address;
    if (site == 0) {
        log_warning("sys_main did not resolve, so the window's close box will do nothing. That is "
                    "what the engine does with it in every shipped build, so nothing is worse than "
                    "it was.");
        return false;
    }
    call_at      = site + SHUTDOWN_CALL_OFFSET;
    displacement = *(const int32_t *)(call_at + 1u);
    close_state.shutdown = (shutdown_fn)(call_at + CALL_LENGTH + (uintptr_t)displacement);

    log_info("the engine's shutdown resolved at %08X; the close box is wrapped as soon as the "
             "engine has made its window, which is after this runs.",
             (unsigned)(uintptr_t)close_state.shutdown);
    return true;
}
