/* npc_spawn_session.h: the panel's half of the spawn contract, while a multiplayer session runs
 * the copies.
 *
 * In a session the panel builds no copy of its own accord. What it is asked for goes to the
 * multiplayer as a wish, and it builds, drops and hands on copies only as the multiplayer's grant
 * record says, under the key the host chose (common/npc_spawn_note.h). This keeps the panel's
 * record: its wishes in serial order behind a queue of its own, stamped with the epoch it read
 * when the wish was described and never stamped again, and every grant answered in order, done or
 * refused, except while something holds every grant alike (a load, a save window, no world).
 *
 * It remembers which life of each key it built, so that a late cancel of a life that is gone
 * touches nothing, and which of its restore wishes carried how a saved copy stood, so that the
 * grant that answers one raises the copy where the save had it.
 *
 * When the epoch moves it ends every wish of another epoch, queued or sent, and forgets the saved
 * state of every restore among them. A client, whose copies belong to its host's world, removes the
 * copies of the old epoch a fresh walk still finds; one it cannot remove yet, because the player
 * rides it or something holds the pool, is asked again every frame until it goes or the world
 * changes under it. The host's stay in its world.
 *
 * Pure: the engine is reached only through the two calls a caller hands in. Internal to
 * dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_SESSION_H
#define DEV_OVERLAY_NPC_SPAWN_SESSION_H

#include "npc_spawn_block.h"
#include "npc_spawn_desc.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wishes waiting behind the six places of the record: a load brings back as many copies as a
 * session holds, one restore wish each. */
#define NPC_SPAWN_SESSION_QUEUE NPC_SPAWN_COPIES_MAX

/* What the engine made of a grant. */
typedef enum npc_spawn_outcome {
    NPC_SPAWN_OUTCOME_DONE = 0,   /* built, removed, or nothing there to remove */
    NPC_SPAWN_OUTCOME_REFUSED,    /* could not be built here, or the player rides it */
    NPC_SPAWN_OUTCOME_NOT_NOW     /* this frame holds every grant alike: answered later */
} npc_spawn_outcome_t;

typedef struct npc_spawn_session_ops {
    /* Builds `desc` under `key`; `saved`, when given, is how a restored copy stood. */
    npc_spawn_outcome_t (*build)(void *user, const npc_spawn_desc_t *desc, uint32_t key,
                                 const npc_spawn_saved_t *saved);
    /* Removes whatever actor carries `key`, a corpse included; REFUSED when the player rides it or
     * the pool could not be walked to the end. */
    npc_spawn_outcome_t (*remove)(void *user, uint32_t key);
    void *user;
    /* Told of every refusal of this panel's own wishes: the serial, the kind of wish when it is
     * still known here (0 when not) and the reason (NPC_SPAWN_REFUSED_*). May be NULL. */
    void (*refused)(void *user, uint32_t wish, uint8_t kind, uint8_t reason);
} npc_spawn_session_ops_t;

typedef struct npc_spawn_session_counters {
    uint32_t wishes;          /* wishes made */
    uint32_t queue_full;      /* a wish with no room even in the queue, dropped */
    uint32_t ended;           /* wishes of another epoch, ended here */
    uint32_t built;
    uint32_t build_refused;
    uint32_t cancelled;
    uint32_t cancel_ridden;
    uint32_t cancel_stale;    /* a cancel of a life this panel no longer holds */
    uint32_t owners;
    uint32_t refusals;        /* refusals of this panel's own wishes */
    uint32_t restores_placed; /* a restore wish's saved state used by its build */
    uint32_t restores_lost;   /* a restore with no room left to keep how its copy stood */
    uint32_t old_removed;     /* a client's copies of an old epoch removed */
    uint32_t old_found;       /* ... found at the epoch, each then removed or taken */
    uint32_t old_gone;        /* ... still standing when the world changed, which took them */
    uint32_t held_back;       /* frames that held every grant alike */
    uint32_t unsound;         /* a wish the contract cannot carry, never queued */
    uint32_t epochs;          /* epochs moved while a grant record was read */
} npc_spawn_session_counters_t;

/* What this panel remembers of its own wishes, so a refusal can say what was refused: the kinds of
 * the last this many serials. A refusal comes within frames of its wish. */
#define NPC_SPAWN_SESSION_KINDS 32u

/* A wish waiting for a place in the record; a restore keeps how its copy stood. */
typedef struct npc_spawn_session_queued {
    npc_spawn_wish_t  wish;
    npc_spawn_saved_t saved;
    bool              restore;
} npc_spawn_session_queued_t;

