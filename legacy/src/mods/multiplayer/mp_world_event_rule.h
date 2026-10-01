/* mp_world_event_rule.h: the moments of the world a host sends inside its enemy block, as a codec
 * and as the decisions both ends make about them.
 *
 * Layer 1, pure. No engine, no address, no socket.
 *
 * A script's emitter on an actor, an NPC's blade clang, a limb taken off: each happens once, and a
 * client whose replica runs no script has to be told. They used to ride the actor's own record, one
 * field each with a number in it, and a field has one place: of two emitters one script starts in
 * the same substep the first was lost, and a number that jumped by two lost the one between without
 * a count. So they travel as a LIST, each with a number of its own, which is the shape Quake 3
 * gives its temporary entities rather than the one it gives an entity's single event field.
 *
 * ====================================== Where the list is ======================================
 *
 * In every enemy block, at a fixed place: behind the presence bitmap and in front of the first
 * placement record. It begins with a head that is always there, a count and the two music calls
 * the host last claimed for this peer, and the count's events follow. The copies' part stays what
 * it was, the rest of the block, so neither part can be taken for the other.
 *
 * ====================================== One event on the wire ==================================
 *
 *     sequence  u16   host wide, never 0, wrapping past 65535 to 1
 *     age       u8    substeps between the event and the block that carries it, under the window
 *     kind      u8    the kind in the low six bits, AT_ACTOR in bit 7, HAS_PLACE in bit 6
 *     place     3 x u16, only with HAS_PLACE: the enemy record's own position quantisation
 *     a         u16   what the kind says, for the three old ones exactly what their field carried
 *     key, life u16 + u8, only with AT_ACTOR: the placement or copy key and its generation
 *     len       u8    how many bytes of tail follow
 *     tail      len bytes, as long as the kind table says for a kind this build knows
 *
 * The length is what lets a build step over a kind it does not know rather than read the block
 * wrong and throw it away; a known kind with a longer tail keeps what it knows and skips the rest,
 * and a shorter one is torn.
 *
 * ================================== Delivered once, and only once ==============================
 *
 * An event stands in every block to a peer until an acknowledgement names a block that carried it,
 * and for at most the window. The client remembers which numbers it has taken over a window of the
 * newest ones, wide enough for everything a host can post inside the window, and takes each number
 * once. A number behind that window is counted and not performed.
 */
#ifndef MULTIPLAYER_MP_WORLD_EVENT_RULE_H
#define MULTIPLAYER_MP_WORLD_EVENT_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The kinds. They stay below bit 6, so the two flags beside them can never be read as a kind. The
 * first three moved here out of the enemy record; the next three are sent by the modules that
 * build their sources, and a kind with no source is never posted. */
typedef enum mp_world_event_kind {
    MP_WORLD_EVENT_NONE = 0,
    MP_WORLD_EVENT_SCRIPT_EMITTER,   /* a: the emitter's template and node, packed */
    MP_WORLD_EVENT_NPC_CLANG,        /* a: the clang's kind, packed */
    MP_WORLD_EVENT_LIMB_FLY,         /* a: the rig ordinal of the node taken off, packed */
    MP_WORLD_EVENT_SCRIPT_SOUND,     /* a: the level's sound call */
    MP_WORLD_EVENT_EXPLODE_AT,       /* tail: the pitch the explosion's sound plays at */
    MP_WORLD_EVENT_ZAP_ARCS,         /* a: whom the arcs reach and which of the two sets */
    MP_WORLD_EVENT_KINDS
} mp_world_event_kind_t;

/* The two flags that share the kind's byte. */
#define MP_WORLD_EVENT_AT_ACTOR  0x80u
#define MP_WORLD_EVENT_HAS_PLACE 0x40u
#define MP_WORLD_EVENT_KIND_BITS 0x3Fu

/* How many substeps an event may still be performed after it happened, 312.5 ms. Beyond that a
 * death blast or a clang belongs to a past the player has already watched without it. */
#define MP_WORLD_EVENT_WINDOW 10u

/* How many events one block carries at most, and the most tail a kind may have. */
#define MP_WORLD_EVENT_PART_MAX 24u
#define MP_WORLD_EVENT_TAIL_MAX 4u

