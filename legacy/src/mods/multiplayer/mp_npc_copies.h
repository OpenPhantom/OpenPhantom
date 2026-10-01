/* mp_npc_copies.h: the NPC copies an editor spawns, as the host hands them out.
 *
 * A copy is an enemy the developer overlay built rather than the level placed. In a session only
 * the host may decide that one exists, because only the host can give it a key nobody else holds:
 * 256 + k, past every placement (common/npc_spawn_note.h). A client asks with a wish (0xA1); the
 * host decides, hands the copy to its own overlay to build, and once its census sees the copy
 * alive, tells everybody (0xA2), and every client's overlay builds the same copy from the same
 * bits.
 *
 * Pure: the host's table of keys, its decision, and the throttle; the messages are
 * mp_npc_copy_wire. mp_npc_copies_run drives it a substep at a time, and layer 3 walks the pool,
 * reads and writes the notes and sends; everything here is driven by what it is told.
 *
 * The table holds a row per k. A row owes things, and they stay owed until they are done: the
 * grant to the host's own overlay, the announcement to everybody, a cancel, an owner change, a
 * refusal to whoever asked. The run does them when their way is free and says so; what cannot go
 * yet waits in the row, which is the queue behind the five slots of the note.
 *
 * An answer of the overlay is read against what was written: a row knows which of its notes the
 * serial it waits on belongs to, and an answer to a grant never undoes a removal.
 *
 * One rule decides when k may be handed out: the row is free and the last complete census saw no
 * actor carrying 256 + k. A corpse carries its key for as long as it lies there, and a second copy
 * under the same key would be two actors to every reader of the key. A census that could not walk
 * the whole pool concludes nothing from what it did not see.
 */
#ifndef MULTIPLAYER_MP_NPC_COPIES_H
#define MULTIPLAYER_MP_NPC_COPIES_H

#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A grant the census never sees alive in three seconds of substeps ends: it was not built, or it
 * was built and died at once. A freed k rests a second before it is handed out again, so that a
 * late message about its last life finds it idle. Both count censuses, not time, so a paused game
 * does not run out its deadlines. */
#define MP_NPC_COPIES_SEEN_DEADLINE 90u
#define MP_NPC_COPIES_REST          30u

/* A wish from the wire: four at once, then one a second, per slot. */
#define MP_NPC_COPIES_BUCKET_BURST 4u
#define MP_NPC_COPIES_BUCKET_MS    1000u

/* The refusal reasons the contract names, 1 to 10; the counters index by them. */
#define MP_NPC_COPIES_REASONS 11u

typedef enum mp_npc_copy_state {
    MP_NPC_COPY_FREE = 0,
    MP_NPC_COPY_GRANTED,   /* handed to the host's overlay, not seen alive yet */
    MP_NPC_COPY_LIVE,      /* seen alive */
    MP_NPC_COPY_ENDING,    /* on its way out: cancelled, never built, or dead */
    MP_NPC_COPY_RESTING,   /* gone, waiting out the rest */
    MP_NPC_COPY_FOREIGN    /* an actor carries 256 + k that this table never handed out */
} mp_npc_copy_state_t;

/* What a row owes. Three go to the host's own overlay through its note, two to the wire. */
#define MP_NPC_COPY_DUTY_GRANT_NOTE   0x01u   /* BUILD to the overlay */
#define MP_NPC_COPY_DUTY_CANCEL_NOTE  0x02u   /* CANCEL to the overlay */
#define MP_NPC_COPY_DUTY_OWNER_NOTE   0x04u   /* OWNER to the overlay */
#define MP_NPC_COPY_DUTY_REFUSE_OWN   0x08u   /* REFUSED to the overlay, for its own wish */
#define MP_NPC_COPY_DUTY_ANNOUNCE     0x10u   /* the build entry to everybody */
#define MP_NPC_COPY_DUTY_REFUSE_ASKER 0x20u   /* NOT_BUILT to the client that asked */

#define MP_NPC_COPY_DUTIES_NOTE                                                                  \
    (MP_NPC_COPY_DUTY_GRANT_NOTE | MP_NPC_COPY_DUTY_CANCEL_NOTE | MP_NPC_COPY_DUTY_OWNER_NOTE | \
     MP_NPC_COPY_DUTY_REFUSE_OWN)
#define MP_NPC_COPY_DUTIES_WIRE (MP_NPC_COPY_DUTY_ANNOUNCE | MP_NPC_COPY_DUTY_REFUSE_ASKER)