typedef struct npc_spawn_session {
    npc_spawn_wish_record_t      record;        /* what this panel publishes */
    npc_spawn_session_queued_t   queue[NPC_SPAWN_SESSION_QUEUE];
    uint32_t                     queued;
    bool                         read;          /* a grant record was read at least once */
    uint8_t                      epoch;         /* the epoch of the last grant record read */
    uint8_t                      own_slot;
    uint16_t                     cap;
    uint8_t                      built[NPC_SPAWN_COPIES_MAX];   /* the life built per k, 0 none */
    bool                         built_as_client[NPC_SPAWN_COPIES_MAX];
    bool                         leftover[NPC_SPAWN_COPIES_MAX];   /* an old epoch's, to remove */
    uint8_t                      owner[NPC_SPAWN_COPIES_MAX];   /* the world slot a copy is for */
    uint32_t                     world;         /* the world the leftovers stand in */
    uint32_t                     kind_serial[NPC_SPAWN_SESSION_KINDS];
    uint8_t                      kind[NPC_SPAWN_SESSION_KINDS];
    npc_spawn_saved_t            restore[NPC_SPAWN_SESSION_QUEUE];
    uint32_t                     restore_serial[NPC_SPAWN_SESSION_QUEUE];
    uint32_t                     restores;
    bool                         changed;       /* the record is to be published again */
    npc_spawn_session_counters_t counters;
} npc_spawn_session_t;

/* From nothing, with what this panel can do (NPC_SPAWN_PANEL_*). The record is to be published at
 * once: a multiplayer that finds none refuses every wish for want of a builder. */
void npc_spawn_session_init(npc_spawn_session_t *session, uint8_t panel);
void npc_spawn_session_set_panel(npc_spawn_session_t *session, uint8_t panel);

/* The world this machine stands in, counted by whoever can see the levels change. A change takes
 * every copy left over from an old epoch with it, so none is asked for again: its key may be a new
 * copy's by then. */
void npc_spawn_session_set_world(npc_spawn_session_t *session, uint32_t world);

/* One frame with the grant record as read now, or NULL when none could be read. `now` is false
 * while something holds every grant alike. Answers whether the wish record is to be published. */
bool npc_spawn_session_frame(npc_spawn_session_t *session,
                             const npc_spawn_grant_record_t *grants, bool now,
                             const npc_spawn_session_ops_t *ops);

/* Whether the multiplayer runs the copies: a host names its cap, a socket client its slot. Read
 * off the last grant record; outside a session the panel builds as it always did. A client is
 * whatever names no cap, since a host always names one, and a client's slot comes a moment after
 * its session does. */
bool npc_spawn_session_active(const npc_spawn_session_t *session);
bool npc_spawn_session_is_client(const npc_spawn_session_t *session);

/* A wish, stamped with the epoch of the last grant record read. `desc` for a spawn and a restore,
 * `saved` for a restore only. False when it cannot be carried or finds no room. */
bool npc_spawn_session_wish(npc_spawn_session_t *session, uint8_t kind,
                            const npc_spawn_desc_t *desc, const npc_spawn_saved_t *saved);

/* The world slot of the player copy k belongs to, as its build and every owner change since said;
 * false for a k this panel holds no life of. */
bool npc_spawn_session_owner(const npc_spawn_session_t *session, uint32_t k, uint8_t *owner);

/* Where the flyer of the copy under `key` flies to. */
typedef enum npc_spawn_flyer {
    NPC_SPAWN_FLYER_LOCAL = 0,   /* over the local player: no session that runs the copies, a
                                  * client's copy (parked, it flies nowhere), or one of no grant */
    NPC_SPAWN_FLYER_ANCHOR,      /* over the anchor published for its owner, in `out` */
    NPC_SPAWN_FLYER_KEEP         /* no anchor of this epoch for its owner: the record stays */
} npc_spawn_flyer_t;

/* The choice, from the anchor record last read, NULL for none. */
npc_spawn_flyer_t npc_spawn_session_flyer(const npc_spawn_session_t *session,
                                          const npc_spawn_anchor_record_t *anchors, uint32_t key,
                                          float out[3]);

const npc_spawn_wish_record_t *npc_spawn_session_record(const npc_spawn_session_t *session);
void npc_spawn_session_published(npc_spawn_session_t *session, bool taken);

/* The panel's description and the contract's, both ways. False for one the other cannot hold. */
bool npc_spawn_session_to_note(const npc_spawn_desc_t *desc, npc_spawn_note_desc_t *out);
bool npc_spawn_session_from_note(const npc_spawn_note_desc_t *desc, npc_spawn_desc_t *out);

#endif /* DEV_OVERLAY_NPC_SPAWN_SESSION_H */
