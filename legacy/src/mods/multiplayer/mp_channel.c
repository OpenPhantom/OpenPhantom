/* mp_channel.c: the reliability bookkeeping, with the two easy mistakes made impossible.
 *
 * Two invariants carry the module, and both are about what is NOT done.
 *
 * A packet whose reliable messages could not all be stored is not acknowledged. Acknowledging it
 * would tell the sender those messages arrived, the sender would retire them, and a full window
 * would have quietly become a lost message. Because the packet stays unacknowledged the sender
 * lays the same messages into fresh packets, the copies that were stored are recognised by their
 * ids, and nothing is lost. The refusal is counted. The builder lays no message the receiver's
 * window could not hold while the receiver moves what it stores out to an inbox, so a count here
 * is a full inbox or a peer ignoring the rule. The payload of a refused packet is handed out.
 *
 * Retirement runs over sent packet records, never over the wire. Each built packet remembers
 * which message ids it carried; an acknowledged sequence retires every message its record names.
 * A message that rode in five packets is retired by whichever of the five is acknowledged first,
 * and the four later acks find empty slots and do nothing.
 */
/* SIZE NOTE: past 600 lines since the receive hands out the payload of a packet refused for the
 * window and the peek sits beside the read. The seam, if this grows again, is the receive half,
 * from the retirement of acknowledged messages to the reader, about 280 lines that share nothing
 * with the build half but the channel itself.
 */
#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The byte order is the protocol's, least significant first, written out a byte at a time so the
 * format cannot change with the machine. */
static void put_u16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
    at[2] = (uint8_t)((value >> 16) & 0xFFu);
    at[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *at)
{
    return (uint16_t)((uint16_t)at[0] | ((uint16_t)at[1] << 8));
}

static uint32_t get_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) |
           ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

void mp_channel_init(mp_channel_t *channel)
{
    memset(channel, 0, sizeof *channel);
}

bool mp_channel_sequence_newer(uint16_t lhs, uint16_t rhs)
{
    if (lhs == rhs) {
        return false;
    }
    if (lhs > rhs) {
        return (uint16_t)(lhs - rhs) <= 0x8000u;
    }
    return (uint16_t)(rhs - lhs) > 0x8000u;
}

bool mp_channel_send(mp_channel_t *channel, const void *data, size_t bytes)
{
    mp_channel_send_slot_t *slot;

    if (bytes > MP_CHANNEL_MESSAGE_BYTES) {
        /* This message could not travel whole in any packet this protocol sends, and splitting it
         * is deliberately not built here. Refused while the caller who owns it still exists; a
         * bulk transfer is a series of messages that each fit. */
        return false;
    }
    if (data == NULL && bytes > 0) {
        return false;
    }

    /* Ids are handed out in order and each id owns one slot of the ring, so the queue is full
     * exactly when the slot the next id would take is still occupied by the message one whole
     * lap older. The id is only consumed on success. */
    slot = &channel->send_slots[channel->next_message_id % MP_CHANNEL_SEND_SLOTS];
    if (slot->used) {
        return false;
    }

    slot->used             = true;
    slot->shrunk           = false;
    slot->kind             = 0u;      /* an event, until mp_channel_state.c says otherwise */
    slot->key              = 0u;
    slot->sent_ever        = false;   /* the slot's last occupant went out; this one has not */
    slot->first_sent_ms    = 0u;
    slot->sent_once        = false;
    slot->offered_once     = false;   /* the slot's last occupant was offered; this one has not
                                       * been */
    slot->id               = channel->next_message_id;
    slot->bytes            = (uint16_t)bytes;
    slot->last_sent_ms     = 0u;
    slot->first_offered_ms = 0u;
    if (bytes > 0) {
        memcpy(slot->data, data, bytes);
    }
    ++channel->next_message_id;
    {
        size_t pending = mp_channel_send_pending(channel);

        if (channel->deepest_pending < (uint32_t)pending) {
            channel->deepest_pending = (uint32_t)pending;
        }
    }
    return true;
}

bool mp_channel_can_send(const mp_channel_t *channel, size_t bytes)
{
    return channel != NULL && bytes <= MP_CHANNEL_MESSAGE_BYTES &&
           !channel->send_slots[channel->next_message_id % MP_CHANNEL_SEND_SLOTS].used;
}

