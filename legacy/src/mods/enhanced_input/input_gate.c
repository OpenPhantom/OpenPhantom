/* input_gate.c: the engine's own input lock, which is not a flag but the absence of a phase.
 *
 * The whole mechanism is in the header. What is here is the one measurement it rests on: the
 * simulation is pinned to 1/32 s, so a player phase runs about every 31 ms while the engine is
 * running them at all. Four steps of tolerance absorbs an ordinary long frame, a level load or a
 * hitch, and still closes the gate an eighth of a second after the phases stop.
 *
 * It is deliberately a time and not a frame count. The render rate is free in this build, so a
 * count of frames would mean a different amount of real time on every machine, and the thing
 * being detected happens on the simulation clock.
 */
#include "input_gate.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>

#define PHASES_STALE_AFTER_MS 125u

typedef struct input_gate_state {
    bool  phase_seen;
    DWORD phase_last_ms;
    bool  logged_closed;
} input_gate_state_t;

static input_gate_state_t gate;

void input_gate_note_phase_ran(void)
{
    gate.phase_seen = true;
    gate.phase_last_ms = GetTickCount();
}

bool input_gate_is_open(void)
{
    return gate.phase_seen && (GetTickCount() - gate.phase_last_ms) <= PHASES_STALE_AFTER_MS;
}

void input_gate_note_closed(void)
{
    if (gate.logged_closed) {
        return;
    }
    gate.logged_closed = true;
    log_info("the player phases are not running, so the view is not being turned and the mouse "
             "movement is dropped rather than banked. This is the state the engine puts a menu, a "
             "dialogue and a cutscene in. Reported once.");
}
