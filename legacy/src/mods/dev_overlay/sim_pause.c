/* sim_pause.c: see sim_pause.h. */
#include "sim_pause.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x0043EA13  the simulation gate inside sys_frame. A data site only, never hooked --------- *
 * Both compared addresses are wildcarded and the second is read out of its operand rather than
 * written down. The pattern runs on through the jump and the push so it is anchored to this gate
 * rather than to any pair of compares: counted against the retail executable, 829,952 bytes, MD5
 * 7c5af8428c19b17cca09ae3a49bd10ef, it matches once. */
static const uint8_t SIG_SIM_GATE[] = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x13, 0x83, 0x3D, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x75, 0x0A, 0x6A, 0x00, 0xE8
};
static const uint8_t MASK_SIM_GATE[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SIM_GATE == sizeof MASK_SIM_GATE,
               "the simulation gate pattern and its mask are different lengths");
#define OFFSET_PAUSE_FLAG 11u

/* --- 0x0041100A and 0x00411019  bapobj_drawResume and bapobj_drawPause, a data site --------- *
 * The two one-line writers of the draw latch sit back to back in the image, each a prologue, one
 * store and a return, and only their stored value differs. Together they are one pattern that
 * matches once; the address is read out of both stores and the two must agree. */
static const uint8_t SIG_DRAW_LATCH[] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xC3,
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5D, 0xC3
};
static const uint8_t MASK_DRAW_LATCH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DRAW_LATCH == sizeof MASK_DRAW_LATCH,
               "the draw latch pattern and its mask are different lengths");
#define OFFSET_DRAW_LATCH_RESUME 5u
#define OFFSET_DRAW_LATCH_PAUSE  20u

enum {
    SITE_SIM_GATE,
    SITE_DRAW_LATCH,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("sim_gate", SIG_SIM_GATE, MASK_SIM_GATE),
    SIGNATURE_ENTRY_MASKED("draw_latch", SIG_DRAW_LATCH, MASK_DRAW_LATCH)
};

typedef struct sim_pause_state {
    bool               resolved;
    uint32_t           holders;     /* bitmask of sim_pause_holder_t; paused while any is set */
    bool               let_run;     /* the holders are kept but the cell says run */
    bool               freeze;      /* a pause also holds the draw latch */

    bool               paused;      /* what this has written: the flag holds this pause's 1 */
    int32_t            restore;     /* what the flag held before it was written */
    volatile int32_t  *flag;

    bool               frozen;      /* the latch holds this pause's 1 */
    int32_t            latch_restore;
    volatile int32_t  *latch;       /* NULL when its site did not resolve */
} sim_pause_state_t;

static sim_pause_state_t pause_state;

/* Every write to either cell goes through here, from whatever changed. A cell is written on the
 * edge only: the value underneath is captured when this takes it and put back when it lets go,
 * so taking the pause while the game is already paused for its own reasons cannot un-pause it on
 * the way out, and a change that makes no difference writes nothing. */
static void apply(void)
{
    bool paused = pause_state.holders != 0u && !pause_state.let_run;
    bool frozen = paused && pause_state.freeze && pause_state.latch != NULL;

    if (paused != pause_state.paused) {
        pause_state.paused = paused;
        if (paused) {
            pause_state.restore = *pause_state.flag;
            *pause_state.flag = 1;
        } else {
            *pause_state.flag = pause_state.restore;
        }
    }
    if (frozen != pause_state.frozen) {
        pause_state.frozen = frozen;
        if (frozen) {
            pause_state.latch_restore = *pause_state.latch;
            *pause_state.latch = 1;
        } else {
            *pause_state.latch = pause_state.latch_restore;
        }
    }
}

static void resolve_draw_latch(void)
{
    uint32_t resume = 0;
    uint32_t pause = 0;

    if (sites[SITE_DRAW_LATCH].address == 0) {
        log_warning("  the draw latch pair did not resolve, so a pause leaves the animations "
                    "running on the spot");
        return;
    }
    if (!memory_read_u32(sites[SITE_DRAW_LATCH].address + OFFSET_DRAW_LATCH_RESUME, &resume) ||
        !memory_read_u32(sites[SITE_DRAW_LATCH].address + OFFSET_DRAW_LATCH_PAUSE, &pause) ||
        resume != pause || !memory_is_inside_image(resume, sizeof(int32_t))) {
        log_warning("  the draw latch pair stores to %08X and %08X, not one cell inside the "
                    "image, so a pause leaves the animations running on the spot",
                    (unsigned)resume, (unsigned)pause);
        return;
    }
    pause_state.latch = (volatile int32_t *)(uintptr_t)resume;
    log_info("  the animations pause with the simulation on the engine's own draw latch at "
             "%08X, the one its pause menu uses", (unsigned)resume);
}

bool sim_pause_install(void)
{
    uint32_t address = 0;

    if (pause_state.resolved) {
        return true;
    }
    (void)signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_SIM_GATE].address == 0) {
        log_warning("  simulation gate did not resolve, the panel will not pause the game");
        return false;
    }
    if (!memory_read_u32(sites[SITE_SIM_GATE].address + OFFSET_PAUSE_FLAG, &address) ||
        !memory_is_inside_image(address, sizeof(int32_t))) {
        log_warning("  pause flag read as %08X, outside the image, the panel will not pause the "
                    "game", (unsigned)address);
        return false;
    }

    pause_state.flag = (volatile int32_t *)(uintptr_t)address;
    pause_state.resolved = true;
    log_info("  simulation pause armed on the engine's own flag at %08X", (unsigned)address);
    resolve_draw_latch();
    return true;
}

bool sim_pause_is_available(void)
{
    return pause_state.resolved;
}

void sim_pause_hold(sim_pause_holder_t who, bool held)
{
    if (!pause_state.resolved) {
        return;
    }
    if (held) {
        pause_state.holders |= (uint32_t)who;
    } else {
        pause_state.holders &= ~(uint32_t)who;
    }
    apply();
}

void sim_pause_let_run(bool run)
{
    if (!pause_state.resolved) {
        return;
    }
    pause_state.let_run = run;
    apply();
}

void sim_pause_set_freeze_animation(bool freeze)
{
    pause_state.freeze = freeze;
    if (pause_state.resolved) {
        apply();
    }
}

bool sim_pause_freeze_animation(void)
{
    return pause_state.freeze;
}

bool sim_pause_freeze_animation_is_available(void)
{
    return pause_state.latch != NULL;
}
