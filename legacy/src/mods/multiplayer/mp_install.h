/* mp_install.h: the stages of the install that stand on the configuration alone.
 *
 * The bank and its lifecycle hulls, the bodies, the input split and the death hull. Each is decided
 * by switches read once before the first frame, none of them needs a session, and with every switch
 * off none of them holds an engine address.
 */
#ifndef MULTIPLAYER_MP_INSTALL_H
#define MULTIPLAYER_MP_INSTALL_H

#include "multiplayer.h"

/* The bank with its lifecycle hulls, and the bodies. `tick` is the one substep-task client, which
 * this registers once the bank stands. */
void mp_install_bank_and_body(const multiplayer_config_t *config, void (*tick)(void));

/* The input split, then the death hull with its provocation and the bank health. */
void mp_install_input_and_death(const multiplayer_config_t *config);

#endif /* MULTIPLAYER_MP_INSTALL_H */
