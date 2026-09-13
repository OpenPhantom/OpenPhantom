/* diag_dialogue_ops.c: see diag_dialogue_ops.h. */
#include "diag_dialogue_ops.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/frame_hook.h"
#include "common/memory.h"
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

/* --- Plr_RunPhases 0x00448297, a data site only ------------------------------------------------ *
 * The same site diag_characters.c and diag_flow.c read the player pointer from, at +0x27; each
 * observer keeps its own copy so the evidence sits beside the code that depends on it. Searched
 * as a detour target because diag_flow.c detours it under Player=1 and gets there first. */
static const uint8_t SIG_PLAYER_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
#define PLAYER_RUN_PHASES_PROLOGUE 6u
#define OFFSET_PLAYER_POINTER      0x27u

/* The player's drawn body and the two clips on it: the same offsets the character census reads,
 * a player record's body at +0x0C, the base clip at +0xE8 and the overlay clip at +0xF4. The
 * player is not in the character pool, so the census never shows it; during a conversation the
 * player's own listening and talking clips live here. */
#define PLAYER_OBJECT_OFFSET       0x0Cu
#define OBJECT_BASE_CLIP_OFFSET    0xE8u
#define OBJECT_BASE_SLOT_OFFSET    0xECu    /* the puppet track the base clip is on */
#define OBJECT_OVERLAY_CLIP_OFFSET 0xF4u
#define OBJECT_OWNER_OFFSET        0xA0u    /* the character record driving this body, if any */
#define OBJECT_THING_OFFSET        0x9Cu
#define THING_PUPPET_OFFSET        0x18u
#define PUPPET_TRACKS_OFFSET       0x08u
#define PUPPET_TRACK_STRIDE        0x14Cu
#define TRACK_COMPLETE_OFFSET      0x140u
#define OWNER_ANIM_PLAYING_OFFSET  0x1BCu   /* the character record's own animation pair */
#define OWNER_ANIM_WANTED_OFFSET   0x1C0u

enum {
    SITE_MENU_OP,
    SITE_STATEMENT_OP,
    SITE_CHECK_FOR_OP,
    SITE_PLAYER_RUN_PHASES,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("op_dialog_box", SIG_MENU_OP, MSK_MENU_OP, MENU_OP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("op_statement", SIG_STATEMENT_OP, STATEMENT_OP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("op_check_for", SIG_CHECK_FOR_OP, MSK_CHECK_FOR_OP,
                                  CHECK_FOR_OP_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("player_run_phases", SIG_PLAYER_RUN_PHASES,
                           PLAYER_RUN_PHASES_PROLOGUE)
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

    /* the player's body clips, one line per change */
    uint32_t *player_slot;
    int32_t   player_base;
    int32_t   player_overlay;
    int32_t   player_complete;
    int32_t   player_wanted;
    bool      player_seen;
} ops;

/* The complete flag of the track the base clip is on, or -1 when the chain does not read. */
static int32_t base_track_complete(uint32_t body)
{
    uint32_t thing = 0;
    uint32_t puppet = 0;
    int32_t  slot = -1;
    int32_t  complete = -1;

    if (!memory_try_read((uintptr_t)body + OBJECT_THING_OFFSET, &thing, sizeof thing) ||
        thing == 0 ||
        !memory_try_read((uintptr_t)thing + THING_PUPPET_OFFSET, &puppet, sizeof puppet) ||
        puppet == 0 ||
        !memory_try_read((uintptr_t)body + OBJECT_BASE_SLOT_OFFSET, &slot, sizeof slot) ||
        slot < 0 || slot > 7 ||
        !memory_try_read((uintptr_t)puppet + PUPPET_TRACKS_OFFSET +
                         (uint32_t)slot * PUPPET_TRACK_STRIDE + TRACK_COMPLETE_OFFSET,
                         &complete, sizeof complete)) {
        return -1;
    }
    return complete;
}

static void player_clips_tick(void)
{
    uint32_t player = 0;
    uint32_t body = 0;
    int32_t  base = -1;
    int32_t  overlay = -1;

    if (ops.player_slot == NULL ||
        !memory_try_read((uintptr_t)ops.player_slot, &player, sizeof player) || player == 0 ||
        !memory_try_read((uintptr_t)player + PLAYER_OBJECT_OFFSET, &body, sizeof body) ||
        body == 0 ||
        !memory_try_read((uintptr_t)body + OBJECT_BASE_CLIP_OFFSET, &base, sizeof base) ||
        !memory_try_read((uintptr_t)body + OBJECT_OVERLAY_CLIP_OFFSET, &overlay,
                         sizeof overlay)) {
        return;
    }
    {
        int32_t  complete = base_track_complete(body);
        uint32_t owner = 0;
        int32_t  wanted = -1;
        int32_t  playing = -1;

        if (memory_try_read((uintptr_t)body + OBJECT_OWNER_OFFSET, &owner, sizeof owner) &&
            owner != 0) {
            (void)memory_try_read((uintptr_t)owner + OWNER_ANIM_WANTED_OFFSET, &wanted,
                                  sizeof wanted);
            (void)memory_try_read((uintptr_t)owner + OWNER_ANIM_PLAYING_OFFSET, &playing,
                                  sizeof playing);
        }
        if (!ops.player_seen || base != ops.player_base || overlay != ops.player_overlay ||
            complete != ops.player_complete || wanted != ops.player_wanted) {
            diag_log_write("dlg  player body clips base=%d overlay=%d complete=%d, owner %08X "
                           "asks %d and believes %d (body %08X)", (int)base, (int)overlay,
                           (int)complete, (unsigned)owner, (int)wanted, (int)playing,
                           (unsigned)body);
            ops.player_base     = base;
            ops.player_overlay  = overlay;
            ops.player_complete = complete;
            ops.player_wanted   = wanted;
            ops.player_seen     = true;
        }
    }
}

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
    ops.player_slot = (uint32_t *)diag_derive_address(sites, SITE_PLAYER_RUN_PHASES,
                                                      OFFSET_PLAYER_POINTER, "pPlayer");
    if (ops.player_slot != NULL && frame_hook_add(player_clips_tick)) {
        diag_log_write("dlg  the player's body clips are reported on each change, base and "
                       "overlay layer");
        ++installed;
    }
    return installed;
}
