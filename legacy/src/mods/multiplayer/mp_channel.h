/* mp_channel.h: reliability over packets that may be lost, duplicated or reordered.
 *
 * Layer 0. No engine, no address and no socket: a packet here is a byte buffer that the caller
 * carries to the peer over whatever transport it has, and what the peer sends comes back the same
 * way. That is what lets every property of this module be proven in a unit test where two channels
 * talk across a function call and the test itself plays the network, losses included.
 *
 * Every packet opens with [protocol id u32][sequence u16][ack u16][ack bits u32]. The ack names
 * the newest sequence seen from the peer and the bits acknowledge the 32 sequences before it, so
 * every acknowledgement travels up to 33 times and needs no reliability of its own.
 *
 * Reliability is per message, never per packet, and no packet is ever sent twice. A reliable
 * message is laid into every new packet until some packet that carried it is acknowledged, held
 * back only by a re-entry throttle that gives an acknowledgement time to arrive; a message small
 * enough to be a moment of play is not held back at all and rides every packet. Retransmitting
 * whole packets would rebuild TCP on top of UDP: the second copy of a packet carries acks and a
 * payload that were already stale when the first copy was lost.
 *
 * The payload is handed through and nothing is promised about it. Each packet carries one payload
 * area that the caller fills, meant for snapshots and inputs. Those are self healing by design,
 * because the next one supersedes the lost one, so reliability spent on them would only be spent
 * on making them late. Guaranteeing nothing about the payload is the point of the split, not a
 * gap in it.
 *
 * No packet exceeds the budget and nothing is fragmented. A message that cannot fit into one
 * packet is refused with a false return where it is handed in, never split. Fragmentation
 * multiplies the loss probability of the whole by its fragment count. A block that has to arrive
 * whole does not belong on this channel at all: it goes on the session's bulk lane, with an
 * acknowledging mask of its own (mp_session_bulk). It was built here as a series of reliable
 * messages until 2026-09-09, and a series of messages of a kilobyte cannot be seated beside a
 * full payload, so it stopped, silently, with the whole ordered channel behind it.
 *
 * Time enters every call that needs it as a millisecond count the caller supplies, never from a
 * clock read in here. The count may wrap; only differences of it are taken. That is not a nicety,
 * it is what makes the throttle testable at all.
 *
 * None of this is new. The header shape has been the consensus for UDP games since 2008 and is
 * unchanged in the modern libraries; reliability per message rather than per packet is the one
 * correction since then, the 2016 finding that killed the sliding window rebuilds; and the
 * 1200 byte budget is the floor the QUIC transport requires every path to carry, adopted because
 * tunnels and address translators eat the difference to the nominal 1500 and games do no path
 * discovery. Refusing to fragment follows from arithmetic: at one percent packet loss a transfer
 * of 256 fragments arrives whole with a probability under eight percent.
 */
#ifndef MULTIPLAYER_MP_CHANNEL_H
#define MULTIPLAYER_MP_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The protocol id opens every packet: three identity bytes and one version byte. A packet from
 * another program, from another protocol of this program or from another version of this protocol
 * fails the comparison and is refused before anything else of it is read. The version is bumped
 * whenever the meaning of any byte after the id changes. */
#define MP_CHANNEL_PROTOCOL_VERSION 1u
#define MP_CHANNEL_PROTOCOL_ID (0x4F504D00u | MP_CHANNEL_PROTOCOL_VERSION)

/* No packet exceeds 1200 bytes. That is the UDP payload size modern transports treat as safe on a
 * path nobody has measured; tunnels and translators eat the difference to the nominal 1500, and
 * games do no path discovery. An oversized packet does not fail loudly, it vanishes on some routes
 * and arrives on others, which is the worst failure shape a transport can have. */
#define MP_CHANNEL_PACKET_BYTES 1200u

/* The packet header. Protocol id, sequence, ack and ack bits are the twelve bytes, and the
 * thirteenth is the count of reliable messages that follow before the payload. */
