/* mp_mouse_latch.c: the mouse buttons the engine leaves down as a level begins. See the header.
 *
 * The table is found through the engine's button reader and not through the flush, whose loop
 * names the table eleven times with one constant in front of each. The pattern is the reader's
 * tail, from the test of its second argument to its return, so it holds none of the bytes a hook
 * on the reader's head would write. The reader stands at 0x0048D5DF and its tail at 0x0048D5FE;
 * the pattern matches once in each of the five retail images it was counted in, and once, at
 * another address, in the level editor's own recompile of the engine.
 *
 * What the header says of the engine, as the retail image has it. The flush at 0x0048E8C1:
 *
 *   0048E8CE  cmp  [the mouse is enabled],0   it leaves when the mouse is not
 *   0048E8F1  call [eax+0x24]                 the device's state into [ebp-0x18]; no test follows
 *   0048E90C  call [ecx+0x28]                 the device's queue, read into nothing
 *   0048E938  mov  dl,[ebp+ecx-0x0C]          the button byte of this pass, ecx from 0 to 3
 *   0048E945  mov  eax,0x125                  the control, the same constant on every pass
 *   0048E94A  shl  eax,2
 *   0048E94D  cmp  [eax+the down cells],0     and the press or the release of that one cell
 *
 * Its one caller is the resync at 0x0048D1CF, which flushes the keyboard and then the mouse. The
 * resync has three callers: the control module on module message 5, a level's beginning; the
 * same module on message 19, behind a focus it has just set; and the input mode's setter, behind
 * the clearing of the bindings, which disables the mouse, so there the flush leaves at its first
 * test. The mouse is
 * opened at 0x0048DA7C with cooperative level 6, not exclusive and foreground only, and the focus
 * setter at 0x0048D719 sets the engine's acquired flag whatever the device answered, so the
 * reader goes on answering from the table while the mouse is not acquired. The mouse defaults at
 * 0x0046586C bind control 0x124 to action 2 and control 0x125 to action 3. The ground actions at
 * 0x0044AE65 ask action 3 as a level and start a jump while it reads down; a landing sets a lock
 * of 0.2 seconds on the next one. A level's beginning is module message 5, which the registry
 * sends from its tail to its head.
 *
 * What the stack holds when the device does not answer was not run here. The cell is released
 * either way, so the fix does not rest on it.
 */
#include "mp_mouse_latch.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The button reader from the test of its out slot to its return: the presses of the control are
 * added to the slot, then the control's down cell is the answer. */
static const uint8_t SIG_MP_BUTTON_READER_TAIL[] = {
    0x83, 0x7D, 0x0C, 0x00,                      /* cmp [ebp+0x0C],0          */
    0x74, 0x14,                                  /* jz  +0x14                 */
    0x8B, 0x4D, 0x0C,                            /* mov ecx,[ebp+0x0C]        */
    0x8B, 0x11,                                  /* mov edx,[ecx]             */
    0x8B, 0x45, 0x08,                            /* mov eax,[ebp+8]           */
    0x03, 0x14, 0x85, 0x00, 0x00, 0x00, 0x00,    /* add edx,[eax*4+presses]   */
    0x8B, 0x4D, 0x0C,                            /* mov ecx,[ebp+0x0C]        */
    0x89, 0x11,                                  /* mov [ecx],edx             */
    0x8B, 0x55, 0x08,                            /* mov edx,[ebp+8]           */
    0x8B, 0x04, 0x95, 0x00, 0x00, 0x00, 0x00,    /* mov eax,[edx*4+down]      */
    0x5D,                                        /* pop ebp                   */
    0xC3                                         /* ret                       */
};
static const uint8_t MSK_MP_BUTTON_READER_TAIL[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF
};
/* Where the table of down cells stands in it: the operand of the last load. */
#define BUTTON_READER_DOWN_CELLS 32u

_Static_assert(sizeof(SIG_MP_BUTTON_READER_TAIL) == 38u &&
                   sizeof(MSK_MP_BUTTON_READER_TAIL) == 38u,
               "the reader's tail and its mask are the length the operand's offset counts in");
_Static_assert(BUTTON_READER_DOWN_CELLS + 4u + 2u == sizeof(SIG_MP_BUTTON_READER_TAIL),
               "the table's operand is followed by the two bytes of the return and nothing else");
_Static_assert(MP_MOUSE_LATCH_FIRST + MP_MOUSE_LATCH_BUTTONS == MP_MOUSE_LATCH_CONTROLS &&
                   MP_MOUSE_LATCH_JUMP == MP_MOUSE_LATCH_FIRST + 1u,
               "the four mouse buttons are the table's last cells, and jump is the second");

/* The lines written one by one. */
#define LINES_WRITTEN 8u

static signature_t latch_site[1] = {
    SIGNATURE_ENTRY_MASKED("button_reader_tail", SIG_MP_BUTTON_READER_TAIL,
                           MSK_MP_BUTTON_READER_TAIL)
};

typedef struct latch_state {
    bool      tried;
    uintptr_t table;          /* the engine's down cells, 0 where they did not resolve */
    uint32_t  level_begins;
    uint32_t  with_a_latch;   /* of them, the ones that found a button down */
    uint32_t  released;       /* cells released */
    uint32_t  released_jump;  /* of them the second button's */
    uint32_t  not_pressed;    /* of them with the button itself up */
    uint32_t  unread;         /* level begins on which the cells did not read */
    uint32_t  lines;
} latch_state_t;

