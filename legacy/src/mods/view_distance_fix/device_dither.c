/* device_dither.c: see device_dither.h. */
#include "device_dither.h"

#include "fog_regime.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stddef.h>
#include <stdint.h>

/* IDirect3DDevice3::SetRenderState, vtable byte offset 0x58. That is not a guess: the fog regime's
 * own signatures match `push 0x23; push dev; call [vtbl+0x58]` in the engine's state commit, where
 * 0x23 is D3DRENDERSTATE_FOGTABLEMODE, so the slot is pinned by code this project already reads. */
#define VTABLE_SET_RENDER_STATE (0x58u / sizeof(void *))

#define D3DRENDERSTATE_DITHERENABLE 26u

typedef long(__stdcall *set_render_state_fn_t)(void *device, uint32_t state, uint32_t value);

static bool  enabled;
static void *last_device;

void device_dither_configure(bool on)
{
    enabled = on;
}

void device_dither_on_frame(void)
{
    void                 *device;
    void                **vtable;
    set_render_state_fn_t set_render_state;

    if (!enabled) {
        return;
    }
    device = fog_regime_device();
    if (device == NULL) {
        return;                                /* not open yet, or this regime never resolved it */
    }
    if (device == last_device) {
        return;                                /* set once per device, see the header */
    }
    if (!memory_is_readable_range((uintptr_t)device, sizeof(void *))) {
        return;
    }
    vtable = *(void ***)device;
    if (!memory_is_readable_range((uintptr_t)vtable,
                                  (VTABLE_SET_RENDER_STATE + 1u) * sizeof(void *))) {
        return;
    }
    set_render_state = (set_render_state_fn_t)vtable[VTABLE_SET_RENDER_STATE];
    if (set_render_state == NULL ||
        !memory_is_executable_range((uintptr_t)set_render_state, 1)) {
        return;
    }

    (void)set_render_state(device, D3DRENDERSTATE_DITHERENABLE, 1u);
    last_device = device;

    log_info("dithering enabled on the device. The frame buffer is 16 bit, because the mode "
             "enumeration only accepts 16-bit RGB, so a full-screen fade multiplying the scene "
             "crosses a 5-bit quantisation boundary in visible jumps: a region of one flat colour "
             "steps as a block while lit geometry does not, and the boundary between them reads as "
             "a flashing line. Nothing in this engine ever writes render state 26, so setting it "
             "once here is not fighting anybody.");
}