size_t mp_channel_send_pending(const mp_channel_t *channel)
{
    size_t index;
    size_t pending = 0;

    for (index = 0; index < MP_CHANNEL_SEND_SLOTS; ++index) {
        if (channel->send_slots[index].used) {
            ++pending;
        }
    }
    return pending;
}

/* The oldest message still here is the one everything else waits behind, so it is the one worth
 * describing: how long a copy of it has been out unanswered, what tag it carries and how big it
 * is. */
static void note_oldest(mp_channel_t *channel, const mp_channel_send_slot_t *slot, uint32_t now_ms)
{
    /* A shrunk state has no bytes left to name it; its kind is its tag. */
    channel->oldest_tag   = slot->kind != 0u ? slot->kind : (slot->bytes > 0u ? slot->data[0] : 0u);
    channel->oldest_bytes = (uint16_t)slot->bytes;
    if (slot->sent_ever) {
        channel->oldest_unacked_ms = now_ms - slot->first_sent_ms;
        if (channel->longest_unacked_ms < channel->oldest_unacked_ms) {
            channel->longest_unacked_ms    = channel->oldest_unacked_ms;
            channel->longest_unacked_tag   = channel->oldest_tag;
            channel->longest_unacked_bytes = channel->oldest_bytes;
        }
    }
}

/* It stays queued and rides a later packet, never a fragment. The skip itself is the design; what
 * is counted is the one refusal a caller can act on, a seat that existed in this packet and that
 * the payload took, and how long the oldest message passed over here has been waiting. A message
 * refused for the room another rider took, or because the packet holds its thirty two already,
 * waits for the next packet and gets it; a message refused for the payload waits for the payload
 * to shrink, which in a level it may never do, with the whole ordered queue behind it. */
static void note_unseated(mp_channel_t *channel, const mp_channel_send_slot_t *slot, size_t need,
                          size_t room, size_t payload_bytes, uint32_t now_ms)
{
    if (need > room && need <= room + payload_bytes) {
        ++channel->seats_lost_to_payload;
    }
    if (channel->seat_wait_ms == 0u) {
        channel->seat_wait_ms = now_ms - slot->first_offered_ms;
        if (channel->longest_seat_wait_ms < channel->seat_wait_ms) {
            channel->longest_seat_wait_ms = channel->seat_wait_ms;
        }
    }
}

/* One message into the packet at `at`, and into the record that retires it. */
static void seat(mp_channel_send_slot_t *slot, uint8_t *at, mp_channel_sent_record_t *record,
                 uint32_t now_ms)
{
    put_u16(at, slot->id);
    put_u16(at + 2, slot->bytes);
    if (slot->bytes > 0) {
        memcpy(at + MP_CHANNEL_MESSAGE_HEADER_BYTES, slot->data, slot->bytes);
    }
    record->ids[record->count] = slot->id;
    ++record->count;
    slot->sent_once    = true;
    slot->last_sent_ms = now_ms;
    if (!slot->sent_ever) {
        slot->sent_ever     = true;
        slot->first_sent_ms = now_ms;
    }
}

/* THE WALK, the one rule by which messages get seats, oldest id first so the message the
 * receiver's window is waiting on is always the first one offered a seat. The build seats by it
 * and the query counts by it, and they are one function so that what the query names is exactly
 * what a build with that room seats: the same skips, the same throttle, the same window, the same
 * thirty two. With `build` NULL it changes nothing and only counts; with `build` it is the same
 * channel as `view`, and everything a build records about the walk is recorded there. Answers the
 * bytes seated, each message with its header. */