static latch_state_t latch;

/* ==============================================================================================
 * The two decisions, on a table a test can hand in.
 * ============================================================================================ */

uint32_t mp_mouse_latch_release(uintptr_t table, bool *read)
{
    const uint32_t up = 0u;
    uint32_t       cells[MP_MOUSE_LATCH_BUTTONS];
    uint32_t       were_down = 0u;
    uintptr_t      first = table + MP_MOUSE_LATCH_FIRST * sizeof cells[0];
    size_t         button;

    if (read != NULL) {
        *read = false;
    }
    if (table == 0u || !memory_try_read(first, cells, sizeof cells)) {
        return 0u;
    }
    if (read != NULL) {
        *read = true;
    }
    for (button = 0u; button < MP_MOUSE_LATCH_BUTTONS; ++button) {
        if (cells[button] != 0u &&
            memory_try_write(first + button * sizeof cells[0], &up, sizeof up)) {
            were_down |= 1u << button;
        }
    }
    return were_down;
}

uint32_t mp_mouse_latch_not_pressed(uint32_t were_down, uint32_t pressed)
{
    return were_down & ~pressed & ((1u << MP_MOUSE_LATCH_BUTTONS) - 1u);
}

/* ==============================================================================================
 * The engine side.
 * ============================================================================================ */

bool mp_mouse_latch_install(void)
{
    uintptr_t table = 0u;

    if (latch.tried) {
        return latch.table != 0u;
    }
    latch.tried = true;
    if (signature_resolve_table(latch_site, 1u) != 1u ||
        !signature_read_address_operand(&latch_site[0], BUTTON_READER_DOWN_CELLS, &table) ||
        !memory_is_inside_image(table, MP_MOUSE_LATCH_CONTROLS * sizeof(uint32_t))) {
        log_warning("the mouse buttons are not released as a level begins: the engine's table "
                    "of down cells did not resolve (the button reader's tail at %08X, its "
                    "operand %08X), so a level that begins in a window that is not in front can "
                    "leave jump held until a menu was opened and closed",
                    (unsigned)latch_site[0].address, (unsigned)table);
        return false;
    }
    latch.table = table;
    log_info("the mouse buttons are released as a level of a session begins: the engine's down "
             "cells are at %08X, read out of the button reader's tail at %08X; the engine's own "
             "flush writes the second button's cell, which is jump, from a device it does not "
             "ask whether it answered", (unsigned)table, (unsigned)latch_site[0].address);
    return true;
}

/* The four buttons as the system says they are pressed now, whoever has the focus: the left,
 * the right, the middle and the first extra one, in the order the device numbers them. */
static uint32_t pressed_now(void)
{
    static const int keys[MP_MOUSE_LATCH_BUTTONS] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON,
                                                      VK_XBUTTON1 };
    uint32_t         pressed = 0u;
    size_t           button;

    for (button = 0u; button < MP_MOUSE_LATCH_BUTTONS; ++button) {
        if ((GetAsyncKeyState(keys[button]) & 0x8000) != 0) {
            pressed |= 1u << button;
        }
    }
    return pressed;
}

void mp_mouse_latch_level_begins(void)
{
    uint32_t were_down;
    uint32_t pressed;
    uint32_t nobody;
    bool     read = false;
    size_t   button;

    if (latch.table == 0u) {
        return;
    }
    ++latch.level_begins;
    were_down = mp_mouse_latch_release(latch.table, &read);
    if (!read) {
        ++latch.unread;
        return;
    }
    if (were_down == 0u) {
        return;
    }
    pressed = pressed_now();
    nobody  = mp_mouse_latch_not_pressed(were_down, pressed);
    ++latch.with_a_latch;
    for (button = 0u; button < MP_MOUSE_LATCH_BUTTONS; ++button) {
        latch.released    += (were_down >> button) & 1u;
        latch.not_pressed += (nobody >> button) & 1u;
    }
    latch.released_jump += (were_down >> (MP_MOUSE_LATCH_JUMP - MP_MOUSE_LATCH_FIRST)) & 1u;
    if (latch.lines < LINES_WRITTEN) {
        ++latch.lines;
        log_info("a level begins with mouse buttons down in the engine's table (bits %X, bit 1 "
                 "is the second button, jump) while the buttons pressed are %X: the cells are "
                 "released, %s", (unsigned)were_down, (unsigned)pressed,
                 nobody != 0u ? "and what nobody pressed was left there by the engine's flush "
                                "of a mouse that did not answer, or by a release that was lost"
                              : "and a button that is really held has to be pressed again");
    }
}

void mp_mouse_latch_report(void)
{
    if (!latch.tried) {
        return;
    }
    if (latch.table == 0u) {
        log_info("  the mouse buttons at a level begin: not bound, nothing was released");
        return;
    }
    log_info("  the mouse buttons at a level begin: %u level begin(s) looked at, %u of them with "
             "a button down in the engine's table; %u cell(s) released, %u of them the second "
             "button's, which is jump, and %u with the button itself not pressed; %u look(s) at "
             "cells that did not read",
             (unsigned)latch.level_begins, (unsigned)latch.with_a_latch,
             (unsigned)latch.released, (unsigned)latch.released_jump,
             (unsigned)latch.not_pressed, (unsigned)latch.unread);
}
