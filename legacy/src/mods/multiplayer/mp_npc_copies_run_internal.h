/* mp_npc_copies_run_internal.h: what the three parts of the run share. Only mp_npc_copies_run.c,
 * mp_npc_copies_run_host.c and mp_npc_copies_run_client.c include this.
 */
#ifndef MULTIPLAYER_MP_NPC_COPIES_RUN_INTERNAL_H
#define MULTIPLAYER_MP_NPC_COPIES_RUN_INTERNAL_H

#include "mp_npc_copies_run.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One entry onto the end of the grant ring under the next serial. False when the ring is full or
 * the serials are spent. */
bool mp_npc_copies_run_append(mp_npc_copies_run_t *run, const npc_spawn_grant_t *grant,
                              uint32_t *serial);

/* A refusal of this machine's wish `wish`, owed to its overlay; and the ones owed, into the ring
 * while it has room. */
void mp_npc_copies_run_refuse_here(mp_npc_copies_run_t *run, uint32_t wish, uint8_t reason);
void mp_npc_copies_run_write_refusals(mp_npc_copies_run_t *run);

/* The roles. A wish of this machine's overlay, taken in its epoch: false on a client when it could
 * not go out and is to be taken again next substep. */
void mp_npc_copies_run_host_wish(mp_npc_copies_run_t *run, const npc_spawn_wish_t *wish,
                                 uint32_t serial);
bool mp_npc_copies_run_client_wish(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in,
                                   const npc_spawn_wish_t *wish, uint32_t serial);

/* What each role owes after the wishes: the ring, the wire and, on a host, the repetition. */
void mp_npc_copies_run_host_duties(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in);
void mp_npc_copies_run_client_duties(mp_npc_copies_run_t *run);

/* The other side's message, once it is known to be one of the two. */
void mp_npc_copies_run_host_take(mp_npc_copies_run_t *run, uint8_t slot, uint64_t connection,
                                 const uint8_t *note, size_t bytes, uint32_t now_ms);
void mp_npc_copies_run_client_take(mp_npc_copies_run_t *run, const uint8_t *note, size_t bytes);

#endif /* MULTIPLAYER_MP_NPC_COPIES_RUN_INTERNAL_H */
