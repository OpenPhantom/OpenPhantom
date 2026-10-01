/* mp_npc_copies_run.h: the NPC copies' protocol, run once a substep, with nothing of the engine or
 * the session under it.
 *
 * The tables of mp_npc_copies (the host's) and mp_npc_copies_client (a client's) say what each key
 * is; this runs them. Once a substep, after the walk of the pool, it reads the record this
 * machine's overlay wrote, the answers first and then the new wishes, decides, writes what the
 * overlay is owed into the grant record, and sends what the other machines are owed. Between
 * substep it takes the other side's messages as well. What it needs of the session comes in as
 * numbers,
 * the overlay's record as a structure, and what it sends goes out through two pointers, so the
 * simulation runs exactly this code. A message of the other side is taken where the drain reads
 * it: inside a substep in a level, between substeps in the lobby.
 *
 * It decides only inside a level. The gate opens in a substep that knows its level and closes with
 * every new world, an arming and a disarming: a message that arrives in the lobby is about a world
 * nobody stands in, and is dropped and counted. The world is the generation of the world this
 * machine entered, not one it has only heard of.
 *
 * With no session, on the loopback and before an arming, the role is off and every call is empty.
 */
#ifndef MULTIPLAYER_MP_NPC_COPIES_RUN_H
#define MULTIPLAYER_MP_NPC_COPIES_RUN_H

#include "mp_npc_copies.h"
#include "mp_npc_copies_client.h"
#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_npc_run_role {
    MP_NPC_RUN_OFF = 0,
    MP_NPC_RUN_HOST,     /* a host over a socket: it hands the keys out */
    MP_NPC_RUN_CLIENT    /* a client over a socket: it builds what its host announced */
} mp_npc_run_role_t;

/* Refusals waiting for the overlay's record, and refusals a host owes its clients on the wire. */
#define MP_NPC_RUN_REFUSALS 16u

/* The wire's duties a host does in one substep, announcements and refusals together: forty copies
 * seen alive at once would otherwise take every ordered slot of the channel from the rest of the
 * session's reliable traffic. */
#define MP_NPC_RUN_WIRE_PER_SUBSTEP 2u

/* A host repeats one live copy this often: a latecomer hears of all 128 in half a minute. */
#define MP_NPC_RUN_REPEAT_MS 250u

/* Blocks a client applies without its host naming a copy before the copy is an orphan. */
#define MP_NPC_RUN_ORPHAN_BLOCKS 30u

/* What one substep needs of the session. */
typedef struct mp_npc_copies_run_in {
    bool                           joined;          /* a peer to send to */
    bool                           level_known;     /* inside a level, not the lobby or a load */
    uint16_t                       level;           /* the level's identity */
    uint8_t                        world;           /* the generation of the world entered */
    uint8_t                        own_slot;        /* a socket client's world slot once told */
    bool                           can_park;        /* a client can hold a replica still */
    const uint64_t                *connection_of;   /* the host's: [NPC_SPAWN_WORLD_SLOTS] */
    uint32_t                       now_ms;
    const npc_spawn_wish_record_t *wishes;          /* the overlay's record read now, or NULL */
    bool (*broadcast)(void *user, const uint8_t *bytes, size_t count);
    bool (*send_to)(void *user, uint8_t slot, const uint8_t *bytes, size_t count);
    void                          *user;
} mp_npc_copies_run_in_t;

/* A refusal a host owes a client, sent from the substep to that client alone. */
typedef struct mp_npc_copies_run_refusal {
    uint8_t             slot;
    uint64_t            connection;
    mp_npc_copy_entry_t entry;
} mp_npc_copies_run_refusal_t;

/* What the tables do not count themselves; the report reads both. */
typedef struct mp_npc_copies_run_counters {
    uint32_t wishes_read;                           /* taken from this machine's overlay */
    uint32_t wishes_stale;                          /* of another epoch, thrown away */
    uint32_t refused_here[MP_NPC_COPIES_REASONS];   /* told this machine's overlay */
    uint32_t own_overflow;                          /* such a refusal found no room */
    uint32_t answers_to_refusals;                   /* the overlay's answers to refusals */
    uint32_t ring_full;                             /* substeps a note duty waited for room */
    uint32_t serials_spent;                         /* the counter reached its end */
    uint32_t published;
    uint32_t publish_refused;
    uint32_t grant_failed;                          /* a grant the table would not make */
    uint32_t unsent;                                /* a send the channel refused */
    uint32_t announced;
    uint32_t repeated;
    uint32_t wire_taken;                            /* host: wishes from the wire */
    uint32_t wire_in_lobby;                         /* host: dropped with no level open */
    uint32_t wire_torn;                             /* ours by tag, and unreadable */
    uint32_t wire_wrong_role;                       /* a message this role never takes */
    uint32_t too_fast_quiet;                        /* host: throttled, and already told */
    uint32_t wire_refusals_sent;
    uint32_t wire_refusals_dropped;                 /* host: the asker had left */
    uint32_t wire_overflow;
    uint32_t wishes_sent;                           /* client */
    uint32_t wishes_held;                           /* client: refused by the channel, kept */
    uint32_t entries_stale;                         /* client: another level or world */
    uint32_t entries_no_panel;                      /* client: no overlay that builds */
    uint32_t entries_no_parking;
    uint32_t parked;
    uint32_t park_failed;
    uint32_t spawned_unknown;                       /* a build under a key not handed here */
    uint32_t removed;
    uint32_t duties_dropped;                        /* client: a duty it could not write */
} mp_npc_copies_run_counters_t;

