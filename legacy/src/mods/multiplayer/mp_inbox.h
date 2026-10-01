/* mp_inbox.h: reliable messages taken off the channel and not yet acted on.
 *
 * Layer 0. A ring of bytes holding whole messages of any length up to the channel's largest, oldest
 * first. The channel's order window counts from what has been taken off it, and the readers act on
 * what they read, which may only happen inside a substep or an open lobby; everything that only
 * receives, the idle pumps above all, used to leave the window standing while it acknowledged what
 * it stored, and once thirty two messages lay unread every later packet with a message was refused,
 * its payload with it. The session moves each message into this ring the moment it arrives, so the
 * window moves with the receive, and the readers take from here.
 *
 * Nothing is ever dropped. A message that does not fit is refused whole and stays where it was.
 * Nothing here allocates, and a ring of all zero bytes is an empty one.
 */
#ifndef MULTIPLAYER_MP_INBOX_H
#define MULTIPLAYER_MP_INBOX_H

#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Room for the messages that arrive while nothing reads them, sized for a level load of ordinary
 * traffic. A fight full of the NPCs' bolts fills it in seconds; past that the channel keeps the
 * rest and its window stops, as it did before this ring existed, and the payloads still pass. */
#define MP_INBOX_BYTES 32768u

/* Each message is framed by its length. */
#define MP_INBOX_LENGTH_BYTES 2u

/* The largest message the channel hands out has to fit an empty ring and its length field, or it
 * would stay on the channel for good with everything behind it. */
_Static_assert(MP_CHANNEL_MESSAGE_BYTES <= 0xFFFFu, "a message length fits the length field");
_Static_assert(MP_INBOX_BYTES >= MP_INBOX_LENGTH_BYTES + MP_CHANNEL_MESSAGE_BYTES,
               "the largest message fits an empty inbox");

typedef struct mp_inbox {
    size_t   head;             /* where the oldest message's length starts */
    size_t   used;             /* bytes held, framing included */
    size_t   count;            /* messages held */
    size_t   most_count;       /* the most messages it ever held at once */
    size_t   most_bytes;       /* the most bytes, which need not have been at the same moment */
    bool     blocked;          /* the last move left a message on the channel for want of room */
    uint32_t stalls;           /* how often a move began leaving one there */
    uint8_t  ring[MP_INBOX_BYTES];
} mp_inbox_t;

void mp_inbox_init(mp_inbox_t *inbox);

/* Whether a message of this many bytes would be taken now. The put asks exactly this. */
bool mp_inbox_fits(const mp_inbox_t *inbox, size_t bytes);

/* Appends one message whole, zero bytes included. False, with nothing written, when it does not
 * fit. */
bool mp_inbox_put(mp_inbox_t *inbox, const void *data, size_t bytes);

/* Hands out the oldest message. False when there is none, or when the buffer is too small, in which
 * case the message stays whole at the head rather than being torn. */
bool mp_inbox_take(mp_inbox_t *inbox, void *buffer, size_t capacity, size_t *bytes);

/* How a move of messages into this ring ended: with a message left behind for want of room, or
 * with nothing left to move. A stall is counted where the first begins after the second. */
void mp_inbox_note_move(mp_inbox_t *inbox, bool left_one_behind);

size_t   mp_inbox_most_count(const mp_inbox_t *inbox);
size_t   mp_inbox_most_bytes(const mp_inbox_t *inbox);
uint32_t mp_inbox_stalls(const mp_inbox_t *inbox);

#endif /* MULTIPLAYER_MP_INBOX_H */
