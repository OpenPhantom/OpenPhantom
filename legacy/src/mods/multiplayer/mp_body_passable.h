/* mp_body_passable.h: what the owner of the far bodies' class is told from outside the body
 * module.
 *
 * Layer 3. The far bodies are made passable while a scene of the host's stands
 * (mp_body_passable.c, and the section of mp_body.h on a far body in the way of a scene). A body
 * of class 0 is skipped by every collision test, so a contact on one says a test was missed. One
 * thing in the engine is no collision test and reaches such a body all the same: a script's own
 * message, which is delivered to whatever its script resolved after a test of height and radius
 * alone. With the far players playing on through a scene, such a message is ordinary, so the two
 * are counted apart, and telling them apart needs to know whether a script runs.
 */
#ifndef MULTIPLAYER_MP_BODY_PASSABLE_H
#define MULTIPLAYER_MP_BODY_PASSABLE_H

#include <stdbool.h>

/* Whether a script of an actor runs now, asked inside a contact's delivery. */
typedef bool (*mp_body_passable_script_fn_t)(void);

/* Who answers that, NULL for nobody. With nobody to answer, every contact on a passable far body
 * is counted as a collision test's. */
void mp_body_passable_set_script_probe(mp_body_passable_script_fn_t probe);

#endif /* MULTIPLAYER_MP_BODY_PASSABLE_H */
