/* mp_use_latch.c: the hull on the use latch's setter. The header carries the reasoning. */
#include "mp_use_latch.h"

#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The setter takes nothing and answers nothing: its whole body is one store, and it returns with
 * a plain ret, so it is called as a cdecl void function. */
typedef void(__cdecl *latch_use_fn_t)(void);

typedef struct use_latch {
    bool                   installed;
    detour_t               hull;
    mp_use_latch_held_fn_t held;
    uint32_t               passed;      /* calls the setter was allowed to make its store */
    uint32_t               swallowed;   /* calls on a client that reached no script */
} use_latch_t;

static use_latch_t latch;

static void __cdecl hook_latch_use(void)
{
    latch_use_fn_t original = (latch_use_fn_t)latch.hull.original;

    /* Asked at every call. The setter runs once per substep while the key is held, which is a
     * human rate, and the answer has to follow the session: a hull has no uninstall, so a flag
     * fixed when it went in would outlive the session it was meant for. */
    if (latch.held != NULL && latch.held()) {
        ++latch.swallowed;
        return;
    }
    ++latch.passed;
    original();
}

void mp_use_latch_set_held_source(mp_use_latch_held_fn_t held)
{
    latch.held = held;
}

bool mp_use_latch_install(void)
{
    uintptr_t site;

    if (latch.installed) {
        return true;
    }
    site = mp_signatures_address(MP_SITE_ENEMY_LATCH_USE);
    if (site == 0u) {
        log_warning("the use latch cannot be held: its setter did not resolve, so a client's use "
                    "press still reaches the scripts that run on it");
        return false;
    }
    /* The whole function is fifteen bytes:
     *
     *     00433CEF  55                               push ebp
     *     00433CF0  8B EC                            mov  ebp, esp
     *     00433CF2  C7 05 AC 4D 6C 00 03 00 00 00    mov  dword [0x6C4DAC], 3
     *     00433CFC  5D                               pop  ebp
     *     00433CFD  C3                               ret
     *
     * A jump needs five bytes and the first instruction boundary at or past five is thirteen, the
     * frame and the whole store. The store has an absolute operand and no relative branch, which is
     * why the trampoline can carry it; on the host the trampoline is what stores the 3. The
     * declared length ends on an instruction boundary, decoded offline against every image. The
     * pattern reaches past the function's end into its neighbour at 0x00433CFE so that the second
     * stage of the resolver still has a tail to match once our own branch sits over the head. The
     * two cells read out of that pattern, the latch at +0x05 and the dialog confirm at +0x16,
     * resolve at startup before any session is armed, so they are read from an untouched head. */
    if (!detour_install(&latch.hull, site, (const void *)&hook_latch_use,
                        mp_signatures_prologue(MP_SITE_ENEMY_LATCH_USE))) {
        log_warning("the use latch cannot be held: its setter at %08X would not take a hull",
                    (unsigned)site);
        return false;
    }
    latch.installed = true;
    log_info("the use latch is held: its setter at %08X is hulled, and on a joined client a use "
             "press reaches no script here", (unsigned)site);
    return true;
}

void mp_use_latch_report(void)
{
    if (!latch.installed) {
        log_info("  the use latch: NOT HELD, so a client's use press reaches the scripts that run "
                 "on it");
        return;
    }
    log_info("  the use latch: %u call(s) passed through to the scripts, %u swallowed on this "
             "client so that no script here could be asked to talk (the setter is called once per "
             "substep while the key is held, so one press is a few calls)",
             (unsigned)latch.passed, (unsigned)latch.swallowed);
}
