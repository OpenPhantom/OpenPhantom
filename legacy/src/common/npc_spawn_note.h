/* common/npc_spawn_note.h: what the developer overlay's spawner and the multiplayer say to each
 * other, so that a copy one player spawns stands on every machine of a session.
 *
 * The overlay builds copies. In a session the multiplayer decides which copy may exist and under
 * which key, because only the host can hand out a key nobody else is using. The two are separate
 * DLLs that may not call each other, so what passes between them is three records filed through
 * common/shared_note:
 *
 *   npc_spawn_wish    written by the overlay: what it was asked to spawn or remove, and which
 *                     grants it has dealt with;
 *   npc_spawn_grant   written by the multiplayer: what the overlay is to build, drop or give to
 *                     another owner, and which wishes it has taken;
 *   npc_spawn_anchor  written by the multiplayer: the point each player's copies fly to.
 *
 * Each record has exactly one writer. An acknowledgement is written by the reader into its own
 * record and never into the one it read. The overlay publishes its record, with entries or with
 * none, as soon as it can build, because a multiplayer that finds no wish record refuses every
 * wish for want of a builder.
 *
 * The rings. The wish and grant records each carry a short run of entries with consecutive
 * serials, the first one's in `first`. The writer appends while there is room and drops from the
 * front what the other side has acknowledged; what does not fit waits in the writer's own queue.
 * The reader takes the entries after what it has already taken, in order, and acknowledges them.
 * Serials start at 1 and 0 names no entry, which is what an acknowledgement of 0 means; a writer's
 * counter stops short of the wrap rather than give out 0, thirty years away at four a second. With
 * no entries, `first` is the serial the next entry will get. A serial
 * counts up for the life of the process and never starts again, not even when a session ends: one
 * that started again would sit below what the reader has taken, and everything after it would be
 * skipped. Serials are compared by distance with npc_spawn_note_serial_after, so the wrap changes
 * nothing. The multiplayer reads the answer to a grant in the same read in which it drops it; the
 * record keeps only the last 32 answers.
 *
 * One bad entry stops both ways. A publisher refuses the whole record for one entry that is not
 * sound, and the acknowledgements in that record with it, and the other side can never take the
 * entry out again. So a writer judges every entry with the predicates below before it queues it,
 * and drops and counts one that fails. For the same reason, because an acknowledgement is a mark
 * and not a list, the overlay holds a grant unanswered only for what holds every grant alike: no
 * world, a load in progress, a save window open. Whatever concerns one grant alone is answered,
 * done or refused.
 *
 * The epoch, in the grant record, says which world the entries belong to. The multiplayer raises it
 * when the engine ends a level (module message 6), restarts one (7) or builds a new world (0x19),
 * and when a session ends; in the same call it empties its grants and its own queue behind them
 * and publishes the raised epoch. A grant emptied that way is
 * never asked about, and the overlay's next answer passes over it: an acknowledgement is a mark,
 * and a serial behind the mark that the record no longer holds reads as not done there. A wish
 * carries the epoch the overlay read when it described the wish, is never stamped again, and is
 * thrown away when it is taken under another epoch. Every wish still open that carries another
 * epoch than the grant record's is ended: the overlay drops it, queued or sent, and counts it
 * ended. The host takes its own overlay's wishes only once a level is entered; until then they
 * wait in the overlay's record, which is where a load's restores are before the level begins.
 *
 * `own_slot` is the world slot of a client that plays over a socket, and 0 everywhere else:
 * outside a session, on the host, on the loopback. It is not the multiplayer's own idea of its
 * slot, which is 1 before any session and keeps a client's after one. So the record that carries
 * a raise is always one the publisher accepts, and a slot that is not 0 is the one sign of a
 * machine that holds copies for another machine's session.
 *
 * Removing. Only the overlay removes a copy, so that one test decides whether the local player is
 * riding it. The multiplayer asks with a CANCEL, and a copy the player rides is answered refused.
 * A corpse is not a removal: a copy the host has killed is made a corpse on a client by the
 * multiplayer, with the engine's removal for a corpse (reason 0x0E), which frees nothing.
 * When the epoch changes, an overlay that built copies under a record that named no cap, a
 * client's, removes those a fresh walk of the pool still finds standing, never through a pointer
 * it kept. One it cannot remove yet, because the player rides it or something holds the pool, it
 * asks again until it goes or the world changes under it. The host's stay in its world.
 *
 * A read that is refused changes nothing on the reader's side. A torn one succeeds a frame later;
 * one of another shape fails the same way every frame, which a reader cannot tell from a record
 * nobody published. For the overlay both mean a session that does not run the copies, which is
 * the answer that builds nothing it was not granted.
 */
