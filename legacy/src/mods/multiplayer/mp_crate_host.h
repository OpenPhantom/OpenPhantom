/* mp_crate_host.h: the host's side of the push blocks, as a machine over a table of engine calls.
 *
 * Layer 1. The host owns every push block of its level. It pushes each one itself, through the
 * engine's own push: for its own player where the player pushes, and for a client on that
 * client's wish, with the client's puppet as the body pushing. It describes every block back in
 * one note, and tells a fall as a moment.
 *
 * Who may push: one rule for everybody, the host's own player included, and it lives in
 * mp_crate_owner_*: whoever pushes first holds the block while pushing and for the holding time
 * after, and a let-go ends it. The host's player pushes inside the engine's own player phase and a
 * client's wish is pushed later in the same substep, so where the two start in one substep the
 * host's player is first. That is where each push happens, not a preference, and it is said here
 * because a tie in the host's favour is what a player will see.
 *
 * What arrives between substeps: a wish can be taken by the idle pump while a level loads, so
 * taking one only files it; every look at the engine, the index check included, waits for
 * mp_crate_host_run inside a substep.
 */
#ifndef MULTIPLAYER_MP_CRATE_HOST_H
#define MULTIPLAYER_MP_CRATE_HOST_H

#include "mp_crate_play.h"
#include "mp_crate_rule.h"
#include "mp_crate_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wishes filed between two runs, and falls waiting for the channel. A client sends a wish at most
 * every four substeps, so four clients with every block in hand stay far below the first; a fall
 * is rare, and one that finds this full is counted rather than dropped silently. */
#define MP_CRATE_INBOX 64u
#define MP_CRATE_FALLS 8u

typedef struct mp_crate_host_block {
    uint8_t          id;
    bool             can_sink;
    mp_crate_owner_t owner;
    bool             working;         /* a client's target is being pushed toward */
    uint8_t          wish_slot;
    uint8_t          wish_flags;
    uint16_t         wish_sequence;   /* the owner's newest wish taken */
    uint8_t          verdict;         /* on that wish */
    float            target[3];
    float            last_distance;
    uint16_t         newest[MP_CRATE_SLOT_LIMIT];   /* per slot, the newest sequence taken */
    uint16_t         newest_known;                  /* one bit per slot */
    bool             own_push_now;    /* the host's player pushed it in this substep */
    bool             own_pushing;     /* and in the one before */
    mp_crate_entry_t sent;
    bool             sent_once;
    uint8_t          said_holder;
    uint32_t         said_at;
    bool             said_once;
} mp_crate_host_block_t;

typedef struct mp_crate_host_stats {
    uint32_t blocks;
    uint32_t can_sink;
    uint32_t away;
    uint32_t sunk;
    uint32_t own_pushes;
    uint32_t own_refused;
    uint32_t taken;
    uint32_t reached;
    uint32_t engine_refused;
    uint32_t other_owner;
    uint32_t stale;
    uint32_t falls;
    uint32_t whole_sent;
    uint32_t change_sent;
    uint32_t largest;
    uint32_t note_refused;
    uint32_t entries_sent;
    uint32_t falls_sent;
    uint32_t falls_waited;
    uint32_t no_block;
    uint32_t no_body;
    uint32_t torn;
    uint32_t stalled;
} mp_crate_host_stats_t;

typedef struct mp_crate_host_filed {
    uint8_t         slot;
    mp_crate_push_t push;
} mp_crate_host_filed_t;

/* A line worth saying when a block changes hands, handed to the binding, which owns the log. */
typedef void (*mp_crate_host_say_fn_t)(uint8_t id, uint8_t slot);

typedef struct mp_crate_host {
    bool                  level_known;
    uint16_t              level;
    uint32_t              generation;
    uint32_t              movers;
    size_t                count;
    mp_crate_host_block_t block[MP_CRATE_MAX];
    mp_crate_cadence_t    cadence;

    mp_crate_host_filed_t inbox[MP_CRATE_INBOX];
    size_t                filed;
    uint32_t              newest_generation[MP_CRATE_SLOT_LIMIT];
    uint16_t              generation_known;              /* one bit per slot */

    /* The note built and not yet answered by the channel. */
    mp_crate_due_t        pending_due;
    bool                  pending_in[MP_CRATE_MAX];
    mp_crate_entry_t      pending_entry[MP_CRATE_MAX];
    uint8_t               pending_count;
    size_t                pending_bytes;

    mp_crate_fall_t       fall[MP_CRATE_FALLS];
    size_t                fall_head;
    size_t                fall_count;
    bool                  fall_waiting;   /* the oldest fall found the channel full once */

    mp_crate_host_say_fn_t say_held;
    mp_crate_host_stats_t  stats;
} mp_crate_host_t;

void mp_crate_host_init(mp_crate_host_t *host);

/* The level this substep runs in. A level, a generation or a mover count other than the last one
 * starts every block over and finds the level's push blocks again by their rig. */
void mp_crate_host_level(mp_crate_host_t *host, const mp_crate_engine_t *engine, uint16_t level,
                         uint32_t generation, uint32_t movers);

/* A peer arrived: the next note is whole. */
void mp_crate_host_arrival(mp_crate_host_t *host);

/* A message from the player in `slot`. True for a wish, torn or not, which is then filed. */
bool mp_crate_host_take(mp_crate_host_t *host, uint8_t slot, const uint8_t *note, size_t bytes);

/* Inside a substep, after the puppets are placed: the filed wishes judged, then every block with a
 * target pushed one step toward it. */
void mp_crate_host_run(mp_crate_host_t *host, const mp_crate_engine_t *engine, uint32_t now);

/* The host's own player is about to push block `id`. False refuses it: another player holds it. */
bool mp_crate_host_own_push(mp_crate_host_t *host, uint32_t id, uint32_t now);

/* The engine's drop answered 1 for block `id` inside a push: the fall is told, once. */
void mp_crate_host_fell(mp_crate_host_t *host, uint32_t tick, uint32_t id, const float position[3],
                        const float delta[3]);

/* A block sank here. */
void mp_crate_host_sank(mp_crate_host_t *host);

/* The end of a substep: the host's player lets go of what it did not push in it. */
void mp_crate_host_end_substep(mp_crate_host_t *host, uint32_t now);

/* This substep's note, or 0 bytes when none is due. Then `mp_crate_host_note_sent` with the
 * channel's answer. */
size_t mp_crate_host_next_note(mp_crate_host_t *host, const mp_crate_engine_t *engine,
                               uint32_t now, uint8_t *buffer, size_t capacity);
void   mp_crate_host_note_sent(mp_crate_host_t *host, bool sent, uint32_t now);

/* The oldest fall not yet sent, or 0 bytes. Then `mp_crate_host_fall_sent`. */
size_t mp_crate_host_next_fall(mp_crate_host_t *host, uint8_t *buffer, size_t capacity);
void   mp_crate_host_fall_sent(mp_crate_host_t *host, bool sent);

#endif /* MULTIPLAYER_MP_CRATE_HOST_H */
