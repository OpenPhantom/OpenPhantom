/* input_freeze.c: the two functions the whole game reads its input through.
 *
 * ==============================================================================================
 * THE TWO SITES
 *
 * Retail WMAIN.EXE, 829,952 bytes, ImageBase 0x400000. Both open the same way and both already
 * know how to answer when there is nothing to report, which is what this borrows.
 *
 *   0048D38D  55 8B EC 83 EC 14        the axis read, answering a float
 *             83 3D C8 19 86 00 00     cmp [008619C8],0     ; the device layer is up
 *             74 09
 *             83 3D D8 9A 4B 00 00     cmp [004B9AD8],0     ; and enabled
 *             75 0B
 *             D9 05 EC 8D 4A 00        fld [004A8DEC]       ; NOT READY: answer this
 *             ...
 *             8B 45 08 6B C0 ..        mov eax,[ebp+8]; imul  ; otherwise index the axis table
 *
 *   0048D4A4  55 8B EC 83 EC 08        the delta read, answering an int
 *             83 3D C8 19 86 00 00     the same two tests
 *             74 09
 *             83 3D D8 9A 4B 00 00
 *             75 07
 *             33 C0                    NOT READY: answer zero
 *             E9 8A 00 00 00
 *             8B 45 08 6B C0 18        mov eax,[ebp+8]; imul eax,0x18  ; stride 0x18 per axis
 *
 * The early answers are the important part. Between the process starting and the first poll the
 * game already runs whole frames in which every axis reads as nothing, so answering that way is a
 * state the engine meets on its own and handles. Inventing a different one, a held key or a
 * centred stick, would be a value nothing downstream has ever been given.
 *
 * ==============================================================================================
 * THE POINTER IS NOT BUILT FROM THESE DELTAS
 *
 * It was, once, and it was not usable: a relative stream has no home position, it drifts, and its
 * speed is a number somebody has to guess. The panel takes the system cursor instead, which is
 * where the game's own menu pointer comes from. So these two only ever withhold.
 * ============================================================================================ */
#include "input_freeze.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x00450FD8, THE PLAYER'S OWN SUSPEND STATE, and this is the lock that actually works.
 *
 * The engine does not gate input with a flag. It reads the mouse inside the steering phase, and the
 * phases are dispatched from a table by the player's task; in a menu, a dialogue and a cutscene it
 * simply does not run them. That is the whole mechanism, and it is why the mod that owns mouse look
 * already stops turning the view when the phases go quiet: it watches for them.
 *
 * So the panel does what those three states do. The task switches on one field:
 *
 *     case 0:  idle, stay in the list and do nothing
 *     case 1:  running, call the phases          <- the game
 *
 * and the engine's own predicate says what suspended means:
 *
 *   00450FD8  55 8B EC                 push ebp; mov ebp,esp
 *   00450FDB  A1 20 52 4B 00           mov eax,[004B5220]      ; the player block
 *   00450FE0  83 78 04 00              cmp dword [eax+4],0     ; the module state
 *   00450FE4  75 07                    non zero: jump PAST the 1, so answer 0
 *   00450FE6  B8 01 00 00 00           zero: answer 1, which is what "suspended" means
 *   00450FEF  5D C3
 *
 * Setting that field to zero while the panel is open is therefore not an invention: it is the state
 * the engine itself calls suspended, reached the way its own suspend reaches it. The previous value
 * is remembered and put back, so a player who was mid jump resumes mid jump.
 *
 * What this deliberately does NOT do is call the engine's own player_suspend. That one refuses
 * unless the player is standing, puts the sabre away and makes the resume read the position back
 * off the model. All three are right for a cutscene and wrong for a cheat panel somebody opened in
 * mid air.
 * ============================================================================================ */
static const uint8_t SIG_IS_SUSPENDED[] = {
    0x55, 0x8B, 0xEC,
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the player block] */
    0x83, 0x78, 0x04, 0x00,                          /* cmp [eax+4],0              */
    0x75, 0x07,
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0xEB, 0x02,
    0x33, 0xC0,
    0x5D, 0xC3
};
static const uint8_t MSK_IS_SUSPENDED[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_IS_SUSPENDED) == sizeof(MSK_IS_SUSPENDED),
               "the is suspended pattern and its mask are different lengths");
#define OFFSET_PLAYER_BLOCK 4u
#define OFFSET_MODULE_STATE 4u

/* --- 0x0048D38D, thirty bytes. The three cells are masked and read back by nobody: what makes the
 * pattern unique is the shape of the two tests and the float it answers with. */
static const uint8_t SIG_READ_AXIS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14,              /* push ebp; mov ebp,esp; sub esp,0x14 */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [device up],0                   */
    0x74, 0x09,                                      /* jz  not ready                       */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [enabled],0                     */
    0x75, 0x0B,                                      /* jnz the real path                   */
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00               /* fld [nothing]                       */
};
static const uint8_t MSK_READ_AXIS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_READ_AXIS) == sizeof(MSK_READ_AXIS),
               "the read axis pattern and its mask are different lengths");

/* --- 0x0048D4A4, thirty seven bytes, ending at the stride that names it. */
static const uint8_t SIG_READ_DELTA[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,              /* push ebp; mov ebp,esp; sub esp,8    */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [device up],0                   */
    0x74, 0x09,                                      /* jz  not ready                       */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [enabled],0                     */
    0x75, 0x07,                                      /* jnz the real path                   */
    0x33, 0xC0,                                      /* xor eax,eax                         */
    0xE9, 0x00, 0x00, 0x00, 0x00,                    /* jmp out                             */
    0x8B, 0x45, 0x08,                                /* mov eax,[ebp+8]     the axis        */
    0x6B, 0xC0, 0x18                                 /* imul eax,eax,0x18   the stride      */
};
static const uint8_t MSK_READ_DELTA[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_READ_DELTA) == sizeof(MSK_READ_DELTA),
               "the read delta pattern and its mask are different lengths");