#ifndef COMMON_NPC_SPAWN_NOTE_H
#define COMMON_NPC_SPAWN_NOTE_H

#include <stdbool.h>
#include <stdint.h>

/* The names the records are filed under. Both sides use these literals and nothing else. */
#define NPC_SPAWN_WISH_NOTE_NAME   "npc_spawn_wish"
#define NPC_SPAWN_GRANT_NOTE_NAME  "npc_spawn_grant"
#define NPC_SPAWN_ANCHOR_NOTE_NAME "npc_spawn_anchor"

/* A copy's key is 256 + k, past every placement a level can hold: a level holds at most 255,
 * indexed 0 to 254. The key is the copy's engine index as well as the name the multiplayer sends
 * it under. k stays below 128, the engine's actor pool, because every copy takes a slot there. */
#define NPC_SPAWN_COPIES_MAX 128u
#define NPC_SPAWN_KEY_FIRST  256u
#define NPC_SPAWN_KEY_END    (NPC_SPAWN_KEY_FIRST + NPC_SPAWN_COPIES_MAX)

/* No placement. The largest placement index is 254, so 255 says "none"; only an archive kind may
 * have none, because a level kind is built from its placement. */
#define NPC_SPAWN_NO_SOURCE 0xFFu

/* An actor file's name, measured: the longest in the archive and in every level's manifest is
 * twelve characters. A longer one is refused, never cut, because a cut name is another file. */
#define NPC_SPAWN_FILE_MAX 12u

/* The actor slots a copy never takes, so that the level's own spawns always find room: a full
 * pool is the one case in which the engine culls every corpse for good. The host counts it before
 * it grants, and every builder counts it again, because two machines' pools are not one pool. */
#define NPC_SPAWN_POOL_RESERVE 16u

/* Stand, follow, attack, help: the scripts a copy can be given. */
#define NPC_SPAWN_BEHAVIOURS 4u

/* The two of them whose copy goes with a player: the multiplayer's aim answers such a copy with
 * its owner. The overlay holds its own names for all four to these numbers. */
#define NPC_SPAWN_BEHAVIOUR_FOLLOW 1u
#define NPC_SPAWN_BEHAVIOUR_HELP   3u

/* The world slots of a session, the host's included: the owner of a copy is one of them. The
 * host's is 0. */
#define NPC_SPAWN_WORLD_SLOTS 16u

#define NPC_SPAWN_WISH_SLOTS  6u
#define NPC_SPAWN_GRANT_SLOTS 5u

/* The description's flags. */
#define NPC_SPAWN_DESC_ARCHIVE 0x01u   /* the file is the archive's, `source` lends it a record */

/* What the overlay has standing, in the wish record's `panel`. */
#define NPC_SPAWN_PANEL_BUILDER     0x01u   /* it can build a copy */
#define NPC_SPAWN_PANEL_SAVE_HULL   0x02u   /* a save keeps copies out of the engine's own block */
#define NPC_SPAWN_PANEL_ARCHIVE     0x04u   /* it can load an actor file from the archive */
#define NPC_SPAWN_PANEL_TRIPOD_LOCK 0x08u   /* it refuses a save while a copied tripod is ridden */

enum {
    NPC_SPAWN_WISH_SPAWN      = 1,   /* build this */
    NPC_SPAWN_WISH_REMOVE_OWN = 2,   /* remove every copy this machine's player owns */
    NPC_SPAWN_WISH_REMOVE_ALL = 3,   /* remove every copy; the host's own overlay only */
    NPC_SPAWN_WISH_RESTORE    = 4    /* build this one a save held; the host's own overlay only */
};

/* A grant that names a key names one life of it, `generation`: a late CANCEL or OWNER for a key
 * handed out again since does not touch the new copy. */
enum {
    NPC_SPAWN_GRANT_BUILD   = 1,   /* build `key` from `desc` */
    NPC_SPAWN_GRANT_REFUSED = 2,   /* the wish `wish` of this machine is refused for `reason` */
    NPC_SPAWN_GRANT_CANCEL  = 3,   /* `key` is gone: drop it if unbuilt, remove it if built */
    NPC_SPAWN_GRANT_OWNER   = 4    /* `key` belongs to `owner` from now on */
};

