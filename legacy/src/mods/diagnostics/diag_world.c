#include "diag_world.h"

#include "diag_install.h"
#include "diag_log.h"
#include "diag_names.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- bapmap_openMover 0x00408B50 -------------------------------------------------------------- *
 * The open command. It fires EVERY FRAME while a body stands on a pressure plate
 * (bapmap_firePlate has no rising edge), which is why the hook snapshots the mover BEFORE and
 * AFTER the call and stays quiet when nothing changed.
 *   world+0x620 = numMovers, world+0x624 = ppMover[] (an INLINE array, not a pointer to one)
 *   mover+0x00 = active, +0x04 = type, +0x08 = id, +0x2C = pose, +0x34 = dir */
static const uint8_t SIG_MOVER_OPEN[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x14,
    0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45
};
#define MOVER_OPEN_PROLOGUE 6u

/* --- bapmap_closeMover 0x00408DF5 ------------------------------------------------------------- *
 * "Close" is the SCRIPT's word for it; what happens is `dir = 2`, and what that means depends on
 * the type (type 1 freezes, types 2/6 begin the open dwell, types 3/4/5 latch for good). */
static const uint8_t SIG_MOVER_CLOSE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x14, 0x83, 0x7D,
    0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x0C, 0x3B, 0x88,
    0x20, 0x06, 0x00, 0x00
};
#define MOVER_CLOSE_PROLOGUE 8u

/* --- bapmap_tickMover 0x00409170 -------------------------------------------------------------- *
 * The integrator, once per active mover and frame. It is the only place a mover advances by
 * itself (a door closing after its dwell, a one-shot latching, a push button entering its wait
 * phase). Level 2, debounced on `dir`. */
static const uint8_t SIG_MOVER_TICK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x83, 0x3D, 0xCC, 0x5F, 0x5B, 0x00,
    0x00, 0x0F, 0x85, 0xC7, 0x04, 0x00, 0x00, 0x8B
};
#define MOVER_TICK_PROLOGUE 6u

/* --- ai_setMode 0x004335A5 / ai_returnMode 0x00433634 ----------------------------------------- *
 * Opcode 0x400 "Set AI Mode" = PUSH, opcode 0x401 "Return Mode" = POP of the FOUR-DEEP stack,
 * the misreading "jump to state 0" cost 348 sites across 11 levels. Both are EDGES, not
 * per-frame traffic.
 *   character: +0x04 name[12], +0x18 enmyIndex, +0x20 state, +0x7C aiMode
 *              +0x80/+0x84/+0x88 the three history slots, +0x8C modeTicks */
static const uint8_t SIG_AI_SET_MODE[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x7C, 0x3B, 0x4D, 0x0C,
    0x74, 0x72, 0x8B, 0x55, 0x08, 0x8B, 0x45, 0x08
};
#define AI_SET_MODE_PROLOGUE 6u
static const uint8_t SIG_AI_RETURN_MODE[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x08, 0x8B, 0x91, 0x80,
    0x00, 0x00, 0x00, 0x89, 0x50, 0x7C, 0x8B, 0x45
};
#define AI_RETURN_MODE_PROLOGUE 6u

/* --- ai_dispatch 0x00433E51 ------------------------------------------------------------------- *
 * The only site in the whole DLL that does not sit on a function entry. The opcode dispatcher was
 * inlined by MSVC INTO ai_run (0x433D0B); it has no symbol and no frame of its own. There is
 * therefore no way to observe "which opcode is running" with an ordinary detour. What follows is a
 * detour INTO THE MIDDLE of a function, and that is deliberately tied to three conditions:
 *
 *   (1) the pattern is the proof. The two stolen instructions
 *         0F BF 4D E4          movsx ecx, word ptr [ebp-0x1C]   ; the resolved opcode
 *         89 8D 70 FF FF FF    mov  [ebp-0x90], ecx
 *       encode the ebp offsets the hook reads. If the pattern matches, the offsets are proven; if
 *       it does not, the hook is off. Nothing is assumed.
 *   (2) 10 bytes, an exact instruction boundary, and a linear disassembly of the WHOLE .text
 *       (three phase offsets) finds NO branch targeting into those 10 bytes.
 *   (3) The hook is `naked` and saves pushad/pushfd AND the complete x87 state (fnsave/frstor,
 *       108 bytes), at this point the engine is in the middle of a frame, and the logger uses
 *       the CRT.
 *
 * Off by default. At about 36 actors and about 10 opcodes per tick it costs roughly 11,000 calls
 * per second; the formatting is the expensive part, not the detour. */