static size_t walk_queue(const mp_channel_t *view, mp_channel_t *build, uint32_t now_ms,
                         size_t room, size_t payload_bytes, bool overflow, uint8_t *packet,
                         mp_channel_sent_record_t *record)
{
    size_t   seated = 0u;
    size_t   at = MP_CHANNEL_HEADER_BYTES;
    uint32_t count = 0u;
    uint16_t step;
    uint16_t oldest = 0;
    bool     have_oldest = false;

    for (step = MP_CHANNEL_SEND_SLOTS; step > 0; --step) {
        uint16_t                      id    = (uint16_t)(view->next_message_id - step);
        size_t                        index = id % MP_CHANNEL_SEND_SLOTS;
        const mp_channel_send_slot_t *slot  = &view->send_slots[index];
        size_t                        need;

        if (!slot->used || slot->id != id) {
            continue;
        }
        /* Never an id the receiver cannot hold. Its window runs thirty two past the message it
         * expects, which is never behind the oldest one still unacknowledged here while it moves
         * what it stores out to an inbox, so this is the one bound that holds whatever was lost.
         * A packet carrying an id past it would not be acknowledged and its messages would come
         * again; the ids are walked oldest first, so everything after this one is past it too. */
        if (!have_oldest) {
            have_oldest = true;
            oldest      = id;
            if (build != NULL) {
                note_oldest(build, slot, now_ms);
            }
        }
        if ((uint16_t)(id - oldest) >= MP_CHANNEL_ORDER_SLOTS) {
            if (build != NULL) {
                ++build->held_past_window;
            }
            break;
        }
        /* The wait for a seat is measured from here, the first build that could have carried the
         * message, and not from the moment it was queued: the send has no clock of its own, and a
         * message cannot be said to wait for a packet before there is one to wait for. */
        if (build != NULL && !slot->offered_once) {
            build->send_slots[index].offered_once     = true;
            build->send_slots[index].first_offered_ms = now_ms;
        }
        if (slot->sent_once && slot->bytes > MP_CHANNEL_EAGER_BYTES &&
            (uint32_t)(now_ms - slot->last_sent_ms) < MP_CHANNEL_RESEND_MS) {
            continue;   /* its last copy may still be answered, give the ack time to arrive */
        }
        if (overflow && slot->sent_once && slot->last_sent_ms == now_ms) {
            continue;   /* laid into the packet this one follows, a moment ago */
        }
        need = (size_t)MP_CHANNEL_MESSAGE_HEADER_BYTES + slot->bytes;
        if (count == MP_CHANNEL_PACKET_MESSAGES || need > room) {
            if (build != NULL) {
                note_unseated(build, slot, need, room, payload_bytes, now_ms);
                build->left_behind = true;
            }
            continue;
        }
        if (build != NULL) {
            seat(&build->send_slots[index], packet + at, record, now_ms);
        }
        at     += need;
        room   -= need;
        seated += need;
        ++count;
    }
    return seated;
}

size_t mp_channel_due_bytes(const mp_channel_t *channel, uint32_t now_ms, size_t limit)
{
    if (channel == NULL) {
        return 0u;
    }
    return walk_queue(channel, NULL, now_ms, limit, 0u, false, NULL, NULL);
}

size_t mp_channel_overflow_bytes(const mp_channel_t *channel, uint32_t now_ms)
{
    if (channel == NULL) {
        return 0u;
    }
    return walk_queue(channel, NULL, now_ms, MP_CHANNEL_PAYLOAD_BYTES, 0u, true, NULL, NULL);
}

bool mp_channel_left_behind(const mp_channel_t *channel)
{
    return channel != NULL && channel->left_behind;
}

static bool build_packet(mp_channel_t *channel, uint32_t now_ms, const void *payload,
                         size_t payload_bytes, bool overflow, uint8_t *packet, size_t capacity,
                         size_t *packet_bytes);

bool mp_channel_packet_build(mp_channel_t *channel, uint32_t now_ms, const void *payload,
                             size_t payload_bytes, uint8_t *packet, size_t capacity,
                             size_t *packet_bytes)
{
    return build_packet(channel, now_ms, payload, payload_bytes, false, packet, capacity,
                        packet_bytes);
}

bool mp_channel_packet_build_overflow(mp_channel_t *channel, uint32_t now_ms, uint8_t *packet,
                                      size_t capacity, size_t *packet_bytes)
{
    return build_packet(channel, now_ms, NULL, 0u, true, packet, capacity, packet_bytes);
}