#define MP_CHANNEL_HEADER_BYTES 13u

/* Each reliable message inside a packet is framed by its id and its byte count. */
#define MP_CHANNEL_MESSAGE_HEADER_BYTES 4u

/* What the carrier takes before this module gets anything, and leaving it out of the arithmetic
 * below cost a field run and three wrong repairs.
 *
 * This module never touches a socket: it builds into a buffer somebody else supplies, and that
 * somebody has already written its own header in front. mp_session writes magic(4), type(1) and
 * connection id(8) and then hands the REST as the capacity, so the room a packet really has is
 * the budget less these thirteen bytes: 1187, never 1200. Until 2026-09-09 the two sizes below
 * were derived from the full 1200 instead, and the difference made the largest legal message
 * UNSEATABLE: mp_channel_send accepted 1183 bytes, no packet the carrier ever built could hold
 * it, it stayed the oldest message in the queue for good, and because this channel is ordered,
 * everything behind it stopped in both directions. That is what held a savegame transfer at 89
 * per cent in the field.
 *
 * The number is asserted against the session's own header, so the two cannot drift apart. */
#define MP_CHANNEL_ENVELOPE_BYTES 13u
#define MP_CHANNEL_BUDGET_BYTES   (MP_CHANNEL_PACKET_BYTES - MP_CHANNEL_ENVELOPE_BYTES)

/* The largest payload and the largest single message. Both are derived from what the carrier
 * really leaves rather than stated, so they move with it if it ever moves. They differ by exactly
 * one message frame header, and mixing them up compiles cleanly everywhere: a test once handed
 * the builder a payload of the larger size read out of a buffer of the smaller one, three bytes
 * short, and the module was right while the test was wrong. Each maximum is now exercised from a
 * buffer of its own size. */
#define MP_CHANNEL_PAYLOAD_BYTES (MP_CHANNEL_BUDGET_BYTES - MP_CHANNEL_HEADER_BYTES)
#define MP_CHANNEL_MESSAGE_BYTES \
    (MP_CHANNEL_BUDGET_BYTES - MP_CHANNEL_HEADER_BYTES - MP_CHANNEL_MESSAGE_HEADER_BYTES)

/* How many reliable messages one packet may carry. Thirty-two per packet at the feature's rate of
 * 32 packets a second is a thousand message deliveries a second, far above what this layer is
 * for: reliable traffic is the rare stuff, joins, chat, level and spawn events, while everything
 * frequent rides the payload. */
#define MP_CHANNEL_PACKET_MESSAGES 32u

/* How long a message rests after being laid into a packet before it is laid into the next one.
 * On a LAN the acknowledgement of a delivered copy arrives inside this. At the planning
 * assumption of a quarter second round trip it does not, and a large message is laid two or
 * three times per round trip, which is what the rest bounds: without it the same message would
 * ride thirty-two packets a second.
 *
 * A resonance worth knowing about, found while writing the loss pattern test rather than the
 * module: with a fixed throttle and a fixed packet cadence the retry period is a whole number of
 * packets, four at 32 a second, and a strictly periodic loss whose period shares a factor with
 * it can drop every retry of a message for ever. A synthetic "every second packet" pattern
 * starves delivery outright, which is why the unit test steps time by 35 ms rather than 31. Real
 * losses are not periodic; if field evidence ever shows a periodic link, the fix is jitter on
 * the throttle, not a redesign. */
#define MP_CHANNEL_RESEND_MS 100u