static const uint8_t SIG_AI_OPCODE[] = {
    0x0F, 0xBF, 0x4D, 0xE4, 0x89, 0x8D, 0x70, 0xFF, 0xFF, 0xFF, 0x81, 0xBD,
    0x70, 0xFF, 0xFF, 0xFF, 0x80, 0x01, 0x00, 0x00
};
#define AI_OPCODE_PROLOGUE 10u

enum {
    SITE_MOVER_OPEN,
    SITE_MOVER_CLOSE,
    SITE_MOVER_TICK,
    SITE_AI_SET_MODE,
    SITE_AI_RETURN_MODE,
    SITE_AI_OPCODE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("mover_open",     SIG_MOVER_OPEN),
    SIGNATURE_ENTRY("mover_close",    SIG_MOVER_CLOSE),
    SIGNATURE_ENTRY("mover_tick",     SIG_MOVER_TICK),
    SIGNATURE_ENTRY("ai_set_mode",    SIG_AI_SET_MODE),
    SIGNATURE_ENTRY("ai_return_mode", SIG_AI_RETURN_MODE),
    SIGNATURE_ENTRY("ai_opcode",      SIG_AI_OPCODE)
};

#define WORLD_MOVER_COUNT 0x620
#define WORLD_MOVER_ARRAY 0x624
#define MOVER_ACTIVE      0x00
#define MOVER_TYPE        0x04
#define MOVER_ID          0x08
#define MOVER_POSE        0x2C
#define MOVER_DIRECTION   0x34

#define CHARACTER_NAME       0x04   /* char[12] */
#define CHARACTER_ENMY_INDEX 0x18
#define CHARACTER_STATE      0x20
#define CHARACTER_AI_MODE    0x7C

typedef void (__cdecl *mover_command_fn_t)(void *world, int32_t index);
typedef void (__cdecl *mover_tick_fn_t)(void *mover, float now);
typedef void (__cdecl *ai_set_mode_fn_t)(void *actor, int32_t new_mode);
typedef void (__cdecl *ai_return_mode_fn_t)(void *actor);

typedef struct diag_world_state {
    bool     sites_resolved;
    detour_t mover_open;
    detour_t mover_close;
    detour_t mover_tick;
    detour_t ai_set_mode;
    detour_t ai_return_mode;
    detour_t ai_opcode;
} diag_world_state_t;

static diag_world_state_t world_state;

/* The opcode hook keeps its trampoline in a file-scope pointer because its only caller is inline
 * assembly, which cannot reach into a structure member by name. */
static void *opcode_trampoline;

static void resolve_sites_once(void)
{
    if (world_state.sites_resolved) {
        return;
    }
    world_state.sites_resolved = true;
    signature_resolve_table(sites, SITE_COUNT);
}

/* ============================================================================================ */
static uint8_t *mover_at(void *world, int32_t index)
{
    uint8_t *record = (uint8_t *)world;

    if (record == NULL || index < 0 || index >= *(const int32_t *)(record + WORLD_MOVER_COUNT)) {
        return NULL;
    }
    return *(uint8_t **)(record + WORLD_MOVER_ARRAY + index * 4);
}

static void snapshot_mover(const uint8_t *mover, int32_t *direction, int32_t *active, float *pose)
{
    *direction = (mover != NULL) ? *(const int32_t *)(mover + MOVER_DIRECTION) : -1;
    *active    = (mover != NULL) ? *(const int32_t *)(mover + MOVER_ACTIVE) : -1;
    *pose      = (mover != NULL) ? *(const float *)(mover + MOVER_POSE) : 0.0f;
}

