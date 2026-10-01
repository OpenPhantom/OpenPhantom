/* mp_signatures_pause.c: the one entry point of the engine's pause, the one call that reaches it,
 * the pause menu's choice of a frame pump, and the cells and calls their bytes name.
 *
 * sys_pause is the whole of the engine's pause: it takes a latch so it cannot be entered twice,
 * tells every module to stop, sets the gate that keeps the substeps from running, drives the pause
 * screen, writes what the screen's answer decides, and puts the rest back. The latch and the gate
 * are each named twice in it, which is what makes reading them evidence rather than a lookup, and
 * the three cells its answer writes are read out of the very instructions that write them, so a
 * session's pause menu that writes the same cells writes them provably.
 *
 * It is a table of its own rather than a row of the main one because the main table's file is at
 * its size limit, and a row there costs two lines in a file that has six left. The world probes
 * are arranged the same way for the same reason, and the resolver, the two stage rule and the
 * reporting are the shared ones either way. The key arm and the pump choice are a second table in
 * this file with a line of their own, so that the line the watchpost has always printed for the
 * first one keeps its words.
 *
 * Nobody detours sys_pause, here or in any other module of the build: imuse_fix carries the same
 * head as a read anchor for the same latch, and reads it from the same two operands. So this is a
 * read anchor too, with no declared prologue, because declaring one would claim a write range this
 * file never writes and would put the other module's pattern at risk on paper. The prologue is ten
 * bytes if a detour is ever wanted here.
 *
 * What IS written is the key hook's call of sys_pause: while a session's transport stands its four
 * operand bytes point at the multiplayer's own pause, and they are put back when it comes down. The
 * redirect is declared below where the pattern that finds it is, which is the one line to search
 * for before another pattern pins those four bytes.
 */
#include "mp_signatures_pause.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* sys_pause, read up to the answer's last write: the latch compared and set, the frame delta pushed
 * for the SUSPEND broadcast, the gate set, the pause menu called, and then the two arms of its
 * answer. Answer 6 sets the level outcome to 3 and the restore flag to 1; every other answer sets
 * the gate back to 0. The literal immediates 6, 3, 1 and 0 are pinned, because they are the rule a
 * session's pause menu repeats; the operands are what this file reports and are wildcards, and so
 * are the frame delta and the two call displacements. */
