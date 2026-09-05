/* player_sites.c: finding the player state machine in this build, or refusing.
 *
 * Five patterns and what each of them is worth. Not one address is written down: every one of them
 * is READ out of an operand at a site that proves what the operand is for. Three builds of this
 * engine ship in one installation and the recompile moves code by more than 0x1E000, so an address
 * table would have written silently into a different function.
 *
 * About half of this file is disassembly rather than code, and that is deliberate: a pattern
 * without its listing is a magic number, and splitting the two apart would put every proof one
 * file away from the code that depends on it.
 */
#include "player_sites.h"

#include "mouse_look.h"
#include "player_record.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The mode is a pointer, and the field that looks like an index is dead during play.
 *
 * The mode used to be read from `record + 0x398` and called "the mode index 0..13". Two things
 * were wrong. That offset is the AUX index; the mode index is at 0x39C. And neither of them is
 * live: both are part of the save tail, written only by player_save 0x4479D2, which recovers each
 * one by searching a pointer table for the pointer the state machine actually holds:
 *
 *     0x4479D6  pAuxAction (+0x64) searched in the table at 0x4B54F0 -> stored to +0x398
 *     0x4479F8  pCurMode   (+0x60) searched in the table at 0x4B54B0 -> stored to +0x39C
 *
 * So during play that word holds whatever the last save left, or -1, and the plausibility gate
 * built on it was permanently false. Mouse look and strafe never ran a single frame, and said
 * nothing, because a gate that is always closed logs nothing.
 *
 * The live answer is the same search the save does: find pCurMode in the descriptor table, which
 * is why this file resolves that table out of player_save's own operand. */

/* --- 0x004482E0  Plr_RunPhases: the mode gate IS a data table -------------------------------- *
 *   8B 4D F8          mov  ecx,[ebp-8]
 *   FF 14 8D <abs32>  call dword ptr [ecx*4 + g_plrPhases]
 *
 * The pattern ends before the operand on purpose: the table address is READ out of it, not
 * written into the pattern. Otherwise one would be searching for the address one wants to find.
 * `FF 14 8D` (call dword ptr [ecx*4+disp32]) is a rare encoding, exactly ONE hit in all eight
 * shipped images, including obiold and netobi, whose VAs differ by more than 0x1E000. */
static const uint8_t SIG_PLAYER_PHASE_TABLE[] = { 0x8B, 0x4D, 0xF8, 0xFF, 0x14, 0x8D };
#define OFFSET_PHASE_TABLE_OPERAND 0x06

/* --- 0x004479D2  player_save's head: where the two descriptor tables are named ---------------- *
 *   55 8B EC 51
 *   A1 <pPlayer> / 8B 48 64 / 51        pAuxAction
 *   68 <aux table> / E8 <find> / 83 C4 08
 *   8B 15 <pPlayer> / 89 82 98030000    -> cachedAuxIndex
 *   A1 <pPlayer> / 8B 48 60 / 51        pCurMode
 *   68 <mode table>                     <- the table we want, operand at +48
 *
 * Read here rather than hard-coded, and read from the ONE function that proves what the table is
 * for: it is the table the engine itself searches to turn pCurMode into a number. */
