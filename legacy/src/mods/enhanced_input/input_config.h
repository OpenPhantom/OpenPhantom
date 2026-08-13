#ifndef INPUT_CONFIG_H
#define INPUT_CONFIG_H

#include <stdbool.h>

/* Everything the input feature reads out of the ini, and nothing it does with it.
 *
 * This half was split off from enhanced_input.c along the seam that file's own size note had been
 * naming: it touches the engine at no point. It reads keys, clamps them, retires the ones that no
 * longer mean anything, and pulls in the configuration of the modules underneath. What is left on
 * the other side is the phase thunks, the installation and the order things have to come up in.
 *
 * The values are handed out as a read-only struct because they are read on the per-substep path and
 * because two dozen one-line getters is not an interface. The one field that changes while the game
 * runs, and it changes from a check box on the controls screen, has a setter of its own.
 */

typedef struct input_config {
    bool  mouse_look;
    bool  strafe;
    bool  strafe_invert;
    bool  strafe_turns_body;
    bool  steer_lean;
    bool  steer_lean_from_hand;      /* the pose follows the hand, not the engine's sawtooth cell */
    bool  restore_turn_rate;
    int   steer_log;
    float steer_lean_test_degrees;   /* whether the model is turned to face the way it travels */
    float strafe_settle_seconds;
    float strafe_turn_rate;    /* degrees per second, the damper's hard rate cap */

    /* Degrees per second A and D turn the player while sideways walking is off. It exists because
     * mouse look has to clear the engine's own turn cell; that cell carries the mouse too, so
     * the keyboard's share has to be re-applied from here or the keys go dead. */
    float key_turn_rate;
} input_config_t;

/* Reads every key of this DLL, clamps every value, and loads the mouse, free look and view lead
 * configuration with it. Safe before anything is resolved; it touches no engine memory. */
void input_config_load(void);

/* The values in force. Never NULL, and zero-filled until the load above has run. */
const input_config_t *input_config(void);

/* Sideways walking, which is a live setting: the controls screen switches it and installation
 * switches it off again when what it depends on did not resolve. Writing the ini is the caller's
 * business, because only the caller knows whether the change came from a player or from a
 * dependency check that the player never asked for. */
void input_config_set_strafe(bool enabled);

#endif /* INPUT_CONFIG_H */
