/* mp_enemy_sync_internal.h: what the two halves of the enemy block module share.
 *
 * The module was one file until the copies an editor spawns needed room of their own. The seam
 * is the one its size note had named: the sending half, which runs on a host, and the receiving
 * half, which runs on a client, share nothing but the table of placements and the census that
 * fills it. Nothing here is public: mp_enemy_sync.h stays the interface. The state lives in
 * mp_enemy_sync.c and is reached through mp_enemy_sync_state, because a mutable global in a
 * header is what this tree forbids.
 */
#ifndef MULTIPLAYER_MP_ENEMY_SYNC_INTERNAL_H
#define MULTIPLAYER_MP_ENEMY_SYNC_INTERNAL_H

#include "mp_enemy_sync.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_queue_rule.h"
#include "mp_enemy_wire.h"
#include "mp_knockback_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One placement, as this module remembers it between substeps. */
typedef struct placement {
    mp_enemy_record_t mirror;      /* on a client, what the host last said */
    mp_enemy_record_t current;     /* on a host, what the census read this substep */
    bool              current_ok;
    mp_enemy_record_t written;     /* the record the flush last gave the body here, which is the
                                    * pose the engine is interpolating away from; a write the
                                    * body refused is given as well, so it proves no write */
    bool              written_known;
    bool              dirty;       /* the mirror moved and the body has not been told yet */
    mp_enemy_queue_t  queue;       /* on a client, a record kept in front of the mirror */
    uint32_t          dressed_pass;   /* the flush pass it was last dressed in, 0 for never */
    bool              let_go;      /* the host stopped listing it; hand it back this substep */
    uintptr_t         actor;       /* where it lives on this machine, 0 when it does not */
    uint8_t           generation;
    bool              live;        /* live in the walk that just ran */
    bool              was_live;    /* live in the previous walk, which is how a respawn is seen */
    bool              known;       /* the mirror holds something rather than nothing */
    bool              wanted;      /* the host has it alive and this machine has no body for it */
    bool              unspawnable; /* asked, and told this is not one a receiver creates */
    bool              health_up;   /* this life was reported with a health above zero */
    uintptr_t         kept_actor;  /* the body a removal kept as a corpse here, 0 for none */
    uint8_t           kept_generation;   /* and the life it was kept for */
    uint8_t           forgot_by;   /* mp_enemy_forgot_t: what last made a client forget it */
} placement_t;

/* What last took a key out of a client's table, for the line that names a record refused for want
 * of a base. Never known is what a key is until its first record is taken. */
typedef enum mp_enemy_forgot {
    MP_ENEMY_FORGOT_NEVER_KNOWN = 0,
    MP_ENEMY_FORGOT_RESET,     /* the table was reset: a level's end, a session's, a restore */
    MP_ENEMY_FORGOT_REMOVAL,   /* a removal named the life it held */
    MP_ENEMY_FORGOT_LET_GO     /* the host stopped listing it and its replica was handed back */
} mp_enemy_forgot_t;

/* Why a client did not take a block. One exit counts it and says it. */
typedef enum mp_enemy_refusal {
    MP_ENEMY_REFUSAL_NONE = 0,
    MP_ENEMY_REFUSAL_TORN,           /* malformed, or too short for its header */
    MP_ENEMY_REFUSAL_TORN_COPIES,    /* malformed in the copies' part */
    MP_ENEMY_REFUSAL_OLDER,          /* older than a block already taken */
    MP_ENEMY_REFUSAL_NO_LEVEL,       /* no level open here */
    MP_ENEMY_REFUSAL_OTHER_LEVEL,    /* about a level this side is not in */
    MP_ENEMY_REFUSAL_BASELESS        /* a record not whole, with nothing to be read against */
} mp_enemy_refusal_t;

/* One refused block, for the line that says it. The key and its lives are the refused record's,
 * set only for MP_ENEMY_REFUSAL_BASELESS. */