static const uint8_t SIG_PLAYER_SAVE_HEAD[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x48, 0x64,
    0x51,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x82, 0x98, 0x03, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x48, 0x60,
    0x51,
    0x68, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_PLAYER_SAVE_HEAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_MODE_TABLE_OPERAND 48u

_Static_assert(sizeof(SIG_PLAYER_SAVE_HEAD) == sizeof(MSK_PLAYER_SAVE_HEAD),
               "the player_save pattern and its mask are different lengths");

/* --- 0x00449F94  Plr_Steer: the MOUSE axis (relative) ---------------------------------------- *
 *   A1 <pPlayer>         mov  eax,[pPlayer]      <- the pPlayer operand sits at -4
 *   89 90 54 01 00 00    mov  [eax+0x154],edx    ; moveInput
 *   6A 00                push 0                  ; axis 0 = turning
 *   E8 <rel32>           call control_readRelAxis
 *
 * Two birds: the site yields the mouse axis reader AND the player record pointer. The A1 opcode
 * is verified before the operand is believed. */
static const uint8_t SIG_PLAYER_MOUSE_AXIS[] = {
    0x89, 0x90, 0x54, 0x01, 0x00, 0x00, 0x6A, 0x00, 0xE8
};
#define OFFSET_MOUSE_AXIS_CALL      0x09
#define OFFSET_MOUSE_AXIS_PLAYER  (-4)
#define OPCODE_MOV_EAX_ABS32       0xA1

/* --- 0x0044A089  Plr_Steer: the KEYBOARD axis (absolute) ------------------------------------- *
 *   E9 9F 01 00 00       jmp  0x44A22D           ; the end of the MOUSE branch
 *   6A 00                push 0
 *   E8 <rel32>           call control_readAbsAxis
 *
 * Mouse and keyboard are MUTUALLY EXCLUSIVE in the original (0x449FB2 branches into the keyboard
 * arm when the mouse is idle; 0x44A089 jumps over it when it is not). That is exactly why the
 * keyboard axis becomes free the moment the mouse owns the view direction. */
static const uint8_t SIG_PLAYER_KEYBOARD_AXIS[] = {
    0xE9, 0x9F, 0x01, 0x00, 0x00, 0x6A, 0x00, 0xE8
};
#define OFFSET_KEYBOARD_AXIS_CALL 0x08

/* --- 0x0041481B  bapobj_setNodeYaw(obj, nodeIdx, degrees) ------------------------------------- *
 *   55 8B EC 83 EC 0C          prologue
 *   8B 45 08 / 89 45 F8        obj
 *   8B 4D F8 / 8B 91 9C000000  edx = obj->pThing          (bapObj +0x9C)
 *   89 55 F4
 *   8B 45 F4 / 8B 48 04        ecx = thing->pModel3       (rdThing +0x04)
 *   89 4D FC
 *   8B 55 FC / 8B 45 0C
 *   3B 42 54                   cmp nodeIdx, model->numNodes
 *   73 1A                      jae -> return 0            (SILENT failure out of range)
 *   8B 4D 0C / 6B C9 0C        idx * 12: one vec3 per node
 *   8B 55 F4 / 8B 42 24        eax = thing->pNodeRot      (rdThing +0x24)
 *   8B 55 10
 *   89 54 08 04                pNodeRot[idx].y = degrees  <- +4 is the YAW component
 *
 * The pattern has to run this far, and that is not thoroughness. bapobj_setNodePitch at 0x00414789
 * is byte-identical to this function except for two places: its `jae` displacement is 0x19 rather
 * than 0x1A, and its store is `89 14 08`, component [0], the PITCH, rather than `89 54 08 04`.
 * A pattern that stopped before the store would match both and could resolve to the wrong one.
 * There is not one address byte in the whole 63, so this needs no mask, and it matches exactly once
 * in every shipped image including the recompile. */
static const uint8_t SIG_SET_NODE_YAW[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
    0x8B, 0x45, 0x08, 0x89, 0x45, 0xF8,
    0x8B, 0x4D, 0xF8, 0x8B, 0x91, 0x9C, 0x00, 0x00, 0x00,
    0x89, 0x55, 0xF4,
    0x8B, 0x45, 0xF4, 0x8B, 0x48, 0x04, 0x89, 0x4D, 0xFC,
    0x8B, 0x55, 0xFC, 0x8B, 0x45, 0x0C,
    0x3B, 0x42, 0x54,
    0x73, 0x1A,
    0x8B, 0x4D, 0x0C, 0x6B, 0xC9, 0x0C,
    0x8B, 0x55, 0xF4, 0x8B, 0x42, 0x24,
    0x8B, 0x55, 0x10,
    0x89, 0x54, 0x08, 0x04
};

enum {
    SITE_PLAYER_PHASE_TABLE,
    SITE_PLAYER_SAVE_HEAD,
    SITE_PLAYER_MOUSE_AXIS,
    SITE_PLAYER_KEYBOARD_AXIS,
    SITE_SET_NODE_YAW,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("player_phase_table",   SIG_PLAYER_PHASE_TABLE),
    SIGNATURE_ENTRY_MASKED("player_save_head", SIG_PLAYER_SAVE_HEAD, MSK_PLAYER_SAVE_HEAD),
    SIGNATURE_ENTRY("player_mouse_axis",    SIG_PLAYER_MOUSE_AXIS),
    SIGNATURE_ENTRY("player_keyboard_axis", SIG_PLAYER_KEYBOARD_AXIS),
    SIGNATURE_ENTRY("set_node_yaw",         SIG_SET_NODE_YAW)
};

/* ============================================================================================ */
static bool resolve_phase_table(player_sites_t *out)
{
    uint32_t  table_address = 0;
    uint32_t *table;
    int       index;

    if (sites[SITE_PLAYER_PHASE_TABLE].address == 0) {
        log_warning("player_phase_table did not resolve, mouse look and strafe stay OFF");
        return false;
    }
    if (!memory_read_u32(sites[SITE_PLAYER_PHASE_TABLE].address + OFFSET_PHASE_TABLE_OPERAND,
                         &table_address) ||
        !memory_is_inside_image(table_address, (PHASE_COUNT + 1) * sizeof(uint32_t))) {
        log_warning("the phase table would be at %08X, outside the image, refused",
                    (unsigned)table_address);
        return false;
    }
    table = (uint32_t *)(uintptr_t)table_address;

    /* Cross-check the SHAPE, not addresses: 13 pointers that all land in the image, followed by
     * the terminator 1. If that does not hold, we found something other than what we wanted. */
    for (index = 0; index < PHASE_COUNT; ++index) {
        if (!memory_is_inside_image(table[index], 1)) {
            log_warning("phase table %08X: entry %d = %08X is outside the image, refused",
                        (unsigned)table_address, index, (unsigned)table[index]);
            return false;
        }
    }
    if (table[PHASE_COUNT] != PHASE_TERMINATOR) {
        log_warning("phase table %08X: terminator is %08X instead of %d, refused",
                    (unsigned)table_address, (unsigned)table[PHASE_COUNT], PHASE_TERMINATOR);
        return false;
    }

    out->phase_table = table;
    log_info("phase table %08X, steer=%08X integrate=%08X", (unsigned)table_address,
             (unsigned)table[PHASE_STEER], (unsigned)table[PHASE_INTEGRATE]);
    return true;
}

/* The mode descriptor table, out of player_save's own operand. Optional: without it the mode is
 * unknown, and an unknown mode means this DLL keeps its hands off entirely, which is the state
 * it was silently in for far too long, so it is now said out loud. */
static void resolve_mode_table(player_sites_t *out)
{
    uint32_t     table_address = 0;
    void *const *table;
    int          count = 0;

    if (sites[SITE_PLAYER_SAVE_HEAD].address == 0 ||
        !memory_read_u32(sites[SITE_PLAYER_SAVE_HEAD].address + OFFSET_MODE_TABLE_OPERAND,
                         &table_address) ||
        !memory_is_inside_image(table_address, PLAYER_MODE_TABLE_MAX * sizeof(uint32_t))) {
        log_warning("the mode descriptor table did not resolve, the mode cannot be told apart, "
                    "so mouse look and strafe stay OFF rather than run in modes that must not "
                    "have them");
        return;
    }

    table = (void *const *)(uintptr_t)table_address;

    /* Shape check: a run of pointers into the image, then the terminating NULL. The shipped table
     * has exactly 14, and a different count means this is not the table we think it is. */
    while (count < PLAYER_MODE_TABLE_MAX && table[count] != NULL) {
        if (!memory_is_inside_image((uintptr_t)table[count], 1)) {
            log_warning("mode table %08X: entry %d is outside the image, refused",
                        (unsigned)table_address, count);
            return;
        }
        ++count;
    }
    if (count != PLAYER_MODE_MAX + 1) {
        log_warning("mode table %08X has %d entries, expected %d, refused",
                    (unsigned)table_address, count, PLAYER_MODE_MAX + 1);
        return;
    }

    out->mode_table = table;
    log_info("mode table %08X, %d modes, the live mode is pCurMode's index in it, not the save "
             "tail's stale copy", (unsigned)table_address, count);
}

static bool resolve_axis_readers(player_sites_t *out)
{
    uintptr_t mouse_site = sites[SITE_PLAYER_MOUSE_AXIS].address;
    uintptr_t keyboard_site = sites[SITE_PLAYER_KEYBOARD_AXIS].address;
    uint32_t  player_pointer_address = 0;
    uint8_t   opcode = 0;
    uint32_t  displacement = 0;
    uintptr_t callee;

    if (mouse_site == 0) {
        log_warning("player_mouse_axis did not resolve, mouse look stays OFF");
        return false;
    }

    /* pPlayer is the operand of the `mov eax,[abs32]` immediately BEFORE the pattern. The opcode
     * is checked before the operand is believed, never take four bytes blind. */
    if (!memory_read_u8(mouse_site - 5, &opcode) || opcode != OPCODE_MOV_EAX_ABS32) {
        log_warning("no `mov eax,[abs32]` in front of the mouse site (found %02X), refused",
                    opcode);
        return false;
    }
    if (!memory_read_u32((uintptr_t)((intptr_t)mouse_site + OFFSET_MOUSE_AXIS_PLAYER),
                         &player_pointer_address) ||
        !memory_is_inside_image(player_pointer_address, sizeof(uint32_t))) {
        log_warning("pPlayer would be at %08X, outside the image, refused",
                    (unsigned)player_pointer_address);
        return false;
    }
    out->player_pointer = (uint8_t **)(uintptr_t)player_pointer_address;

    /* The pattern ends ON the E8, so the displacement follows it directly. */
    if (!memory_read_u32(mouse_site + OFFSET_MOUSE_AXIS_CALL, &displacement)) {
        return false;
    }
    callee = mouse_site + OFFSET_MOUSE_AXIS_CALL + 4 + displacement;
    if (!memory_is_inside_image(callee, 1)) {
        log_warning("the relative axis reader would be at %08X, refused", (unsigned)callee);
        return false;
    }
    out->read_relative_axis = (input_axis_fn_t)callee;

    if (keyboard_site != 0 &&
        memory_read_u32(keyboard_site + OFFSET_KEYBOARD_AXIS_CALL, &displacement)) {
        callee = keyboard_site + OFFSET_KEYBOARD_AXIS_CALL + 4 + displacement;
        if (memory_is_inside_image(callee, 1)) {
            out->read_absolute_axis = (input_axis_fn_t)callee;
        } else {
            log_warning("the absolute axis reader would be at %08X, strafe stays OFF",
                        (unsigned)callee);
        }
    } else {
        log_warning("player_keyboard_axis did not resolve, strafe stays OFF");
    }

    log_info("pPlayer=%08X relativeAxis=%08X absoluteAxis=%08X",
             (unsigned)player_pointer_address, (unsigned)(uintptr_t)out->read_relative_axis,
             (unsigned)(uintptr_t)out->read_absolute_axis);
    return true;
}

/* ============================================================================================ */
/* The parameter is deliberately not called `sites`: that name belongs to this file's own signature
 * table above, and shadowing it would hide which of the two a line refers to. */
uint8_t *player_sites_record(const player_sites_t *resolved)
{
    uint8_t *record;

    if (resolved == NULL || resolved->player_pointer == NULL) {
        return NULL;
    }
    record = *resolved->player_pointer;
    if (record == NULL || !memory_is_readable_range((uintptr_t)record, PLAYER_RECORD_SIZE)) {
        return NULL;
    }
    return record;
}

int player_sites_mode_index(const player_sites_t *resolved, const uint8_t *record)
{
    const void *current;
    int         index;

    if (resolved == NULL || record == NULL || resolved->mode_table == NULL) {
        return -1;
    }

    current = *(const void *const *)(record + PLAYER_CUR_MODE);
    if (current == NULL) {
        return -1;
    }

    for (index = 0; index < PLAYER_MODE_TABLE_MAX; ++index) {
        if (resolved->mode_table[index] == NULL) {
            break;
        }
        if (resolved->mode_table[index] == current) {
            return index;
        }
    }
    return -1;
}

/* ============================================================================================ */
bool player_sites_resolve(player_sites_t *out)
{
    out->phase_table        = NULL;
    out->mode_table         = NULL;
    out->player_pointer     = NULL;
    out->read_relative_axis = NULL;
    out->read_absolute_axis = NULL;
    out->set_node_yaw       = NULL;

    (void)signature_resolve_table(sites, SITE_COUNT);

    if (!resolve_phase_table(out)) {
        return false;
    }
    /* Optional, and its absence is a REFUSAL rather than a shrug: without the mode table the mode
     * cannot be told apart, and the phase-2 gate then keeps its hands off every frame. */
    resolve_mode_table(out);
    if (!resolve_axis_readers(out)) {
        return false;
    }

    /* Optional. Without it the walk still comes out sideways with the right clip and the right
     * speed; the character simply keeps facing the way he looks, which is how the sidestep behaved
     * before. So a missing entry costs the appearance, not the feature. */
    out->set_node_yaw = (set_node_yaw_fn_t)sites[SITE_SET_NODE_YAW].address;
    if (out->set_node_yaw == NULL) {
        log_warning("bapobj_setNodeYaw did not resolve, sideways walking works, but the body will "
                    "keep facing the way the camera looks instead of the way it travels");
    }
    return true;
}
