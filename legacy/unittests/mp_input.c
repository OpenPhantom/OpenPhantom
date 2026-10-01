/* The input split's decision core, and the module's refusals with no game.
 *
 * What is provable here is the per-bank answer itself: which action reads which command field,
 * the bound the digital axes honour, and the tap-versus-hold contract, whose accumulator belongs
 * to the caller and whose return is non-zero on exactly the release call. The four detours and
 * the bank dispatch are engine choreography and are proven in game, not here; what this process
 * can say about them is that an install with nothing resolved refuses instead of writing.
 */
#include "unittest.h"

#include "mp_input.h"

#include <string.h>

int main(void)
{
    mp_input_command_t command;

    memset(&command, 0, sizeof(command));

    ut_section("the digital axes");
    command.turn_axis = 0.5f;
    command.move_axis = -0.25f;
    ut_near(mp_input_answer_digital_axis(&command, 0), 0.5, 0.0, "action 0 reads the turn axis");
    ut_near(mp_input_answer_digital_axis(&command, 1), -0.25, 0.0, "action 1 reads the move axis");
    ut_near(mp_input_answer_digital_axis(&command, 2), 0.0, 0.0,
            "an action that is no axis reads zero");
    command.turn_axis = 3.0f;
    ut_near(mp_input_answer_digital_axis(&command, 0), 1.0, 0.0,
            "an overdriven turn is clamped to the bound the engine itself enforces");
    command.move_axis = -3.0f;
    ut_near(mp_input_answer_digital_axis(&command, 1), -1.0, 0.0,
            "the clamp holds on the negative side too");
    ut_near(mp_input_answer_digital_axis(NULL, 0), 0.0, 0.0,
            "no command at all reads stillness, not a fault");

    ut_section("the relative axis");
    command.mouse_turn = -7.5f;
    ut_near(mp_input_answer_relative_axis(&command, 0), -7.5, 0.0,
            "action 0 reads the raw relative turn, unclamped like the engine's own");
    ut_near(mp_input_answer_relative_axis(&command, 1), 0.0, 0.0,
            "the engine only ever asks the relative reader for action 0, and so does the stand-in");
    ut_near(mp_input_answer_relative_axis(NULL, 0), 0.0, 0.0, "no command reads no motion");

    ut_section("the buttons");
    command.buttons = (1u << 3) | (1u << 6) | (1u << 0x15);
    ut_check(mp_input_answer_is_down(&command, 3) == 1u, "a set bit answers held");
    ut_check(mp_input_answer_is_down(&command, 6) == 1u, "the use action answers held");
    ut_check(mp_input_answer_is_down(&command, 0x15) == 1u,
             "the highest shipped action id still fits the field");
    ut_check(mp_input_answer_is_down(&command, 5) == 0u, "a clear bit answers released");
    ut_check(mp_input_answer_is_down(&command, -1) == 0u, "a negative action id answers released");
    ut_check(mp_input_answer_is_down(&command, 32) == 0u,
             "an action past the field answers released instead of reading a stranger's bit");
    ut_check(mp_input_answer_is_down(NULL, 3) == 0u, "no command holds nothing");

    ut_section("the tap-versus-hold contract");
    {
        float accum = 0.0f;
        float released;

        command.buttons = (1u << 2);
        ut_near(mp_input_advance_hold(&command, 2, &accum, 0.03125f), 0.0, 0.0,
                "a held action returns zero while the accumulator advances");
        ut_near(mp_input_advance_hold(&command, 2, &accum, 0.03125f), 0.0, 0.0,
                "and keeps returning zero on every held call");
        ut_near(accum, 0.0625, 1e-6, "two held calls accumulate two frame deltas");

        command.buttons = 0u;
        released = mp_input_advance_hold(&command, 2, &accum, 0.03125f);
        ut_near(released, 0.0625, 1e-6,
                "the release call returns the accumulated time, and only that call");
        ut_near(accum, 0.0, 0.0, "the release zeroes the caller's accumulator");
        ut_near(mp_input_advance_hold(&command, 2, &accum, 0.03125f), 0.0, 0.0,
                "a further released call returns zero, so the tap fires exactly once");

        command.buttons = (1u << 2);
        ut_near(mp_input_advance_hold(&command, 2, &accum, 0.05f), 0.0, 0.0,
                "a new press starts a new accumulation");
        ut_near(accum, 0.05, 1e-6, "with the fresh delta in the accumulator");

        ut_near(mp_input_advance_hold(NULL, 2, &accum, 0.05f), 0.05, 1e-6,
                "losing the command mid-press releases the accumulated time instead of leaking it");
        ut_near(mp_input_advance_hold(&command, 2, NULL, 0.05f), 0.0, 0.0,
                "a missing accumulator is answered with zero, never dereferenced");
    }

    ut_section("who answers: the far body's command first, then a session's pause menu");
    ut_check(mp_input_route(true, true, true, 0) == MP_INPUT_ROUTE_BANK,
             "bank 1 reads its injected command even while this player's input is held");
    ut_check(mp_input_route(true, true, false, 2) == MP_INPUT_ROUTE_BANK,
             "and without a hold, as it always did");
    ut_check(mp_input_route(true, false, true, 0) == MP_INPUT_ROUTE_HELD,
             "bank 0 under a session's pause menu: the steer's axis is held");
    ut_check(mp_input_route(true, false, true, 0x17) == MP_INPUT_ROUTE_HELD,
             "and so is a dialogue key, which would otherwise answer a choice behind the menu");
    ut_check(mp_input_route(true, false, true, 0x1A) == MP_INPUT_ROUTE_ENGINE,
             "the pad's menu moves reach the engine");
    ut_check(mp_input_route(true, false, true, 0x20) == MP_INPUT_ROUTE_ENGINE,
             "and its menu buttons");
    ut_check(mp_input_route(true, false, false, 0) == MP_INPUT_ROUTE_ENGINE,
             "with no hold, bank 0 reads the engine");
    ut_check(mp_input_route(false, false, true, 0) == MP_INPUT_ROUTE_ENGINE,
             "and a split that is not live answers nothing of its own");

    ut_section("a hold banked before the menu opened is dropped, not paid out after it closed");
    {
        float accum = 0.0f;
        bool  dropped = false;

        memset(&command, 0, sizeof command);
        command.buttons = (1u << 2);
        (void)mp_input_advance_hold(&command, 2, &accum, 0.5f);
        ut_near(accum, 0.5, 1e-6, "the attack is charged for half a second before the menu");

        accum = mp_input_hold_withheld(accum, &dropped);
        ut_check(dropped, "a held read under the menu finds the charge and drops it");
        ut_near(accum, 0.0, 0.0, "the caller's accumulator is empty afterwards");

        command.buttons = 0u;   /* the key was let go inside the menu */
        ut_near(mp_input_advance_hold(&command, 2, &accum, 0.03125f), 0.0, 0.0,
                "so the first read after the menu releases nothing and no attack fires");

        (void)mp_input_hold_withheld(0.0f, &dropped);
        ut_check(!dropped, "an empty accumulator is not counted as a dropped hold");
    }

    ut_section("the module with no game");
    ut_check(!mp_input_installed(), "nothing is installed before the installer ran");
    ut_check(!mp_input_install(),
             "an install with no site resolved refuses before anything is written");
    ut_check(!mp_input_installed(), "and the refusal leaves the module uninstalled");

    return ut_summary("mp_input");
}