static bool build_packet(mp_channel_t *channel, uint32_t now_ms, const void *payload,
                         size_t payload_bytes, bool overflow, uint8_t *packet, size_t capacity,
                         size_t *packet_bytes)
{
    mp_channel_sent_record_t *record;
    size_t   limit;
    size_t   room;
    size_t   at;

    if (packet == NULL || packet_bytes == NULL) {
        return false;
    }
    if (payload == NULL && payload_bytes > 0) {
        return false;
    }
    if (payload_bytes > MP_CHANNEL_PAYLOAD_BYTES) {
        /* Too large for any packet this protocol sends. Refused rather than fragmented, and the
         * caller learns it while the payload still exists. */
        return false;
    }

    limit = (capacity < MP_CHANNEL_PACKET_BYTES) ? capacity : MP_CHANNEL_PACKET_BYTES;
    if (limit < MP_CHANNEL_HEADER_BYTES + payload_bytes) {
        return false;
    }
    mp_channel_state_promote(channel);

    put_u32(packet, MP_CHANNEL_PROTOCOL_ID);
    put_u16(packet + 4, channel->local_sequence);
    /* Before anything has been received there is no honest ack value, and the header has no way
     * to say none. The convention is an ack of 0xFFFF with no bits: a sequence the peer will not
     * have used until a whole lap of its own numbering, by which time this side has long since
     * received something or the session above has given up. The residual hazard is bounded: the
     * value falsely names a real packet only after this side has sent a full lap of 65536
     * packets, 34 minutes at 32 a second, without receiving anything, and every session layer
     * gives up after seconds of silence. Skipping sequence 0 instead would put a permanent hole
     * in the ack bit arithmetic to close a gap that cannot occur in a live session. */
    put_u16(packet + 6, channel->remote_seen ? channel->remote_sequence : 0xFFFFu);
    put_u32(packet + 8, channel->remote_seen ? channel->remote_ack_bits : 0u);

    record = &channel->sent_records[channel->local_sequence % MP_CHANNEL_SENT_RECORDS];
    record->used     = true;
    record->sequence = channel->local_sequence;
    record->count    = 0;
    record->sent_ms  = now_ms;
    channel->last_build_ms = now_ms;

    /* The payload area is reserved first: it is the time critical traffic, and a queue of riders
     * must not starve it. What is left seats reliable messages by the walk above. The two phase
     * split of the bridge sizes the payload by what the query says is due, so the enemy block is
     * what gives way and the bodies still never do. */
    room = limit - MP_CHANNEL_HEADER_BYTES - payload_bytes;
    channel->seat_wait_ms = 0u;
    /* What a packet that carries a payload really leaves for messages. The channel's floor is six
     * bytes by arithmetic; what it is in a fight is a measurement, and nobody had it. */
    if (payload_bytes > 0u && (!channel->least_room_seen || room < channel->least_room_left)) {
        channel->least_room_seen = true;
        channel->least_room_left = room;
    }
    channel->oldest_unacked_ms = 0u;
    channel->oldest_tag        = 0u;
    channel->oldest_bytes      = 0u;
    channel->left_behind       = false;
    at = MP_CHANNEL_HEADER_BYTES +
         walk_queue(channel, channel, now_ms, room, payload_bytes, overflow, packet, record);
    packet[12] = record->count;

    if (payload_bytes > 0) {
        memcpy(packet + at, payload, payload_bytes);
    }
    *packet_bytes = at + payload_bytes;
    ++channel->local_sequence;
    return true;
}

/* Retires every message the record of one acknowledged sequence names. An ack for a sequence with
 * no matching record, one never sent or older than the record ring, is simply nothing. */
static void retire_sequence(mp_channel_t *channel, uint16_t sequence)
{
    mp_channel_sent_record_t *record;
    mp_channel_send_slot_t   *slot;
    uint8_t  index;
    uint16_t id;

    record = &channel->sent_records[sequence % MP_CHANNEL_SENT_RECORDS];
    if (!record->used || record->sequence != sequence) {
        return;
    }
    for (index = 0; index < record->count; ++index) {
        id   = record->ids[index];
        slot = &channel->send_slots[id % MP_CHANNEL_SEND_SLOTS];
        /* The id check matters: the slot may already hold a younger message after the older one
         * was retired through another packet's record. */
        if (slot->used && slot->id == id) {
            slot->used = false;
        }
    }
    record->used = false;
}