/* The bytes of one event: what every event has, a place, an actor, and the longest tail. */
#define MP_WORLD_EVENT_FIXED_BYTES 7u
#define MP_WORLD_EVENT_PLACE_BYTES 6u
#define MP_WORLD_EVENT_ACTOR_BYTES 3u
#define MP_WORLD_EVENT_MAX_BYTES                                                                  \
    (MP_WORLD_EVENT_FIXED_BYTES + MP_WORLD_EVENT_PLACE_BYTES + MP_WORLD_EVENT_ACTOR_BYTES +       \
     MP_WORLD_EVENT_TAIL_MAX)

/* The part's head, which every block carries: the count, then the music state and the music
 * sequence the host last claimed for this peer, a sound call each and 0 for none. */
#define MP_WORLD_EVENT_HEAD_BYTES 5u
#define MP_WORLD_EVENT_MUSIC_NONE 0u

/* The events behind the head at most. */
#define MP_WORLD_EVENT_PART_MAX_BYTES (MP_WORLD_EVENT_PART_MAX * MP_WORLD_EVENT_MAX_BYTES)

/* How many events a host takes in one substep, and so how many can be alive inside the window.
 * An event past the first is counted and dropped, never numbered, so the client's memory below is
 * sized against the number that can really be in flight. */
#define MP_WORLD_EVENT_POSTS_PER_SUBSTEP 32u
#define MP_WORLD_EVENT_RING (MP_WORLD_EVENT_POSTS_PER_SUBSTEP * MP_WORLD_EVENT_WINDOW)

/* How many of the newest numbers a client remembers. */
#define MP_WORLD_EVENT_MEMORY_BITS 512u

/* What a heard event adds to the radius it was posted with: the host does not know where a
 * client's camera stands, and the engine's own start gate on the client decides exactly. */
#define MP_WORLD_EVENT_SOUND_MARGIN 25.0f

/* One event, freshly read or ready to write. */
typedef struct mp_world_event {
    uint16_t sequence;
    uint8_t  age;
    uint8_t  kind;          /* mp_world_event_kind_t, without the two flags */
    bool     at_actor;
    bool     has_place;
    uint16_t place[3];      /* mp_enemy_wire_put_position, one axis each */
    uint16_t a;
    uint16_t key;           /* at an actor only */
    uint8_t  life;          /* at an actor only */
    uint8_t  tail_bytes;    /* as far as this build knows the kind */
    uint8_t  tail[MP_WORLD_EVENT_TAIL_MAX];
} mp_world_event_t;

/* The part's head. */
typedef struct mp_world_event_head {
    uint8_t  count;
    uint16_t music_state;
    uint16_t music_sequence;
} mp_world_event_head_t;

/* ==============================================================================================
 * The kinds, as a table.
 * ============================================================================================ */

bool        mp_world_event_kind_known(uint8_t kind);
bool        mp_world_event_kind_heard(uint8_t kind);   /* concerns a peer by its earshot */
bool        mp_world_event_kind_seen(uint8_t kind);    /* shows on screen, not only in the ear */
size_t      mp_world_event_tail_bytes(uint8_t kind);
const char *mp_world_event_kind_name(uint8_t kind);

/* Whether an event of kind `held`, taken in a substep that is now full, gives its place to one of
 * kind `coming`. A kind that is only heard gives way to one that is seen: a burst of script sounds
 * must not cost a limb, a blast, an emitter or a clang in the same substep, and a sound a script
 * holds comes again within a second anyway. Two kinds of the same rank never displace each other;
 * the substep is first come, first served among them. Music is not an event and never competes. */
bool mp_world_event_gives_way(uint8_t held, uint8_t coming);

/* What the host does with an event whose actor the census did not read under a key. `in_pool`
 * says the census's walk still met the actor, so it read another actor under that key (the last
 * one wins) and the event is not that key's; `life_noted` says a life of its key was counted when
 * it was posted. Only an actor that is really gone keeps that life. */
typedef enum mp_world_event_unread {
    MP_WORLD_EVENT_ANCHORED = 0,   /* gone: the life last counted for its key */
    MP_WORLD_EVENT_PASSED_OVER,    /* still there, and the census read another under its key */
    MP_WORLD_EVENT_UNCLAIMED       /* gone, and no life of its key was counted */
} mp_world_event_unread_t;