/* Who a copy is for. The host is slot 0 with connection 0; a connection is the session's 64-bit
 * id of a peer's current connection, so a replacement on the same slot is somebody else. */
typedef struct mp_npc_copy_who {
    uint8_t  owner;
    uint64_t owner_connection;
    uint8_t  asker;
    uint64_t asker_connection;
    bool     asker_own;   /* the host's own overlay asked; `wish` is then its note serial */
    uint32_t wish;        /* the asker's serial: the wire's sixteen bits, or the note's */
} mp_npc_copy_who_t;

/* Which note a row's open serial belongs to. */
typedef enum mp_npc_copy_wait {
    MP_NPC_COPY_WAIT_NONE = 0,
    MP_NPC_COPY_WAIT_GRANT,
    MP_NPC_COPY_WAIT_CANCEL
} mp_npc_copy_wait_t;

typedef struct mp_npc_copy_row {
    uint8_t           state;          /* mp_npc_copy_state_t */
    uint8_t           generation;     /* the life last handed out, 1 to 255, kept by a clear */
    uint8_t           duties;         /* MP_NPC_COPY_DUTY_* */
    uint8_t           waiting_for;    /* mp_npc_copy_wait_t: what `waiting` answers */
    bool              present;        /* the last census that could tell saw an actor with the
                                       * key: one that saw it, or a complete one */
    bool              live;           /* and it was alive */
    bool              seen;           /* this census: seen at all */
    bool              seen_live;      /* this census: seen alive */
    bool              announced;      /* its first announcement went out */
    bool              recancelled;    /* a live copy in ENDING got its one further cancel */
    bool              swept;          /* its corpse got its cancel */
    bool              stuck;          /* counted once as alive in ENDING after both */
    mp_npc_copy_who_t who;
    uint32_t          waiting;        /* the serial of the last grant or cancel written, 0 none */
    uint32_t          owner_waiting;  /* the serial of the last owner note, 0 none */
    uint32_t          since;          /* the census count at the last change of state */
    uint8_t           desc[MP_NPC_COPY_DESC_BYTES];
} mp_npc_copy_row_t;

typedef struct mp_npc_copies_counters {
    uint32_t taken;                           /* wishes decided */
    uint32_t from_wire;                       /* of them from a client */
    uint32_t throttled;                       /* a client's, refused as too fast */
    uint32_t granted;                         /* wishes granted */
    uint32_t refused[MP_NPC_COPIES_REASONS];  /* wishes refused, by reason */
    uint32_t stale;                           /* the host's own wish from another epoch */
    uint32_t removals;                        /* removal wishes carried out */
    uint32_t removals_empty;                  /* of them with nothing to remove */
    uint32_t first_seen;                      /* granted, and then seen alive */
    uint32_t never_seen;                      /* granted, and no actor within the deadline */
    uint32_t built_dead;                      /* granted, and only ever seen dead */
    uint32_t not_built;                       /* the host's overlay refused a grant */
    uint32_t gone;                            /* live copies seen dead or away */
    uint32_t died_unannounced;                /* of them before their first announcement */
    uint32_t ridden;                          /* a cancel refused: the copy stays, live */
    uint32_t recancelled;                     /* a live copy in ENDING cancelled once more */
    uint32_t stuck;                           /* and still alive after that */
    uint32_t corpses_swept;
    uint32_t foreign;                         /* rows that turned foreign */
    uint32_t doubles;                         /* one key seen twice in one census; must be 0 */
    uint32_t incomplete;                      /* censuses that could not walk the whole pool */
    uint32_t answers_unmatched;
    uint32_t answers_lost;
    uint32_t answers_open;                    /* an answer that was none, refused */
    uint32_t duties_unowed;                   /* a duty reported done that was not owed */
    uint32_t refusals_dropped;                /* owed to an asker who is gone */
    uint32_t owners_changed;
} mp_npc_copies_counters_t;

typedef struct mp_npc_copies_bucket {
    uint64_t connection;
    uint32_t due_ms;        /* the theoretical arrival time of the next wish */
    uint32_t answered_ms;   /* when a refusal for going too fast was last said */
    bool     used;
    bool     answered;
} mp_npc_copies_bucket_t;

typedef struct mp_npc_copies {
    mp_npc_copy_row_t        row[MP_WIRE_COPY_MAX];
    mp_npc_copies_bucket_t   bucket[NPC_SPAWN_WORLD_SLOTS];
    uint32_t                 censuses;
    uint32_t                 chain_length;      /* the pool at the last complete census */
    uint32_t                 capacity;
    bool                     pool_known;
    uint32_t                 corpse_censuses;   /* sweep a copy's corpse after this many, 0 never */
    mp_npc_copies_counters_t counters;
} mp_npc_copies_t;

