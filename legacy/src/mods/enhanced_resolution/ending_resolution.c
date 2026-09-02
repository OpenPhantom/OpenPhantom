/* ending_resolution.c: see ending_resolution.h for what this is and why it is safe. */
#include "ending_resolution.h"

#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdint.h>

/* The ending sequence, from the push of 480 to the two arguments of the movie name call.
 *
 * ADDRESS FREE ON PURPOSE. Three operands are masked out: the two call displacements and the
 * pointer to "movie\scene8". The string address would have been the shortest way to tell this site
 * apart from the five other places that push 640x480, and it is exactly the sort of anchor that
 * stops resolving under forced ASLR. The tail carries the distinction instead: the lea of the same
 * stack buffer, the push, and the 0/1 pair the movie call takes.
 *
 * Measured: ONE match in the retail WMAIN.EXE. */
static const uint8_t SIG_ENDING_MODE_DROP[] = {
    0x68, 0xE0, 0x01, 0x00, 0x00,              /* push 480                                      */
    0x68, 0x80, 0x02, 0x00, 0x00,              /* push 640                                      */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call graphics_setResolution                   */
    0x83, 0xC4, 0x08,                          /* add  esp,8                                    */
    0x68, 0x00, 0x00, 0x00, 0x00,              /* push "movie\scene8"                           */
    0x8D, 0x95, 0x7C, 0xFF, 0xFF, 0xFF,        /* lea  edx,[ebp-0x84]                           */
    0x52,                                      /* push edx                                      */
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call the name builder                         */
    0x83, 0xC4, 0x08,                          /* add  esp,8                                    */
    0x6A, 0x00,                                /* push 0                                        */
    0x6A, 0x01                                 /* push 1                                        */
};
static const uint8_t MSK_ENDING_MODE_DROP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF
};

/* The call sits ten bytes into the match, after the two pushes. */
#define OFFSET_MODE_DROP_CALL 0x0Au
#define MODE_DROP_CALL_SIZE   5u

enum { SITE_ENDING_MODE_DROP, SITE_COUNT };

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("ending_mode_drop", SIG_ENDING_MODE_DROP, MSK_ENDING_MODE_DROP)
};

void ending_resolution_install(bool enabled)
{
    static const uint8_t NOPS[MODE_DROP_CALL_SIZE] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    uintptr_t call_site;

    if (!enabled) {
        log_info("EndingKeepsResolution=0, the ending drops the display to 640x480 exactly as the "
                 "retail game does, and the credits run in a window in the corner");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_ENDING_MODE_DROP].address == 0) {
        log_warning("the ending's 640x480 switch did not resolve, so it is left alone: finishing "
                    "the game will drop the display and the credits will run at 640x480");
        return;
    }

    call_site = sites[SITE_ENDING_MODE_DROP].address + OFFSET_MODE_DROP_CALL;

    /* The two pushes stay and so does the `add esp,8` after, so the stack balances with or without
     * this write. Only the call is removed. */
    if (patch_write_bytes(call_site, NOPS, sizeof NOPS) != PATCH_RESULT_OK) {
        log_error("the ending's 640x480 switch at %08X could not be removed, so the ending still "
                  "drops the display", (unsigned)call_site);
        return;
    }

    log_info("the ending keeps your resolution: the 640x480 switch before the closing cutscene is "
             "removed at %08X. It belonged to the retail Bink path and graphics_playMovie sets that "
             "mode itself anyway, so nothing that needed it has lost it",
             (unsigned)call_site);
}
