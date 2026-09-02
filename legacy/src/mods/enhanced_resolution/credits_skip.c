/* credits_skip.c: see credits_skip.h. */
#include "credits_skip.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/signature.h"

#include <windows.h>

#include <stdint.h>

/* --- 0x004470F8 credits_screen ---------------------------------------------------------------
 * Detoured only to BOUND the skip. The flag must not survive one run of the credits into the next,
 * and must not be set by a key pressed anywhere else in the game. The one honest way to know the
 * credits are running is to be inside the function that runs them.
 *
 *   004470F8  55                 push ebp
 *   004470F9  8B EC              mov  ebp,esp
 *   004470FB  B8 30 14 00 00     mov  eax,0x1430      ; the frame size, and it is unmistakable
 *   00447100  E8 <rel32>         call the stack probe ; displacement masked
 *   00447105  56 57              push esi / push edi
 *   00447107  C7 85 DC F5 ...    mov  [ebp-0xA24], -1 ; exitCode = -1
 *
 * Eight bytes of prologue: push, mov, mov eax,imm32. Measured: ONE match. */
static const uint8_t SIG_CREDITS_SCREEN[] = {
    0x55, 0x8B, 0xEC, 0xB8, 0x30, 0x14, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x56, 0x57, 0xC7, 0x85, 0xDC, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t MSK_CREDITS_SCREEN[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define CREDITS_SCREEN_PROLOGUE 8u

/* --- 0x004476EB credits_readLine -------------------------------------------------------------
 * The leaf whose answer ends the crawl. Address free: a 0x100 byte line buffer, a push of 0x80 and
 * a lea of that buffer are the whole anchor.
 *
 * Nine bytes of prologue: push, mov, sub esp,imm32. Measured: ONE match. */
static const uint8_t SIG_CREDITS_READ_LINE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00,
    0x56, 0x57, 0x68, 0x80, 0x00, 0x00, 0x00,
    0x8D, 0x85, 0x00, 0xFF, 0xFF, 0xFF, 0x50
};
#define CREDITS_READ_LINE_PROLOGUE 9u

enum { SITE_CREDITS_SCREEN, SITE_CREDITS_READ_LINE, SITE_COUNT };

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("credits_screen",    SIG_CREDITS_SCREEN, MSK_CREDITS_SCREEN),
    SIGNATURE_ENTRY       ("credits_read_line", SIG_CREDITS_READ_LINE)
};

typedef int32_t (__cdecl *credits_screen_fn_t)(void);
typedef int32_t (__cdecl *credits_read_line_fn_t)(int32_t slot, char *out, int32_t *style);

static struct {
    detour_t screen_detour;
    detour_t read_line_detour;
    bool     inside_credits;
    bool     skip_requested;
    bool     key_was_down;
    bool     said_it;
} skip_state;

