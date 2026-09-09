/* frame_clock.c: see frame_clock.h. */
#include "frame_clock.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stddef.h>
#include <stdint.h>

static const float *frame_delta;

bool frame_clock_install(void)
{
    uintptr_t site    = frame_hook_site();
    uint32_t  address = 0;

    frame_delta = NULL;
    if (site == 0) {
        return false;
    }

    /* Read out of the operand rather than embedded, so the cell is found again after a forced
     * relocation and after any patch that moves a nearby immediate. */
    if (!memory_read_u32(site + FRAME_HOOK_FRAME_DELTA_OPERAND_OFFSET, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        log_warning("g_frameDelta would be at %08X, outside the image", (unsigned)address);
        return false;
    }

    frame_delta = (const float *)(uintptr_t)address;
    return true;
}

float frame_clock_seconds(void)
{
    return (frame_delta != NULL) ? *frame_delta : 0.0f;
}