typedef struct mp_enemy_refusal_note {
    mp_enemy_refusal_t why;
    uint32_t           tick;             /* the host's substep that carried it */
    uint16_t           block_level;
    uint16_t           key;
    uint8_t            life;             /* the life its identity names */
    bool               names_life;       /* whether its mask carried the life field */
    bool               names_position;
} mp_enemy_refusal_note_t;

/* How many of those lines one reset may print, by kind, and how many a view of a host may. */
#define MP_ENEMY_SYNC_LINES_BASELESS 8u
#define MP_ENEMY_SYNC_LINES_LOADING  4u   /* other refusals before the first block taken */
#define MP_ENEMY_SYNC_LINES_LATER    4u   /* and after it, which is what a level has in it */
#define MP_ENEMY_SYNC_LINES_VIEW     6u

/* How many deaths the report follows one by one, the first of a run. */
#define MP_ENEMY_SYNC_DEATHS 8u

/* One death the host reported, and what became of the replica from its first dead record on. */
typedef struct death_watch {
    uint16_t level;           /* the level it died in, so a later level cannot match it */
    uint16_t key;
    uint8_t  generation;
    uint8_t  state;           /* the state, the clip and the health the host reported */
    uint8_t  clip;
    uint8_t  outcome;         /* what the first dead record met on this side */
    int32_t  health;
    uint32_t last_clip;       /* the clip last written to the replica */
    uint32_t last_head;       /* and its playhead, sixteenths of a frame */
    uint32_t after_removal;   /* writes that reached the body after a removal kept it */
    uint8_t  laid;            /* the replica had no class after a write, as the host's body */
    uint8_t  death_state;     /* the host reported a death state for that life */
} death_watch_t;

/* What one peer is believed to hold of every placement, on a host. */
/* How many sent payloads a view remembers by their keys. The acknowledgement has to arrive inside
 * this window or the view is given up whole, which costs one round of full records and is correct.
 * The channel's own reordering window is 32 and a relay's turnaround is a handful of substeps, so
 * 32 is both generous and the number the rest of the wire already uses. */
#define MP_ENEMY_SYNC_ACK_RING 32u

#define MP_ENEMY_SYNC_KEY_BITMAP_BYTES ((MP_ENEMY_SYNC_KEYS + 7u) / 8u)

/* How many replicas handed back are remembered, for the line that names a level switch. */
#define MP_ENEMY_SYNC_LET_GO 16u

/* What the interest rule keeps of one view between substeps. A view given up whole keeps it,
 * because its peer still holds every replica it was told about and a silence that gave the view up
 * is still a silence; a new connection and a reset start it over with the rest of the view. */
typedef struct send_interest {
    mp_enemy_interest_row_t row[MP_ENEMY_SYNC_KEYS];
    uint32_t                silent;   /* substeps since an acknowledgement last moved the view on */
} send_interest_t;

/* What the report says of one view, across resets and connections like the other counters. */
typedef struct interest_counts {
    uint32_t blocks;
    uint32_t records;
    uint32_t by_reach[4];     /* indexed by mp_enemy_reach_t: none, near, middle, far */
    uint32_t must;            /* records that could not wait, written or not */
    uint32_t must_state;
    uint32_t must_life;
    uint32_t must_unfit;      /* of them, left out for want of room: must stay 0 */
    uint32_t first_near;      /* first descriptions in the near class, written */
    uint32_t first_event;     /* first descriptions for a world event, not near, written */
    uint32_t deferred;        /* offers left for a later block, for room or for a silent peer */
    uint32_t oldest[4];       /* the longest any key waited, by class, in substeps */
    uint32_t whole;
    uint32_t throttled;       /* substeps this view counted as silent */
    uint32_t far_withheld;    /* far keys this peer was never told about, per substep */
    uint32_t must_bytes_most;
    uint32_t must_bytes_apart;   /* blocks whose must-send bytes the query did not predict */
    uint32_t floor_raised;       /* blocks whose floor rose above the fixed one */
    uint32_t floor_most;         /* the highest floor a block of this view asked for */
    uint32_t floor_short;        /* of the raised, blocks given less room than their floor */
    uint32_t struck_noted;       /* hits on this view's player noted for the enemy that made them */
    uint32_t struck_boosted;     /* key substeps doubled for a hit where the target alone was not */
} interest_counts_t;

