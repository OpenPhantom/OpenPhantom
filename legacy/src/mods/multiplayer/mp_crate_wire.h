/* mp_crate_wire.h: the three messages a push block travels in.
 *
 * Layer 1, pure. No address and no game in the process.
 *
 * A push block, the crate a player shoves across the floor, is a mover record with no timeline:
 * its state is where it stands, one flag byte, the carrier it rides and, while it falls, where it
 * will land. The host owns that state. A client says where it would like a block to go, the host
 * pushes it there through the engine's own push and describes every block back, and a fall
 * crosses once as a moment of its own, because a fall that arrived as a state would already be
 * over.
 *
 *   THE NOTE (host to everybody): a header and one entry per block of the level. Whole once a
 *   second, and on a change only the entries that changed, at most once in four substeps. Every
 *   entry is absolute and stands for itself at the host's tick of the note that carries it, so a
 *   change note needs nothing under it and a late whole note cannot undo a newer entry.
 *
 *   THE WISH (a client to the host): where the client's own push has put a block, as an absolute
 *   position with a sequence number. An absolute target heals itself when one is lost or late; a
 *   step would have to arrive once and exactly once, or leave a lasting offset.
 *
 *   THE FALL (host to everybody): which block started to fall, from where and in which direction,
 *   so a client can run the engine's own drop with the same input and land it with its own floor.
 *
 * The flag byte is the engine's. Bit 0x02 is FALLING and bit 0x04 is CARRYING, which an older
 * account had the wrong way round; bit 0x10 sinks the block where it lands.
 */
#ifndef MULTIPLAYER_MP_CRATE_WIRE_H
#define MULTIPLAYER_MP_CRATE_WIRE_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The three tags are handed out in mp_wire.h, beside the scene's, so that two features added
 * together could not take one number twice. */

/* The sizes. The header is the tag, the host's tick, the level, the generation, whole or change,
 * and the count. An entry is the id, the kind, the flags, the position, the pusher, the pusher's
 * acknowledged sequence number and the verdict on it; a carried block adds its carrier, the
 * carrier's part and the offset it keeps, a carrying block the block it carries. */
#define MP_CRATE_NOTE_HEADER_BYTES 13u
#define MP_CRATE_ENTRY_BYTES       19u
#define MP_CRATE_CARRIED_BYTES     14u
#define MP_CRATE_CARRYING_BYTES    1u
#define MP_CRATE_PUSH_BYTES        27u   /* tag, tick, level, generation, id, sequence, flags,
                                          * target */
#define MP_CRATE_FALL_BYTES        32u   /* tag, tick, level, generation, id, position,
                                          * direction */

/* The note's byte that says whole or a change, after the tag, the tick, the level and the
 * generation. A whole note is the host's state of every block and may stand for an older whole
 * note; a change note carries only what changed and is an event, so the session asks this byte, and
 * nothing else, before it treats a note as the newest state (mp_state_note_classify). */
#define MP_CRATE_NOTE_WHOLE_AT 11u
#define MP_CRATE_NOTE_WHOLE    1u
#define MP_CRATE_NOTE_CHANGE   0u

/* How many push blocks one level may hold here. The eleven shipped levels hold 43 between them and
 * BIGCITY the most, eight. A level with more is described up to this many and says so. */
#define MP_CRATE_MAX 16u
#define MP_CRATE_NOTE_MAX_BYTES                                                                    \
    (MP_CRATE_NOTE_HEADER_BYTES +                                                                  \
     MP_CRATE_MAX * (MP_CRATE_ENTRY_BYTES + MP_CRATE_CARRIED_BYTES + MP_CRATE_CARRYING_BYTES))

/* The engine's flag byte on a push block, and the part of it that travels. */
#define MP_CRATE_FLAG_CARRIED  0x01u   /* it rides another mover */
#define MP_CRATE_FLAG_FALLING  0x02u
#define MP_CRATE_FLAG_CARRYING 0x04u   /* another block rides it */
#define MP_CRATE_FLAG_SINK     0x10u   /* it sinks where it lands */
#define MP_CRATE_FLAGS_TRAVEL  (MP_CRATE_FLAG_CARRIED | MP_CRATE_FLAG_FALLING |                   \
                                MP_CRATE_FLAG_CARRYING | MP_CRATE_FLAG_SINK)

/* The engine's mover kind on a push block: 7 while it can be pushed, and 4 once it has sunk, or 3
 * for a block whose authored rig asks for that instead. No shipped level has one of those. */
#define MP_CRATE_KIND_BLOCK    7u
#define MP_CRATE_KIND_SUNK     4u
#define MP_CRATE_KIND_SUNK_ALT 3u