/* Why a wish was refused. A number the overlay does not know is shown as a number. */
enum {
    NPC_SPAWN_REFUSED_CAP        = 1,    /* the session holds as many copies as its host allows */
    NPC_SPAWN_REFUSED_POOL       = 2,    /* the actor pool is too full to take another */
    NPC_SPAWN_REFUSED_NO_LEVEL   = 3,    /* the lobby, or a world that changed under the wish */
    NPC_SPAWN_REFUSED_NO_BUILDER = 4,    /* the host has no overlay that could build it */
    NPC_SPAWN_REFUSED_SERVER     = 5,    /* the host is a dedicated server */
    NPC_SPAWN_REFUSED_TOO_FAST   = 6,    /* more wishes than a player may send */
    NPC_SPAWN_REFUSED_NOT_BUILT  = 7,    /* the host's overlay could not build it */
    NPC_SPAWN_REFUSED_NO_PARKING = 8,    /* this machine cannot hold a copy still */
    NPC_SPAWN_REFUSED_UNSOUND    = 9,    /* the description is not one the wire can carry */
    NPC_SPAWN_REFUSED_NO_KEY     = 10    /* under the cap, but every free k is still resting */
};

/* What the one who asks decides. The rest of a copy's record is derived from this and from the
 * level, the same way on every machine that holds the same level and archive. In a session the
 * multiplayer rounds the position and the facing the way the wire does before it hands them back,
 * to its own overlay as well, so every machine builds from the same bits. */
typedef struct npc_spawn_note_desc {
    uint8_t source;                    /* a level kind's placement, an archive kind's donor */
    uint8_t behaviour;                 /* below NPC_SPAWN_BEHAVIOURS */
    uint8_t flags;                     /* NPC_SPAWN_DESC_* */
    uint8_t reserved;
    float   position[3];               /* at the feet; a flyer's height is the builder's */
    float   facing;                    /* degrees, as a placement's start yaw */
    char    file[NPC_SPAWN_FILE_MAX];  /* NUL filled after the name, none after twelve */
} npc_spawn_note_desc_t;

typedef struct npc_spawn_wish {
    uint8_t               kind;    /* NPC_SPAWN_WISH_* */
    uint8_t               epoch;   /* the grant record's epoch when this was described */
    uint16_t              reserved;
    npc_spawn_note_desc_t desc;    /* all zero for a removal */
} npc_spawn_wish_t;

typedef struct npc_spawn_wish_record {
    uint16_t         version;
    uint8_t          count;          /* entries in use, from entry[0] */
    uint8_t          panel;          /* NPC_SPAWN_PANEL_* */
    uint32_t         first;          /* entry[0]'s serial */
    uint32_t         grants_done;    /* every grant up to this serial is dealt with */
    uint32_t         refused;        /* bit i: grant grants_done minus i was not done here */
    uint32_t         reserved;
    npc_spawn_wish_t entry[NPC_SPAWN_WISH_SLOTS];
} npc_spawn_wish_record_t;

typedef struct npc_spawn_grant {
    uint8_t               kind;         /* NPC_SPAWN_GRANT_* */
    uint8_t               generation;   /* which life of `key` */
    uint16_t              key;          /* NPC_SPAWN_KEY_FIRST + k; 0 in a refusal */
    uint8_t               owner;        /* the owner's world slot */
    uint8_t               reason;       /* NPC_SPAWN_REFUSED_*, in a refusal only */
    uint16_t              reserved;
    uint32_t              wish;         /* this machine's wish it answers, else 0 */
    npc_spawn_note_desc_t desc;         /* for a build; all zero otherwise */
} npc_spawn_grant_t;

typedef struct npc_spawn_grant_record {
    uint16_t          version;
    uint8_t           count;          /* entries in use, from entry[0] */
    uint8_t           own_slot;       /* a socket client's world slot, else 0 (see the top) */
    uint32_t          first;          /* entry[0]'s serial */
    uint32_t          wishes_taken;   /* every wish up to this serial is taken; taken is not
                                       * granted, and a grant made for another machine names no
                                       * wish */
    uint16_t          cap;            /* the host's copies at most, 0 outside a session */
    uint8_t           epoch;          /* see the top of this file */
    uint8_t           reserved;
    npc_spawn_grant_t entry[NPC_SPAWN_GRANT_SLOTS];
} npc_spawn_grant_record_t;

/* Where the copies of each world slot's player fly to, already resolved: a player who is dead or
 * gone has the host's point in their slot, and a slot with no point to fly to is not valid. */
typedef struct npc_spawn_anchor_record {
    uint16_t version;
    uint16_t valid;                               /* bit s: point[s] is one */
    uint8_t  epoch;                               /* the grant record's epoch it belongs to */
    uint8_t  reserved[3];
    float    point[NPC_SPAWN_WORLD_SLOTS][3];     /* zero in a slot that is not valid */
} npc_spawn_anchor_record_t;