/* A message this small or smaller rests for nothing: it rides every packet until some packet that
 * carried it is acknowledged. The messages this is for are the moments of play, a shot or a push
 * of a few bytes, whose value is in arriving on the packet after the lost one rather than a
 * round trip plus a throttle later; four copies of a shot cost less than one snapshot. The
 * throttle above stays for everything larger, where repeating would cost real bandwidth.
 *
 * The delivery time under the throttle was computed before this class existed: at twenty
 * percent loss on a LAN a mean of twenty five milliseconds with a tail to three hundred, and
 * over an eighty millisecond round trip a mean over a hundred with a tail past four hundred; the
 * "about a tenth of a second" the design was written to was a ceiling, not a floor. The cost
 * under a dead peer, whose acknowledgements never come, is bounded: thirty two eager messages of
 * up to thirty six framed bytes repeat in every packet until the session times the peer out,
 * about 1.1 kB per packet for the length of the timeout, and it stops with the peer. */
#define MP_CHANNEL_EAGER_BYTES 32u

/* Unacknowledged messages the sender holds. A message is retired about one round trip plus one
 * throttle after it first travels, a third of a second, so steady reliable traffic occupies
 * around a dozen slots; sixty-four covers a five-fold burst. It is no longer anybody's flow
 * window: the one block that used to be paced by this queue rides the bulk lane now. */
#define MP_CHANNEL_SEND_SLOTS 64u

/* How far ahead of the next expected message the receiver holds messages that arrived early. The
 * window has to bridge what the sender lays out between a lost packet and its redelivery, one
 * round trip plus one throttle, about a dozen packets; thirty-two is nearly three times that. A
 * packet with a message past the window is not acknowledged, so its messages come again; its
 * payload is handed out all the same, and the refusal is counted. The builder lays no such
 * message: it holds back every id at or past the oldest unacked one plus this window, which
 * holds for as long as the receiver moves each message it stores out to an inbox at once. A
 * full inbox, or a peer that ignores the rule, is what is refused. */
#define MP_CHANNEL_ORDER_SLOTS 32u

/* Sent packet records kept for retirement: two seconds of packets at one a substep, one second at
 * the two a substep an overflow packet can make. An ack naming a sequence older than the ring
 * finds no record and retires nothing, which is safe: the messages that packet carried are either
 * retired already or still queued and still travelling, and only repeated. */
#define MP_CHANNEL_SENT_RECORDS 128u

/* How many kinds of state note one channel keeps apart (mp_channel_state.c): the ten the
 * session treats as a state, the scene, the whole crate note and the host's world settings among
 * them, and no row to spare. A kind that finds every row taken by others cannot wait outside the
 * channel, and is refused where it should wait, so the session asserts that every kind it treats
 * as a state has a row. */
#define MP_CHANNEL_STATE_KINDS 10u

/* One queued reliable message on the sending side. */
typedef struct mp_channel_send_slot {
    uint32_t first_sent_ms;    /* when a copy of this message first went out */
    bool     sent_ever;
    bool     used;
    bool     sent_once;
    bool     offered_once;      /* a build has walked past it, seated or not */
    bool     shrunk;            /* a state note a younger copy replaced while it was in flight */
    uint8_t  kind;              /* a state note's kind, its tag; 0 for an event */
    uint16_t id;
    uint16_t bytes;
    uint32_t key;               /* a state note's content without its clock */
    uint32_t last_sent_ms;
    uint32_t first_offered_ms;  /* the clock of the first build that could have seated it */
    uint8_t  data[MP_CHANNEL_MESSAGE_BYTES];
} mp_channel_send_slot_t;

/* One kind of state note on one channel: the newest copy when it has to wait outside the channel,
 * and what was done with the copies, for the report. */
typedef struct mp_channel_state_kind {
    uint8_t  kind;              /* 0 while the row is free */
    bool     waiting;
    uint16_t waiting_bytes;
    uint32_t waiting_key;
    uint32_t sent;              /* copies queued as a message of their own */
    uint32_t replaced;          /* copies that overwrote one not yet sent, or one waiting */
    uint32_t shrunk;            /* copies in flight shrunk to nothing by a younger one */
    uint32_t unchanged;         /* repeats left out while an equal copy was on its way */
    uint32_t waited;            /* copies that had to wait behind a shrunk one */
    uint8_t  waiting_data[MP_CHANNEL_MESSAGE_BYTES];
} mp_channel_state_kind_t;

