/* mp_enemy_body.h: how an enemy's body ends, as the host has it, on the machine that only watches.
 *
 * Layer 2. The body field of the enemy record (MP_ENEMY_F_BODY, laid out in mp_enemy_body_rule.h)
 * says whether the host read a body, whether it is drawn, whether it collides, whether it casts a
 * shadow, its alpha, how far it has dissolved, and how often its base clip has started. A replica
 * runs none of the arms that change those, so without them it stands, solid and opaque, where the
 * host's body lies down, fades or dissolves.
 *
 * The host reads the five values where the engine's own arms write them: bit 0 and bit 1 of the
 * body's flag word, its class, and the alpha and dissolve of its drawn thing. The count of starts
 * is kept here per key, from the base track the census sees each substep.
 *
 * A client writes them onto the replica by the rule in mp_enemy_body_rule.h, only where the host's
 * value changed against the one it last acted on. What it last acted on is remembered per key,
 * together with the life and the body it acted on; a record written with no previous one, another
 * life, another body, or the enemy table's reset all make the next write a first one, so a body a
 * savegame reloaded at the same address is not taken for the one before it. "Solid" puts back the
 * body's own shooter class, the one value the engine ever restores; of the flag word only the drawn
 * bit and the shadow bit are touched, every other bit belongs to another writer.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BODY_H
#define MULTIPLAYER_MP_ENEMY_BODY_H

#include "mp_enemy_body_rule.h"
#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* On the host, from the binding's read of `actor` under `key`, whose body is `body`. */
void mp_enemy_body_read(uintptr_t actor, uint32_t body, uint32_t key, mp_enemy_record_t *record);

/* The five values of `body` as that read takes them, and nothing else: no start counted, no
 * counter moved. False when the flag word or the class does not read. For the double probe. */
bool mp_enemy_body_peek(uint32_t body, mp_enemy_body_state_t *out);

/* On a client, from the binding's write of the host's `record` onto the replica `actor`, with the
 * record written before it, or NULL for a first write. */
void mp_enemy_body_apply(uintptr_t actor, uint32_t body, uint32_t key,
                         const mp_enemy_record_t *record, const mp_enemy_record_t *previous);

/* Forgets what a client acted on for `key`, so that its next write is a first one. From the
 * moment a replica is parked, which is when an actor is bound to the key again. */
void mp_enemy_body_forget(uint32_t key);

/* Forgets every key: what the host counted and what a client acted on. From the enemy table's
 * reset, which every way out of a level, a session and a savegame load takes. */
void mp_enemy_body_reset(void);

/* The host's line when `described` says this side described its enemies, a client's when
 * `applied` says it took the host's blocks; each printed at nought as well. */
void mp_enemy_body_report(bool described, bool applied);

#endif /* MULTIPLAYER_MP_ENEMY_BODY_H */