typedef struct send_view {
    mp_enemy_record_t mirror[MP_ENEMY_SYNC_KEYS];    /* believed to be held there */
    mp_enemy_record_t pending[MP_ENEMY_SYNC_KEYS];   /* described, not sent yet */
    bool              known[MP_ENEMY_SYNC_KEYS];
    bool              described[MP_ENEMY_SYNC_KEYS];     /* written by the encode just built */

    /* Which keys each sent payload carried, by the SUBSTEP that carried it. This is the whole cost
     * of noticing a payload that never arrived: 48 bytes a substep, not a copy of the records.
     * The acknowledgement names the newest payload the far side decoded, so a substep that the
     * acknowledgements step OVER was never taken, and the keys it carried are opened again. */
    uint32_t          sent_tick[MP_ENEMY_SYNC_ACK_RING];
    bool              sent_valid[MP_ENEMY_SYNC_ACK_RING];   /* the slot holds a stamp at all */
    uint8_t           sent_keys[MP_ENEMY_SYNC_ACK_RING][MP_ENEMY_SYNC_KEY_BITMAP_BYTES];
    uint32_t          acked_tick;

    /* Whether an acknowledgement has named a payload of this view since the view was last reset,
     * which is the only proof that the far side holds anything to read a delta against. Until
     * then every record goes out whole. A join, a level change, a reset and a view given up whole
     * all clear it with the rest of the view. */
    bool              confirmed;

    /* Whether the far side has acknowledged a payload that carried this key's life WHOLE. `known`
     * moves on when a payload goes out, so a key whose first whole record was lost was deltaed
     * against a base the far side never got, and read there against nothing. A delta therefore
     * waits for this as well; until it holds, the key goes whole. `whole_keys` is which keys each
     * stamped payload carried whole, cleared with its slot, and `described_whole` is the encode's
     * word for the record it just wrote. */
    bool              based[MP_ENEMY_SYNC_KEYS];
    bool              described_whole[MP_ENEMY_SYNC_KEYS];
    uint8_t           whole_keys[MP_ENEMY_SYNC_ACK_RING][MP_ENEMY_SYNC_KEY_BITMAP_BYTES];

    /* The round robin's cursor over every key, placements and copies alike. It breaks the last tie
     * of the interest rule, so with equal priorities a block starts behind the key the last one
     * wrote last, as the walk it replaces did. */
    size_t            next;

    send_interest_t   interest;
} send_view_t;

