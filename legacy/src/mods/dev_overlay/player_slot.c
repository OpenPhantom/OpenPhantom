/* player_slot.c: see player_slot.h. */
#include "player_slot.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x00450FD8, the engine's own "is the player suspended" predicate ------------------------- *
 *
 *   00450FD8  55 8B EC                 push ebp; mov ebp,esp
 *   00450FDB  A1 20 52 4B 00           mov eax,[004B5220]      ; the player record
 *   00450FE0  83 78 04 00              cmp dword [eax+4],0     ; the module state
 *   00450FE4  75 07                    non zero: jump PAST the 1, so answer 0
 *   00450FE6  B8 01 00 00 00           zero: answer 1, and 1 is what "suspended" means
 *   00450FEF  5D C3
 *
 * Twenty five bytes, the whole function, with the one operand masked. It is the cleanest load of
 * the cell in the image: nothing else happens in front of it, and the shape of the test after it
 * keeps the pattern unique. The input freeze reads +4 through the same predicate, so the cell and
 * the field it tests are proven by one match. */
static const uint8_t SIG_IS_SUSPENDED[] = {
    0x55, 0x8B, 0xEC,
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the player record] */
    0x83, 0x78, 0x04, 0x00,                          /* cmp [eax+4],0               */
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
#define IS_SUSPENDED_CELL_OPERAND 4u

static struct {
    bool      tried;
    uintptr_t cell;
} slot_state;

bool player_slot_resolve(void)
{
    uintptr_t site;
    uint32_t  cell = 0;

    if (slot_state.tried) {
        return slot_state.cell != 0;
    }
    slot_state.tried = true;

    site = signature_find_unique(SIG_IS_SUSPENDED, MSK_IS_SUSPENDED, sizeof SIG_IS_SUSPENDED);
    if (site == 0) {
        log_warning("the player's own suspend predicate did not resolve, so the cell holding the "
                    "player record is unknown. Every cheat that reads the player stays "
                    "unavailable, and the panel cannot hold the player still");
        return false;
    }
    if (!memory_read_u32(site + IS_SUSPENDED_CELL_OPERAND, &cell) ||
        !memory_is_inside_image(cell, sizeof(void *))) {
        log_warning("the suspend predicate at %08X loads the player from %08X, which is not "
                    "inside the image, so the player record cell is treated as unknown",
                    (unsigned)site, (unsigned)cell);
        return false;
    }
    slot_state.cell = cell;
    log_info("the player record is read through %08X, out of the suspend predicate at %08X",
             (unsigned)cell, (unsigned)site);
    return true;
}

void *const volatile *player_slot(void)
{
    return (slot_state.cell != 0) ? (void *const volatile *)slot_state.cell : NULL;
}

void *player_slot_current(void)
{
    return (slot_state.cell != 0) ? *(void *const volatile *)slot_state.cell : NULL;
}

uintptr_t player_slot_address(void)
{
    return slot_state.cell;
}
