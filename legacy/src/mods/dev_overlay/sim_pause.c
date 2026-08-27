/* sim_pause.c: see sim_pause.h. */
#include "sim_pause.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x0043EA13  the simulation gate inside sys_frame. A DATA SITE ONLY, never hooked --------- *
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
#define OFFSET_PAUSE_FLAG 11u

enum {
    SITE_SIM_GATE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("sim_gate", SIG_SIM_GATE, MASK_SIM_GATE)
};

typedef struct sim_pause_state {
    bool               resolved;
    bool               held;        /* whether THIS is the reason the flag is set */
    int32_t            restore;     /* what the flag held before this set it */
    volatile int32_t  *flag;
} sim_pause_state_t;

static sim_pause_state_t pause_state;

bool sim_pause_install(void)
{
    uint32_t address = 0;

    if (pause_state.resolved) {
        return true;
    }
    if (signature_resolve_table(sites, SITE_COUNT) != SITE_COUNT) {
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
    return true;
}

bool sim_pause_is_available(void)
{
    return pause_state.resolved;
}

void sim_pause_set(bool paused)
{
    if (!pause_state.resolved) {
        return;
    }
    if (paused == pause_state.held) {
        /* Already in that state. Returning here is what makes this safe to call every frame:
           without it, a second hold would remember the value this one just wrote and the release
           would restore a pause instead of lifting it. */
        return;
    }

    if (paused) {
        pause_state.restore = *pause_state.flag;
        *pause_state.flag = 1;
        pause_state.held = true;
    } else {
        /* Restored rather than zeroed, so opening the panel while the game is already paused for
           its own reasons cannot un-pause it on the way out. */
        *pause_state.flag = pause_state.restore;
        pause_state.held = false;
    }
}
