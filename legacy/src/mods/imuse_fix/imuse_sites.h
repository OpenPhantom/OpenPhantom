/* imuse_sites.h: the four things inside iMUSE.DLL that say whether its heartbeat is alive.
 *
 * This is a SECOND module, not the host image, so none of the shared signature machinery applies:
 * common/signature.c scans the host's .text by design. Everything here is scoped to the DLL's own
 * mapped image, whose base comes from GetModuleHandle and whose .text range is parsed out of its
 * headers at run time. iMUSE.DLL carries a .reloc section, so it may load anywhere and not one
 * address below is written down.
 */
#ifndef IMUSE_FIX_IMUSE_SITES_H
#define IMUSE_FIX_IMUSE_SITES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct imuse_sites {
    uintptr_t module_base;

    /* The counter that gates the whole script engine. ImHeartbeat runs only while it is 0.
     * Incremented and decremented WITHOUT any interlocked instruction, from two threads. */
    volatile int32_t *gate;

    /* Bumped once by the multimedia-timer callback for every time it CALLED the heartbeat.
     * It says the Windows timer is alive; it does NOT say the heartbeat did any work. */
    volatile int32_t *heartbeat_ticks;

    /* The millisecond stamp of the last heartbeat that actually ran its BODY. This is the cell
     * that separates the two failures from each other: the callback can keep firing while the
     * body is turned away at one of its four gates, and only this stamp shows it. */
    volatile int32_t *heartbeat_last_ms;

    /* The heartbeat's own re-entrancy flag, set before the body and cleared after it. Stuck at 1
     * means the body never returned, a different fault from a stuck gate, and worth telling
     * apart rather than lumping together. */
    volatile int32_t *heartbeat_reentry;

    /* The exported cue setters, taken from the export table rather than from a pattern. */
    int32_t (__cdecl *set_state)(int32_t cue);
    int32_t (__cdecl *set_sequence)(int32_t cue);

    /* The two halves of the broken lock, and the function that forgets to release it.
     *
     * ImLock and ImUnlock are not exported and come from a pattern. ImSetParam IS exported, but
     * the export is a thin forwarder and the body is what matters: the internal callers, the
     * fade tick above all, reach the body directly, so hooking the export would miss exactly
     * the traffic worth guarding. */
    uintptr_t im_lock;
    uintptr_t im_unlock;
    uintptr_t set_param_body;

    /* iMUSE writes its own running commentary through ONE host-supplied function pointer, which
     * the game hands it at startup and which it calls with a finished string. The cell holding
     * that pointer is read out of ImPrintf's own body, so taking a copy of the commentary costs
     * one dword and leaves the game's own handling in place. 59 call sites feed it. */
    void (__cdecl **trace_slot)(const char *text);
} imuse_sites_t;

/* Returns false when iMUSE.DLL is not loaded or the two cells cannot be resolved and
 * cross-checked. Logs one line per finding, and names which check failed. */
bool imuse_sites_resolve(imuse_sites_t *out);

#endif /* IMUSE_FIX_IMUSE_SITES_H */
