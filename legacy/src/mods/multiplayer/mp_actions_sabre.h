/* mp_actions_sabre.h: the local player's sabre actions, caught at the four engine functions that
 * begin or end them.
 *
 * A swing, a block, a parry and the disarm that ends each of them are moments the far side must
 * perform on its puppet, and none of them can be read off state: the swing table row decides the
 * contact node, radius and impact of a swing, the block and parry clips are chosen against a bolt
 * or an attacker the far machine never sees, and the disarm happens at a marker inside a clip.
 * So each is caught where the engine makes it and handed to a listener as an action and an
 * operand; the listener owns the queue and the tick, this module owns only the hulls.
 *
 * Only the local player's moments count. Every one of these functions is also entered for the
 * puppet, inside its bank window, when the far player's events are performed, and noting those
 * would send the far player's actions straight back to their sender.
 */
#ifndef MULTIPLAYER_MP_ACTIONS_SABRE_H
#define MULTIPLAYER_MP_ACTIONS_SABRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many hulls a complete installation stands on: the swing starter, the swing end, the deflect
 * and the parry. */
#define MP_ACTIONS_SABRE_HULLS 4u

/* One caught action: one of the MP_SABRE_* actions and its operand, a swing table row for a
 * swing, an overlay clip ordinal for a block or a parry, 0 for a disarm. */
typedef void (*mp_actions_sabre_listener_t)(uint8_t action, uint8_t operand);

/* Hulls what resolved, chaining in front of any branch already on a head, and reports how many of
 * the four stand. A site that did not resolve leaves that action uncaught and says so; a null
 * listener installs nothing. Idempotent: a second call answers the first call's count. */
size_t mp_actions_sabre_install(mp_actions_sabre_listener_t listener);

size_t   mp_actions_sabre_installed(void);
uint32_t mp_actions_sabre_caught(void);

/* Catches that were not handed on: a swing row past the table, a clip ordinal past a byte, or a
 * body that did not read. Each is the engine doing something this module does not understand,
 * counted rather than sent wrong. */
uint32_t mp_actions_sabre_refused(void);

#endif /* MULTIPLAYER_MP_ACTIONS_SABRE_H */
