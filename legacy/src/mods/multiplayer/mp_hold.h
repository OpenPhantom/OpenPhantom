/* mp_hold.h: reliable messages a host holds for one peer whose channel has no room for them.
 *
 * Layer 0. A broadcast is one send per peer, and each peer's queue is its own: that is what ENet,
 * GameNetworkingSockets and yojimbo all do, and it is what this session did not. A message one
 * channel refused while the others took it reached only some of the players, and the caller was
 * told it had gone. The session now puts such a message here instead, and hands everything held
 * on to that peer's channel, oldest first, as its room returns. Nothing here knows what a message
 * means or which peer it is for.
 *
 * A byte ring of whole entries, each framed by a header that carries the message's length, its
 * kind, a dead flag, the moment it was held and a key. The kind and the key are how a state note
 * is told from an event: a newer copy of the same state marks the older one dead in place and is
 * appended behind everything else, so the hold never delivers a state that has already been
 * superseded. An event has kind 0 and is never marked.
 *
 * Nothing is ever dropped from here. A message that does not fit is refused whole, and what the
 * session does then is its own business: it sends the peer away, because a peer that cannot be
 * held for any more is a peer the session can no longer keep in step. Nothing here allocates, and
 * a ring of all zero bytes is an empty one.
 */
#ifndef MULTIPLAYER_MP_HOLD_H
#define MULTIPLAYER_MP_HOLD_H

#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sixty four kilobytes a peer. A client of a four player session receives about 740 bytes a
 * second of reliable notes, and a peer that has stopped answering has to be held for the whole
 * connected timeout of thirty seconds before that timeout decides, which is 22 kilobytes on the
 * average of that run; the rest is room for a fight, whose bolts are the one burst the channel
 * carries. The number of messages is only counted, never limited. */
#define MP_HOLD_BYTES 65536u

/* length u16, kind u8, flags u8, the moment it was held u32, the key u32 */
#define MP_HOLD_HEADER_BYTES 12u

_Static_assert(MP_CHANNEL_MESSAGE_BYTES <= 0xFFFFu, "a message length fits the length field");
_Static_assert(MP_HOLD_BYTES >= 32u * (MP_HOLD_HEADER_BYTES + MP_CHANNEL_MESSAGE_BYTES),
               "the hold takes thirty two of the largest messages the channel carries");

/* What one entry says about itself. */
typedef struct mp_hold_entry {
    uint8_t  kind;        /* 0 for an event; for a state note, its tag */
    uint32_t key;         /* for a state note, what it says without its clock; 0 for an event */
    uint32_t held_ms;     /* when it was held */
    size_t   bytes;
} mp_hold_entry_t;

typedef struct mp_hold {
    size_t   head;        /* where the oldest entry's header starts; always a live one */
    size_t   used;        /* bytes held, headers and dead entries included */
    size_t   live;        /* entries not marked dead */

    /* What the hold did, kept until the peer connects again, like the channel's counters. */
    uint32_t held;        /* messages taken in */
    uint32_t held_bytes;
    uint32_t delivered;   /* handed on to the channel */
    uint32_t superseded;  /* marked dead by a newer copy of their kind */
    size_t   most_live;   /* the most messages it held at once */
    size_t   most_bytes;  /* the most bytes, which need not have been at the same moment */
    uint32_t longest_ms;  /* the longest a message waited here before it was handed on */
    uint8_t  ring[MP_HOLD_BYTES];
} mp_hold_t;

void mp_hold_init(mp_hold_t *hold);

/* Whether a message of this many bytes would be taken now. The put asks exactly this. */
bool mp_hold_fits(const mp_hold_t *hold, size_t bytes);

/* Appends one message whole, behind everything held. False, with nothing written, when it does
 * not fit or is larger than any channel carries. `entry->bytes` is the length of `data`. */
bool mp_hold_put(mp_hold_t *hold, const mp_hold_entry_t *entry, const void *data,
                 uint32_t now_ms);

/* The oldest message held, without taking it. False when none is held, or when the buffer is
 * too small, in which case it stays whole. */
bool mp_hold_oldest(const mp_hold_t *hold, mp_hold_entry_t *entry, void *buffer,
                    size_t capacity);

/* Takes the oldest message away, once the channel has accepted it. */
void mp_hold_pop(mp_hold_t *hold, uint32_t now_ms);

/* Marks every live message of `kind` dead, which is at most one: a newer copy of that state is
 * about to be held behind it. Kind 0 is an event and marks nothing. Answers whether one was. */
bool mp_hold_supersede(mp_hold_t *hold, uint8_t kind);

/* The key of the newest live message of `kind`, if one is held. */
bool mp_hold_newest_key(const mp_hold_t *hold, uint8_t kind, uint32_t *key);

bool     mp_hold_empty(const mp_hold_t *hold);
size_t   mp_hold_count(const mp_hold_t *hold);

/* How long the oldest message has been held, 0 when none is. */
uint32_t mp_hold_oldest_age_ms(const mp_hold_t *hold, uint32_t now_ms);

#endif /* MULTIPLAYER_MP_HOLD_H */
