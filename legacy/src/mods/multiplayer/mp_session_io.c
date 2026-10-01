/* mp_session_io.c: what a connected session hands in and out, and the counters it shows.
 *
 * The seam the session's own size note named, drawn on 2026-09-04 before the payload ring, the
 * receive and service split and the parallel handshake went into mp_session.c. Everything here
 * touches a peer that is already connected, or reads a counter; nothing here changes a peer's
 * state or sends a packet. The payload ring's read side lives here with the reliable
 * pass-throughs, its insert side with the packet dispatch in mp_session.c, and the two agree
 * through the fields in the header. The line this file writes to the log about a channel whose
 * oldest message is being passed over is here for the same reason the counters are: it is a
 * reading of them, made in the one place they are read. The other, about a message that could not
 * be moved into its inbox, belongs to the move beside the reliable read.
 */
#include "mp_session.h"

#include "mp_channel.h"
#include "mp_hold.h"
#include "mp_inbox.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_peer_t *connected_peer(mp_session_t *session, size_t peer_index)
{
    if (peer_index >= MP_SESSION_MAX_PEERS ||
        session->peers[peer_index].state != MP_PEER_CONNECTED) {
        return NULL;
    }
    return &session->peers[peer_index];
}

size_t mp_session_peer_count(const mp_session_t *session)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].state == MP_PEER_CONNECTED) {
            ++count;
        }
    }
    return count;
}

bool mp_session_is_connected(const mp_session_t *session)
{
    return session->role == MP_SESSION_CLIENT && session->peers[0].state == MP_PEER_CONNECTED;
}

bool mp_session_read_reliable(mp_session_t *session, size_t peer_index, void *buffer,
                              size_t capacity, size_t *bytes)
{
    mp_peer_t *peer = connected_peer(session, peer_index);

    if (peer == NULL) {
        return false;
    }
    mp_session_move_notes(session, peer);
    return mp_inbox_take(&peer->inbox, buffer, capacity, bytes);
}

/* A message moves only after the inbox has said it has room, with the one predicate its put asks
 * again, so a message is never taken off the channel with nowhere to go. A put that fails anyway
 * would be a defect in the inbox, not a full one, and it is said. */
void mp_session_move_notes(mp_session_t *session, mp_peer_t *peer)
{
    static bool said;
    uint8_t     message[MP_CHANNEL_MESSAGE_BYTES];
    size_t      bytes = 0;
    bool        left_one_behind = false;

    while (mp_channel_message_ready(&peer->channel, &bytes)) {
        if (bytes == 0u && mp_channel_message_read(&peer->channel, message, sizeof message,
                                                   &bytes)) {
            ++peer->empty_notes;   /* a superseded state; a younger copy follows it */
            continue;
        }
        if (!mp_inbox_fits(&peer->inbox, bytes)) {
            left_one_behind = true;
            break;
        }
        if (!mp_channel_message_read(&peer->channel, message, sizeof message, &bytes) ||
            !mp_inbox_put(&peer->inbox, message, bytes)) {
            if (!said) {
                said = true;
                log_error("a reliable message from peer %u could not be moved into an inbox that "
                          "had room for it; it may be lost",
                          (unsigned)(peer - session->peers));
            }
            break;
        }
    }
    mp_inbox_note_move(&peer->inbox, left_one_behind);
}

bool mp_session_set_payload(mp_session_t *session, size_t peer_index, const void *data,
                            size_t bytes)
{
    mp_peer_t *peer = connected_peer(session, peer_index);

    if (peer == NULL || bytes > MP_SESSION_PAYLOAD_BYTES) {
        return false;
    }
    if (bytes > 0) {
        memcpy(peer->out_payload, data, bytes);
    }
    peer->out_payload_bytes = bytes;
    return true;
}

size_t mp_session_broadcast_payload(mp_session_t *session, const void *data, size_t bytes)
{
    size_t sent = 0;
    size_t i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (mp_session_set_payload(session, i, data, bytes)) {
            ++sent;
        }
    }
    return sent;
}

/* Oldest first. The ring is drained in the order the peer sent, so a caller that decodes a delta
 * against the previous payload's state sees them in sequence; a payload too large for the buffer
 * stays at the head rather than being torn, which is the caller's bug to see, not this side's.
 *
 * The counters this file reads exist because of one field run against the listen host: the host
 * sent 5297 snapshots and the client decoded 4983, the client sent 5158 own states of which 4983
 * landed, three to six per cent missing over a loopback, and no number said whether the wire
 * lost them or the ring threw them away. A single stale count came back reading 400 and 451 and
 * could not answer either, because a reordering and a burst after a stall are opposite findings
 * added together. Split, a later run read 0 reordered and 118 overrun: the wire lost nothing and
 * the ring discarded everything. Split again by the wait, a discard that waited less than the
 * stall line is arrivals outrunning a running drain, and the rest were thrown away while nothing
 * drained at all, which no ring depth would help. */
