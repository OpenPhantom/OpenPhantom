/* mp_npc_copies_model_internal.h: what the two halves of the two-machine model share.
 *
 * mp_npc_copies_model.c is the world: the machines, the channel, the level and the counts.
 * mp_npc_copies_model_panel.c is the overlays: the model's own, and the door an outside one comes
 * through. One state, s_sim, for the one run there is at a time.
 */
#ifndef UNITTESTS_MP_NPC_COPIES_MODEL_INTERNAL_H
#define UNITTESTS_MP_NPC_COPIES_MODEL_INTERNAL_H

#include "mp_npc_copies_model.h"

#include "mp_npc_copies_run.h"
#include "mp_npc_copy_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POOL        128u
#define QUEUE       1024u
#define CLIENT_SLOT 1u
#define WISHES      8192u
#define LEVEL       1u
#define CAP         12u

enum { M_NOTE = 1, M_DESPAWN, M_BLOCK, M_WORLD };
enum { W_NONE = 0, W_OPEN, W_EXEMPT };

typedef struct actor {
    bool     used;
    bool     live;
    uint16_t key;
    uint32_t leaves_at;   /* a corpse leaves the pool at this step */
} actor_t;

typedef struct message {
    uint32_t due;
    uint8_t  kind;
    uint8_t  world;
    uint8_t  bytes[MP_NPC_COPY_ENTRY_BYTES];
    size_t   count;
    uint32_t k;
    uint8_t  generation;
    uint8_t  bitmap[MP_WIRE_COPY_MAX / 8u];
    uint8_t  generations[MP_WIRE_COPY_MAX];
} message_t;

/* Each wish an overlay made: open or not tracked, how often it was answered, its epoch. */
typedef struct track {
    uint8_t state[WISHES];
    uint8_t answers[WISHES];
    uint8_t epoch[WISHES];
} track_t;

/* An overlay: the record it publishes, what it last read of the multiplayer's, its wishes. */
typedef struct panel {
    npc_spawn_wish_record_t  record;
    npc_spawn_grant_record_t view;
    bool                     absent;
    uint8_t                  epoch;
    track_t                  track;
} panel_t;

typedef struct machine {
    mp_npc_copies_run_t run;
    actor_t             pool[POOL];
    panel_t             panel;
    bool                ridden[MP_WIRE_COPY_MAX];
    uint8_t             world;
    uint32_t            lobby_until;   /* no level open before this step */
    uint32_t            slot_at;       /* a client is told its slot at this step */
} machine_t;

typedef struct sim {
    uint32_t           random;
    uint32_t           step;
    int                breaking;
    bool               quiet;
    machine_t          host;
    machine_t          client;
    message_t          to_client[QUEUE];
    size_t             to_client_count;
    message_t          to_host[QUEUE];
    size_t             to_host_count;
    uint64_t           connection;
    npc_model_result_t counts;
    uint16_t           hidden_key;
    bool               last_walk_whole;
    uint32_t           apart_until;       /* no peer joined before this step */
    uint32_t           unparked_until;    /* the client cannot park before this step */
    const npc_model_overlay_t *overlay;   /* NULL: the model's own overlays */
} sim_t;

extern sim_t s_sim;

/* The world's helpers the overlays use. */
uint32_t roll(sim_t *s, uint32_t range);
actor_t *find(actor_t *pool, uint32_t key);
actor_t *add(actor_t *pool, uint32_t key);
void     random_desc(sim_t *s, npc_spawn_note_desc_t *desc);
void     despawn_to_client(sim_t *s, uint32_t k, uint8_t generation);

/* The overlays, as the world drives them. */
void       answered(sim_t *s, panel_t *p, uint32_t wish, bool builds);
int        machine_index(const sim_t *s, const machine_t *m);
machine_t *machine_at(sim_t *s, int machine);
void       panel_reset(sim_t *s, machine_t *m, uint32_t first, bool absent);
uint32_t   panel_wish(sim_t *s, machine_t *m, uint8_t kind, bool tracked);
void       panel_step(sim_t *s, machine_t *m, bool host);

#endif /* UNITTESTS_MP_NPC_COPIES_MODEL_INTERNAL_H */