/* A mover index travels in one byte; the largest shipped level holds a hundred movers. */
#define MP_CRATE_ID_MAX 0xFFu

/* A pusher is a world slot, or nobody. */
#define MP_CRATE_NOBODY     0xFFu
#define MP_CRATE_SLOT_LIMIT 16u

/* What the host did with the pusher's newest wish. */
typedef enum mp_crate_verdict {
    MP_CRATE_VERDICT_ON_THE_WAY   = 0,   /* taken, the block is going there */
    MP_CRATE_VERDICT_REACHED      = 1,   /* the block is there, or started to fall on the way */
    MP_CRATE_VERDICT_ENGINE       = 2,   /* the engine refused, or the block stopped short */
    MP_CRATE_VERDICT_OTHER_OWNER  = 3,   /* another player holds the block */
    MP_CRATE_VERDICT_STALE        = 4,   /* older than one already taken */
    MP_CRATE_VERDICT_COUNT
} mp_crate_verdict_t;

/* What a wish says the player did. */
#define MP_CRATE_PUSH_FORWARD 0x01u   /* pushed the block away */
#define MP_CRATE_PUSH_PULL    0x02u   /* pulled it along */
#define MP_CRATE_PUSH_LET_GO  0x04u   /* stopped; the target is where the block was left */

typedef struct mp_crate_entry {
    uint8_t  id;             /* the mover index */
    uint8_t  kind;
    uint8_t  flags;          /* masked to what travels */
    float    position[3];
    uint8_t  pusher;         /* a world slot, or MP_CRATE_NOBODY */
    uint16_t sequence;       /* the pusher's newest wish the host has taken */
    uint8_t  verdict;        /* mp_crate_verdict_t */
    uint8_t  carrier;        /* while carried: the mover it rides */
    uint8_t  carrier_part;
    float    carry_offset[3];
    uint8_t  carrying;       /* while carrying: the block riding it */
} mp_crate_entry_t;

typedef struct mp_crate_note {
    uint32_t         tick;         /* the host's substep */
    uint16_t         level;        /* the mover count, as every map message names its level */
    uint32_t         generation;   /* the host's bank transitions */
    bool             whole;        /* every block of the level, or only the ones that changed */
    uint8_t          count;
    mp_crate_entry_t entry[MP_CRATE_MAX];
} mp_crate_note_t;

typedef struct mp_crate_push {
    uint32_t tick;           /* the client's substep */
    uint16_t level;
    uint32_t generation;     /* the client's bank transitions */
    uint8_t  id;
    uint16_t sequence;       /* per client and block, compared with its sign */
    uint8_t  flags;          /* MP_CRATE_PUSH_* */
    float    target[3];
} mp_crate_push_t;

typedef struct mp_crate_fall {
    uint32_t tick;           /* the host's substep */
    uint16_t level;
    uint32_t generation;
    uint8_t  id;
    float    position[3];    /* where the block stood when it went over */
    float    direction[2];   /* the push, as a unit vector: all the engine's drop reads of it */
} mp_crate_fall_t;

/* Each encoder answers the length, or 0 for a message it will not describe: a position that is
 * not a number or lies past the wire's range, a kind or a verdict the engine has not got, a count
 * past the cap or a buffer too small. The flags are masked, not refused. */
size_t mp_crate_note_encode(const mp_crate_note_t *note, uint8_t *buffer, size_t capacity);
size_t mp_crate_push_encode(const mp_crate_push_t *push, uint8_t *buffer, size_t capacity);
size_t mp_crate_fall_encode(const mp_crate_fall_t *fall, uint8_t *buffer, size_t capacity);

/* Whether a message is one of these three by its tag and length. A message that is one but does
 * not decode is torn, and its taker still owns it. */
bool mp_crate_is_note(const uint8_t *buffer, size_t bytes);
bool mp_crate_is_push(const uint8_t *buffer, size_t bytes);
bool mp_crate_is_fall(const uint8_t *buffer, size_t bytes);

/* Read whole or refused whole: a length that is not exactly what the entries need, a flag outside
 * what travels, a kind, a verdict or a slot the engine has not got, a wish that names no action
 * or both directions at once, or a fall with no direction. */
bool mp_crate_note_decode(const uint8_t *buffer, size_t bytes, mp_crate_note_t *out);
bool mp_crate_push_decode(const uint8_t *buffer, size_t bytes, mp_crate_push_t *out);
bool mp_crate_fall_decode(const uint8_t *buffer, size_t bytes, mp_crate_fall_t *out);

#endif /* MULTIPLAYER_MP_CRATE_WIRE_H */
