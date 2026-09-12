/* diag_dialogue_ops.c: see diag_dialogue_ops.h. */
#include "diag_dialogue_ops.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- opcode 0x500 "Dialog Box", ai_runMenu 0x004358B0 ----------------------------------------- *
 * The same pattern dialogue_anim_fix uses for it: one masked call and one absolute cell, the
 * suspend flag, inside the head. The prologue is thirteen bytes to a clean boundary. Its answer
 * is 0 while the menu is open or the visit was refused, 1 when a row was chosen, 2 with no rows.
 * The operands: A[0] the camera group, A[1] the clip, A[2] the line. */
static const uint8_t SIG_MENU_OP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xC0, 0x75, 0x09,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x0C
};
static const uint8_t MSK_MENU_OP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_MENU_OP == sizeof MSK_MENU_OP,
               "the menu opcode pattern and its mask are different lengths");
#define MENU_OP_PROLOGUE 13u

/* --- opcode 0x504 "Statement", say_line 0x00435A0A ------------------------------------------- *
 * No absolute address in it. A[0] the voice slot, A[1] the line, remembered at actor+0x78. */
static const uint8_t SIG_STATEMENT_OP[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x10, 0x8B, 0x51, 0x04, 0x89, 0x50, 0x78,
    0x8B, 0x45, 0x10, 0x8B, 0x08, 0x89, 0x4D, 0xFC,
    0x83, 0x7D, 0xFC, 0x10, 0x7C, 0x07,
    0xC7, 0x45, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF
};
#define STATEMENT_OP_PROLOGUE 16u

/* --- opcode 0x605 "Check For", op_checkFor 0x0042EB8D --------------------------------------- *
 * The gate a script puts in front of a conversation: mode 0 is the use key latched, 1 the
 * player's own talk flag, 2 the cinematic lock, 3 a line active, 4 the confirm edge, 5 the
 * player's reply playing, 6 the actor's line playing; 7 to 10 are difficulty, detail, water and
 * a player status slot. The head zeroes the answer, copies the mode and bounds it at ten before
 * the jump table, whose absolute address is masked. Prologue six bytes. */
static const uint8_t SIG_CHECK_FOR_OP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14,
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x0C, 0x89, 0x45, 0xF4,
    0x83, 0x7D, 0xF4, 0x0A, 0x0F, 0x87, 0x2E, 0x01, 0x00, 0x00,
    0x8B, 0x4D, 0xF4, 0xFF, 0x24, 0x8D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CHECK_FOR_OP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_CHECK_FOR_OP == sizeof MSK_CHECK_FOR_OP,
               "the check-for pattern and its mask are different lengths");
#define CHECK_FOR_OP_PROLOGUE 6u

enum {
    SITE_MENU_OP,
    SITE_STATEMENT_OP,
    SITE_CHECK_FOR_OP,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("op_dialog_box", SIG_MENU_OP, MSK_MENU_OP, MENU_OP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("op_statement", SIG_STATEMENT_OP, STATEMENT_OP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("op_check_for", SIG_CHECK_FOR_OP, MSK_CHECK_FOR_OP,
                                  CHECK_FOR_OP_PROLOGUE)
};

typedef int32_t (__cdecl *menu_op_fn_t)(int32_t actor, void *ip, const uint32_t *operands);
typedef void    (__cdecl *statement_op_fn_t)(int32_t actor, void *ip, const uint32_t *operands);
typedef int32_t (__cdecl *check_for_fn_t)(int32_t actor, uint32_t mode, int32_t arg);

static struct {
    detour_t menu_op;
    detour_t statement_op;
    detour_t check_for_op;

    /* the last (actor, mode, answer) of a check, one line per change */
    int32_t  check_actor;
    uint32_t check_mode;
    int32_t  check_result;

    /* the menu is visited every step while open, so one line per change of shape and a count */
    int32_t  last_actor;
    int32_t  last_line;
    int32_t  last_result;
    unsigned visits;
} ops;

static void flush_menu_streak(void)
{
    if (ops.visits > 1) {
        diag_log_write("dlg  menu op actor %08X line %d: the same answer %d for %u visits in a "
                       "row", (unsigned)ops.last_actor, (int)ops.last_line, (int)ops.last_result,
                       ops.visits - 1);
    }
    ops.visits = 0;
}

static int32_t __cdecl hook_menu_op(int32_t actor, void *ip, const uint32_t *operands)
{
    int32_t line = (int32_t)operands[2];
    int32_t result = ((menu_op_fn_t)ops.menu_op.original)(actor, ip, operands);

    if (actor != ops.last_actor || line != ops.last_line || result != ops.last_result) {
        flush_menu_streak();
        diag_log_write("dlg  menu op actor %08X line %d camera=%d -> %d (%s)", (unsigned)actor,
                       (int)line, (int)operands[0], (int)result,
                       (result == 1) ? "a row was chosen"
                                     : (result == 2) ? "no rows, over"
                                                     : "open, or the visit was refused");
        ops.last_actor  = actor;
        ops.last_line   = line;
        ops.last_result = result;
    }
    ++ops.visits;
    return result;
}

static void __cdecl hook_statement_op(int32_t actor, void *ip, const uint32_t *operands)
{
    flush_menu_streak();
    ops.last_actor = 0;
    diag_log_write("dlg  statement op actor %08X line %d slot %d", (unsigned)actor,
                   (int)operands[1], (int)operands[0]);
    ((statement_op_fn_t)ops.statement_op.original)(actor, ip, operands);
}

/* Modes 0 to 6 are the dialogue ones; the rest are left to the opcode trace. */
static int32_t __cdecl hook_check_for_op(int32_t actor, uint32_t mode, int32_t arg)
{
    int32_t result = ((check_for_fn_t)ops.check_for_op.original)(actor, mode, arg);

    if (mode <= 6 &&
        (actor != ops.check_actor || mode != ops.check_mode || result != ops.check_result)) {
        diag_log_write("dlg  check-for op actor %08X mode %u arg %d -> %d", (unsigned)actor,
                       (unsigned)mode, (int)arg, (int)result);
        ops.check_actor  = actor;
        ops.check_mode   = mode;
        ops.check_result = result;
    }
    return result;
}

int diag_dialogue_ops_install(int dialogue_level)
{
    int installed = 0;

    if (dialogue_level <= 0) {
        return 0;
    }
    signature_resolve_table(sites, SITE_COUNT);
    installed += diag_install_observer(sites, SITE_MENU_OP, &ops.menu_op,
                                       (const void *)hook_menu_op, MENU_OP_PROLOGUE,
                                       "opcode 0x500 Dialog Box, each change of actor, line or "
                                       "answer") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_STATEMENT_OP, &ops.statement_op,
                                       (const void *)hook_statement_op, STATEMENT_OP_PROLOGUE,
                                       "opcode 0x504 Statement, every one") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_CHECK_FOR_OP, &ops.check_for_op,
                                       (const void *)hook_check_for_op, CHECK_FOR_OP_PROLOGUE,
                                       "opcode 0x605 Check For, the dialogue modes, each change")
                 ? 1 : 0;
    return installed;
}