/* The newest ack against the time its packet was built. A record that is not there, one never
 * sent or older than the ring, gives no sample; a clock that ran backwards gives none either. */
static void note_round_trip(mp_channel_t *channel, uint16_t ack, uint32_t now_ms)
{
    const mp_channel_sent_record_t *record;
    uint32_t sample;

    record = &channel->sent_records[ack % MP_CHANNEL_SENT_RECORDS];
    if (!record->used || record->sequence != ack) {
        return;
    }
    sample = now_ms - record->sent_ms;
    if (sample >= 0x80000000u) {
        return;
    }
    channel->rtt_ms = (channel->rtt_ms == 0u) ? sample : (channel->rtt_ms * 7u + sample) / 8u;
}

static void process_acks(mp_channel_t *channel, uint16_t ack, uint32_t ack_bits)
{
    uint32_t bit;

    retire_sequence(channel, ack);
    for (bit = 0; bit < 32u; ++bit) {
        if ((ack_bits & (1u << bit)) != 0u) {
            retire_sequence(channel, (uint16_t)(ack - 1u - bit));
        }
    }
    /* A shrunk copy answered frees the newest one waiting behind it. */
    mp_channel_state_promote(channel);
}

/* Proves the message framing stays inside the packet before anything acts on it. This function
 * parses bytes a stranger wrote, so every length is checked against what is actually there, and a
 * stated message size can never exceed what one packet can carry. */
static bool framing_is_sound(const uint8_t *packet, size_t bytes, size_t *messages_end)
{
    size_t   at = MP_CHANNEL_HEADER_BYTES;
    uint8_t  count = packet[12];
    uint8_t  index;
    uint16_t size;

    for (index = 0; index < count; ++index) {
        if (bytes - at < MP_CHANNEL_MESSAGE_HEADER_BYTES) {
            return false;
        }
        size = get_u16(packet + at + 2);
        if (size > MP_CHANNEL_MESSAGE_BYTES ||
            bytes - at - MP_CHANNEL_MESSAGE_HEADER_BYTES < size) {
            return false;
        }
        at += (size_t)MP_CHANNEL_MESSAGE_HEADER_BYTES + size;
    }
    *messages_end = at;
    return true;
}

/* Stores the messages of a framing checked packet into the ordering window. False when any of
 * them lies past the window, in which case the caller must not acknowledge the packet: the stored
 * ones stay stored, their next copies are recognised as duplicates, and the refused ones return
 * in fresh packets. A message behind the expected id was delivered before and is dropped without
 * comment, which is the deduplication, not a loss. */
static bool store_messages(mp_channel_t *channel, const uint8_t *packet)
{
    mp_channel_order_slot_t *slot;
    size_t   at = MP_CHANNEL_HEADER_BYTES;
    uint8_t  count = packet[12];
    uint8_t  index;
    uint16_t id;
    uint16_t size;
    uint16_t ahead;
    bool     stored_all = true;

    for (index = 0; index < count; ++index) {
        id    = get_u16(packet + at);
        size  = get_u16(packet + at + 2);
        ahead = (uint16_t)(id - channel->expected_message_id);

        if (ahead < 0x8000u) {
            if (ahead >= MP_CHANNEL_ORDER_SLOTS) {
                stored_all = false;
            } else {
                /* Ids inside the window are unique modulo the window size, so an occupied slot
                 * can only be this same message stored from an earlier copy. */
                slot = &channel->order_slots[id % MP_CHANNEL_ORDER_SLOTS];
                if (!slot->used) {
                    slot->used  = true;
                    slot->id    = id;
                    slot->bytes = size;
                    if (size > 0) {
                        memcpy(slot->data, packet + at + MP_CHANNEL_MESSAGE_HEADER_BYTES, size);
                    }
                }
            }
        }
        at += (size_t)MP_CHANNEL_MESSAGE_HEADER_BYTES + size;
    }
    return stored_all;
}

