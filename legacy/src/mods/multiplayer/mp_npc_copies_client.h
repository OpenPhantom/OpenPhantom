/* mp_npc_copies_client.h: the NPC copies a client holds for its host, and the wishes it sent.
 *
 * A client builds a copy only because its host announced one, and only through its own overlay,
 * which the note between the two DLLs tells what to build and what to drop. This table is what the
 * client has told its overlay about each k, and it keeps two promises. A life of a copy, a
 * generation, is handed to the overlay at most once: refused, removed or lost here, it is never
 * handed again. And two actors never carry one key: a new life of k waits until every cancel for
 * the old one is answered and a complete census finds no actor with 256 + k. The very first life
 * after `init` is handed before any census has run, which is safe only because a table is made
 * before a session's first level, when no copy stands.
 *
 * And every wish of this machine gets one answer for its overlay: the build the host announced for
 * it, or a refusal, the host's own or one said here for a wish that ran out or could not be kept.
 *
 * Pure. mp_npc_copies_run feeds it the host's entries, the overlay's answers and the census, the
 * enemy block the blocks it applies, and the run does the duties it owes: the build, the cancel
 * and the owner change for the overlay, and the refusals.
 */
#ifndef MULTIPLAYER_MP_NPC_COPIES_CLIENT_H
#define MULTIPLAYER_MP_NPC_COPIES_CLIENT_H

#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wishes a client keeps open at once, and how long, in censuses: ten seconds. A removal ends this
 * way unless the host refuses it, because the host answers a removal only with a refusal. */
#define MP_NPC_COPIES_CLIENT_WISHES 16u
#define MP_NPC_COPIES_WISH_DEADLINE 300u

/* Refusals waiting to be written to the overlay. */
#define MP_NPC_COPIES_CLIENT_REFUSALS 32u

/* A cancel the overlay refused, because the player rides the copy, is asked again after this many
 * applied blocks. */
#define MP_NPC_COPIES_CANCEL_RETRY_BLOCKS 60u

typedef enum mp_npc_held_state {
    MP_NPC_HELD_NONE = 0,
    MP_NPC_HELD_HANDED,       /* the build is owed to the overlay or written, its answer open */
    MP_NPC_HELD_BUILT,
    MP_NPC_HELD_REFUSED,      /* the overlay refused this life */
    MP_NPC_HELD_CANCELLING,   /* a cancel is owed or written; only its answer leads out */
    MP_NPC_HELD_GIVEN_UP      /* this life is gone here */
} mp_npc_held_state_t;

#define MP_NPC_HELD_DUTY_HAND   0x01u   /* BUILD to the overlay */
#define MP_NPC_HELD_DUTY_CANCEL 0x02u   /* CANCEL to the overlay */
#define MP_NPC_HELD_DUTY_OWNER  0x04u   /* OWNER to the overlay */

/* Why a copy was given up here, for the report. */
typedef enum mp_npc_give_up {
    MP_NPC_GIVE_UP_ORPHAN = 0,   /* the host's blocks stopped naming it */
    MP_NPC_GIVE_UP_DESPAWN,      /* the host removed it */
    MP_NPC_GIVE_UP_TOMBSTONE,    /* the host removed it before it was built here */
    MP_NPC_GIVE_UP_RELEASE,      /* the enemies were let go here */
    MP_NPC_GIVE_UP_GENERATION,   /* the block names another life of k */
    MP_NPC_GIVE_UP_PARKING,      /* built, and it could not be held still */
    MP_NPC_GIVE_UP_REASONS
} mp_npc_give_up_t;

typedef struct mp_npc_held {
    uint8_t   state;             /* mp_npc_held_state_t */
    uint8_t   generation;        /* the life this row is about */
    uint8_t   owner;
    uint8_t   duties;            /* MP_NPC_HELD_DUTY_* */
    uint8_t   desc[MP_NPC_COPY_DESC_BYTES];
    uint32_t  wish;              /* this machine's wish the build answers, else 0 */
    uint32_t  hand_serial;       /* the note serial of the build, 0 while unwritten */
    bool      hand_answered;
    uint32_t  cancel_serial;     /* the note serial of the cancel, 0 while unwritten */
    bool      cancel_answered;
    bool      cancel_refused;    /* and the answer was no */
    uint32_t  owner_serial;      /* the note serial of the last owner change */
    uint32_t  blocks;            /* blocks in a row without k, or since a refused cancel */
    uintptr_t actor;             /* the replica the overlay built, told by the park listener */
    bool      present;           /* a census saw an actor with the key, the last complete one or
                                  * a short one since */
    bool      seen;              /* this census */
    bool      waiting;           /* a newer life waits for this one to be gone */
    uint8_t   next_generation;
    uint8_t   next_owner;
    bool      next_mine;         /* its owner is this machine */
    uint8_t   next_desc[MP_NPC_COPY_DESC_BYTES];
} mp_npc_held_t;

typedef struct mp_npc_open_wish {
    bool     used;
    uint8_t  kind;
    uint32_t note;           /* the wish's serial in the note */
    uint16_t wire;           /* its low sixteen bits, as it travelled */
    uint32_t deadline;       /* the census count it expires at */
    uint8_t  desc[MP_NPC_COPY_DESC_BYTES];
} mp_npc_open_wish_t;

typedef struct mp_npc_refusal {
    uint32_t note;           /* the wish's serial in the note */
    uint8_t  reason;
} mp_npc_refusal_t;

