/* mp_enemy_dead_watch.h: the dead that stood on the host, measured.
 *
 * Layer 2. A player reported a dead droid standing, doing nothing and taking no hits, on the host
 * as on every client. The engine has ways to leave a dead enemy up (a death in the landing state
 * 7, which tests no health, and an undead after a wall bounce or a fall that ran out of time, both
 * of which lower the health past the death's own test), and a mod can stand a corpse back up. This
 * module changes none of it. It reads what the census reads anyway and says in the host's report
 * how many dead stood longer than any death of the engine's takes, where they stood, and how many
 * corpses had their clip changed by something other than the engine. The rule is in
 * mp_enemy_dead_watch_rule.h.
 *
 * It runs where the census runs, which is the host of a session with a peer, one census a
 * substep; without a session it is never called. The per-life state goes with the enemy table:
 * the table's reset, which every way out of a level or a session takes, is noticed on the next row
 * and forgets every life and every name. The counters stay for the run, as the table's own do.
 */
#ifndef MULTIPLAYER_MP_ENEMY_DEAD_WATCH_H
#define MULTIPLAYER_MP_ENEMY_DEAD_WATCH_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* On the host, from the census, once for every live key whose record was read, after the record
 * is complete. The actor's body and death cells are read only for a life that has fallen. */
void mp_enemy_dead_watch_row(uint32_t key, uint8_t generation, uint32_t actor,
                             const mp_enemy_record_t *record);

/* The two lines and the first eight of each kind by name. Printed when `described` says this
 * side described its enemies, which is what makes it the host, or when a row was ever read. */
void mp_enemy_dead_watch_report(bool described);

/* ==============================================================================================
 * The client's half, in mp_enemy_dead_watch_client.c.
 *
 * The same question asked of the replicas: a life the host reported falling to zero or below,
 * whose replica stays drawn and solid here longer than any death of the engine's takes. The
 * replica's body is read here, where it stands; its reaction state is the host's word and ends
 * no run, because a replica standing in a death state is what this half is there to see. A run
 * that passes the limit says what the host's body was at that moment: lying or gone on the host is
 * this machine's fault and must not happen once the body travels, standing on the host as well is
 * the host's own dead that stood, which its half names.
 * ============================================================================================ */

/* On a client, from the flush, once for every record of `key` it wrote or would have written:
 * `actor` is the replica, or 0 when the key's slot no longer holds it. */
void mp_enemy_dead_watch_replica(uint32_t key, uint8_t generation, uint32_t actor,
                                 const mp_enemy_record_t *record);

/* The client's line and the first eight since the last report by name. Printed when `applied`
 * says this side took the host's enemy blocks, or when a record was ever read. */
void mp_enemy_dead_watch_client_report(bool applied);

#endif /* MULTIPLAYER_MP_ENEMY_DEAD_WATCH_H */