static void __cdecl hook_mover_open(void *world, int32_t index)
{
    mover_command_fn_t original = (mover_command_fn_t)world_state.mover_open.original;
    uint8_t *mover = mover_at(world, index);
    int32_t  direction_before;
    int32_t  active_before;
    int32_t  direction_after;
    int32_t  active_after;
    float    pose_before;
    float    pose_after;

    snapshot_mover(mover, &direction_before, &active_before, &pose_before);
    original(world, index);
    snapshot_mover(mover, &direction_after, &active_after, &pose_after);

    /* Pressure plates call this EVERY FRAME. Only the change is an event. */
    if (mover != NULL && (direction_before != direction_after || active_before != active_after)) {
        diag_log_write("trg  mover %d (%s) OPEN: dir %s -> %s, active %d -> %d, pose %.2f",
                       (int)*(const int32_t *)(mover + MOVER_ID),
                       diag_numbered_name(diag_mover_types,
                                          *(const int32_t *)(mover + MOVER_TYPE)),
                       diag_numbered_name(diag_mover_directions, direction_before),
                       diag_numbered_name(diag_mover_directions, direction_after),
                       (int)active_before, (int)active_after, (double)pose_after);
    } else if (mover == NULL) {
        diag_log_write("trg  mover %d OPEN: index outside the mover table, engine ignores it",
                       (int)index);
    }
}

static void __cdecl hook_mover_close(void *world, int32_t index)
{
    mover_command_fn_t original = (mover_command_fn_t)world_state.mover_close.original;
    uint8_t *mover = mover_at(world, index);
    int32_t  direction_before;
    int32_t  active_before;
    int32_t  direction_after;
    int32_t  active_after;
    float    pose_before;
    float    pose_after;

    snapshot_mover(mover, &direction_before, &active_before, &pose_before);
    original(world, index);
    snapshot_mover(mover, &direction_after, &active_after, &pose_after);

    if (mover != NULL && direction_before != direction_after) {
        diag_log_write("trg  mover %d (%s) CLOSE: dir %s -> %s, pose %.2f",
                       (int)*(const int32_t *)(mover + MOVER_ID),
                       diag_numbered_name(diag_mover_types,
                                          *(const int32_t *)(mover + MOVER_TYPE)),
                       diag_numbered_name(diag_mover_directions, direction_before),
                       diag_numbered_name(diag_mover_directions, direction_after),
                       (double)pose_after);
    }
}

static void __cdecl hook_mover_tick(void *mover, float now)
{
    mover_tick_fn_t original = (mover_tick_fn_t)world_state.mover_tick.original;
    uint8_t *record = (uint8_t *)mover;
    int32_t  direction_before = (record != NULL)
                              ? *(const int32_t *)(record + MOVER_DIRECTION) : -1;

    original(mover, now);

    if (record != NULL && *(const int32_t *)(record + MOVER_DIRECTION) != direction_before) {
        diag_log_write("trg  mover %d (%s) advances: dir %s -> %s, pose %.2f",
                       (int)*(const int32_t *)(record + MOVER_ID),
                       diag_numbered_name(diag_mover_types,
                                          *(const int32_t *)(record + MOVER_TYPE)),
                       diag_numbered_name(diag_mover_directions, direction_before),
                       diag_numbered_name(diag_mover_directions,
                                          *(const int32_t *)(record + MOVER_DIRECTION)),
                       (double)*(const float *)(record + MOVER_POSE));
    }
}

/* ============================================================================================ */
static const char *actor_tag(const void *actor)
{
    static char buffers[2][64];
    static int  turn;
    const uint8_t *record = (const uint8_t *)actor;
    char          *buffer;

    turn = (turn + 1) & 1;
    buffer = buffers[turn];

    if (record == NULL) {
        _snprintf(buffer, sizeof(buffers[0]), "%s", "(null)");
        buffer[sizeof(buffers[0]) - 1] = '\0';
        return buffer;
    }
    _snprintf(buffer, sizeof(buffers[0]), "#%d \"%s\" [%s]",
              (int)*(const int32_t *)(record + CHARACTER_ENMY_INDEX),
              diag_safe_string((const char *)(record + CHARACTER_NAME), 12),
              diag_numbered_name(diag_enemy_states,
                                 *(const int32_t *)(record + CHARACTER_STATE)));
    buffer[sizeof(buffers[0]) - 1] = '\0';
    return buffer;
}

static void __cdecl hook_ai_set_mode(void *actor, int32_t new_mode)
{
    ai_set_mode_fn_t original = (ai_set_mode_fn_t)world_state.ai_set_mode.original;
    int32_t old_mode = (actor != NULL)
                     ? *(const int32_t *)((const uint8_t *)actor + CHARACTER_AI_MODE) : -1;

    original(actor, new_mode);
    if (actor == NULL) {
        return;
    }

    if (old_mode == new_mode) {
        diag_log_write("fsm  %s SetMode %d == the current mode -> only the mode timer is zeroed",
                       actor_tag(actor), (int)new_mode);
    } else {
        diag_log_write("fsm  %s SetMode %d -> %d  (PUSH onto the 4-deep stack)",
                       actor_tag(actor), (int)old_mode, (int)new_mode);
    }
}