/* Both open `push ebp | mov ebp,esp | sub esp,N`, so the boundaries are 0, 1, 3 and 6. Six is the
 * first one at or past the five bytes a jump needs. */
#define READER_PROLOGUE 6u

typedef float (__cdecl *read_axis_fn_t)(int32_t axis);
typedef int32_t (__cdecl *read_delta_fn_t)(int32_t axis);

typedef struct input_freeze_state {
    bool            installed;
    bool            frozen;
    detour_t        axis_detour;
    detour_t        delta_detour;
    void *const volatile *player_block;   /* the engine's own pointer at the player            */
    int32_t         saved_module_state;
    bool            suspended;
    read_axis_fn_t  axis_original;
    read_delta_fn_t delta_original;
} input_freeze_state_t;

static input_freeze_state_t freeze_state;

/* ============================================================================================ */

static float __cdecl hook_read_axis(int32_t axis)
{
    if (freeze_state.frozen) {
        return 0.0f;
    }
    return freeze_state.axis_original(axis);
}

static int32_t __cdecl hook_read_delta(int32_t axis)
{
    if (freeze_state.frozen) {
        return 0;
    }
    return freeze_state.delta_original(axis);
}

/* ============================================================================================ */

static bool install_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
                        const void *hook, detour_t *detour, const char *what)
{
    uintptr_t site = signature_find_detour_target(bytes, mask, size, READER_PROLOGUE);

    if (site == 0) {
        log_warning("%s did not resolve", what);
        return false;
    }
    if (!detour_install(detour, site, hook, READER_PROLOGUE)) {
        log_warning("%s at %08X could not be detoured", what, (unsigned)site);
        return false;
    }
    log_info("%s hooked at %08X", what, (unsigned)site);
    return true;
}

bool input_freeze_install(void)
{
    bool axis;
    bool delta;

    if (freeze_state.installed) {
        return true;
    }

    {
        uintptr_t suspended = signature_find_unique(SIG_IS_SUSPENDED, MSK_IS_SUSPENDED,
                                                    sizeof SIG_IS_SUSPENDED);
        uint32_t  block = 0;

        if (suspended != 0 && memory_read_u32(suspended + OFFSET_PLAYER_BLOCK, &block) &&
            memory_is_inside_image(block, sizeof(void *))) {
            freeze_state.player_block = (void *const volatile *)(uintptr_t)block;
            log_info("while the panel is open the player's phases are stopped, which is what the "
                     "engine does for a menu and a cutscene and is the only thing that really "
                     "holds this game still (player block at %08X)", (unsigned)block);
        } else {
            log_warning("the player's own suspend state did not resolve, so the player will keep "
                        "moving while the panel is open");
        }
    }



    axis = install_one(SIG_READ_AXIS, MSK_READ_AXIS, sizeof SIG_READ_AXIS,
                       (const void *)&hook_read_axis, &freeze_state.axis_detour,
                       "the axis read");
    if (axis) {
        freeze_state.axis_original = (read_axis_fn_t)freeze_state.axis_detour.original;
    }

    delta = install_one(SIG_READ_DELTA, MSK_READ_DELTA, sizeof SIG_READ_DELTA,
                        (const void *)&hook_read_delta, &freeze_state.delta_detour,
                        "the axis delta read");
    if (delta) {
        freeze_state.delta_original = (read_delta_fn_t)freeze_state.delta_detour.original;
    }

    freeze_state.installed = axis || delta || freeze_state.player_block != NULL;
    return freeze_state.installed;
}

bool input_freeze_is_available(void)
{
    return freeze_state.installed;
}

/* Stops the player's phases from running, and puts the previous state back. Answers whether it did
 * anything, so the log can say which half of the freeze a session actually got. */
static void suspend_player(bool suspend)
{
    volatile int32_t *state;
    void             *block;

    if (freeze_state.player_block == NULL) {
        return;
    }
    block = *freeze_state.player_block;
    if (block == NULL) {
        return;                          /* refused rather than assumed; the cell is a pointer */
    }
    state = (volatile int32_t *)((uint8_t *)block + OFFSET_MODULE_STATE);

    if (suspend) {
        if (freeze_state.suspended) {
            return;
        }
        freeze_state.saved_module_state = *state;
        *state = 0;                      /* the engine's own idle: in the list, doing nothing */
        freeze_state.suspended = true;
        return;
    }
    if (!freeze_state.suspended) {
        return;
    }
    /* ONLY IF IT IS STILL THE ZERO WE WROTE.
     *
     * The engine has its own suspend, and a cutscene starting while the panel is open would save
     * our zero into its own slot and put that zero back afterwards. Writing our remembered value
     * over the top of that would resume a player the engine still believes it is holding. A level
     * ending, which re-initialises the whole block, has the same shape. So the restore only fires
     * while the field is untouched; otherwise whatever owns it now keeps it. */
    if (*state == 0) {
        *state = freeze_state.saved_module_state;
    }
    freeze_state.suspended = false;
}

void input_freeze_set(bool frozen)
{
    if (freeze_state.frozen == frozen) {
        return;
    }
    freeze_state.frozen = frozen;
    suspend_player(frozen);
}

