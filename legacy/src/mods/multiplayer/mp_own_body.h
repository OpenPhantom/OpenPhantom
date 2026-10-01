/* mp_own_body.h: where this machine's own body stands, one answer for everybody who asks.
 *
 * Layer 2. Two readers used to answer it differently. The send reads the hero block, and while a
 * script holds the player module for a scene it reads the model instead, because the block stands
 * still for as long as the scene lasts; a reader that took the block alone measured every hero
 * scene against the place the scene began. So the model's half of the send is here, and the send
 * and the judgement of a spoken line both ask it. That includes when the model is NOT the answer:
 * held for a death, the block is.
 */
#ifndef MULTIPLAYER_MP_OWN_BODY_H
#define MULTIPLAYER_MP_OWN_BODY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum mp_own_model {
    MP_OWN_MODEL_NOT_HELD = 0,   /* the module runs, or there is no object: the block answers */
    MP_OWN_MODEL_DEATH,          /* held dying or respawning: the block answers */
    MP_OWN_MODEL_UNREAD,         /* held, and the object did not read whole */
    MP_OWN_MODEL_READ
} mp_own_model_t;

/* The model's place and heading while the player module is held for anything but a death.
 * `position` is written as it is read, so a read that fails half way leaves what it read;
 * `heading` only on a whole read, and neither for a death. `state` gets the module state whenever
 * the module is held. Only READ puts the body at the model. */
mp_own_model_t mp_own_body_model(float position[3], float *heading, uint32_t *state);

/* Where this body stands: the block, and the model while the module is held for anything but a
 * death, which are the two reads the send makes. False with no block or one that will not read,
 * and inside a bank window, where the block is a far body's. `off_the_model` says which answered.
 * Counts nothing. */
bool mp_own_body_place(float out[3], bool *off_the_model);

#endif /* MULTIPLAYER_MP_OWN_BODY_H */