typedef struct mp_npc_copies_client_counters {
    uint32_t entries;                          /* build entries taken */
    uint32_t repeats;                          /* of them already held */
    uint32_t handed;
    uint32_t built;                            /* the overlay answered built */
    uint32_t not_built;                        /* the overlay refused the build */
    uint32_t parked;                           /* a handed or built copy parked here */
    uint32_t given_up[MP_NPC_GIVE_UP_REASONS]; /* lives given up, by reason */
    uint32_t waiting_dropped;                  /* a waiting life given up before it began */
    uint32_t lost_here;                        /* built, and then no actor carried the key */
    uint32_t cancel_retries;
    uint32_t answers_unmatched;
    uint32_t answers_lost;
    uint32_t answers_open;
    uint32_t duties_unowed;
    uint32_t waited;                           /* a new life held back behind an old one */
    uint32_t wishes_opened;
    uint32_t wishes_answered_built;
    uint32_t refusals;                         /* refusals taken for a wish of this machine */
    uint32_t refusals_unmatched;
    uint32_t refusals_not_mine;
    uint32_t refusals_said_here;               /* for a wish that ran out or was dropped */
    uint32_t refusals_overflow;
    uint32_t wishes_expired;
    uint32_t wishes_pushed_out;
} mp_npc_copies_client_counters_t;

typedef struct mp_npc_copies_client {
    mp_npc_held_t                   held[MP_WIRE_COPY_MAX];
    mp_npc_open_wish_t              wish[MP_NPC_COPIES_CLIENT_WISHES];
    mp_npc_refusal_t                refusal[MP_NPC_COPIES_CLIENT_REFUSALS];
    size_t                          refusals;
    uint32_t                        censuses;
    uint32_t                        orphan_blocks;   /* blocks without k before it is an orphan */
    mp_npc_copies_client_counters_t counters;
} mp_npc_copies_client_t;

/* From nothing, with the orphan threshold; and emptied at every epoch, counters kept. What the
 * census saw survives a clear: an actor still carrying a key keeps a new life of it waiting. */
void mp_npc_copies_client_init(mp_npc_copies_client_t *client, uint32_t orphan_blocks);
void mp_npc_copies_client_clear(mp_npc_copies_client_t *client);

/* A build entry from the host, for this machine in `own_slot`. A first life of k with no actor
 * carrying the key is handed at once; any other life waits for the census. */
void mp_npc_copies_client_take_entry(mp_npc_copies_client_t *client,
                                     const mp_npc_copy_entry_t *entry, uint8_t own_slot);

/* A refusal from the host: queued for the overlay when it answers a wish of this machine. */
bool mp_npc_copies_client_take_refusal(mp_npc_copies_client_t *client,
                                       const mp_npc_copy_entry_t *entry, uint8_t own_slot);

/* The overlay's answer to the note entry with `serial`. OPEN is no answer and is refused. */
void mp_npc_copies_client_take_answer(mp_npc_copies_client_t *client, uint32_t serial,
                                      npc_spawn_answer_t answer);

/* Gives up the copy under k, the life `generation` or, for 0, whatever life is held, and a newer
 * life waiting under that generation, or any for 0. */
void mp_npc_copies_client_give_up(mp_npc_copies_client_t *client, uint32_t k,
                                  uint8_t generation, mp_npc_give_up_t why);

/* Every copy held and every life waiting, for the enemies being let go here. */
void mp_npc_copies_client_release_all(mp_npc_copies_client_t *client);

/* The census: an actor carries 256 + k. `complete` false concludes nothing from absence. */
void mp_npc_copies_client_census_begin(mp_npc_copies_client_t *client);
void mp_npc_copies_client_census_saw(mp_npc_copies_client_t *client, uint32_t k);
void mp_npc_copies_client_census_end(mp_npc_copies_client_t *client, bool complete);

/* A block applied here, with its copies' bitmap (bit k set: the host has an actor with 256 + k),
 * or NULL and 0 for a block without a copies part. */
void mp_npc_copies_client_block(mp_npc_copies_client_t *client, const uint8_t *bitmap,
                                size_t bytes);

/* A record of the life `generation` of k in a block: true when it is to be applied to the replica
 * built here. A held life of another generation is given up. */
bool mp_npc_copies_client_record(mp_npc_copies_client_t *client, uint32_t k, uint8_t generation);

/* The park listener's word: the overlay's build of 256 + k is the actor `actor`. */
void mp_npc_copies_client_parked(mp_npc_copies_client_t *client, uint32_t k, uintptr_t actor);

/* The replica of the life `generation` of k, or 0. */
uintptr_t mp_npc_copies_client_replica(const mp_npc_copies_client_t *client, uint32_t k,
                                       uint8_t generation);

/* Duties: the next one, the note entry it writes, and done with its serial. */
bool mp_npc_copies_client_next_duty(const mp_npc_copies_client_t *client, uint32_t *k,
                                    uint8_t *duty);
bool mp_npc_copies_client_note_for(const mp_npc_copies_client_t *client, uint32_t k,
                                   uint8_t duty, npc_spawn_grant_t *out);
void mp_npc_copies_client_duty_done(mp_npc_copies_client_t *client, uint32_t k, uint8_t duty,
                                    uint32_t serial);

/* The next refusal owed to the overlay, removed from the queue once taken. */
bool mp_npc_copies_client_next_refusal(mp_npc_copies_client_t *client,
                                       mp_npc_refusal_t *refusal);

/* A wish of this machine's overlay goes out: its note serial, kind and, for a spawn, its rounded
 * description's wire bytes. Answers the wire serial. */
uint16_t mp_npc_copies_client_open_wish(mp_npc_copies_client_t *client, uint32_t note_serial,
                                        uint8_t kind, const uint8_t *desc);

uint8_t mp_npc_copies_client_state(const mp_npc_copies_client_t *client, uint32_t k);

#endif /* MULTIPLAYER_MP_NPC_COPIES_CLIENT_H */