static const uint8_t SIG_MP_SYS_PAUSE[94] = {
    0x55, 0x8B, 0xEC, 0x83, 0x3D, 0xE0, 0xCF, 0x6C, 0x00, 0x01, 0x75, 0x02,
    0xEB, 0x6D, 0xC7, 0x05, 0xE0, 0xCF, 0x6C, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xA1, 0x14, 0x87, 0x86, 0x00, 0x50, 0x6A, 0x08, 0x6A, 0x00, 0xE8, 0xCD,
    0xF9, 0x02, 0x00, 0x83, 0xC4, 0x0C, 0xC7, 0x05, 0xD8, 0xCF, 0x6C, 0x00,
    0x01, 0x00, 0x00, 0x00, 0xE8, 0x41, 0x33, 0x00, 0x00, 0x83, 0xF8, 0x06,
    0x75, 0x16, 0xC7, 0x05, 0x68, 0x13, 0x88, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x40, 0x13, 0x88, 0x00, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x0A,
    0xC7, 0x05, 0xD8, 0xCF, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_SYS_PAUSE[94] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
/* Where in sys_pause each cell is named. The latch twice and the gate twice, and each pair has to
 * agree. */
#define LATCH_FIRST_OPERAND   0x05u
#define LATCH_SECOND_OPERAND  0x10u
#define SIM_GATE_OPERAND      0x2Cu
#define SIM_GATE_FREE_OPERAND 0x56u
#define OUTCOME_OPERAND       0x40u
#define RESTORE_OPERAND       0x4Au
#define SYS_PAUSE_MENU_CALL   0x34u

/* The escape arm of the key hook, from its case compare to the store of "swallowed":
 *
 *   cmp [ebp-8], 0x1B; je +2; jmp out
 *   cmp [outcome], 2; jne +0x0E           the level must be running
 *   call player_isDead; test eax,eax; jne  and the player alive
 *   call sys_pause                         the one call, at +0x1A
 *   mov [ebp-4], 1
 *
 * The case compare is what makes it unique: without the first eight bytes the same shape matches
 * the backspace arm of the cheat console twenty bytes further on as well. The outcome operand and
 * both call displacements are wildcards, so the pattern still finds its site after this feature has
 * repointed the call, and after a later one repoints the player_isDead call at +0x11. */
static const uint8_t SIG_MP_PAUSE_KEY_ARM[38] = {
    0x83, 0x7D, 0xF8, 0x1B, 0x74, 0x02, 0xEB, 0x3E, 0x83, 0x3D, 0x68, 0x13,
    0x88, 0x00, 0x02, 0x75, 0x0E, 0xE8, 0xD9, 0x1B, 0x01, 0x00, 0x85, 0xC0,
    0x75, 0x05, 0xE8, 0x2F, 0x04, 0x00, 0x00, 0xC7, 0x45, 0xFC, 0x01, 0x00,
    0x00, 0x00
};
static const uint8_t MSK_MP_PAUSE_KEY_ARM[38] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
#define KEY_ARM_OUTCOME_OPERAND 0x0Au
#define KEY_ARM_PAUSE_CALL      0x1Au

/* The call whose operand a session's transport repoints. */
SIGNATURE_REDIRECTED_CALL(SIG_MP_PAUSE_KEY_ARM, KEY_ARM_PAUSE_CALL);

/* The pause menu's frame, as every menu loop of the engine chooses it:
 *
 *   cmp [backdrop], 0; jne +7; call sys_frame; jmp +5; call swmenu_pumpFrame
 *   mov edx, [the menu stack]; mov eax, [edx+0x34]; mov [ebp-0x30], eax; mov [ebp-0x60], -1
 *
 * Sixteen places test the same cell, so the pattern reaches into what follows the choice until it
 * is unique. The cell is what a session's pause menu clears before it runs, and the first call is
 * the world's frame the player list over it pumps. */
static const uint8_t SIG_MP_PAUSE_PUMP_CHOICE[40] = {
    0x83, 0x3D, 0xC4, 0xFD, 0x6C, 0x00, 0x00, 0x75, 0x07, 0xE8, 0x0F, 0xB9,
    0xFF, 0xFF, 0xEB, 0x05, 0xE8, 0x47, 0xC5, 0x01, 0x00, 0x8B, 0x15, 0x70,
    0xD3, 0x86, 0x00, 0x8B, 0x42, 0x34, 0x89, 0x45, 0xD0, 0xC7, 0x45, 0xA0,
    0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t MSK_MP_PAUSE_PUMP_CHOICE[40] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
#define PUMP_BACKDROP_OPERAND 0x02u
#define PUMP_WORLD_CALL       0x09u
#define PUMP_MENU_CALL        0x10u

/* The E8 of a near call. */
#define CALL_REL32_OPCODE 0xE8u

static signature_t pause_sites[1] = {
    SIGNATURE_ENTRY_MASKED("sys_pause", SIG_MP_SYS_PAUSE, MSK_MP_SYS_PAUSE)
};
static signature_t menu_sites[2] = {
    SIGNATURE_ENTRY_MASKED("pause_key_arm", SIG_MP_PAUSE_KEY_ARM, MSK_MP_PAUSE_KEY_ARM),
    SIGNATURE_ENTRY_MASKED("pause_pump_choice", SIG_MP_PAUSE_PUMP_CHOICE,
                           MSK_MP_PAUSE_PUMP_CHOICE)
};

_Static_assert(sizeof(SIG_MP_SYS_PAUSE) == sizeof(MSK_MP_SYS_PAUSE),
               "the pause pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PAUSE_KEY_ARM) == sizeof(MSK_MP_PAUSE_KEY_ARM),
               "the key arm pattern and its mask differ in length");
_Static_assert(sizeof(SIG_MP_PAUSE_PUMP_CHOICE) == sizeof(MSK_MP_PAUSE_PUMP_CHOICE),
               "the pump choice pattern and its mask differ in length");
_Static_assert(SYS_PAUSE_MENU_CALL + 5u <= sizeof(SIG_MP_SYS_PAUSE) &&
                   PUMP_MENU_CALL + 5u <= sizeof(SIG_MP_PAUSE_PUMP_CHOICE),
               "every call read here lies inside the pattern that finds it");

static uintptr_t pause_cells[MP_PAUSE_CELL_COUNT];
static bool      resolved_once;
static bool      menu_resolved_once;

static const signature_t *site_of(mp_pause_site_t site)
{
    switch (site) {
    case MP_PAUSE_SITE_SYS_PAUSE:
        return &pause_sites[0];
    case MP_PAUSE_SITE_KEY_ARM:
        return &menu_sites[0];
    case MP_PAUSE_SITE_PUMP_CHOICE:
        return &menu_sites[1];
    case MP_PAUSE_SITE_COUNT:
    default:
        break;
    }
    return NULL;
}

static bool read_cell(mp_pause_site_t site, size_t offset, uintptr_t *out)
{
    const signature_t *entry = site_of(site);

    *out = 0u;
    return entry != NULL && entry->address != 0u &&
           signature_read_address_operand(entry, offset, out) && *out != 0u;
}

/* A cell named twice: both readings have to answer the same, or the cell is left unknown rather
 * than believed, the way the shared cell table does it. */
static uintptr_t read_twice(mp_pause_site_t first_site, size_t first,
                            mp_pause_site_t second_site, size_t second, const char *what)
{
    uintptr_t one = 0u;
    uintptr_t two = 0u;

    if (!read_cell(first_site, first, &one) || !read_cell(second_site, second, &two)) {
        return 0u;
    }
    if (one != two) {
        log_warning("  the %s is named twice and the two operands disagree, %08X against %08X, "
                    "so it is refused", what, (unsigned)one, (unsigned)two);
        return 0u;
    }
    return one;
}

static void read_the_cells(void)
{
    pause_cells[MP_PAUSE_CELL_LATCH] =
        read_twice(MP_PAUSE_SITE_SYS_PAUSE, LATCH_FIRST_OPERAND, MP_PAUSE_SITE_SYS_PAUSE,
                   LATCH_SECOND_OPERAND, "pause latch");
    pause_cells[MP_PAUSE_CELL_SIM_GATE] =
        read_twice(MP_PAUSE_SITE_SYS_PAUSE, SIM_GATE_OPERAND, MP_PAUSE_SITE_SYS_PAUSE,
                   SIM_GATE_FREE_OPERAND, "simulation gate");
    (void)read_cell(MP_PAUSE_SITE_SYS_PAUSE, RESTORE_OPERAND,
                    &pause_cells[MP_PAUSE_CELL_RESTORE]);
}

size_t mp_signatures_pause_resolve(void)
{
    size_t resolved;

    if (resolved_once) {
        return pause_sites[0].address != 0u ? 1u : 0u;
    }
    resolved_once = true;

    resolved = signature_resolve_table(pause_sites, 1u);
    if (resolved != 0u) {
        read_the_cells();
    }
    log_info("%u of %u pause site(s) resolved; the pause latch is %08X and the simulation gate "
             "%08X", (unsigned)resolved, 1u, (unsigned)pause_cells[MP_PAUSE_CELL_LATCH],
             (unsigned)pause_cells[MP_PAUSE_CELL_SIM_GATE]);
    return resolved;
}

size_t mp_signatures_pause_menu_resolve(void)
{
    size_t resolved;

    (void)mp_signatures_pause_resolve();
    if (menu_resolved_once) {
        return (menu_sites[0].address != 0u ? 1u : 0u) + (menu_sites[1].address != 0u ? 1u : 0u);
    }
    menu_resolved_once = true;

    resolved = signature_resolve_table(menu_sites, 2u);
    /* The outcome is named by the key arm's test and by sys_pause's own write. */
    pause_cells[MP_PAUSE_CELL_OUTCOME] =
        read_twice(MP_PAUSE_SITE_KEY_ARM, KEY_ARM_OUTCOME_OPERAND, MP_PAUSE_SITE_SYS_PAUSE,
                   OUTCOME_OPERAND, "level outcome");
    (void)read_cell(MP_PAUSE_SITE_PUMP_CHOICE, PUMP_BACKDROP_OPERAND,
                    &pause_cells[MP_PAUSE_CELL_BACKDROP]);
    log_info("the pause menu's own call and its pump: %u of 2 site(s) resolved; the call %08X, "
             "the backdrop cell %08X, the level outcome %08X, the restore flag %08X",
             (unsigned)resolved, (unsigned)mp_signatures_pause_call(MP_PAUSE_CALL_PAUSE),
             (unsigned)pause_cells[MP_PAUSE_CELL_BACKDROP],
             (unsigned)pause_cells[MP_PAUSE_CELL_OUTCOME],
             (unsigned)pause_cells[MP_PAUSE_CELL_RESTORE]);
    return resolved;
}

uintptr_t mp_signatures_pause_address(mp_pause_site_t site)
{
    const signature_t *entry = site_of(site);

    return entry != NULL ? entry->address : 0u;
}

uintptr_t mp_signatures_pause_cell(mp_pause_cell_t cell)
{
    if ((size_t)cell >= (size_t)MP_PAUSE_CELL_COUNT) {
        return 0u;
    }
    (void)mp_signatures_pause_resolve();
    return pause_cells[cell];
}

uintptr_t mp_signatures_pause_call(mp_pause_call_t call)
{
    mp_pause_site_t site;
    size_t          offset;
    uintptr_t       address;
    uint8_t         opcode = 0u;

    switch (call) {
    case MP_PAUSE_CALL_PAUSE:
        site   = MP_PAUSE_SITE_KEY_ARM;
        offset = KEY_ARM_PAUSE_CALL;
        break;
    case MP_PAUSE_CALL_MENU:
        site   = MP_PAUSE_SITE_SYS_PAUSE;
        offset = SYS_PAUSE_MENU_CALL;
        break;
    case MP_PAUSE_CALL_WORLD_PUMP:
        site   = MP_PAUSE_SITE_PUMP_CHOICE;
        offset = PUMP_WORLD_CALL;
        break;
    case MP_PAUSE_CALL_MENU_PUMP:
        site   = MP_PAUSE_SITE_PUMP_CHOICE;
        offset = PUMP_MENU_CALL;
        break;
    case MP_PAUSE_CALL_COUNT:
    default:
        return 0u;
    }
    address = mp_signatures_pause_address(site);
    if (address == 0u) {
        return 0u;
    }
    address += offset;
    return memory_read_u8(address, &opcode) && opcode == CALL_REL32_OPCODE ? address : 0u;
}
