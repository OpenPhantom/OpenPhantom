/* mp_enemy_shield.h: a droideka's shield on the running engine, read on the host and followed on a
 * client.
 *
 * Layer 2. The rule is mp_enemy_shield_rule. This reads the body's shield word and the shield's
 * pool slot, and on a client gives the director its own four commands with the replica as the
 * actor, through the trampoline of this feature's hull on the director, so the replica gets exactly
 * what the engine gives the host's actor and none of it is counted as this side's own script.
 *
 * The pool is not in any table: it is read out of the director's command 4, whose call names the
 * function that shows a shield, whose body names the pool. Command 5, the size, is proved the same
 * way and has to name the same pool; without it the shield still follows, at the size a hang
 * gives. The first read or write after the director is hulled finds both, once.
 */
#ifndef MULTIPLAYER_MP_ENEMY_SHIELD_H
#define MULTIPLAYER_MP_ENEMY_SHIELD_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* On the host: the record's shield field, out of the body. */
void mp_enemy_shield_read(uint32_t body, mp_enemy_record_t *record);

/* On a client: the replica brought to the record, only where it differs. `index` names it in a
 * line. */
void mp_enemy_shield_apply(uintptr_t actor, uint32_t body, uint32_t index,
                           const mp_enemy_record_t *record);

void mp_enemy_shield_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_SHIELD_H */
