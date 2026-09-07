/* pad_run.c: see pad_run.h. */
#include "pad_run.h"

#include "pad_stick.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdint.h>


/* --- 0x004653CE  control_isHeld(functionId, out) -------------------------------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 EC 1C              sub esp,0x1C
 *   C7 45 F8 00000000     mov [ebp-8],0
 *   83 7D 0C 00           cmp [ebp+0xC],0
 *   74 09                 je  +9
 *   8B 45 0C              mov eax,[ebp+0xC]
 *   C7 00 00000000        mov [eax],0
 *
 * The whole run is literal, with no address or displacement in it, so it needs no mask. Twenty
 * eight bytes was the length at which it became unique in the image; the prologue this project
 * detours is the first six, which is a clean instruction boundary and contains nothing relative. */
static const uint8_t SIG_CONTROL_IS_HELD[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0xEC, 0x1C,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x7D, 0x0C, 0x00,
    0x74, 0x09,
    0x8B, 0x45, 0x0C,
    0xC7, 0x00, 0x00, 0x00, 0x00, 0x00
};
#define CONTROL_IS_HELD_PROLOGUE 6u

/* The engine's own action id for Run, from j0nny's control table. */
#define ACTION_RUN 7

typedef uint32_t(__cdecl *is_held_fn_t)(int action, uint32_t *out);

static detour_t run_detour;

static uint32_t __cdecl hook_is_held(int action, uint32_t *out)
{
    is_held_fn_t original = (is_held_fn_t)run_detour.original;

    /* ONLY EVER ADDS, never subtracts. If the original says the action is held it is held, whatever
     * the stick is doing; this can only turn a no into a yes, and only for Run. That is what keeps
     * a bound Run key working exactly as it always did, and what makes this safe on the many other
     * actions that come through here every frame. */
    if (action == ACTION_RUN && pad_stick_wants_run()) {
        if (out != NULL) {
            *out = 0;    /* the engine zeroes this itself on the held path; match it rather than
                          * leave a caller's stack cell untouched */
        }
        return 1u;
    }
    return original(action, out);
}

bool pad_run_install(void)
{
    uintptr_t target = signature_find_detour_target(SIG_CONTROL_IS_HELD, NULL,
                                                    sizeof SIG_CONTROL_IS_HELD,
                                                    CONTROL_IS_HELD_PROLOGUE);

    if (target == 0) {
        log_warning("the Run action query was not found, so the left stick cannot break into a "
                    "run: the stick still steers and still carries a magnitude, but the gait stays "
                    "whatever the Run key gives it");
        return false;
    }
    if (!detour_install(&run_detour, target, (const void *)hook_is_held,
                        CONTROL_IS_HELD_PROLOGUE)) {
        log_warning("the Run action query at %08X could not be detoured, so the left stick cannot "
                    "break into a run", (unsigned)target);
        return false;
    }

    log_info("walk and run are on the left stick: a light push walks, a full one runs. Running is "
             "a BUTTON in this engine rather than a speed, so this answers that button rather than "
             "writing a speed. The clips play at a fixed rate with nothing scaling them by how "
             "fast the body is really moving, so the gait is two steps and not a slide: a "
             "variable pace would slide the feet along the ground. A bound Run key still works "
             "and still wins");
    return true;
}