/* Which message ids one sent packet carried, so that an ack of ANY packet that carried a message
 * retires it, however many packets it rode in. */
typedef struct mp_channel_sent_record {
    bool     used;
    uint16_t sequence;
    uint8_t  count;
    uint16_t ids[MP_CHANNEL_PACKET_MESSAGES];
    uint32_t sent_ms;       /* when it was built, so an ack of it is a round trip */
} mp_channel_sent_record_t;

/* One message held on the receiving side until everything sent before it has been read. */
typedef struct mp_channel_order_slot {
    bool     used;
    uint16_t id;
    uint16_t bytes;
    uint8_t  data[MP_CHANNEL_MESSAGE_BYTES];
} mp_channel_order_slot_t;

/* Everything one connection to one peer needs, about 118 KB, all of it inline: sixty four send
 * slots of 1200 bytes, thirty two order slots of 1188 and sixty four sent records of about
 * seventy. One of these per remote peer, so a four player host holds three, about 350 KB, and a
 * client holds one. Nothing in this module allocates, ever; when a capacity is reached the call
 * that needed more returns false. Bandwidth was never the argument at this player count; the
 * constants are sized for correctness margins, not for bytes: a message is retired about one
 * round trip plus one throttle after it first travels, a third of a second or eleven to twelve
 * packets at 32 a second, and every ring above is a multiple of that. */
typedef struct mp_channel {
    uint16_t local_sequence;        /* the sequence the next built packet will carry */
    uint16_t next_message_id;       /* the id the next queued message will get */
    bool     remote_seen;           /* false until the first packet is accepted */
    uint16_t remote_sequence;       /* newest accepted sequence from the peer */
    uint32_t remote_ack_bits;       /* the 32 sequences before it, bit 0 the nearest */
    uint16_t expected_message_id;   /* the next message the reader will be handed */
    uint32_t rtt_ms;                /* the smoothed round trip, 0 until the first ack; see below */
    uint32_t last_build_ms;         /* the clock the last build saw, for a receive told no time */

    /* What the payload reservation costs, counted rather than promised. A due message that finds
     * no seat is skipped, silently, and rides a later packet; that is the design. What the design
     * cannot say on its own is whether "later" ever comes: a message larger than what a full
     * payload leaves is skipped by every packet while the payload stays full, and because the
     * channel is ordered everything queued behind it waits with it. In the field that took a
     * whole transfer down without a single number moving. These three are the numbers. */
    uint32_t seats_lost_to_payload; /* a due message did not fit, and would have without the
                                     * payload */
    uint32_t seat_wait_ms;          /* how long the oldest due message the LAST build left unseated
                                     * had been waiting for a seat; 0 when it left none unseated */
    uint32_t longest_seat_wait_ms;  /* the most any message has ever waited for a seat */
    uint32_t held_past_window;      /* builds that held a due message back for the far window */
    bool     left_behind;           /* the last build left a due message unseated, for room or
                                     * for the thirty two, which an overflow packet may carry */
    /* What the oldest unacknowledged message is, and how long it has been out there. A message
     * that IS in every packet and is never answered looks exactly like one that never gets a seat
     * if the only number kept is a wait, and the two want opposite repairs: the first is a far
     * side that is not reading, the second is a payload that leaves no room. */
    uint32_t oldest_unacked_ms;
    uint32_t longest_unacked_ms;    /* the most the one above has read, a stall that is over */
    uint8_t  longest_unacked_tag;   /* and what the message was that waited that long */
    uint16_t longest_unacked_bytes;
    uint32_t deepest_pending;       /* the most messages ever queued at once */
    uint8_t  oldest_tag;            /* its first byte, which is the wire tag a caller would name */
    uint16_t oldest_bytes;
    size_t   least_room_left;       /* the least room a build WITH a payload left for messages */
    bool     least_room_seen;
    uint32_t refused_past_window;   /* packets refused here for a message past this window */

    mp_channel_send_slot_t   send_slots[MP_CHANNEL_SEND_SLOTS];
    mp_channel_sent_record_t sent_records[MP_CHANNEL_SENT_RECORDS];
    mp_channel_order_slot_t  order_slots[MP_CHANNEL_ORDER_SLOTS];
    mp_channel_state_kind_t  state_kinds[MP_CHANNEL_STATE_KINDS];
} mp_channel_t;

