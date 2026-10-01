/* input_owner.c: see input_owner.h.
 *
 * The flight's hiding of the panel moved here from overlay_input.c, with the reasoning it carried:
 *
 * The panel is held open while the camera is flying, and this is a repair, not a policy. Free
 * camera runs on the panel being up: closing it releases SIM_PAUSE_PANEL and the input freeze out
 * from under a camera that is still detoured and still flying, and the camera is left broken.
 * Field confirmed.
 *
 * So the two ways a person closes the panel, Escape and the key that opened it, do not close it
 * while the camera is on. They hide it instead: the panel stops being drawn and everything else
 * about it stays as it is, the freeze, the pause and the keys it swallows, so the picture is the
 * camera's alone and nothing under it has changed. The same two keys show it again, and it shows
 * itself the moment the flight ends, so a panel that is holding the game is never one that cannot
 * be seen. Nothing is taken away: free camera cannot be switched on at all without a key bound, and
 * F4 is always there besides, so the flight can always be ended and the panel closes normally the
 * moment it is. Ending the flight is the way out, and it is the only thing that was ever going to
 * leave both halves consistent.
 */
#include "input_owner.h"

#include "cheats_openphantom.h"
#include "overlay_input.h"
#include "session_lock.h"
#include "sim_pause.h"
#include "spawn_place.h"

#include "common/logging.h"

/* The one wheel message this reads, spelled as the engine compares it. */
#define MSG_MOUSE_WHEEL 0x020A

static bool hidden_for_flight;

/* Notches scrolled since the last take, positive away from the player; moved here from
 * overlay_input.c with the question of who takes them. The free camera's fly speed, the panel's
 * scroll and the placement mode's turn read it, each only while it owns the wheel. Unlike
 * everything the panel does, gated on its being open, this is observed unconditionally, from the
 * message hook itself, since the camera flies with the panel hidden. It is never consumed: the
 * message still reaches the engine afterwards exactly as if this were not here, since nothing is
 * known to need the wheel message for anything else and there is no reason to find out by
 * swallowing it.
 *
 * No locking: the message hook and the consumers all run on the single game thread (a chained
 * window-message dispatch and the per-frame hooks), never concurrently, the same reasoning that
 * lets the panel's other state go unguarded too. */
static int32_t wheel_delta_accum;

void input_owner_observe_wheel(int32_t message, int32_t wparam)
{
    if (message != MSG_MOUSE_WHEEL) {
        return;
    }
    /* WHEEL_DELTA notches live in the high word of wParam, signed. */
    wheel_delta_accum += (int32_t)(int16_t)((uint32_t)wparam >> 16);
}

int32_t input_owner_take_wheel(void)
{
    int32_t delta = wheel_delta_accum;

    wheel_delta_accum = 0;
    return delta;
}

input_owner_t input_owner_decide(const input_owner_facts_t *facts)
{
    if (facts == NULL || !facts->panel_open) {
        return INPUT_OWNER_GAME;
    }
    if (facts->free_camera) {
        return facts->hidden_for_flight ? INPUT_OWNER_FREE_CAMERA : INPUT_OWNER_PANEL;
    }
    return facts->placing ? INPUT_OWNER_PLACEMENT : INPUT_OWNER_PANEL;
}

bool input_owner_hides_panel(input_owner_t owner)
{
    return owner == INPUT_OWNER_FREE_CAMERA || owner == INPUT_OWNER_PLACEMENT;
}

bool input_owner_panel_pauses(input_owner_t owner, bool settling)
{
    if (owner == INPUT_OWNER_PLACEMENT) {
        return !settling;
    }
    return owner == INPUT_OWNER_PANEL || owner == INPUT_OWNER_FREE_CAMERA;
}

bool input_owner_free_camera_holds_panel(void)
{
    return cheats_openphantom_is_on(CHEATS_OWN_FREECAM);
}

void input_owner_toggle_hidden_for_flight(void)
{
    hidden_for_flight = !hidden_for_flight;
    log_info("the overlay is %s while the free camera flies; the same key %s it again, and it "
             "comes back when the flight ends",
             hidden_for_flight ? "hidden" : "shown", hidden_for_flight ? "shows" : "hides");
}

void input_owner_panel_opened(void)
{
    hidden_for_flight = false;
}

input_owner_t input_owner_now(void)
{
    input_owner_facts_t facts;

    if (hidden_for_flight && !input_owner_free_camera_holds_panel()) {
        hidden_for_flight = false;
        log_info("the overlay is shown again: the flight has ended");
    }
    facts.panel_open        = overlay_input_is_open();
    facts.hidden_for_flight = hidden_for_flight;
    facts.free_camera       = input_owner_free_camera_holds_panel();
    facts.placing           = spawn_place_state()->on;
    return input_owner_decide(&facts);
}

input_owner_t input_owner_sync(void)
{
    input_owner_t owner = input_owner_now();

    /* In a session the world is not this machine's alone to stop, so the hold is not taken there
     * whoever owns the panel (session_lock.c). The hold is idempotent per holder, so driving it
     * every frame changes nothing until the owner does. */
    sim_pause_hold(SIM_PAUSE_PANEL,
                   input_owner_panel_pauses(owner, spawn_place_state()->settling) &&
                       session_lock_panel_may_pause());
    return owner;
}
