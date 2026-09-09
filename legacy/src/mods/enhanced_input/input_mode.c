/* input_mode.c: see input_mode.h. */
#include "input_mode.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdint.h>

/* --- input_setMode 0x004658C1 ----------------------------------------------------------------- *
 *   55 8B EC 51           push ebp / mov ebp,esp / push ecx
 *   A1 5C 5D 6D 00        mov eax,[g_inputMode]      the cell, as an absolute operand
 *   3B 45 08              cmp eax,[ebp+8]            already in that mode? then nothing to do
 *   75 02 EB 4A           jne +2 / jmp out
 *
 * Nine bytes are already unique and sixteen are taken. NOT detoured: the site is resolved only so
 * the cell can be read out of its own first instruction, so the address appears once here rather
 * than being written down as a constant. */
static const uint8_t SIG_INPUT_SET_MODE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xA1, 0x5C, 0x5D, 0x6D, 0x00, 0x3B, 0x45, 0x08,
    0x75, 0x02, 0xEB, 0x4A
};
#define INPUT_SET_MODE_CELL_OPERAND 0x05u

static struct {
    const int32_t *cell;
    bool           tried;
} mode_state;

bool input_mode_allows_movement(bool resolved, int32_t mode)
{
    return !resolved || mode == INPUT_MODE_GAMEPLAY;
}

void input_mode_resolve(void)
{
    uintptr_t site;
    uint32_t  address = 0;

    if (mode_state.tried) {
        return;
    }
    mode_state.tried = true;

    site = signature_find_unique(SIG_INPUT_SET_MODE, NULL, sizeof SIG_INPUT_SET_MODE);
    if (site == 0 ||
        !memory_read_u32(site + INPUT_SET_MODE_CELL_OPERAND, &address) || address == 0 ||
        !memory_is_readable_range((uintptr_t)address, sizeof(int32_t))) {
        log_warning("the engine's input mode could not be read, so the pad stick drives the player "
                    "whenever it is pushed, a dialogue choice menu included. That is the one place "
                    "it shows: the menu scrolls and the player walks at the same time");
        return;
    }

    mode_state.cell = (const int32_t *)(uintptr_t)address;
    log_info("the engine's input mode is at %08X, so the pad stick stands down whenever the "
             "bindings are not the gameplay ones. A dialogue swaps the whole binding set rather "
             "than testing a flag, so a stick read straight from XInput has to ask",
             (unsigned)address);
}

bool input_mode_is_gameplay(void)
{
    return input_mode_allows_movement(mode_state.cell != NULL,
                                      (mode_state.cell != NULL) ? *mode_state.cell : 0);
}