bool mp_session_read_payload(mp_session_t *session, size_t peer_index, void *buffer,
                             size_t capacity, size_t *bytes)
{
    mp_peer_t            *peer = connected_peer(session, peer_index);
    mp_session_payload_t *oldest;

    if (peer == NULL || peer->in_count == 0u) {
        return false;
    }
    oldest = &peer->in_ring[peer->in_head];
    if (oldest->bytes > capacity) {
        return false;
    }
    memcpy(buffer, oldest->data, oldest->bytes);
    if (bytes != NULL) {
        *bytes = oldest->bytes;
    }
    (void)mp_session_note_payload_leaving(session, oldest);
    peer->in_head = (peer->in_head + 1u) % MP_SESSION_PAYLOAD_RING;
    --peer->in_count;
    return true;
}

const mp_peer_t *mp_session_peer(const mp_session_t *session, size_t index)
{
    if (index >= MP_SESSION_MAX_PEERS) {
        return NULL;
    }
    return &session->peers[index];
}

uint32_t mp_session_joins(const mp_session_t *session)    { return session->joins; }
bool mp_session_connect_wanted(const mp_session_t *session)
{ return session != NULL && session->connect_wanted; }

size_t mp_session_reliable_pending(const mp_session_t *session)
{
    size_t pending = 0;
    size_t i;

    for (i = 0; session != NULL && i < MP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].state == MP_PEER_CONNECTED) {
            pending += mp_channel_send_pending(&session->peers[i].channel);
            pending += mp_channel_state_waiting(&session->peers[i].channel);
            pending += mp_hold_count(&session->peers[i].hold);
        }
    }
    return pending;
}
uint32_t mp_session_refused_foreign(const mp_session_t *session)
{ return (session != NULL) ? session->refused_foreign : 0u; }
uint32_t mp_session_refused_unhandled(const mp_session_t *session)
{ return (session != NULL) ? session->refused_unhandled : 0u; }
uint32_t mp_session_denied(const mp_session_t *session)   { return session->denied; }
uint32_t mp_session_drops(const mp_session_t *session)    { return session->drops; }
uint32_t mp_session_leaves(const mp_session_t *session)   { return session->leaves; }
uint32_t mp_session_replaced(const mp_session_t *session) { return session->replaced; }
uint32_t mp_session_payloads_reordered(const mp_session_t *session)
{
    return session->payloads_reordered;
}

uint32_t mp_session_payloads_overrun(const mp_session_t *session)
{
    return session->payloads_overrun;
}

uint32_t mp_session_payloads_overrun_fresh(const mp_session_t *session)
{
    return session->payloads_overrun_fresh;
}

uint32_t mp_session_deepest_backlog(const mp_session_t *session)
{
    return session->deepest_backlog;
}

uint32_t mp_session_longest_wait_ms(const mp_session_t *session)
{
    return session->longest_wait_ms;
}

uint32_t mp_session_packets_with_payload(const mp_session_t *session)
{
    return session->packets_with_payload;
}

uint32_t mp_session_packets_without_payload(const mp_session_t *session)
{
    return session->packets_without_payload;
}

/* Every slot, not every connected peer: a channel keeps its counters until the slot is connected
 * again and mp_channel_init starts it over, so a peer that has left is still in the sum. */
uint32_t mp_session_seats_lost_to_payload(const mp_session_t *session)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        total += mp_channel_seats_lost_to_payload(&session->peers[i].channel);
    }
    return total;
}

uint32_t mp_session_held_past_window(const mp_session_t *session)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        total += mp_channel_held_past_window(&session->peers[i].channel);
    }
    return total;
}

uint32_t mp_session_refused_past_window(const mp_session_t *session)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        total += mp_channel_refused_past_window(&session->peers[i].channel);
    }
    return total;
}

/* The least room any packet that carried a payload left for messages, over every peer: the
 * channel's floor as it really is rather than as the arithmetic allows. Zero when no packet has
 * carried a payload yet, which is a lobby. */
size_t mp_session_least_room_left(const mp_session_t *session)
{
    size_t least = 0;
    bool   seen  = false;
    size_t i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        size_t room = mp_channel_least_room_left(&session->peers[i].channel);

        if (room == 0u) {
            continue;   /* that channel has carried no payload yet, which is a lobby */
        }
        if (!seen || room < least) {
            seen  = true;
            least = room;
        }
    }
    return least;
}

/* The longest any message has been out unanswered, over every peer. A number that grows while the
 * seats lost stay at nought is a far side that is not reading, which is the other half of the
 * question the warning used to answer with one word. */
