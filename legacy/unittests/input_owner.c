/* input_owner.c: who has the pointer, the wheel and the panel's picture, and who holds the pause.
 *
 * One answer for four parties, so every combination of the four facts is walked, and then the
 * parts that keep state: the player's hiding of the panel during a flight, which ends with the
 * flight, and the wheel's notches, which go to whoever takes them and to nobody twice.
 *
 * The panel, the free camera and the pause are the real modules' in the game and a process of
 * their own here: the four answers input_owner.c reads are stood up below, so the test decides
 * what the world looks like.
 */
#include "unittest.h"

#include "cheats_openphantom.h"
#include "input_owner.h"
#include "overlay_input.h"
#include "session_lock.h"
#include "sim_pause.h"
#include "spawn_place.h"

#include <string.h>

static bool stub_open;
static bool stub_flying;
static bool stub_may_pause = true;
static bool stub_paused;

bool overlay_input_is_open(void)
{
    return stub_open;
}

bool cheats_openphantom_is_on(cheats_own_id_t id)
{
    return id == CHEATS_OWN_FREECAM && stub_flying;
}

bool session_lock_panel_may_pause(void)
{
    return stub_may_pause;
}

void sim_pause_hold(sim_pause_holder_t who, bool held)
{
    if (who == SIM_PAUSE_PANEL) {
        stub_paused = held;
    }
}

static input_owner_t decide(bool open, bool hidden, bool flying, bool placing)
{
    input_owner_facts_t f;

    f.panel_open        = open;
    f.hidden_for_flight = hidden;
    f.free_camera       = flying;
    f.placing           = placing;
    return input_owner_decide(&f);
}

static void the_rule(void)
{
    uint32_t bits;
    bool     all_shut_game = true;

    ut_section("the rule, every combination");
    for (bits = 0; bits < 8u; ++bits) {
        if (decide(false, (bits & 1u) != 0, (bits & 2u) != 0, (bits & 4u) != 0) !=
            INPUT_OWNER_GAME) {
            all_shut_game = false;
        }
    }
    ut_check(all_shut_game, "a shut panel gives the game everything, whatever else is on");
    ut_check(decide(true, false, false, false) == INPUT_OWNER_PANEL, "open alone: the panel");
    ut_check(decide(true, false, false, true) == INPUT_OWNER_PLACEMENT,
             "open with the placement mode on: the mode");
    ut_check(decide(true, true, true, false) == INPUT_OWNER_FREE_CAMERA,
             "flying with the panel hidden for it: the camera");
    ut_check(decide(true, false, true, false) == INPUT_OWNER_PANEL,
             "flying with the panel shown: the panel");
    ut_check(decide(true, true, true, true) == INPUT_OWNER_FREE_CAMERA &&
                 decide(true, false, true, true) == INPUT_OWNER_PANEL,
             "the camera wins over a placement mode that is on: the mode waits");
    ut_check(decide(true, true, false, false) == INPUT_OWNER_PANEL,
             "a hiding left over without a flight hides nothing");
    ut_check(decide(true, true, false, true) == INPUT_OWNER_PLACEMENT,
             "and does not stop the mode either");
    ut_check(input_owner_decide(NULL) == INPUT_OWNER_GAME, "no facts is the game");

    ut_check(!input_owner_hides_panel(INPUT_OWNER_GAME) &&
                 !input_owner_hides_panel(INPUT_OWNER_PANEL) &&
                 input_owner_hides_panel(INPUT_OWNER_FREE_CAMERA) &&
                 input_owner_hides_panel(INPUT_OWNER_PLACEMENT),
             "the picture is hidden under the camera and under the mode, and only there");
    ut_check(!input_owner_panel_pauses(INPUT_OWNER_GAME, false) &&
                 input_owner_panel_pauses(INPUT_OWNER_PANEL, false) &&
                 input_owner_panel_pauses(INPUT_OWNER_FREE_CAMERA, false) &&
                 input_owner_panel_pauses(INPUT_OWNER_PLACEMENT, false),
             "the panel holds the pause for itself, the camera and the placement mode, which is a "
             "state of the panel");
    ut_check(!input_owner_panel_pauses(INPUT_OWNER_PLACEMENT, true) &&
                 input_owner_panel_pauses(INPUT_OWNER_PANEL, true) &&
                 !input_owner_panel_pauses(INPUT_OWNER_GAME, true),
             "the settle after a copy placed lets the world run under the mode, and only there");
}

static void the_state(void)
{
    spawn_place_t *place = spawn_place_state();

    ut_section("the flight's hiding and the pause");
    memset(place, 0, sizeof *place);
    stub_open   = true;
    stub_flying = true;
    input_owner_panel_opened();
    ut_check(input_owner_now() == INPUT_OWNER_PANEL, "a fresh open during a flight: the panel");
    input_owner_toggle_hidden_for_flight();
    ut_check(input_owner_now() == INPUT_OWNER_FREE_CAMERA, "hidden for the flight: the camera");
    stub_flying = false;
    ut_check(input_owner_now() == INPUT_OWNER_PANEL, "the flight ends and the panel is shown");
    stub_flying = true;
    ut_check(input_owner_now() == INPUT_OWNER_PANEL,
             "and stays shown when a new flight begins: the end of the old one cleared the hide");
    input_owner_toggle_hidden_for_flight();
    input_owner_panel_opened();
    ut_check(input_owner_now() == INPUT_OWNER_PANEL, "every open and close clears the hide");
    stub_flying = false;

    place->on = true;
    stub_may_pause = true;
    ut_check(input_owner_sync() == INPUT_OWNER_PLACEMENT && stub_paused,
             "in the mode in a single player game the world is held, as under the panel");
    place->settling = true;
    ut_check(input_owner_sync() == INPUT_OWNER_PLACEMENT && !stub_paused,
             "the settle after a copy placed lets it run");
    place->settling = false;
    ut_check(input_owner_sync() == INPUT_OWNER_PLACEMENT && stub_paused,
             "and the settle over, it is held again");
    stub_may_pause = false;
    ut_check(input_owner_sync() == INPUT_OWNER_PLACEMENT && !stub_paused,
             "in a session the mode holds nothing: the world runs, as built");
    stub_may_pause = true;
    place->on = false;
    ut_check(input_owner_sync() == INPUT_OWNER_PANEL && stub_paused, "back in the panel it holds");
    stub_may_pause = false;
    ut_check(input_owner_sync() == INPUT_OWNER_PANEL && !stub_paused,
             "in a session it holds nothing, whoever owns the panel");
    stub_may_pause = true;
    stub_open = false;
    ut_check(input_owner_sync() == INPUT_OWNER_GAME && !stub_paused, "a shut panel holds nothing");
}

static void the_wheel(void)
{
    ut_section("the wheel");
    (void)input_owner_take_wheel();
    input_owner_observe_wheel(0x020A, (int32_t)(120u << 16));
    input_owner_observe_wheel(0x020A, (int32_t)(120u << 16));
    input_owner_observe_wheel(0x0200, (int32_t)(120u << 16));
    ut_check(input_owner_take_wheel() == 240, "two notches away are counted; a mouse move is not");
    ut_check(input_owner_take_wheel() == 0, "and taken once");
    input_owner_observe_wheel(0x020A, (int32_t)(0xFF88u << 16));
    ut_check(input_owner_take_wheel() == -120, "a notch towards the player is negative");
}

int main(void)
{
    the_rule();
    the_state();
    the_wheel();
    return ut_summary("input owner");
}