mp_world_event_unread_t mp_world_event_unread(bool in_pool, bool life_noted);

/* Whether an event concerns one peer. A kind performed at an actor concerns the peer that holds
 * the actor's life or would be told of it (`interest`); a kind heard at a place concerns the peer
 * whose player stands within `radius` and the margin of it. With nothing to measure it concerns
 * everybody, and the receiver's own engine decides. */
bool mp_world_event_concerns(uint8_t kind, bool at_actor, bool interest, bool has_place,
                             const float place[3], bool viewer_placed, const float viewer[3],
                             float radius);

/* What a client does with one event now. `replica` says a replica of the event's actor, of that
 * life, stands here; `age_here` is the event's age plus the substeps it has waited here. */
typedef enum mp_world_event_verdict {
    MP_WORLD_EVENT_AT_REPLICA = 0,   /* perform it on the replica */
    MP_WORLD_EVENT_AT_PLACE,         /* perform it at its place, with no actor */
    MP_WORLD_EVENT_WAIT,             /* its replica may still come inside the window */
    MP_WORLD_EVENT_DROP              /* nothing to perform it on, and no time left to wait */
} mp_world_event_verdict_t;

mp_world_event_verdict_t mp_world_event_where(const mp_world_event_t *event, bool replica,
                                              uint32_t age_here);

/* ==============================================================================================
 * The codec. A part is read whole or refused whole.
 * ============================================================================================ */

/* The bytes an event takes, 0 for one this build would not write: kind unknown or none, number 0,
 * age past the window, or a tail that is not its kind's. */
size_t mp_world_event_bytes(const mp_world_event_t *event);

bool mp_world_event_put(const mp_world_event_t *event, uint8_t *out, size_t room, size_t *written);

typedef enum mp_world_event_read {
    MP_WORLD_EVENT_READ_OK = 0,
    MP_WORLD_EVENT_READ_SKIPPED,     /* a kind this build does not know, stepped over */
    MP_WORLD_EVENT_READ_TORN
} mp_world_event_read_t;

mp_world_event_read_t mp_world_event_get(const uint8_t *in, size_t available,
                                         mp_world_event_t *out, size_t *read);

void mp_world_event_put_head(const mp_world_event_head_t *head, uint8_t *out);
void mp_world_event_get_head(const uint8_t *in, mp_world_event_head_t *head);

/* ==============================================================================================
 * The numbers, and a client's memory of them.
 * ============================================================================================ */

/* The number after `sequence`, 1..65535 and never 0. */
uint16_t mp_world_event_sequence_next(uint16_t sequence);

/* How far `a` lies after `b` the nearer way round, negative when before. */
int32_t mp_world_event_sequence_distance(uint16_t a, uint16_t b);

typedef struct mp_world_event_memory {
    bool     any;
    uint16_t highest;
    uint8_t  seen[MP_WORLD_EVENT_MEMORY_BITS / 8u];   /* bit k: the number k below the highest */
    bool     block_known;
    uint32_t block_tick;    /* the last taken block that carried events */
} mp_world_event_memory_t;

typedef enum mp_world_event_novelty {
    MP_WORLD_EVENT_FIRST_TIME = 0,
    MP_WORLD_EVENT_SEEN_AGAIN,
    MP_WORLD_EVENT_PAST_MEMORY       /* older than every number the memory still holds */
} mp_world_event_novelty_t;

void mp_world_event_memory_reset(mp_world_event_memory_t *memory);

/* A taken block of substep `tick` carries events. A block a whole window after the last one that
 * did cannot carry any event that one carried, so the memory starts over, which is also what keeps
 * the numbers from reading as old after a long silence. */
void mp_world_event_memory_block(mp_world_event_memory_t *memory, uint32_t tick);

/* Whether `sequence` is new here, and it is remembered either way. */
mp_world_event_novelty_t mp_world_event_memory_note(mp_world_event_memory_t *memory,
                                                    uint16_t sequence);

#endif /* MULTIPLAYER_MP_WORLD_EVENT_RULE_H */
