/* keyboard_gate.c: see keyboard_gate.h. */
#include "keyboard_gate.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/platform.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define POLL_PROLOGUE     9u
#define OPERAND_DEVICE    0x12u   /* cmp [device],0 */
#define HELD_TEST         0xCAu   /* cmp [eax*4+held],0 */
#define OPERAND_HELD      (HELD_TEST + 3u)
#define HELD_KEYS         256u
#define VTABLE_GET_DATA   (0x28u / sizeof(void *))
#define OBJECT_DATA_SIZE  0x10u

/* controller_input's pattern for the same function (background_input.c). */
static const uint8_t SIG_KEYBOARD_POLL[74] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x24, 0x03, 0x00, 0x00, 0xC7, 0x45, 0xF8,
    0x20, 0x00, 0x00, 0x00, 0x83, 0x3D, 0xC4, 0x19, 0x86, 0x00, 0x00, 0x74,
    0x09, 0x83, 0x3D, 0xB8, 0x19, 0x86, 0x00, 0x00, 0x74, 0x05, 0xE9, 0xCA,
    0x02, 0x00, 0x00, 0x6A, 0x00, 0x8D, 0x45, 0xF8, 0x50, 0x8D, 0x8D, 0xF8,
    0xFD, 0xFF, 0xFF, 0x51, 0x6A, 0x10, 0x8B, 0x15, 0xC4, 0x19, 0x86, 0x00,
    0x52, 0xA1, 0xC4, 0x19, 0x86, 0x00, 0x8B, 0x08, 0xFF, 0x51, 0x28, 0x89,
    0x45, 0xFC
};
static const uint8_t MSK_KEYBOARD_POLL[74] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
static const uint8_t HELD_TEST_BYTES[3] = { 0x83, 0x3C, 0x85 };

typedef void (__cdecl *poll_keyboard_fn_t)(void);
typedef HRESULT (WINAPI *get_device_data_fn_t)(void *self, DWORD size, void *data, DWORD *count,
                                               DWORD flags);

static detour_t          poll;
static void *volatile   *device;
static volatile int32_t *held;
static bool              was_behind;
static uint32_t          drains;

static void drain(void)
{
    void  *self = *device;
    DWORD  count = INFINITE;

    if (self == NULL) {
        return;
    }
    /* A NULL buffer with INFINITE items is DirectInput's flush. */
    (void)((get_device_data_fn_t)(*(void ***)self)[VTABLE_GET_DATA])(self, OBJECT_DATA_SIZE, NULL,
                                                                     &count, 0);
    ++drains;
}

static void __cdecl hook_poll_keyboard(void)
{
    bool behind = !platform_foreground_is_ours();

    if (behind) {
        drain();
        if (!was_behind) {
            memset((void *)held, 0, HELD_KEYS * sizeof *held);
            log_info("keyboard gate: this window went behind, keys released (%u drain(s) so far)",
                     drains);
        }
    }
    was_behind = behind;
    ((poll_keyboard_fn_t)poll.original)();
}

static bool read_operand(uintptr_t at, void **out)
{
    uintptr_t address;

    memcpy(&address, (const void *)at, sizeof address);
    if (address < host_image_base() || address >= host_image_end()) {
        return false;
    }
    *out = (void *)address;
    return true;
}

void keyboard_gate_install(void)
{
    uintptr_t head = signature_find_detour_target(SIG_KEYBOARD_POLL, MSK_KEYBOARD_POLL,
                                                  sizeof SIG_KEYBOARD_POLL, POLL_PROLOGUE);
    void     *device_cell = NULL;
    void     *held_table  = NULL;

    if (head == 0 || memcmp((const void *)(head + HELD_TEST), HELD_TEST_BYTES,
                            sizeof HELD_TEST_BYTES) != 0 ||
        !read_operand(head + OPERAND_DEVICE, &device_cell) ||
        !read_operand(head + OPERAND_HELD, &held_table)) {
        log_warning("keyboard gate: the keyboard poll did not resolve (%08X), every copy reads the "
                    "keyboard", (unsigned)head);
        return;
    }
    device = (void *volatile *)device_cell;
    held   = (volatile int32_t *)held_table;
    if (!detour_install(&poll, head, (const void *)&hook_poll_keyboard, POLL_PROLOGUE)) {
        log_warning("keyboard gate: the keyboard poll at %08X could not be detoured", (unsigned)head);
        return;
    }
    log_info("keyboard gate: poll %08X%s, device %08X, held keys %08X; the keyboard reaches this "
             "copy only while its window is in front", (unsigned)head,
             poll.chained ? " (chained)" : "", (unsigned)(uintptr_t)device,
             (unsigned)(uintptr_t)held);
}