mp_channel_receipt_t mp_channel_packet_take(mp_channel_t *channel, uint32_t now_ms,
                                            const uint8_t *packet, size_t bytes,
                                            const uint8_t **payload, size_t *payload_bytes,
                                            uint16_t *packet_sequence)
{
    size_t   messages_end = 0;
    uint16_t sequence;
    uint16_t behind = 0;
    uint16_t jump;

    if (packet == NULL || payload == NULL || payload_bytes == NULL || packet_sequence == NULL) {
        return MP_CHANNEL_DROPPED;
    }
    *payload         = NULL;
    *payload_bytes   = 0;
    *packet_sequence = 0;

    /* Oversize is refused along with undersize: no packet this protocol builds exceeds the
     * budget, so a longer one is not this protocol however its first four bytes read. */
    if (bytes < MP_CHANNEL_HEADER_BYTES || bytes > MP_CHANNEL_PACKET_BYTES) {
        return MP_CHANNEL_DROPPED;
    }
    if (get_u32(packet) != MP_CHANNEL_PROTOCOL_ID) {
        return MP_CHANNEL_DROPPED;
    }
    if (!framing_is_sound(packet, bytes, &messages_end)) {
        return MP_CHANNEL_DROPPED;
    }
    sequence = get_u16(packet + 4);

    /* Acks first, and from stale or duplicate packets too: what a packet says about our own sent
     * packets was true when it was written and does not expire in transit. */
    note_round_trip(channel, get_u16(packet + 6), now_ms);
    process_acks(channel, get_u16(packet + 6), get_u32(packet + 8));

    if (channel->remote_seen) {
        if (sequence == channel->remote_sequence) {
            return MP_CHANNEL_DROPPED;   /* a duplicate of the newest packet */
        }
        if (!mp_channel_sequence_newer(sequence, channel->remote_sequence)) {
            behind = (uint16_t)(channel->remote_sequence - sequence);
            if (behind > 32u) {
                /* Too old for the ack field to name. Accepting it could not be acknowledged, so
                 * the sender would resend its messages anyway; dropping it whole is the same
                 * outcome with one copy fewer stored. */
                return MP_CHANNEL_DROPPED;
            }
            if ((channel->remote_ack_bits & (1u << (behind - 1u))) != 0u) {
                return MP_CHANNEL_DROPPED;   /* a duplicate, its bit is already set */
            }
        }
    }

    *payload         = packet + messages_end;
    *payload_bytes   = bytes - messages_end;
    *packet_sequence = sequence;
    if (!store_messages(channel, packet)) {
        /* Not acknowledged, so nothing in it is lost. The payload goes on: the packet passed every
         * other check, and only its messages have to come again. */
        ++channel->refused_past_window;
        return MP_CHANNEL_PAST_WINDOW;
    }

    /* Only now, with every check passed and every message stored, does the packet enter the
     * window that the next built packet will acknowledge. */
    if (!channel->remote_seen) {
        channel->remote_seen     = true;
        channel->remote_sequence = sequence;
        channel->remote_ack_bits = 0u;
    } else if (mp_channel_sequence_newer(sequence, channel->remote_sequence)) {
        jump = (uint16_t)(sequence - channel->remote_sequence);
        if (jump > 32u) {
            channel->remote_ack_bits = 0u;   /* everything the bits could name is under the gap */
        } else if (jump == 32u) {
            channel->remote_ack_bits = 0x80000000u;
        } else {
            channel->remote_ack_bits =
                (channel->remote_ack_bits << jump) | (1u << (jump - 1u));
        }
        channel->remote_sequence = sequence;
    } else {
        channel->remote_ack_bits |= 1u << (behind - 1u);
    }

    return MP_CHANNEL_ACCEPTED;
}

bool mp_channel_packet_receive_at(mp_channel_t *channel, uint32_t now_ms, const uint8_t *packet,
                                  size_t bytes, const uint8_t **payload, size_t *payload_bytes)
{
    uint16_t sequence = 0;

    if (payload == NULL || payload_bytes == NULL) {
        return false;
    }
    if (mp_channel_packet_take(channel, now_ms, packet, bytes, payload, payload_bytes,
                               &sequence) != MP_CHANNEL_ACCEPTED) {
        *payload       = NULL;
        *payload_bytes = 0;
        return false;
    }
    return true;
}