static void __cdecl hook_ai_return_mode(void *actor)
{
    ai_return_mode_fn_t original = (ai_return_mode_fn_t)world_state.ai_return_mode.original;
    int32_t old_mode = (actor != NULL)
                     ? *(const int32_t *)((const uint8_t *)actor + CHARACTER_AI_MODE) : -1;

    original(actor);
    if (actor != NULL) {
        diag_log_write("fsm  %s ReturnMode %d -> %d  (POP; the floor refills from the start mode)",
                       actor_tag(actor), (int)old_mode,
                       (int)*(const int32_t *)((const uint8_t *)actor + CHARACTER_AI_MODE));
    }
}

/* External linkage so the optimiser cannot remove it, its only caller is inline assembly. */
void __stdcall diag_on_ai_opcode(int opcode, void *actor)
{
    const char *name = diag_name_of(diag_fsm_opcodes, (int32_t)opcode);

    diag_log_write("fsm  %s op %03X %s", actor_tag(actor), (unsigned)opcode,
                   (name != NULL) ? name : "(no handler)");
}

/* NAKED. Saves the GP registers, the flags and the complete x87 state, the detour sits in the
 * middle of ai_run, ebp points at ITS frame, and the logger uses the CRT. fnsave reinitialises
 * the FPU as a side effect; frstor restores stack AND control word. */
static __declspec(naked) void hook_ai_opcode(void)
{
    __asm {
        pushad
        pushfd
        sub     esp, 112
        fnsave  [esp]
        movsx   eax, word ptr [ebp - 01Ch]     /* the resolved opcode, offset proven by the pattern */
        mov     ecx, [ebp + 8]                 /* ai_run(character *actor)                            */
        push    ecx
        push    eax
        call    diag_on_ai_opcode              /* __stdcall: cleans up after itself                   */
        frstor  [esp]
        add     esp, 112
        popfd
        popad
        jmp     dword ptr [opcode_trampoline]
    }
}

/* ============================================================================================ */
int diag_trigger_install(int trigger_level)
{
    int installed = 0;

    if (trigger_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    installed += diag_install_observer(sites, SITE_MOVER_OPEN, &world_state.mover_open,
                                       (const void *)hook_mover_open, MOVER_OPEN_PROLOGUE,
                                       "mover OPEN - debounced, because pressure plates call it "
                                       "every frame") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_MOVER_CLOSE, &world_state.mover_close,
                                       (const void *)hook_mover_close, MOVER_CLOSE_PROLOGUE,
                                       "mover CLOSE (the script command)") ? 1 : 0;
    if (trigger_level >= 2) {
        installed += diag_install_observer(sites, SITE_MOVER_TICK, &world_state.mover_tick,
                                           (const void *)hook_mover_tick, MOVER_TICK_PROLOGUE,
                                           "phase changes of the mover integrator (a door closing "
                                           "by itself and so on)") ? 1 : 0;
    }
    return installed;
}

int diag_fsm_install(int fsm_level)
{
    int installed = 0;

    if (fsm_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    installed += diag_install_observer(sites, SITE_AI_SET_MODE, &world_state.ai_set_mode,
                                       (const void *)hook_ai_set_mode, AI_SET_MODE_PROLOGUE,
                                       "opcode 0x400 Set AI Mode (PUSH)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_AI_RETURN_MODE, &world_state.ai_return_mode,
                                       (const void *)hook_ai_return_mode,
                                       AI_RETURN_MODE_PROLOGUE,
                                       "opcode 0x401 Return Mode (POP)") ? 1 : 0;

    if (fsm_level >= 2) {
        if (diag_install_observer(sites, SITE_AI_OPCODE, &world_state.ai_opcode,
                                  (const void *)hook_ai_opcode, AI_OPCODE_PROLOGUE,
                                  "EVERY executed opcode, a detour INTO ai_run, expensive, the "
                                  "x87 state is saved")) {
            opcode_trampoline = world_state.ai_opcode.original;
            ++installed;
        }
    }
    return installed;
}