void mp_channel_init(mp_channel_t *channel);

/* Whether lhs is the more recent of two wrapping sequence numbers. Equal is not newer, and a
 * distance of exactly half the range reads as newer in one direction only, which both peers agree
 * on because they run this same comparison. */
bool mp_channel_sequence_newer(uint16_t lhs, uint16_t rhs);

/* Queues one reliable message. False when the message is larger than any packet can carry, which
 * no retry will heal, or when every slot is taken, which the next acknowledged packet heals; a
 * caller with a note that is not a moment of play offers it again later. A zero byte message is
 * legal, its arrival is its meaning. */
bool mp_channel_send(mp_channel_t *channel, const void *data, size_t bytes);

/* Whether mp_channel_send would take a message of this many bytes now, changing nothing. The
 * session asks every peer before it queues a broadcast anywhere. */
bool mp_channel_can_send(const mp_channel_t *channel, size_t bytes);

/* How many reliable messages are queued and not yet acknowledged. */
size_t mp_channel_send_pending(const mp_channel_t *channel);

/* The bytes, message headers included, that a build at `now_ms` would seat if it had `limit`
 * bytes of room for messages, changing nothing. The same walk as the build, so a build whose
 * payload leaves exactly this much room seats exactly these messages. */
size_t mp_channel_due_bytes(const mp_channel_t *channel, uint32_t now_ms, size_t limit);

/* ================================ More packets, not larger ones ================================
 *
 * When the build with a payload left a due message behind, a second packet in the same service,
 * with messages only, can carry it. It is the same walk with one more rule: nothing that was laid
 * into a packet at this same moment is laid again, because a small message rides every packet and
 * would otherwise go twice for nothing. Nothing past the far window, as in every build. */

/* Whether the last build left a due message unseated. */
bool mp_channel_left_behind(const mp_channel_t *channel);

/* The bytes an overflow packet built now would seat, message headers included, changing nothing. */
size_t mp_channel_overflow_bytes(const mp_channel_t *channel, uint32_t now_ms);

/* Builds that overflow packet: acks and messages, no payload. */
bool mp_channel_packet_build_overflow(mp_channel_t *channel, uint32_t now_ms, uint8_t *packet,
                                      size_t capacity, size_t *packet_bytes);

/* ================================ State notes (mp_channel_state.c) ============================
 *
 * A state note is sent as the newest copy of its kind rather than as one more message: a copy
 * not yet sent is overwritten in place, keeping its id; a copy in flight is shrunk to nothing,
 * keeping its id and its place in the order, and the younger one is queued behind it, so the
 * receiver gets either the old copy, which is then out of date and harmless, or an empty message,
 * and then the new one. At most one shrunk copy of a kind is unacknowledged at a time: while it
 * is, the newest copy waits outside the channel and goes in when it is answered. A repeat whose
 * key equals the newest copy on its way is left out. A zero byte message is legal on this channel
 * and the session skips it on the far side. Nothing here knows which tags are states; the session
 * says so, with the kind and the key. */

typedef enum mp_channel_state_outcome {
    MP_CHANNEL_STATE_REFUSED = 0,   /* no seat and nowhere to wait; nothing changed */
    MP_CHANNEL_STATE_QUEUED,        /* a message of its own, an older copy in flight shrunk */
    MP_CHANNEL_STATE_REPLACED,      /* it overwrote a copy not yet sent, or the one waiting */
    MP_CHANNEL_STATE_WAITING,       /* it waits outside the channel behind a shrunk copy */
    MP_CHANNEL_STATE_UNCHANGED      /* an equal copy is on its way; left out */
} mp_channel_state_outcome_t;

