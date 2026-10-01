/* mp_npc_copies_model.h: a model of two machines running the NPC copies' protocol, for the tests.
 *
 * Two machines, each running mp_npc_copies_run, the code the multiplayer runs, with an actor pool
 * and an overlay that speaks the records of common/npc_spawn_note: it answers a step or two late,
 * now and then holds every grant alike, stamps a wish with the epoch it read and ends its wishes
 * when the epoch moves. A channel between the machines delivers in order and late, and now and then
 * will not take a message, and now and then the two lose each other or the client cannot hold a
 * copy still. And everything that happens in a level: wishes, bursts of them, restores
 * and removals, copies killed and lying as corpses, a player riding a copy, walks of the pool that
 * stop short, the lobby between two worlds, a client replaced on its slot, new worlds that reach
 * the client later than the host, and a new host process that counts its generations from one.
 *
 * A run counts what should never happen. Four ways of running break one thing on purpose, so that
 * a test can show each count trips when the thing it watches is broken; a fifth leaves the client
 * without an overlay, which is every machine that has no panel.
 *
 * The overlays are the model's own unless one is handed in (npc_model_run_with):
 * npc_spawn_session_sim.c runs the panel's session rules here, against the real core, and the
 * model keeps the pool, the channel and the counts.
 */
#ifndef UNITTESTS_MP_NPC_COPIES_MODEL_H
#define UNITTESTS_MP_NPC_COPIES_MODEL_H

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum npc_model_break {
    NPC_MODEL_HONEST = 0,
    NPC_MODEL_LYING_WALKS,      /* walks that stop short and say they did not */
    NPC_MODEL_HIDDEN_KEY,       /* one actor every walk misses, and says it saw all */
    NPC_MODEL_PHANTOM_LIVE,     /* a row made live with no actor behind it */
    NPC_MODEL_SAID_TWICE,       /* a wish answered a second time at its overlay */
    NPC_MODEL_NO_CLIENT_PANEL   /* not a break: the client has no overlay at all */
} npc_model_break_t;

typedef struct npc_model_result {
    uint32_t doubles;                 /* one key held twice in a pool */
    uint32_t built_over_an_actor;     /* an overlay asked to build over a standing actor */
    uint32_t uncounted;               /* a live copy on the host without a counting row */
    uint32_t live_without_actor;      /* a live row whose actor a complete census did not see */
    uint32_t announced_absent;        /* a build entry sent for a copy that does not stand */
    uint32_t grants_overdue;
    uint32_t answered_twice;
    uint32_t unanswered;              /* after the quiet: a wish not answered exactly once */
    uint32_t mismatched;              /* after the quiet: a key the two machines hold differently */
    uint32_t granted_across_epochs;   /* a wish described in one world and built in the next */
    uint32_t misrouted;               /* a refusal sent to a slot that did not ask */
    uint32_t handed_without_panel;    /* a build written for a client overlay that is not there */
    uint32_t entries_held_back;       /* build entries a client without an overlay kept back */
    uint32_t records_refused;         /* a record the contract's publisher would not carry */
    uint32_t copies_seen;             /* copy-steps alive, how busy the run was */
    uint32_t granted;
    uint32_t built;                   /* built on the client */
    uint32_t rides;                   /* refused cancels, on either machine */
} npc_model_result_t;

/* Twenty thousand steps of a level, then fifteen hundred quiet ones, and what was counted. */
void npc_model_run(uint32_t seed, int breaking, npc_model_result_t *out);

/* An overlay from outside the model. `machine` is 0 for the host and 1 for the client. */
typedef struct npc_model_overlay {
    /* A new overlay process on `machine`, its first serial `first`; none at all when `absent`. */
    void (*start)(void *user, int machine, uint32_t first, bool absent);
    /* A wish of `kind`, `desc` for a spawn or a restore: its serial when it went into the record at
     * once, 0 when it waits behind it. */
    uint32_t (*wish)(void *user, int machine, uint8_t kind, const npc_spawn_note_desc_t *desc);
    /* One step: the grant record as read now, and false when the step holds every grant alike. */
    void (*step)(void *user, int machine, const npc_spawn_grant_record_t *grants, bool now);
    /* The wish record the overlay publishes. */
    const npc_spawn_wish_record_t *(*record)(void *user, int machine);
    void *user;
} npc_model_overlay_t;

/* What an outside overlay reaches the engine through, on `machine`, during a run. A build answers
 * whether the copy stands now; a removal answers false when the player rides the copy. */
bool npc_model_build(int machine, uint32_t key);
bool npc_model_remove(int machine, uint32_t key);

/* The same run with `overlay` in place of the model's own overlays. */
void npc_model_run_with(uint32_t seed, int breaking, const npc_model_overlay_t *overlay,
                        npc_model_result_t *out);

#endif /* UNITTESTS_MP_NPC_COPIES_MODEL_H */