/* Where the next message in order lies, when it has arrived. The one question the peek and the
 * read both ask, so the two cannot disagree about whether a message is there. */
static bool next_in_order(const mp_channel_t *channel, size_t *index)
{
    size_t at = channel->expected_message_id % MP_CHANNEL_ORDER_SLOTS;

    if (!channel->order_slots[at].used ||
        channel->order_slots[at].id != channel->expected_message_id) {
        return false;
    }
    *index = at;
    return true;
}

bool mp_channel_message_ready(const mp_channel_t *channel, size_t *bytes)
{
    size_t index = 0;

    if (channel == NULL || bytes == NULL || !next_in_order(channel, &index)) {
        return false;
    }
    *bytes = channel->order_slots[index].bytes;
    return true;
}

bool mp_channel_message_read(mp_channel_t *channel, void *buffer, size_t capacity, size_t *bytes)
{
    mp_channel_order_slot_t *slot;
    size_t                   index = 0;

    if (buffer == NULL || bytes == NULL) {
        return false;
    }
    if (!next_in_order(channel, &index)) {
        return false;   /* the next message in order has not arrived yet */
    }
    slot = &channel->order_slots[index];
    if (capacity < slot->bytes) {
        return false;   /* refused whole; the message stays rather than being truncated */
    }
    if (slot->bytes > 0) {
        memcpy(buffer, slot->data, slot->bytes);
    }
    *bytes     = slot->bytes;
    slot->used = false;
    ++channel->expected_message_id;
    return true;
}

bool mp_channel_packet_receive(mp_channel_t *channel, const uint8_t *packet, size_t bytes,
                               const uint8_t **payload, size_t *payload_bytes)
{
    return mp_channel_packet_receive_at(channel, channel->last_build_ms, packet, bytes, payload,
                                        payload_bytes);
}

uint32_t mp_channel_rtt_ms(const mp_channel_t *channel)
{
    return channel != NULL ? channel->rtt_ms : 0u;
}

uint32_t mp_channel_seats_lost_to_payload(const mp_channel_t *channel)
{
    return channel != NULL ? channel->seats_lost_to_payload : 0u;
}

uint32_t mp_channel_seat_wait_ms(const mp_channel_t *channel)
{
    return channel != NULL ? channel->seat_wait_ms : 0u;
}

uint32_t mp_channel_longest_seat_wait_ms(const mp_channel_t *channel)
{
    return channel != NULL ? channel->longest_seat_wait_ms : 0u;
}

uint32_t mp_channel_oldest_unacked_ms(const mp_channel_t *channel)
{
    return channel != NULL ? channel->oldest_unacked_ms : 0u;
}

uint8_t mp_channel_oldest_tag(const mp_channel_t *channel)
{
    return channel != NULL ? channel->oldest_tag : 0u;
}

uint16_t mp_channel_oldest_bytes(const mp_channel_t *channel)
{
    return channel != NULL ? channel->oldest_bytes : 0u;
}

uint32_t mp_channel_longest_unacked_ms(const mp_channel_t *channel)
{
    return channel != NULL ? channel->longest_unacked_ms : 0u;
}

uint8_t mp_channel_longest_unacked_tag(const mp_channel_t *channel)
{
    return channel != NULL ? channel->longest_unacked_tag : 0u;
}

uint16_t mp_channel_longest_unacked_bytes(const mp_channel_t *channel)
{
    return channel != NULL ? channel->longest_unacked_bytes : 0u;
}

uint32_t mp_channel_deepest_pending(const mp_channel_t *channel)
{
    return channel != NULL ? channel->deepest_pending : 0u;
}

size_t mp_channel_least_room_left(const mp_channel_t *channel)
{
    return (channel != NULL && channel->least_room_seen) ? channel->least_room_left : 0u;
}

uint32_t mp_channel_held_past_window(const mp_channel_t *channel)
{
    return channel != NULL ? channel->held_past_window : 0u;
}

uint32_t mp_channel_refused_past_window(const mp_channel_t *channel)
{
    return channel != NULL ? channel->refused_past_window : 0u;
}