/* The whole table and its counters from nothing, once, with the corpse sweep: 0 leaves a copy's
 * corpse to the engine, which keeps it twenty minutes of level time. */
void mp_npc_copies_init(mp_npc_copies_t *copies, uint32_t corpse_censuses);

/* Empties the table for a new world or a session's end. Generations and counters survive: a
 * generation is per process, and the report at a level's end is read after the clear. */
void mp_npc_copies_clear(mp_npc_copies_t *copies);

/* ---- The census, once every armed substep, with or without a peer. ---- */
void mp_npc_copies_census_begin(mp_npc_copies_t *copies);
void mp_npc_copies_census_saw(mp_npc_copies_t *copies, uint32_t k, bool live);
/* `complete` is false when the walk stopped short; then only what it saw counts. */
void mp_npc_copies_census_end(mp_npc_copies_t *copies, bool complete, uint32_t chain_length,
                              uint32_t capacity);

/* ---- The decision. ---- */
typedef struct mp_npc_copy_ask {
    uint8_t  kind;                /* NPC_SPAWN_WISH_* */
    bool     from_wire;           /* a client's 0xA1; only these are throttled */
    bool     bucket_ok;           /* the asker's slot had a token, asked of the throttle */
    uint8_t  wish_epoch;          /* the host's own wish: the epoch it was described in */
    uint8_t  epoch;               /* the grant note's epoch now */
    bool     in_level;            /* the host plays a level, not the lobby */
    uint16_t wish_level;          /* a wire wish: its level */
    uint16_t level;               /* and the host's */
    uint8_t  wish_world;          /* a wire wish: its world generation */
    uint8_t  world;               /* and the host's */
    bool     has_builder;         /* the host's overlay said it can build */
    bool     description_sound;   /* the description rounded */
    uint32_t cap;                 /* the host's copies at most */
} mp_npc_copy_ask_t;

typedef enum mp_npc_copy_outcome {
    MP_NPC_COPY_GRANT = 1,   /* hand out `k` */
    MP_NPC_COPY_REFUSE,      /* answer `reason` */
    MP_NPC_COPY_STALE,       /* the host's own wish from another epoch: no answer, the overlay ends
                              * it itself */
    MP_NPC_COPY_REMOVE       /* a removal passed: carry it out with cancel_owned or cancel_all */
} mp_npc_copy_outcome_t;

typedef struct mp_npc_copy_verdict {
    uint8_t  outcome;   /* mp_npc_copy_outcome_t */
    uint8_t  reason;    /* NPC_SPAWN_REFUSED_* for a refusal */
    uint32_t k;         /* for a grant */
} mp_npc_copy_verdict_t;

/* Decides one wish, and counts it. Checked in this order: another epoch (the host's own wish), the
 * throttle (a wire wish), the level and world, then for a spawn the builder, the description, the
 * cap, the pool (mp_pool_may_take with NPC_SPAWN_POOL_RESERVE, the grants not yet built counted)
 * and a key. */
void mp_npc_copies_decide(mp_npc_copies_t *copies, const mp_npc_copy_ask_t *ask,
                          mp_npc_copy_verdict_t *verdict);

/* Hands out `k` after a grant verdict, with the rounded description's wire bytes. Answers the new
 * generation, or 0 when k may not be handed out. */
uint8_t mp_npc_copies_grant(mp_npc_copies_t *copies, uint32_t k, const mp_npc_copy_who_t *who,
                            const uint8_t *desc);

/* Carries out a removal: every granted and live copy of that owner on that connection, or every
 * copy. Answers how many. */
uint32_t mp_npc_copies_cancel_owned(mp_npc_copies_t *copies, uint8_t slot, uint64_t connection);
uint32_t mp_npc_copies_cancel_all(mp_npc_copies_t *copies);

/* A copy whose owner's slot now holds another connection, or nobody, goes to the host. Reads
 * NPC_SPAWN_WORLD_SLOTS entries. */
void mp_npc_copies_owners(mp_npc_copies_t *copies, const uint64_t *connection_of);

/* ---- Duties. ---- */

/* The lowest k owing one of the duties in `mask`, and the first of them. False when none is owed.
 * The mask is what can go now: the note's duties while it has room, the wire's while the channel
 * takes them. */
