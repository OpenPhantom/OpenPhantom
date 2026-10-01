/* mp_input.c: four bank-aware detours over the engine's input readers.
 *
 * All four targets are cdecl, keyed by an action id, and read global state only, so a detour that
 * answers for a bank needs no player argument anywhere. The dispatch condition is the active bank
 * class rather than the swapped flag, because bank 1 is active in two different ways: swapped in
 * through the player pointer, or ticked through the content swap of the hero block, and the
 * readers must answer the injected command in both.
 *
 * The button reader's ABI is the one to be careful with. It takes TWO arguments, and the second is
 * an optional out slot the engine zeroes at entry and lets the platform layer accumulate hold time
 * into. Every player call site in the image passes NULL for it, but the hook keeps the exact two
 * argument shape and honours the zeroing, because a cdecl caller cleans eight bytes off the stack
 * whatever the callee thought its arity was.
 *
 * The tap-versus-hold reader owns none of its state: the CALLER owns the accumulator and the
 * engine only advances it, adding the frame delta while held and returning the accumulated time on
 * the release call. The injected path reproduces that contract against the same accumulator
 * pointer the caller passed, which during the second body's tick is the banked copy, so each bank
 * keeps its own hold time without this file doing anything about it.
 *
 * The other DLL that cares about these four calls them rather than hooking them: it lifts the
 * callee addresses out of the steer's own call sites at install time and keeps them as plain
 * function pointers. Those calls land on these detours and pass through at bank 0, and its
 * extraction reads call operands in code this module never writes, so the load order between the
 * two DLLs stays free.
 *
 * A third answer stands between the two. While a session's pause menu is up, or the player types
 * into the chat, the world keeps running and so does this player's body, and what stands still is
 * its input: at bank 0 every id the menus do not read answers as nothing is pressed, and a hold
 * banked before the hold began is emptied rather than frozen. The injected command of bank 1 comes
 * first and is untouched by it.
 */
#include "mp_input.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_pause_rule.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef float(__cdecl *read_axis_fn_t)(int32_t action);
typedef uint32_t(__cdecl *is_down_fn_t)(int32_t action, int32_t *time_out);
typedef float(__cdecl *hold_release_fn_t)(int32_t action, float *held_for);

typedef struct mp_input_state {
    bool               installed;
    bool               dead;          /* a mid-install failure: every hook passes through */
    bool               has_command;
    mp_input_command_t command;

    uintptr_t          frame_delta_cell;

    detour_t           digital_axis;
    detour_t           relative_axis;
    detour_t           is_down;
    detour_t           hold_release;

    uint32_t           injected_digital;
    uint32_t           injected_relative;
    uint32_t           injected_down;
    uint32_t           injected_hold;
    uint32_t           delta_faults;   /* the frame delta cell was unreadable on an injected hold */

    /* The same four reads while the pause menu or the chat held this player's input. */
    uint32_t           withheld_digital;
    uint32_t           withheld_relative;
    uint32_t           withheld_down;
    uint32_t           withheld_hold;
    uint32_t           holds_dropped;    /* a charge that was banked when the menu opened */
    uint32_t           hold_faults;      /* the caller's accumulator did not read or write */
    uint32_t           menu_reads;       /* ids the menus read, passed while the hold stood */
} mp_input_state_t;

static mp_input_state_t input;

bool mp_input_installed(void)
{
    return input.installed && !input.dead;
}

/* ==============================================================================================
 * The decision core. Pure, so the unit test can drive it without a game.
 * ============================================================================================ */

