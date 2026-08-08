/* frame_hook.h: "call me once per rendered frame", without every feature re-deriving the site.
 *
 * render_frameEnd 0x0046C139 sits inside sys_frame, which is what every game loop AND every
 * blocking menu loop calls. It is therefore the only place guaranteed to run in both, and a
 * menu that blocks in its own `while (result < 0) { sys_frame(); }` is exactly where a live
 * slider preview has to work.
 *
 * Each feature DLL links its own copy of this module and installs its own detour. Two DLLs on the
 * same target are fine: common/detour.c chains them, so the callbacks of both run, in whichever
 * order the DLLs happened to load. Nothing here is shared between DLLs at run time.
 *
 * Callbacks must be cheap and must not log every frame.
 */
#ifndef COMMON_FRAME_HOOK_H
#define COMMON_FRAME_HOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*frame_hook_callback_t)(void);

/* Installs the detour on the first call and appends `callback`. Idempotent per callback.
 * Returns false when the site did not resolve or the detour failed, the caller must then say
 * so in its log and fall back to whatever it can still do without a per-frame tick. */
bool frame_hook_add(frame_hook_callback_t callback);

/* True once the detour stands. Lets a feature word its fallback message accurately. */
bool frame_hook_is_installed(void);

/* The address of render_frameEnd, or 0. Exposed because g_frameDelta is the first thing that
 * function reads and a feature that needs the frame time reads it out of this site's operand. */
uintptr_t frame_hook_site(void);

/* Offset of the absolute operand of `mov eax,[g_frameDelta]` inside render_frameEnd. */
#define FRAME_HOOK_FRAME_DELTA_OPERAND_OFFSET 0x0Au

#endif /* COMMON_FRAME_HOOK_H */