bool mp_npc_copies_next_duty(const mp_npc_copies_t *copies, uint8_t mask, uint32_t *k,
                             uint8_t *duty);

/* The note entry a grant, cancel, owner or own refusal duty writes, ready for npc_spawn_note. */
bool mp_npc_copies_note_for(const mp_npc_copies_t *copies, uint32_t k, uint8_t duty,
                            npc_spawn_grant_t *out);

/* The wire entry an announce or refusal duty sends, with the host's level and world. */
bool mp_npc_copies_entry_for(const mp_npc_copies_t *copies, uint32_t k, uint8_t duty,
                             uint16_t level, uint8_t world, mp_npc_copy_entry_t *out);

/* The refusal of a client's wish, with the level and world the wish came with, so that the one
 * that says "another world" is not dropped by the asker for coming from one. */
bool mp_npc_copies_refusal_for(const mp_npc_copy_wish_t *wish, uint8_t asker, uint8_t reason,
                               mp_npc_copy_entry_t *out);

/* Whether the client a refusal is owed to still sits on its slot on the same connection. */
bool mp_npc_copies_asker_present(const mp_npc_copies_t *copies, uint32_t k,
                                 const uint64_t *connection_of);

/* A duty done: `serial` is the note serial for the note duties. A duty not owed is counted and
 * changes nothing. A duty dropped, because its asker is gone, is counted. */
void mp_npc_copies_duty_done(mp_npc_copies_t *copies, uint32_t k, uint8_t duty, uint32_t serial);
void mp_npc_copies_duty_drop(mp_npc_copies_t *copies, uint32_t k, uint8_t duty);

/* The overlay's answer to the note entry with `serial`. OPEN is no answer and is refused. */
void mp_npc_copies_take_answer(mp_npc_copies_t *copies, uint32_t serial,
                               npc_spawn_answer_t answer);

/* ---- Reading. ---- */
/* The life of a copy this table handed out and still holds, for the block and the relay. */
bool     mp_npc_copies_generation(const mp_npc_copies_t *copies, uint32_t k, uint8_t *out);
bool     mp_npc_copies_owner(const mp_npc_copies_t *copies, uint32_t k, uint8_t *out);
/* Whether the enemy block may describe the actor under 256 + k. */
bool     mp_npc_copies_describable(const mp_npc_copies_t *copies, uint32_t k);
/* The next k after `*cursor`, round the table, that is live and seen alive by the last census,
 * for the repetition to late joiners. The clock is the caller's: four a second at most. */
bool     mp_npc_copies_next_live(const mp_npc_copies_t *copies, uint32_t *cursor, uint32_t *k);
/* Granted and live copies, and ending ones still alive: what the cap counts. */
uint32_t mp_npc_copies_count(const mp_npc_copies_t *copies);

/* ---- Whom a copy goes with. ---- */

/* Whether copy k goes with a player: a following or a helping copy with a life. `owner` is the
 * world slot of the player it belongs to, 0 for the host. */
bool mp_npc_copies_follows(const mp_npc_copies_t *copies, uint32_t k, uint8_t *owner);

/* The one rule for both of its readers, the aim and the flyers' anchors: a copy goes with its
 * owner while the owner stands, and with the host, 0, when the owner is the host, is gone or is
 * dead. What "the host" means is each reader's: the aim hands the engine's own answer back, the
 * anchors the host's point. */
uint8_t mp_npc_copies_goes_with(uint8_t owner, bool owner_stands);

/* The anchor record for the overlay's flyers: slot s is its player's point while bit s of
 * `stands` says that player stands, and the host's point, slot 0, otherwise; with no host point a
 * slot whose player does not stand is not valid. */
void mp_npc_copies_anchors(const float point[NPC_SPAWN_WORLD_SLOTS][3], uint16_t stands,
                           uint8_t epoch, npc_spawn_anchor_record_t *out);

/* A wish from the wire takes a token from its slot's bucket. A new connection on the slot starts a
 * full bucket. Times are milliseconds and may wrap. */
bool mp_npc_copies_throttle(mp_npc_copies_t *copies, uint8_t slot, uint64_t connection,
                            uint32_t now_ms);

/* Whether a refusal for going too fast is said to this slot now: once an interval, so that a
 * client that floods is not answered in kind. */
bool mp_npc_copies_throttle_answer(mp_npc_copies_t *copies, uint8_t slot, uint32_t now_ms);

#endif /* MULTIPLAYER_MP_NPC_COPIES_H */