mp_channel_state_outcome_t mp_channel_send_state(mp_channel_t *channel, uint8_t kind, uint32_t key,
                                                 const void *data, size_t bytes);

/* Whether mp_channel_send_state would take this note now, answered by the same decision without
 * carrying it out. */
bool mp_channel_state_would_take(const mp_channel_t *channel, uint8_t kind, uint32_t key,
                                 size_t bytes);

/* The key of the newest copy of `kind` this channel has on its way or waiting, if any. */
bool mp_channel_state_newest_key(const mp_channel_t *channel, uint8_t kind, uint32_t *key);

/* Puts every waiting copy into the channel whose shrunk predecessor has been answered. The channel
 * calls it after every acknowledgement it processes and before every build. */
void mp_channel_state_promote(mp_channel_t *channel);

/* How many copies wait outside the channel: pending as much as a queued message is. */
size_t mp_channel_state_waiting(const mp_channel_t *channel);

/* A repeat the session left out before it reached this channel, because a hold in front of the
 * channel carries an equal copy; counted with the ones the channel leaves out itself. */
void mp_channel_state_count_unchanged(mp_channel_t *channel, uint8_t kind);

/* Builds the next outgoing packet: the header, then every queued reliable message that is due and
 * fits, oldest first, then the caller's payload. The payload area is reserved before any message
 * is seated, so a message that does not fit simply waits for a later packet. That no longer means
 * reliable traffic cannot crowd out anything: the bridge asks mp_channel_due_bytes first and makes
 * the enemy block give way for what is due, so the bodies keep their room and the enemies take the
 * rest. False when the payload alone can fit no packet, or the buffer cannot take what must be
 * written; nothing is fragmented in either case. A packet consumes one sequence number even when
 * it carries nothing, because an empty packet still carries acks. */
bool mp_channel_packet_build(mp_channel_t *channel, uint32_t now_ms, const void *payload,
                             size_t payload_bytes, uint8_t *packet, size_t capacity,
                             size_t *packet_bytes);

/* Takes one received packet apart. On true the packet was accepted into the receive window, its
 * messages were stored, and *payload points at the unreliable area inside the caller's own
 * buffer, valid exactly as long as that buffer is. On false nothing is owed: the packet was
 * foreign, malformed, stale, a duplicate, or a message in it ran past the ordering window; in
 * that last case the packet is deliberately not acknowledged, so its messages come again and the
 * refusal loses nothing. Acks are processed from every structurally sound packet, stale ones
 * included, because what a packet says about our own sent packets does not expire in transit. */
bool mp_channel_packet_receive(mp_channel_t *channel, const uint8_t *packet, size_t bytes,
                               const uint8_t **payload, size_t *payload_bytes);

/* The same receive told what time it is, which is what makes the acks in the packet a round trip
 * measurement: the newest ack names a sequence this side built at a time it wrote down, and the
 * difference is the round trip. Seven eighths of the old estimate and one eighth of the sample,
 * the classic smoothing, so one late packet does not swing the display. The form above answers
 * with the time of the last build, which is at most one packet interval stale. */
bool mp_channel_packet_receive_at(mp_channel_t *channel, uint32_t now_ms, const uint8_t *packet,
                                  size_t bytes, const uint8_t **payload, size_t *payload_bytes);

/* What the receive the session makes did with a packet. One refused for a message past the order
 * window is not acknowledged and does not enter the window, as above, but it is genuine and fresh
 * and only its messages have to come again, so its payload is handed out with its own sequence.
 * A refused payload used to take the far body and the host's world with it. */
