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
#include "common/session_note.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>

#define PHASES_STALE_AFTER_MS 125u

typedef struct input_gate_state {
    bool     phase_seen;
    DWORD    phase_last_ms;
    bool     logged_closed;
    bool     held;
    DWORD    held_since_ms;
    uint32_t holds;
} input_gate_state_t;

static input_gate_state_t gate;

void input_gate_note_phase_ran(void)
{
    gate.phase_seen = true;
    gate.phase_last_ms = GetTickCount();
}

bool input_gate_session_holds(void)
{
    DWORD now  = GetTickCount();
    bool  held = session_note_input_held((uint32_t)now);

    if (held && !gate.held) {
        ++gate.holds;
        gate.held_since_ms = now;
        log_info("the player's input is held by the multiplayer session (hold %u), by its pause "
                 "menu, its chat or a scene: the view is not turned, the pad walk is left to the "
                 "engine's own reading, and what the mouse banked is dropped",
                 (unsigned)gate.holds);
    } else if (!held && gate.held) {
        log_info("the player's input is free again after the multiplayer session held it for %lu "
                 "ms", (unsigned long)(now - gate.held_since_ms));
    }
    gate.held = held;
    return held;
}

bool input_gate_is_open(void)
{
    if (!gate.phase_seen || (GetTickCount() - gate.phase_last_ms) > PHASES_STALE_AFTER_MS) {
        return false;
    }
    return !input_gate_session_holds();
}

void input_gate_note_closed(void)
{
    if (gate.logged_closed || gate.held) {
        return;
    }
    gate.logged_closed = true;
    log_info("the player phases are not running, so the view is not being turned and the mouse "
             "movement is dropped rather than banked. This is the state the engine puts a menu, a "
             "dialogue and a cutscene in. Reported once.");
}