typedef struct mp_npc_copies_run {
    uint8_t                      role;            /* mp_npc_run_role_t */
    bool                         ready;           /* the tables were made, once a process */
    mp_npc_copies_t              host;
    mp_npc_copies_client_t       client;
    npc_spawn_grant_record_t     record;          /* the grant record as it is published */
    bool                         dirty;           /* it changed since it last went out */
    bool                         in_level;        /* the gate */
    uint16_t                     level;
    uint8_t                      world;
    uint8_t                      own_slot;
    bool                         can_park;
    bool                         builder;         /* an overlay that builds, read this arming */
    uint16_t                     cap;
    npc_spawn_grant_t            own_refusal[MP_NPC_RUN_REFUSALS];
    uint32_t                     own_refusals;
    mp_npc_copies_run_refusal_t  wire_refusal[MP_NPC_RUN_REFUSALS];
    uint32_t                     wire_refusals;
    uint32_t                     granted[MP_WIRE_COPY_MAX / 32u];   /* keys granted, for the aim */
    uint32_t                     repeat_cursor;
    uint32_t                     repeat_at_ms;
    bool                         repeat_known;
    mp_npc_copies_run_counters_t counters;
} mp_npc_copies_run_t;

/* Once a process: both tables from nothing, and the first serial. */
void mp_npc_copies_run_init(mp_npc_copies_run_t *run);

/* A session's arming: its role, the host's cap, the corpse sweep and the orphan threshold. The
 * tables are emptied, generations and counters kept, and the epoch rises. */
void mp_npc_copies_run_arm(mp_npc_copies_run_t *run, mp_npc_run_role_t role, uint16_t cap,
                           uint32_t corpse_censuses, uint32_t orphan_blocks);

/* The session is over: the epoch rises, the record empties and names no cap, the role is off. */
void mp_npc_copies_run_disarm(mp_npc_copies_run_t *run);

/* A level ended or restarted, a world was built, or a session ended while the transport stays: the
 * epoch rises, and the tables, the ring and every queue behind them are emptied. The gate closes
 * until a substep knows its level again. */
void mp_npc_copies_run_new_world(mp_npc_copies_run_t *run);

/* The walk of the pool, into the table of the role. */
void mp_npc_copies_run_census_begin(mp_npc_copies_run_t *run);
void mp_npc_copies_run_census_saw(mp_npc_copies_run_t *run, uint32_t k, bool live);
void mp_npc_copies_run_census_end(mp_npc_copies_run_t *run, bool complete,
                                  uint32_t chain_length, uint32_t capacity);

/* One substep, after the census. True when the grant record is to be published again. */
bool mp_npc_copies_run_substep(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in);

/* A message from `slot` on `connection`: a host takes a wish, a client an entry. True when it is
 * one of the two by length and tag, whatever became of it. */
bool mp_npc_copies_run_take(mp_npc_copies_run_t *run, uint8_t slot, uint64_t connection,
                            const uint8_t *note, size_t bytes, uint32_t now_ms);

/* A client's overlay built 256 + k as `actor`; `parked` says whether it could be held still. One
 * that could not is given up at once. */
void mp_npc_copies_run_spawned(mp_npc_copies_run_t *run, uint32_t k, uintptr_t actor,
                               bool parked);

/* The host removed the life `generation` of k; `built` says whether a replica stands here. */
void mp_npc_copies_run_removed(mp_npc_copies_run_t *run, uint32_t k, uint8_t generation,
                               bool built);

/* The next key granted since the last ask, for the aim that forgets what it knew of the key. */
bool mp_npc_copies_run_next_granted(mp_npc_copies_run_t *run, uint32_t *k);

/* The table of the role, for the enemy block; NULL in any other role. */
const mp_npc_copies_t  *mp_npc_copies_run_host_table(const mp_npc_copies_run_t *run);
mp_npc_copies_client_t *mp_npc_copies_run_client_table(mp_npc_copies_run_t *run);

/* The grant record to publish, and whether the channel took it. */
const npc_spawn_grant_record_t *mp_npc_copies_run_record(const mp_npc_copies_run_t *run);
void mp_npc_copies_run_published(mp_npc_copies_run_t *run, bool taken);

#endif /* MULTIPLAYER_MP_NPC_COPIES_RUN_H */