float mp_input_answer_digital_axis(const mp_input_command_t *command, int32_t action)
{
    float value;

    if (command == NULL) {
        return 0.0f;
    }
    if (action == 0) {
        value = command->turn_axis;
    } else if (action == 1) {
        value = command->move_axis;
    } else {
        return 0.0f;
    }

    /* The engine clamps the summed axis to -1..1 before anyone consumes it, so an injected value
     * honours the same bound rather than handing the steer a magnitude no hardware can produce.
     *
     * The sign is NOT calibrated. The injected value bypasses the binding table, and a shipped
     * binding may carry the invert flag, so the engine's own naming says nothing about the
     * screen; the one field run read the body's constant turn as a LEFT turn under stated
     * uncertainty. When a real command source exists the sign has to be pinned by one deliberate
     * run with a known command against the observed direction. */
    if (value < -1.0f) {
        return -1.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float mp_input_answer_relative_axis(const mp_input_command_t *command, int32_t action)
{
    if (command == NULL || action != 0) {
        return 0.0f;
    }
    return command->mouse_turn;  /* raw device ticks; the engine applies no bound here either */
}

uint32_t mp_input_answer_is_down(const mp_input_command_t *command, int32_t action)
{
    if (command == NULL || action < 0 || (uint32_t)action >= MP_INPUT_MAX_ACTIONS) {
        return 0u;
    }
    return (command->buttons >> action) & 1u;
}

/* Two departures from a literal copy of the engine's loop, both deliberate. Losing the command
 * mid press answers the release arm, so an accumulated press fires once instead of leaking into
 * the next command's first read. A NULL accumulator answers zero without writing; the engine
 * would fault there, and the hook has no assert handler behind it in the retail build. */
float mp_input_advance_hold(const mp_input_command_t *command, int32_t action,
                            float *held_for, float dt)
{
    float released = 0.0f;

    if (held_for == NULL) {
        return 0.0f;
    }
    if (mp_input_answer_is_down(command, action) != 0u) {
        *held_for += dt;
        return 0.0f;
    }
    if (*held_for > 0.0f) {
        released  = *held_for;
        *held_for = 0.0f;
    }
    return released;
}

mp_input_route_t mp_input_route(bool split_live, bool bank_one_active, bool input_held,
                                int32_t action)
{
    if (!split_live) {
        return MP_INPUT_ROUTE_ENGINE;
    }
    if (bank_one_active) {
        return MP_INPUT_ROUTE_BANK;
    }
    return input_held && mp_pause_rule_holds_action(action) ? MP_INPUT_ROUTE_HELD
                                                            : MP_INPUT_ROUTE_ENGINE;
}

float mp_input_hold_withheld(float banked, bool *dropped)
{
    if (dropped != NULL) {
        *dropped = banked > 0.0f;
    }
    return 0.0f;
}

/* ==============================================================================================
 * The hooks. At bank 0 each one is the original, unless the pause menu or the chat holds the
 * input; at any other bank the command answers. The counts they keep were held against a field
 * run of 322 ticked substeps: 644 digital reads, two per substep (the steer's move axis and its
 * keyboard turn after the relative read answered zero), 322 relative, one per substep, and 1932
 * button reads, six per substep, three from the ground actions (sidle, jump, use) and three from
 * mode owned descriptor words (run in the stand clip select, attack and jump in the attack
 * timers), which are exactly the reads a phase plan replacement would have missed; 0 hold reads,
 * as the withheld input phase predicts.
 * ============================================================================================ */

/* Which answer this read gets. A read the menus make while the hold stands is counted as passed,
 * which is the evidence that the menu stayed usable with a pad. */
static mp_input_route_t route_of(int32_t action)
{
    bool             live = input.installed && !input.dead;
    bool             held = live && mp_armed_input_held();
    mp_input_route_t route = mp_input_route(live, live && mp_bank_active_class() != 1, held,
                                            action);

    if (held && route == MP_INPUT_ROUTE_ENGINE) {
        ++input.menu_reads;
    }
    return route;
}

static const mp_input_command_t *active_command(void)
{
    return input.has_command ? &input.command : NULL;
}

/* engine: f32 control_readAxis(i32 axisId) */
static float __cdecl hook_digital_axis(int32_t action)
{
    switch (route_of(action)) {
    case MP_INPUT_ROUTE_BANK:
        ++input.injected_digital;
        return mp_input_answer_digital_axis(active_command(), action);
    case MP_INPUT_ROUTE_HELD:
        ++input.withheld_digital;
        return 0.0f;
    case MP_INPUT_ROUTE_ENGINE:
    default:
        return ((read_axis_fn_t)input.digital_axis.original)(action);
    }
}

/* engine: f32 control_readAxisRelative(i32 axisId) */
static float __cdecl hook_relative_axis(int32_t action)
{
    switch (route_of(action)) {
    case MP_INPUT_ROUTE_BANK:
        ++input.injected_relative;
        return mp_input_answer_relative_axis(active_command(), action);
    case MP_INPUT_ROUTE_HELD:
        ++input.withheld_relative;
        return 0.0f;
    case MP_INPUT_ROUTE_ENGINE:
    default:
        return ((read_axis_fn_t)input.relative_axis.original)(action);
    }
}

/* The target reads both arguments: at 004653DB it tests the second against zero and, when it is
 * a pointer, zeroes the pointee before the scan, and the platform layer below then adds each
 * binding's hold time into it. A byte census over all 28 call sites in the image shows the
 * pushed pair as `push 0; push <action>` at every one, so the slot is dead in the shipped build;
 * the stand-in keeps the shape and the zeroing so a caller that does pass it reads a defined
 * value.
 *
 * engine: u32 control_isDown(i32 keyId, i32 *time) */
static uint32_t __cdecl hook_is_down(int32_t action, int32_t *time_out)
{
    mp_input_route_t route = route_of(action);

    if (route == MP_INPUT_ROUTE_ENGINE) {
        return ((is_down_fn_t)input.is_down.original)(action, time_out);
    }
    if (time_out != NULL) {
        *time_out = 0;  /* the engine zeroes the optional out slot at entry; so does the stand-in */
    }
    if (route == MP_INPUT_ROUTE_HELD) {
        ++input.withheld_down;
        return 0u;
    }
    ++input.injected_down;
    return mp_input_answer_is_down(active_command(), action);
}

/* A held tap-versus-hold read: nothing is pressed, and the caller's accumulator is emptied, so a
 * charge banked before the menu opened cannot come out as a release after it closed. The
 * accumulator is the caller's own memory, so it is read and written through the guarded helpers. */
static float withhold_the_hold(float *held_for)
{
    float banked = 0.0f;
    float emptied;
    bool  dropped = false;

    ++input.withheld_hold;
    if (held_for == NULL) {
        return 0.0f;
    }
    if (!memory_try_read((uintptr_t)held_for, &banked, sizeof banked)) {
        ++input.hold_faults;
        return 0.0f;
    }
    emptied = mp_input_hold_withheld(banked, &dropped);
    if (!dropped) {
        return 0.0f;
    }
    if (!memory_try_write((uintptr_t)held_for, &emptied, sizeof emptied)) {
        ++input.hold_faults;
        return 0.0f;
    }
    ++input.holds_dropped;
    return 0.0f;
}

/* The engine's version advances the accumulator by the frame delta at 00868714 while any binding
 * is down and returns the accumulated seconds on the release call only. Its one player side
 * caller passes the accumulator that sits one dword past the hero block, which the bank copies
 * with the block, so during the tick the pointer arriving here is already the banked copy.
 *
 * engine: f32 control_takeHoldTime(i32 keyId, f32 *pHeldFor) */
static float __cdecl hook_hold_release(int32_t action, float *held_for)
{
    float dt = 0.0f;

    switch (route_of(action)) {
    case MP_INPUT_ROUTE_HELD:
        return withhold_the_hold(held_for);
    case MP_INPUT_ROUTE_BANK:
        break;
    case MP_INPUT_ROUTE_ENGINE:
    default:
        return ((hold_release_fn_t)input.hold_release.original)(action, held_for);
    }
    ++input.injected_hold;
    if (!memory_try_read(input.frame_delta_cell, &dt, sizeof(dt))) {
        ++input.delta_faults;
        dt = 0.0f;  /* a hold that does not advance is recoverable; a fault here is not */
    }
    return mp_input_advance_hold(active_command(), action, held_for, dt);
}

/* ==============================================================================================
 * Installation and reporting.
 * ============================================================================================ */

void mp_input_set_command(const mp_input_command_t *command)
{
    if (command == NULL) {
        mp_input_clear_command();
        return;
    }
    input.command     = *command;
    input.has_command = true;
}

void mp_input_clear_command(void)
{
    input.has_command         = false;
    input.command.turn_axis   = 0.0f;
    input.command.move_axis   = 0.0f;
    input.command.mouse_turn  = 0.0f;
    input.command.buttons     = 0u;
}

typedef struct input_target {
    mp_site_t   site;
    detour_t   *slot;
    const void *hook;
} input_target_t;

/* The four readers: the absolute axis at 0046507E, the relative axis at 004652C3, the button
 * at 004653CE and the hold at 00465502 in the retail image. All four open `55 8B EC 83 EC xx`,
 * three instructions and six bytes with no relative operand, which is what the trampoline needs.
 * The absolute axis and the hold open as near twins, the same two zeroed locals and the same
 * loop skip, differing in the frame size byte (0x24 against 0x20) and in the local slots (FC/F4
 * against F0/EC); their patterns run 28 bytes, through the first loop increment, and stop before
 * the binding table operand at byte 40, so no absolute address is a required byte. A census over
 * all six images finds each pattern exactly once per image, the recompile at the moved entries
 * 0046501E and 004654A2. */
bool mp_input_install(void)
{
    /* Order matters only for the log: a refusal names the first missing site. */
    const input_target_t targets[4] = {
        { MP_SITE_INPUT_DIGITAL_AXIS, &input.digital_axis,  (const void *)&hook_digital_axis  },
        { MP_SITE_INPUT_AXIS,         &input.relative_axis, (const void *)&hook_relative_axis },
        { MP_SITE_INPUT_IS_HELD,      &input.is_down,       (const void *)&hook_is_down       },
        { MP_SITE_INPUT_HOLD_RELEASE, &input.hold_release,  (const void *)&hook_hold_release  },
    };
    size_t i;

    if (input.installed) {
        return !input.dead;
    }

    /* Everything is checked before anything is written, because the detours cannot be removed. */
    for (i = 0; i < 4; ++i) {
        if (mp_signatures_address(targets[i].site) == 0) {
            log_warning("the input split cannot install: reader %u did not resolve", (unsigned)i);
            return false;
        }
    }
    input.frame_delta_cell = mp_cells_address(MP_CELL_FRAME_DELTA);
    if (input.frame_delta_cell == 0) {
        log_warning("the input split cannot install: the frame delta cell did not resolve");
        return false;
    }

    for (i = 0; i < 4; ++i) {
        if (!detour_install(targets[i].slot, mp_signatures_address(targets[i].site),
                            targets[i].hook, mp_signatures_prologue(targets[i].site))) {
            /* Some hooks are already placed and cannot be taken back, so they are switched to
             * pass-through for every bank, which is byte for byte the pre-install behaviour. */
            input.dead      = true;
            input.installed = true;
            log_error("the input split failed on reader %u of 4 and is disabled; the hooks "
                      "already placed pass every call through", (unsigned)i);
            return false;
        }
    }

    input.installed = true;
    mp_input_clear_command();
    log_info("the input split stands: four readers answer per bank, and bank 1 reads an injected "
             "command instead of the keys");
    return true;
}

/* The actions a command carries, by engine id: attack, jump, force, sidle, use, run, the six
 * weapon slots and the two cycles. The sample reads each one's held state. The objectives key is
 * not sampled: its only consumer is the input phase's quick save, which is the local machine's. */
static const int32_t sampled_actions[] = {
    2, 3, 4, 5, 6, 7, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0x15
};

bool mp_input_sample_local(mp_input_command_t *out)
{
    size_t i;

    if (out == NULL || !input.installed || input.dead) {
        return false;
    }
    out->turn_axis  = ((read_axis_fn_t)input.digital_axis.original)(0);
    out->move_axis  = ((read_axis_fn_t)input.digital_axis.original)(1);
    out->mouse_turn = 0.0f;
    out->buttons    = 0u;
    for (i = 0; i < sizeof sampled_actions / sizeof sampled_actions[0]; ++i) {
        if (((is_down_fn_t)input.is_down.original)(sampled_actions[i], NULL) != 0u) {
            out->buttons |= 1u << sampled_actions[i];
        }
    }
    return true;
}

void mp_input_report(const char *why)
{
    if (!input.installed) {
        return;
    }
    if (input.dead) {
        log_info("the input split at %s: dead after a partial install, every call passed through",
                 why);
        return;
    }
    log_info("the input split at %s: %u digital, %u relative, %u button and %u hold reads "
             "answered from the injected command%s",
             why, (unsigned)input.injected_digital, (unsigned)input.injected_relative,
             (unsigned)input.injected_down, (unsigned)input.injected_hold,
             input.has_command ? "" : ", which was empty, so the second body read stillness");
    if (input.delta_faults != 0) {
        log_warning("the frame delta cell was unreadable on %u injected hold read(s)",
                    (unsigned)input.delta_faults);
    }
    log_info("  the local input held by the pause menu or the chat: withheld %u axis, %u relative, "
             "%u button and %u hold read(s), %u hold(s) dropped (%u accumulator(s) unreadable); "
             "%u read(s) of the menu's own actions passed",
             (unsigned)input.withheld_digital, (unsigned)input.withheld_relative,
             (unsigned)input.withheld_down, (unsigned)input.withheld_hold,
             (unsigned)input.holds_dropped, (unsigned)input.hold_faults,
             (unsigned)input.menu_reads);
}
