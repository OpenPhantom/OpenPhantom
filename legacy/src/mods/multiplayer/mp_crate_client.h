/* mp_crate_client.h: a client's side of the push blocks, as a machine over a table of engine calls.
 *
 * Layer 1. A client never decides where a block is. Its own player still pushes like a single
 * player, because a push that waited a round trip for every step would feel like wading, and that
 * push is a prediction: each target it reaches goes to the host as a wish, and the host's note
 * says what became of it. Every other block this side merely draws where the host has it, catching
 * up at the engine's own pace and jumping only past a quarter of a unit.
 *
 * A client NEVER SINKS a block by itself. Its own landing is held (mp_crate_client_hold_sink) and
 * the block lies where it landed, whole, until the host's note says the host sank it too; then it
 * is sunk here the way a savegame sinks one, with the crush left to the host, which already hits
 * everybody standing there. A fall crosses as a moment and is run here with the engine's own drop,
 * so the block lands on this side's own floor.
 *
 * Everything that arrives is only filed. The machine acts at the start of a substep, in
 * mp_crate_client_apply, the one moment the world is certainly whole.
 */
#ifndef MULTIPLAYER_MP_CRATE_CLIENT_H
#define MULTIPLAYER_MP_CRATE_CLIENT_H

#include "mp_crate_play.h"
#include "mp_crate_rule.h"
#include "mp_crate_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The targets a client remembers to hold the host's answer against, and the falls it holds for
 * the next substep. Eight wishes are twice the window. */
#define MP_CRATE_REMEMBERED 8u
#define MP_CRATE_FALL_INBOX 8u

typedef struct mp_crate_client_block {
    uint8_t          id;
    bool             used;

    /* The host's newest entry, and whether it is news since the last substep. */
    bool             known;
    mp_crate_entry_t host;
    uint16_t         host_level;       /* the level the note that carried it described */
    uint32_t         host_tick;
    uint32_t         host_at;          /* this side's substep it arrived in */
    bool             fresh;

    /* A landing of this side's own, waiting for the host. */
    bool             sink_held;
    bool             sink_waiting;     /* the host sank it while this side's copy still fell */
    uint32_t         sink_wait_since;

    /* This player's own pushing of it. */
    bool             push_now;         /* a push call in this substep */
    bool             pushing;          /* and in the one before */
    bool             target_waiting;   /* a target not yet sent */
    bool             first;            /* the waiting target starts a push */
    bool             let_go_waiting;
    uint8_t          push_flags;
    float            target[3];
    uint16_t         sequence;         /* the newest sent */
    bool             sent_any;
    uint32_t         last_sent;
    uint16_t         taken;            /* the newest the host has taken */
    bool             taken_known;
    uint16_t         remembered_sequence[MP_CRATE_REMEMBERED];
    float            remembered_target[MP_CRATE_REMEMBERED][3];
    bool             remembered_used[MP_CRATE_REMEMBERED];
    bool             mine;
    uint32_t         mine_since;       /* the last wish sent or answer taken */
    bool             locked;
    uint32_t         locked_until;
    bool             correcting;
    float            correction[3];
} mp_crate_client_block_t;

typedef struct mp_crate_client_stats {
    uint32_t whole_taken;
    uint32_t change_taken;
    uint32_t torn;
    uint32_t elsewhere;
    uint32_t stale_generation;
    uint32_t change_before_whole;
    uint32_t agreed;
    uint32_t chased;
    uint32_t chase_steps;
    uint32_t jumped;
    float    worst_jump;
    uint32_t held_falling;
    uint32_t sinks_performed;
    uint32_t sinks_held;
    uint32_t sinks_cancelled;
    uint32_t unrepairable;
    uint32_t older;
    uint32_t push_calls;
    uint32_t let_through;
    uint32_t refused_owner;
    uint32_t refused_ahead;
    uint32_t refused_locked;
    uint32_t refused_falling;
    uint32_t wishes_sent;
    uint32_t let_go_sent;
    uint32_t wish_refused;
    uint32_t corrections;
    float    worst_correction;
    uint32_t falls_performed;
    uint32_t falls_otherwise;
    uint32_t falls_already;
} mp_crate_client_stats_t;

typedef struct mp_crate_client {
    bool                    level_known;
    uint16_t                level;
    uint32_t                newest_generation;
    bool                    generation_known;
    bool                    host_speaks;      /* a note of this level has arrived */
    bool                    whole_seen;       /* and a whole one among them */
    mp_crate_client_block_t block[MP_CRATE_MAX];

    mp_crate_fall_t         fall[MP_CRATE_FALL_INBOX];
    size_t                  falls;

    /* The wish built and not yet answered by the channel. */
    bool                    pending;
    size_t                  pending_block;
    uint16_t                pending_sequence;
    bool                    pending_let_go;

    mp_crate_client_stats_t stats;
} mp_crate_client_t;

void mp_crate_client_init(mp_crate_client_t *client);

/* The level this substep runs in: another one starts every block over. */
void mp_crate_client_level(mp_crate_client_t *client, uint16_t level);

/* A message from the host. True for a note or a fall, torn or not. `now` is this side's substep. */
bool mp_crate_client_take(mp_crate_client_t *client, const uint8_t *note, size_t bytes,
                          uint32_t now);

/* The start of a substep: the falls run, then every block the host has described is held against
 * this side's own and put right by the table. */
void mp_crate_client_apply(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                           uint8_t my_slot, uint32_t now);

/* This player's push of block `id` is about to go to the engine. Anything but LET leaves the
 * player standing. */
mp_crate_gate_t mp_crate_client_gate(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                                     uint32_t id, uint8_t my_slot, uint32_t now);

/* What the engine's push did with it: its answer and the block as it stands now. */
void mp_crate_client_pushed(mp_crate_client_t *client, uint32_t id, int32_t answer,
                            const mp_crate_body_t *after, bool pull, uint32_t now);

/* This side's own landing of block `id` would sink it: held for the host instead. */
void mp_crate_client_hold_sink(mp_crate_client_t *client, uint32_t id);

/* The end of a substep: a block this player stopped pushing gets its let-go. */
void mp_crate_client_end_substep(mp_crate_client_t *client, uint32_t now);

/* The next wish due, or 0 bytes. Then `mp_crate_client_push_sent` with the channel's answer; a
 * wish the channel refused stays and goes with the next substep. */
size_t mp_crate_client_next_push(mp_crate_client_t *client, uint16_t level, uint32_t generation,
                                 uint32_t now, uint8_t *buffer, size_t capacity);
void   mp_crate_client_push_sent(mp_crate_client_t *client, bool sent, uint32_t now);

#endif /* MULTIPLAYER_MP_CRATE_CLIENT_H */