static bool escape_is_down(void)
{
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

/* Once per drawn frame, and it does nothing at all unless the credits are on screen.
 *
 * Polling here rather than inside credits_readLine is the difference between a tap and having to
 * hold the key down: that function is called once per SCROLL STEP, which is a good fraction of a
 * second apart, so a tap between two of them would simply be missed. */
static void on_frame(void)
{
    bool down;

    if (!skip_state.inside_credits) {
        skip_state.key_was_down = false;
        return;
    }

    down = escape_is_down();
    if (down && !skip_state.key_was_down && !skip_state.skip_requested) {
        skip_state.skip_requested = true;
        if (!skip_state.said_it) {
            skip_state.said_it = true;
            log_info("the credits were skipped: the line reader is answering end of file, so the "
                     "game ends them by its own path, blanking the rows and fading the music");
        }
    }
    skip_state.key_was_down = down;
}

static int32_t __cdecl hook_credits_read_line(int32_t slot, char *out, int32_t *style)
{
    credits_read_line_fn_t original =
        (credits_read_line_fn_t)skip_state.read_line_detour.original;

    if (original == NULL) {
        return 0;                    /* the un-armed instant between the write and the state */
    }

    if (skip_state.skip_requested) {
        /* An empty line and "not a heading", which is what the real function answers at end of
         * file. The engine blanks its own rows a step later; matching it here keeps the two
         * consistent in the meantime rather than leaving whatever the buffer last held. */
        if (out != NULL) {
            out[0] = '\0';
        }
        if (style != NULL) {
            *style = 0;
        }
        return 0;
    }

    return original(slot, out, style);
}

static int32_t __cdecl hook_credits_screen(void)
{
    credits_screen_fn_t original = (credits_screen_fn_t)skip_state.screen_detour.original;
    int32_t             result;

    if (original == NULL) {
        return -1;
    }

    skip_state.inside_credits = true;
    skip_state.skip_requested = false;

    /* SEEDED FROM THE LIVE KEY, NOT CLEARED. The closing cutscene runs immediately before this and
     * is skippable with the same key, so the finger is very likely still down as the credits open.
     * Starting from the current state means only a FRESH press counts, and the credits are not
     * skipped before their first frame is drawn. */
    skip_state.key_was_down = escape_is_down();

    result = original();

    skip_state.inside_credits = false;
    skip_state.skip_requested = false;
    return result;
}

void credits_skip_install(bool enabled)
{
    if (!enabled) {
        log_info("SkipCredits=0, the credits run to their end as the engine intends and cannot be "
                 "left early");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_CREDITS_SCREEN].address == 0 || sites[SITE_CREDITS_READ_LINE].address == 0) {
        log_warning("the credits screen %s and its line reader %s, so the credits keep the "
                    "engine's own behaviour and cannot be skipped",
                    (sites[SITE_CREDITS_SCREEN].address == 0) ? "did not resolve" : "resolved",
                    (sites[SITE_CREDITS_READ_LINE].address == 0) ? "did not" : "did");
        return;
    }

    /* The tick FIRST. Without it the key would only be seen once a scroll step and would have to be
     * held down, which is worse than not offering the feature; declining is the honest answer. */
    if (!frame_hook_add(on_frame)) {
        log_warning("the per frame tick is not available, so the credits skip is NOT installed: "
                    "polling the key once per scroll step would need it held down to register");
        return;
    }

    /* THE SCREEN DETOUR BEFORE THE READER, and the order is what makes a partial failure safe.
     * skip_requested is only ever set while inside_credits is up, and only the screen detour raises
     * that, so a reader detour standing on its own can never answer end of file: it passes every
     * call straight through. The reverse order would leave the reader armed with nothing to bound
     * it. There is no detour_remove in this project, so ordering is the rollback. */
    if (!detour_install(&skip_state.screen_detour, sites[SITE_CREDITS_SCREEN].address,
                        (const void *)hook_credits_screen, CREDITS_SCREEN_PROLOGUE)) {
        log_warning("credits_screen at %08X could not be detoured, so the credits cannot be "
                    "skipped", (unsigned)sites[SITE_CREDITS_SCREEN].address);
        return;
    }

    if (!detour_install(&skip_state.read_line_detour, sites[SITE_CREDITS_READ_LINE].address,
                        (const void *)hook_credits_read_line, CREDITS_READ_LINE_PROLOGUE)) {
        log_warning("credits_readLine at %08X could not be detoured, so the credits cannot be "
                    "skipped. The screen detour above stands and is harmless: it only raises a "
                    "flag nothing now reads",
                    (unsigned)sites[SITE_CREDITS_READ_LINE].address);
        return;
    }

    log_info("Escape skips the end credits (credits_screen %08X, credits_readLine %08X). The key "
             "is answered by telling the reader the text has run out, so the game ends the crawl "
             "the way it ends it normally: the rows blank, the music fades over four seconds and "
             "the screen closes itself. A controller's Start works too, because controller_input "
             "synthesises the same key",
             (unsigned)sites[SITE_CREDITS_SCREEN].address,
             (unsigned)sites[SITE_CREDITS_READ_LINE].address);
}