typedef struct enemy_sync_state {
    placement_t placement[MP_ENEMY_SYNC_KEYS];   /* a row per key: placements, then copies */
    send_view_t view[MP_ENEMY_SYNC_VIEWS];
    bool        send_ready;        /* the census ran this substep */
    uint8_t     bitmap[MP_ENEMY_SYNC_BITMAP_BYTES];
    uint8_t     copy_bitmap[MP_ENEMY_SYNC_COPY_BITMAP_BYTES];
    size_t      copy_bitmap_bytes; /* as far as the highest live copy needs; 0 with none */
    uint16_t    level;             /* the level this machine is in, as the bridge told it */
    uint16_t    applied_level;     /* the level the wishes are about */
    bool        level_known;
    uint32_t    blocks_sent;
    uint32_t    records_sent;
    uint32_t    blocks_applied;
    uint32_t    records_applied;
    uint32_t    missing_actor;     /* live on the host, no local actor to put it on */
    uint32_t    spawned;
    uint32_t    spawn_later;       /* asked, and told to ask again */
    uint32_t    spawn_never;       /* asked, and told not to */
    uint32_t    other_level;       /* blocks about a level this machine is not in */
    uint32_t    no_level;          /* blocks arriving with no level open here */
    uint32_t    out_of_order;      /* blocks older than one already taken */
    uint32_t    acked;             /* acknowledgements that moved a view forward */
    uint32_t    reopened;          /* keys of a payload the far side never named */
    uint32_t    bits_stepped;      /* stamped substeps an acknowledgement stepped over */
    uint32_t    bits_kept;         /* of them, decoded there by the word of the bits */
    uint32_t    bits_missing;      /* of them, missing there, and their keys opened again */
    uint32_t    bits_missing_keys; /* the keys those opened */
    uint32_t    bits_unstamped;    /* bits naming a substep this view never stamped */
    uint32_t    view_given_up;     /* the view could not tell what was missed and started over */
    uint32_t    given_up_unheard;  /* of those, at a send: no acknowledgement for a whole ring */
    uint32_t    sent_whole;        /* records sent whole to a view nothing had confirmed yet */
    uint32_t    opened_whole;      /* and to a confirmed view, for a key it did not know */
    uint32_t    whole_unbased;     /* and for a key it knew whose whole record was unacknowledged */
    uint32_t    based_acknowledged;   /* keys an acknowledgement proved held whole there */
    uint32_t    census_new_level;  /* censuses that found a level other than the last one's */
    uint16_t    census_level;      /* the level the last census of a host ran in */
    bool        census_level_known;
    uint8_t     lines_baseless;    /* the lines printed since the last reset, by budget */
    uint8_t     lines_loading;
    uint8_t     lines_later;
    uint8_t     lines_view[MP_ENEMY_SYNC_VIEWS];
    uint32_t    lines_left_out;    /* lines their budget held back, across resets */
    uint32_t    views_confirmed;   /* acknowledgements that confirmed a view */
    uint32_t    baseless_refused;  /* blocks refused for a record with no base that was not whole */
    uint32_t    baseless_positioned;   /* of them, the record named a position */
    uint32_t    applied_tick;      /* the newest payload whose block was taken */
    bool        applied_tick_known;
    uint32_t    unbuilt;           /* encodes refused because no level was open here */
    uint32_t    refused;
    uint32_t    index_too_large;
    uint32_t    resets;
    uint32_t    released;
    uintptr_t   let_go[MP_ENEMY_SYNC_LET_GO];   /* the last replicas handed back, for a line */
    uint32_t    let_go_next;
    uint32_t    flushes;     /* bodies written from a substep rather than from the pump */
    uint32_t    flush_pass;  /* flushes run, the substep a write belongs to */
    uint32_t    flush_gone;  /* the actor was gone by the time the write was due */
    uint32_t    gone_freed;      /* of those, on a slot the pool had taken back */
    uint32_t    gone_other;      /* of those, on a slot another life holds */
    uint32_t    gone_old_corpse; /* of those, a corpse kept here for an older life */
    uint32_t    corpse_written;  /* writes to a body a removal kept, which the host lists */
    uint32_t    released_corpses;   /* of the let go, bodies a removal kept */
    death_watch_t deaths[MP_ENEMY_SYNC_DEATHS];
    uint32_t    death_count;
    mp_knockback_edges_t knockback;   /* the throws the host reported, as written here */
    uint32_t    abandoned;
    uint32_t    copy_blocks_sent;      /* blocks with a copies' part */
    uint32_t    copy_records_sent;
    uint32_t    copy_crowded;          /* not built: no room for the part's head */
    uint32_t    copy_blocks_applied;
    uint32_t    copy_records_applied;
    uint32_t    copy_unbuilt;          /* records for a copy nobody built here for the host */
    uint32_t    copy_refused;          /* blocks refused for their copies' part */
    uint32_t    copy_written;          /* records applied to a replica built here */
    uint32_t    copy_undescribed;      /* a live copy the host's table does not hand out */
    const mp_npc_copies_t  *copies_host;     /* the copies' tables, NULL outside a session */
    mp_npc_copies_client_t *copies_client;
    bool        enabled;
    bool        index_logged;
    bool        missing_logged;
    bool        spawned_logged;
    uint32_t    released_unlisted;       /* of the let go, because the host stopped listing them */
    uint32_t    released_unlisted_dead;  /* of those, dead in the host's last record of them */

    /* The world the interest rule reads, and what it read of it this substep: each key once for
     * every view, each view's player once. Both are read on first use after a census, so the
     * question for the floor and the block that follows it see the same answers. */
    mp_enemy_sync_interest_t interest;
    mp_enemy_subject_t       subject[MP_ENEMY_SYNC_KEYS];
    bool                     subjects_read;
    mp_enemy_viewer_t        viewer[MP_ENEMY_SYNC_VIEWS];
    bool                     viewer_read[MP_ENEMY_SYNC_VIEWS];
    interest_counts_t        counts[MP_ENEMY_SYNC_VIEWS];

    /* A client's witness of the same rule: when each placement the host lists last came with a
     * record, and how often two of its records lay further apart than the far class allows. */
    uint32_t    gap_tick[MP_ENEMY_SYNC_MAX_PLACEMENTS];
    bool        gap_known[MP_ENEMY_SYNC_MAX_PLACEMENTS];
    uint32_t    gap_resets;          /* the reset count the ticks belong to */
    uint32_t    gaps_measured;
    uint32_t    gaps_over;
    uint32_t    gap_longest;
} enemy_sync_state_t;