typedef enum mp_channel_receipt {
    MP_CHANNEL_DROPPED = 0,   /* foreign, malformed, stale or a duplicate: nothing is owed */
    MP_CHANNEL_ACCEPTED,      /* acknowledged, its messages stored */
    MP_CHANNEL_PAST_WINDOW    /* not acknowledged, the messages inside the window stored */
} mp_channel_receipt_t;

mp_channel_receipt_t mp_channel_packet_take(mp_channel_t *channel, uint32_t now_ms,
                                            const uint8_t *packet, size_t bytes,
                                            const uint8_t **payload, size_t *payload_bytes,
                                            uint16_t *packet_sequence);

/* The smoothed round trip in milliseconds, 0 before the first acknowledged packet. Only the
 * NEWEST ack of a packet feeds it: the bits behind it acknowledge older packets whose round
 * trips are already stale by the time they are seen. */
uint32_t mp_channel_rtt_ms(const mp_channel_t *channel);

/* Hands out the next reliable message in the order the peer sent them. False when that message
 * has not arrived yet, or when the buffer is too small, in which case the message stays whole and
 * waits rather than being truncated. */
bool mp_channel_message_read(mp_channel_t *channel, void *buffer, size_t capacity, size_t *bytes);

/* Whether the next message in order has arrived, and how large it is, without taking it. The
 * session asks before it moves a message out, so one it has no room for stays here whole. */
bool mp_channel_message_ready(const mp_channel_t *channel, size_t *bytes);

/* How often a due message was refused a seat that the payload alone had taken: the message would
 * have fitted a packet with no payload. Counted per message per build, so a message that is
 * skipped by thirty packets counts thirty. A refusal because other riders took the room, or
 * because the packet already carries its thirty two messages, is not counted here: that is
 * ordinary queueing and the next packet heals it. */
uint32_t mp_channel_seats_lost_to_payload(const mp_channel_t *channel);

/* How long the oldest due message the last build left unseated had by then been waiting for a
 * seat, measured from the first build that could have carried it; 0 when the last build seated
 * every due message. A caller that wants to say something once, when the wait crosses a line,
 * reads this after each build. The longest such wait ever is kept beside it for the report. */
uint32_t mp_channel_seat_wait_ms(const mp_channel_t *channel);
uint32_t mp_channel_longest_seat_wait_ms(const mp_channel_t *channel);

/* The two halves of the window rule. How many builds held a due message back because the far
 * side's ordering window could not have held it, which is ordinary under a burst; and how many
 * received packets this side refused for a message past its own window, which a peer that
 * honours the rule causes only while the inbox this side moves its messages into is full. */
/* The oldest message this channel has sent and not had answered, in milliseconds, and what it is.
 * Zero when everything sent has been acknowledged. */
uint32_t mp_channel_oldest_unacked_ms(const mp_channel_t *channel);
uint8_t mp_channel_oldest_tag(const mp_channel_t *channel);
uint16_t mp_channel_oldest_bytes(const mp_channel_t *channel);
/* The most the first of those has ever read here, so a stall that is over still shows, and the
 * tag and size of the message that waited that long. */
uint32_t mp_channel_longest_unacked_ms(const mp_channel_t *channel);
uint8_t  mp_channel_longest_unacked_tag(const mp_channel_t *channel);
uint16_t mp_channel_longest_unacked_bytes(const mp_channel_t *channel);

/* The most messages this channel ever had queued and unanswered at once, of its sixty four. */
uint32_t mp_channel_deepest_pending(const mp_channel_t *channel);

/* The least room a packet that carried a payload left for messages. The floor of the channel in
 * practice, as against the six bytes it is in principle. */
size_t mp_channel_least_room_left(const mp_channel_t *channel);

uint32_t mp_channel_held_past_window(const mp_channel_t *channel);
uint32_t mp_channel_refused_past_window(const mp_channel_t *channel);

#endif /* MULTIPLAYER_MP_CHANNEL_H */
