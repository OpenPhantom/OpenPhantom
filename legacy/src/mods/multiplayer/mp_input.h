/* mp_input.h: input per bank, so the second body stops reading the local keys.
 *
 * The engine reads the player's input through four cdecl functions that take an action id and no
 * player argument. Every consumer the pipeline has, the steer, the ground actions, and every
 * mode's own descriptor function, goes through those four, and when the second body is ticked all
 * of them run inside the bank window. Detouring the four at their shared entry is therefore one
 * bottleneck for everything the pipeline can ask about input: while bank 0 is active each hook is
 * a pure pass-through, and while another bank is active the answer comes from that bank's injected
 * command instead of the hardware. Nothing the local player presses reaches the second body, and
 * nothing the second body consumes is taken away from the player.
 *
 * The alternative, replacing the steer and ground-action entries in the second body's own phase
 * plan, was measured and rejected: the plan only withholds DEFAULT phases, and the mode-owned
 * functions that replace them (the midair attack trigger, the stand clip select at 0044CD61, the
 * hang update at 0044FA78, the sidle update at 0045005A, the slide update, the turret) read the
 * same four functions and would have kept feeding the local keys to the second body. Replacing
 * phase 5 would also have meant rebuilding the USE priority chain: turret, button, push block,
 * NPC latch.
 */
#ifndef MULTIPLAYER_MP_INPUT_H
#define MULTIPLAYER_MP_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One substep of intent for one bank: two analogue axes, one relative axis, and a bitfield of the
 * digital actions keyed by the engine's own action ids. This is the same shape the wire's input
 * packet carries, so a network command can be applied without translation. */
typedef struct mp_input_command {
    float    turn_axis;   /* the absolute turn read, action 0; the engine clamps to -1..1 */
    float    move_axis;   /* the absolute move read, action 1 */
    float    mouse_turn;  /* the relative turn read, action 0; raw device ticks, unclamped */
    uint32_t buttons;     /* bit N set = action N is held */
} mp_input_command_t;

#define MP_INPUT_MAX_ACTIONS 32u

/* Resolves the four reader sites and the frame delta cell, then installs the four detours, all or
 * nothing: a missing site refuses before anything is written. After a mid-install failure the
 * hooks that did land are put into pass-through for every bank and the refusal is logged, which
 * restores exactly the pre-install behaviour. */
bool mp_input_install(void);

bool mp_input_installed(void);

/* The command bank 1 answers with from the next read on. NULL or clear means no intent: every
 * axis reads zero and no button is down, so a ticked second body stands still instead of
 * mirroring the player. */
void mp_input_set_command(const mp_input_command_t *command);
void mp_input_clear_command(void);

void mp_input_report(const char *why);

/* Samples the LOCAL player's current intent through the engine's own readers, for a client that
 * sends its input to a host. Reads only the pure readers (the two absolute axes and the held state
 * of every networked action), through the detours' kept originals, so the sample neither consumes
 * anything nor depends on which bank is active. The relative (mouse) axis is not sampled: a
 * networked command is per-substep intent, which the digital axes model; the mouse half of a remote
 * body's turn is a known gap. False before the split is installed. */
bool mp_input_sample_local(mp_input_command_t *out);

/* The decision core, pure so the unit test can drive it without a game. The three answer forms
 * mirror the engine readers they stand in for; advance_hold reproduces the tap-versus-hold
 * contract, where the caller owns the accumulator and the return is non-zero on exactly the
 * release call, carrying how long the press lasted. */
float    mp_input_answer_digital_axis(const mp_input_command_t *command, int32_t action);
float    mp_input_answer_relative_axis(const mp_input_command_t *command, int32_t action);
uint32_t mp_input_answer_is_down(const mp_input_command_t *command, int32_t action);
float    mp_input_advance_hold(const mp_input_command_t *command, int32_t action,
                               float *held_for, float dt);

/* Who answers a reader's call, in this order: the far body's injected command while bank 1 is
 * active, whatever else holds; a neutral answer while the pause menu or the chat holds this
 * player's input and the id is one the menu does not read; the engine otherwise. */
typedef enum mp_input_route {
    MP_INPUT_ROUTE_ENGINE = 0,
    MP_INPUT_ROUTE_BANK,
    MP_INPUT_ROUTE_HELD
} mp_input_route_t;

mp_input_route_t mp_input_route(bool split_live, bool bank_one_active, bool input_held,
                                int32_t action);

/* What a caller's hold accumulator holds after a held read: nothing. A hold banked before the
 * menu opened would otherwise come out as a release on the first read after it closed, and fire
 * the charged attack the player let go of inside the menu. `dropped` says whether anything was
 * there. */
float mp_input_hold_withheld(float banked, bool *dropped);

#endif /* MULTIPLAYER_MP_INPUT_H */