uint32_t mp_session_longest_unacked_ms(const mp_session_t *session)
{
    uint32_t longest = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        uint32_t out = mp_channel_longest_unacked_ms(&session->peers[i].channel);

        if (longest < out) {
            longest = out;
        }
    }
    return longest;
}

uint32_t mp_session_longest_seat_wait_ms(const mp_session_t *session)
{
    uint32_t longest = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        uint32_t wait = mp_channel_longest_seat_wait_ms(&session->peers[i].channel);

        if (longest < wait) {
            longest = wait;
        }
    }
    return longest;
}

/* Once per session, not per peer and not per substep: the first channel to cross the line names
 * the finding, and the report's sums say how far it went.
 *
 * The line used to blame the payload whatever had happened, and the field showed what that is
 * worth: one run warned with five seconds on the clock and NOUGHT seats lost to the payload, and
 * another with nine hundred milliseconds and the same nought. The wait is measured from the first
 * build that could have carried the message, so it counts the time a message spends out on the
 * wire unanswered as readily as the time it spends unseated, and those want opposite repairs. So
 * the line now says which of the two it is, names the message, and gives the other number beside
 * it. */
void mp_session_watch_seat_wait(mp_session_t *session, const mp_peer_t *peer)
{
    uint32_t waited   = mp_channel_seat_wait_ms(&peer->channel);
    uint32_t unacked  = mp_channel_oldest_unacked_ms(&peer->channel);
    uint32_t lost     = mp_channel_seats_lost_to_payload(&peer->channel);
    unsigned index    = (unsigned)(peer - session->peers);
    unsigned tag      = (unsigned)mp_channel_oldest_tag(&peer->channel);
    unsigned bytes    = (unsigned)mp_channel_oldest_bytes(&peer->channel);

    if (session->seat_wait_said || waited < MP_SESSION_SEAT_WAIT_WARN_MS) {
        return;
    }
    session->seat_wait_said = true;
    if (unacked >= MP_SESSION_SEAT_WAIT_WARN_MS) {
        log_warning("the reliable channel to peer %u has had its oldest message (tag %02X, %u "
                    "byte(s)) out for %u ms with no answer: the far side is not taking what it is "
                    "sent, and every message queued behind it waits with it. The payload kept it "
                    "out of %u packet(s) in the same stretch. Later channels are counted in the "
                    "report, not said again",
                    index, tag, bytes, (unsigned)unacked, (unsigned)lost);
        return;
    }
    log_warning("the reliable channel to peer %u has passed its oldest message (tag %02X, %u "
                "byte(s)) over for %u ms: the payload takes the room the message needs (%u seat(s) "
                "lost to it on this channel so far), and every message queued behind it waits with "
                "it. Later channels that starve are counted in the report, not said again",
                index, tag, bytes, (unsigned)waited, (unsigned)lost);
}

/* Over every slot, as the channel counters above: a slot keeps its inbox's figures until a peer
 * connects to it again. */
uint32_t mp_session_inbox_most_notes(const mp_session_t *session)
{
    uint32_t most = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        size_t count = mp_inbox_most_count(&session->peers[i].inbox);

        most = (count > most) ? (uint32_t)count : most;
    }
    return most;
}

uint32_t mp_session_inbox_most_bytes(const mp_session_t *session)
{
    uint32_t most = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        size_t bytes = mp_inbox_most_bytes(&session->peers[i].inbox);

        most = (bytes > most) ? (uint32_t)bytes : most;
    }
    return most;
}

uint32_t mp_session_inbox_stalls(const mp_session_t *session)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        total += mp_inbox_stalls(&session->peers[i].inbox);
    }
    return total;
}

uint32_t mp_session_payloads_past_window(const mp_session_t *session)
{
    return session->payloads_past_window;
}

void mp_session_note_payload_parts(mp_session_t *session, size_t index, size_t bodies_bytes,
                                   size_t enemy_bytes)
{
    mp_peer_t *peer = connected_peer(session, index);

    if (peer != NULL) {
        mp_session_meter_parts(&peer->meter, bodies_bytes, enemy_bytes);
    }
}

uint32_t mp_session_empty_notes(const mp_session_t *session)
{
    uint32_t total = 0;
    size_t   i;

    for (i = 0; session != NULL && i < MP_SESSION_MAX_PEERS; ++i) {
        total += session->peers[i].empty_notes;
    }
    return total;
}

mp_deny_reason_t mp_session_last_deny(const mp_session_t *session) { return session->last_deny; }

size_t mp_session_last_deny_detail(const mp_session_t *session, const uint8_t **bytes)
{
    if (bytes != NULL) {
        *bytes = session != NULL ? session->deny_detail : NULL;
    }
    return session != NULL ? session->deny_detail_bytes : 0u;
}
