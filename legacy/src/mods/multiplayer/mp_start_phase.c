/* mp_start_phase.c: the title menu drive's one value. See the header. */
#include "mp_start_phase.h"

#include <stdbool.h>
#include <stdint.h>

void mp_start_machine_ask(mp_start_machine_t *machine, mp_start_kind_t kind)
{
    machine->kind         = kind;
    machine->phase        = MP_START_PHASE_ASKED;
    machine->rounds       = 0u;
    machine->focus_misses = 0u;
}

bool mp_start_machine_wants_focus(const mp_start_machine_t *machine)
{
    return machine->kind != MP_START_NONE && machine->phase == MP_START_PHASE_FOCUS_SET;
}

mp_start_action_t mp_start_machine_drive(mp_start_machine_t *machine, bool focus_holds)
{
    /* Nothing asked, a map whose accept went, a savegame the load screen took: the phase goes
     * down on the next frame whatever it was, which is the step the old guard never cleared. */
    if (machine->kind == MP_START_NONE) {
        machine->phase = MP_START_PHASE_IDLE;
        return MP_START_ACT_PASS;
    }
    switch (machine->phase) {
    case MP_START_PHASE_FOCUS_SET:
        if (!focus_holds) {
            if (++machine->focus_misses >= MP_START_FOCUS_MISSES_MAX) {
                mp_start_machine_cancel(machine);
                return MP_START_ACT_GIVE_UP;
            }
            return MP_START_ACT_FOCUS;   /* set again; the round is not counted */
        }
        /* A savegame keeps its kind for the load screen's detour; a map is done with it. */
        machine->phase = MP_START_PHASE_ACCEPT_SENT;
        if (machine->kind != MP_START_SAVE) {
            machine->kind = MP_START_NONE;
        }
        return MP_START_ACT_ACCEPT;
    case MP_START_PHASE_ACCEPT_SENT:
        /* Only a savegame is still here: the accept did not reach the load screen. */
        if (++machine->rounds >= MP_START_DRIVE_ROUNDS) {
            mp_start_machine_cancel(machine);
            return MP_START_ACT_GIVE_UP;
        }
        machine->phase = MP_START_PHASE_ASKED;
        return MP_START_ACT_PASS;
    case MP_START_PHASE_IDLE:
    case MP_START_PHASE_ASKED:
    default:
        machine->phase = MP_START_PHASE_FOCUS_SET;
        return MP_START_ACT_FOCUS;
    }
}

/* A request still waiting for its first focus is nothing asked once its kind has gone; in any
 * later phase the next frame puts it down. */
void mp_start_machine_consume(mp_start_machine_t *machine)
{
    if (machine->kind != MP_START_SAVE) {
        return;
    }
    machine->kind = MP_START_NONE;
    if (machine->phase == MP_START_PHASE_ASKED) {
        machine->phase = MP_START_PHASE_IDLE;
    }
}

void mp_start_machine_cancel(mp_start_machine_t *machine)
{
    machine->kind   = MP_START_NONE;
    machine->phase  = MP_START_PHASE_IDLE;
    machine->rounds = 0u;
}

bool mp_start_machine_pending(const mp_start_machine_t *machine)
{
    return machine->phase != MP_START_PHASE_IDLE;
}