/* Whether a key is a copy's rather than a placement's. The overlay asks this; the multiplayer
 * asks its own wire's test, which is held to the same range where it is compiled. */
bool npc_spawn_key_is_copy(uint32_t key);

/* Whether an entry is one the records may carry: a known kind, a copy's key where the kind needs
 * one and none in a refusal, an owner inside the session, zero where nothing goes, and a
 * description with a file name the archive could hold, a placement unless it is an archive kind,
 * a known behaviour and flag, and finite numbers. A writer asks before it queues (see the top). */
bool npc_spawn_note_desc_is_sound(const npc_spawn_note_desc_t *desc);
bool npc_spawn_note_wish_is_sound(const npc_spawn_wish_t *wish);
bool npc_spawn_note_grant_is_sound(const npc_spawn_grant_t *grant);

/* Files a record under its name, with the version filled in and the unused entries and points
 * zeroed. False when the count is past the slots, an entry in use is not sound, a valid anchor is
 * not finite or the own slot is past the session, or when the channel refused; nothing is
 * published then and a reader goes on seeing the previous record. A publisher that answers false
 * after it once answered true is looking at an entry its writer should not have queued. */
bool npc_spawn_note_publish_wishes(const npc_spawn_wish_record_t *record);
bool npc_spawn_note_publish_grants(const npc_spawn_grant_record_t *record);
bool npc_spawn_note_publish_anchors(const npc_spawn_anchor_record_t *record);

/* Reads one back. False when nobody has published it, it is torn or of another shape, or it holds
 * anything the publisher would have refused; `out` is left alone then. What comes back true is
 * what the publisher accepted, so a caller judges an entry by its meaning, not its form. */
bool npc_spawn_note_read_wishes(npc_spawn_wish_record_t *out);
bool npc_spawn_note_read_grants(npc_spawn_grant_record_t *out);
bool npc_spawn_note_read_anchors(npc_spawn_anchor_record_t *out);

/* Whether `serial` comes after `mark`: less than half the counter ahead of it. The one comparison
 * of serials on both sides. */
bool npc_spawn_note_serial_after(uint32_t serial, uint32_t mark);

/* The rings (see the top), for the writer of each record. An append takes one sound entry at the
 * serial after the last one, `first + count`, and only while there is room; false, and nothing
 * changed, otherwise. A drop takes off the front every entry whose serial does not come after the
 * other side's mark and answers how many. A restart empties the grant ring, which is what an epoch
 * does, with the serial the next entry will get in `first`: it never moves back. */
bool npc_spawn_note_grants_append(npc_spawn_grant_record_t *record, uint32_t serial,
                                  const npc_spawn_grant_t *grant);
uint32_t npc_spawn_note_grants_drop(npc_spawn_grant_record_t *record, uint32_t done);
void npc_spawn_note_grants_restart(npc_spawn_grant_record_t *record);
bool npc_spawn_note_wishes_append(npc_spawn_wish_record_t *record, uint32_t serial,
                                  const npc_spawn_wish_t *wish);
uint32_t npc_spawn_note_wishes_drop(npc_spawn_wish_record_t *record, uint32_t taken);

typedef enum npc_spawn_answer {
    NPC_SPAWN_ANSWER_OPEN,      /* the overlay has not dealt with it yet */
    NPC_SPAWN_ANSWER_DONE,      /* built, dropped, removed or given to its owner */
    NPC_SPAWN_ANSWER_REFUSED,   /* not done here, and not to be handed again in this life */
    NPC_SPAWN_ANSWER_LOST       /* dealt with too long ago for the record to say how */
} npc_spawn_answer_t;

/* The overlay's answer to grant `serial`, which must come after its `grants_done`: done, or
 * refused. Serials it skips, which an epoch emptied before the overlay saw them, are marked not
 * done here; nobody asks about them. False, and nothing written, for a serial that does not come
 * after the mark. */
bool npc_spawn_note_acknowledge(npc_spawn_wish_record_t *wishes, uint32_t serial, bool refused);

/* What the overlay's record says about grant `serial`. The record keeps the last 32 answers, and a
 * multiplayer that reads each answer in the read that drops its grant never has more than
 * NPC_SPAWN_GRANT_SLOTS of them waiting, so LOST is a writer that broke the protocol, never an
 * ordinary answer. */
npc_spawn_answer_t npc_spawn_note_answer(const npc_spawn_wish_record_t *wishes, uint32_t serial);

#endif /* COMMON_NPC_SPAWN_NOTE_H */