/* The one state both halves read and write. */
enemy_sync_state_t *mp_enemy_sync_state(void);

/* One pass over the pool, leaving `live` and `actor` current and `was_live` as it was. A host
 * takes it once per substep before it describes; a client takes it in every apply. Taken twice
 * in one host substep it would move `was_live` and swallow the generation of every new life. */
void mp_enemy_sync_take_census(void);

/* What begin_send makes of a census: the two bitmaps, each row's life, each live row read once.
 * Apart from the census, so a test can hand it rows without an engine. */
void mp_enemy_sync_describe_census(void);

/* What a key's record carries in its index field, a byte: a placement's key, a copy's k. */
uint32_t mp_enemy_sync_wire_index(size_t key);

/* Follows the deaths the host reports, for the report: `now` is the record the flush gave the
 * replica, which is not the mirror while an older record is written first, `slot` what the flush
 * found for it, `wrote` whether it reached the replica. For a replica gone before the write it is
 * the mirror, the host's newest word. Every record that reaches a replica is also counted for the
 * knockback line first, a copy's included. Called before `written` takes `now`. */
void mp_enemy_sync_watch_death(size_t key, placement_t *p, mp_enemy_slot_t slot, bool wrote,
                               const mp_enemy_record_t *now);

/* One record onto its replica from the substep, parked and written: the flush's and the spawner's
 * one way into a body. A second write of one replica in one flush pass, or a record naming a life
 * other than the table's, is counted as a fault the report says must be 0. */
bool mp_enemy_sync_dress(size_t key, placement_t *p, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *previous);

/* Empties the record a key kept in front of its mirror, and counts it among the ones a let go or a
 * removal dropped. Every way out of holding a key takes it; a reset clears the table with it. */
void mp_enemy_sync_queue_drop(size_t key, placement_t *p);

/* On a client, a block this side took: which placements the host listed, which of them it
 * described, and on which substep. Counts the gaps between two records of one replica for the
 * report; changes nothing. */
void mp_enemy_sync_note_gaps(const uint8_t *bitmap, const bool *named, uint32_t tick);

/* On a client, a block this side refused: one line within its budget, a count past it. The budgets
 * are cleared by mp_enemy_sync_reset, so every world says its first refusals again. */
void mp_enemy_sync_note_refusal(const mp_enemy_refusal_note_t *note);

/* One replica handed back because the host stopped listing it, counted apart from the ones a reset
 * or a session end lets go, and with whether the host's last word was a death. Called before the
 * mirror is forgotten. A removal note that arrives after the block that dropped the actor finds
 * the replica already handed back, and these two numbers are how often that race is run. */
void mp_enemy_sync_count_unlisted(const placement_t *p);

#endif /* MULTIPLAYER_MP_ENEMY_SYNC_INTERNAL_H */
