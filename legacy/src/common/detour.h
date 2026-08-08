/* detour.h: inline detours that SEVERAL INDEPENDENT DLLs may place on the same function.
 *
 * ==============================================================================================
 * Why this is not the usual trampoline detour
 *
 * The features are separate DLLs that do not know about each other, and more than one of them
 * legitimately wants the same engine function. render_frameEnd alone is wanted by the frame-rate
 * fix, the FOV menu, the resolution fix and the cell watchdog; rdCamera_BuildProjection is wanted
 * by both the FOV fix and the view-distance fix.
 *
 * A naive second detour on an already-detoured target copies the first detour's `E9 rel32` into
 * its own trampoline and produces an INFINITE LOOP that reports success. So:
 *
 *   FIRST INSTALLER  the target is untouched. Copy `prologue_size` bytes into a freshly allocated
 *                    trampoline, append `jmp target + prologue_size`, then overwrite the prologue
 *                    with `jmp hook` padded with NOPs to an instruction boundary.
 *                    `original` = the trampoline.
 *
 *   LATER INSTALLER  the target already begins with 0xE9. Do NOT copy any bytes, the originals
 *                    are gone. Read the existing displacement, keep that address as `original`,
 *                    and rewrite the displacement to point at the new hook.
 *                    `original` = the PREVIOUS HOOK.
 *
 * Each hook calls `original` exactly as if it were the real function, so the chain unwinds
 * correctly whichever order the DLLs happened to load in:
 *
 *     hook_C -> hook_B -> hook_A -> trampoline -> engine
 *
 * ==============================================================================================
 * What this does not do, stated plainly
 *
 *   * NO UNINSTALL. Removing a link from the middle of a chain would require knowing who points
 *     at us, and nothing here does. Feature DLLs are loaded once by the mod loader and are never
 *     freed, so the pointers stay valid for the life of the process.
 *   * NO INSTRUCTION DECODING. `prologue_size` must be an exact instruction boundary and the
 *     copied bytes must contain no rip- or rel-relative operand. Every target used in this
 *     project is a plain MSVC `push ebp; mov ebp,esp; sub esp,imm` prologue whose exact byte
 *     sequence is part of the signature that found it, so the boundary is checked, not assumed.
 *   * it chains onto foreign hooks too. If a graphics wrapper detoured the function before us,
 *     the 0xE9 branch treats that wrapper's hook as `original`, which is the correct behaviour.
 */
#ifndef COMMON_DETOUR_H
#define COMMON_DETOUR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct detour {
    uintptr_t target;
    void     *original;       /* call this to reach the previous behaviour; never NULL if installed */
    size_t    prologue_size;  /* 0 when we chained onto an existing hook */
    bool      installed;
    bool      chained;        /* true = we linked in front of a hook that was already there */
} detour_t;

/* `prologue_size` must be 5..16 and land on an instruction boundary.
 * Returns false and leaves the target untouched on any failure. */
bool detour_install(detour_t *detour, uintptr_t target, const void *hook, size_t prologue_size);

#endif /* COMMON_DETOUR_H */
