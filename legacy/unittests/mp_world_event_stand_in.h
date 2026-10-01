/* unittests/mp_world_event_stand_in.h: the world and the log the tests of mp_world_event run in.
 *
 * The host half of the world events runs for real in these tests: posting, the census that settles
 * each event's life, the part in every view's block, the acknowledgements. What it asks about the
 * world is answered here: each peer's player and each key's actor as the interest reads them, the
 * census's rows laid by hand as the neighbouring tests of the enemy sync lay them, a block per peer
 * written through the real encoder, and a player for every kind that counts what it was asked to
 * perform. The client half reads the very bytes the host wrote. Both halves share one process,
 * which the module's state allows because the host's ring and a client's queue are apart.
 *
 * The log is kept in memory, every entry of common/logging, so a test can read back the host's
 * line and each peer's line of a report written now. The counters run for the process, so a test
 * compares two readings.
 *
 * Two test programs link it: mp_world_event holds the delivery to every peer and the client, and
 * mp_world_event_limits what the host refuses, drops and counts on its own.
 */
#ifndef UNITTESTS_MP_WORLD_EVENT_STAND_IN_H
#define UNITTESTS_MP_WORLD_EVENT_STAND_IN_H

#include "mp_enemy_interest_rule.h"
#include "mp_enemy_sync.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where the part begins in a block: behind the count, the level and the bitmap. */
#define WS_PART_AT (1u + MP_ENEMY_SYNC_LEVEL_BYTES + MP_ENEMY_SYNC_BITMAP_BYTES)

typedef struct world_stand_in {
    /* The block each view was last sent, and its length; 0 for a view left out. */
    uint8_t block[MP_ENEMY_SYNC_VIEWS][2048];
    size_t  bytes[MP_ENEMY_SYNC_VIEWS];

    /* Where each peer's player stands, and which keys have an actor in the pool. */
    mp_enemy_viewer_t viewers[MP_ENEMY_SYNC_VIEWS];
    bool              live[MP_ENEMY_SYNC_KEYS];
    uintptr_t         beside;   /* an actor the walk meets and reads under no key */

    /* What the players handed in were asked to perform, and the last replica and value. */
    unsigned  played[MP_WORLD_EVENT_KINDS];
    uintptr_t last_replica;
    uint16_t  last_a;
} world_stand_in_t;

extern world_stand_in_t ws;

/* The player every kind is handed: it counts the event and remembers where it was performed. */
bool ws_test_player(const mp_world_event_t *event, uintptr_t replica);

/* The actor that carries `key` in the pool. */
uintptr_t ws_actor_of(size_t key);

/* An actor under `key`, alive, at `x` along the first axis, with the radii the interest reads. */
void ws_enemy(size_t key, float x, float wake, float keep);

/* A host in a level, nobody described, every peer's player at the origin, the players handed in
 * and the counts of these tests at nought. */
void ws_fresh_host(void);

/* One census as the host takes it: the rows by hand, the walk meeting every actor in the pool, and
 * the census's own description of them, which settles the lives of the posted events. */
void ws_census(void);

/* An event of `kind` at the actor of `key`, with no tail. */
bool ws_post(uint8_t kind, size_t key, uint16_t a);

/* The rest of one host substep: a block for every view in the bit mask `views`, each sent on this
 * substep's tick, then the substep's end. Returns the tick. */
uint32_t ws_send_blocks(unsigned views, size_t capacity);

/* The events the part of the block last sent to `view` carries. */
unsigned ws_events_in(size_t view);

/* A client reading one view's block: the part at its place, staged and taken. */
void ws_client_takes(size_t view, uint32_t tick);

/* The number in front of `phrase` in the host's line of a report written now, or 0xFFFFFFFF when
 * the line does not have it. */
unsigned ws_host_count(const char *phrase);

/* The number right before `marker` in peer `view`'s line of a report written now, or -1. */
long ws_peer_says(size_t view, const char *marker);

#endif /* UNITTESTS_MP_WORLD_EVENT_STAND_IN_H */
