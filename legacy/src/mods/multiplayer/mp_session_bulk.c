/* mp_session_bulk.c: a large block beside the game, in a lane that is not the channel.
 *
 * Everything about why this exists is in mp_session.h under THE BULK LANE. What is here is the
 * whole of it: a note goes out in a datagram of its own the moment it is handed in, an arrived one
 * waits in a small ring until its reader takes it, and nothing in between is ordered, queued,
 * retried or acknowledged. The repair for a lost note lives above this file, where the thing being
 * carried knows how to name its own pieces.
 *
 * SIZE NOTE: well under the limit and meant to stay there. If this file grows, it will be
 * because somebody put a retry or a window in it, and that is the mistake this lane was cut out to
 * make impossible.
 */
#include "mp_session_bulk.h"

#include "mp_channel.h"
#include "mp_session_meter.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

mp_peer_t *mp_session_peer_at(mp_session_t *session, uint32_t endpoint)
{
    size_t i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].state != MP_PEER_FREE && session->peers[i].endpoint == endpoint) {
            return &session->peers[i];
        }
    }
    return NULL;
}

void mp_session_bulk_arrived(mp_session_t *session, uint32_t endpoint, uint64_t connection_id,
                             const uint8_t *note, size_t bytes)
{
    mp_peer_t         *peer = mp_session_peer_at(session, endpoint);
    mp_session_bulk_t *slot;

    if (peer == NULL || peer->state != MP_PEER_CONNECTED ||
        peer->connection_id != connection_id || bytes == 0u || bytes > MP_SESSION_BULK_BYTES) {
        return;
    }
    peer->last_recv_ms = session->now_ms;
    if (peer->bulk_count == MP_SESSION_BULK_RING) {
        /* Counted, never guessed at. The mask above this layer will name this piece again, so the
         * honest thing here is to drop it and say how often that happened; anything cleverer would
         * be a retry, and a retry here is what the whole lane exists to avoid. */
        ++session->bulk_overrun;
        return;
    }
    slot = &peer->bulk_ring[(peer->bulk_head + peer->bulk_count) % MP_SESSION_BULK_RING];
    memcpy(slot->data, note, bytes);
    slot->bytes = bytes;
    ++peer->bulk_count;
}

bool mp_session_send_bulk(mp_session_t *session, size_t peer_index, const void *data, size_t bytes)
{
    uint8_t          buffer[MP_CHANNEL_PACKET_BYTES];
    mp_wire_writer_t w;
    mp_peer_t       *peer;

    if (session == NULL || data == NULL || bytes == 0u || bytes > MP_SESSION_BULK_BYTES ||
        peer_index >= MP_SESSION_MAX_PEERS) {
        return false;
    }
    peer = &session->peers[peer_index];
    if (peer->state != MP_PEER_CONNECTED) {
        return false;
    }
    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_BULK_MAGIC);
    mp_wire_put_u8(&w, (uint8_t)MP_SESSION_BULK_TYPE);
    /* HIGH HALF FIRST, which is how the rest of the session writes a connection id and therefore
     * how the dispatch reads one. Written the other way round the id read as a stranger's, every
     * bulk note was dropped as unrecognised, and nothing said so, the transfer simply never
     * moved. The loopback check in unittests/mp_session.c is what caught it. */
    mp_wire_put_u32(&w, (uint32_t)(peer->connection_id >> 32));
    mp_wire_put_u32(&w, (uint32_t)peer->connection_id);
    if (w.overflowed || w.at + bytes > sizeof buffer) {
        return false;
    }
    memcpy(buffer + w.at, data, bytes);
    /* Deliberately NOT peer->last_send_ms. That stamp paces the keepalive, and a transfer that
     * kept it fresh would silence the keepalive, and with it the acknowledgements the reliable
     * channel rides on. A lane of its own means a clock of its own.
     *
     * The transport's answer is the caller's: a refused datagram is a peer that is gone or a
     * socket that is full, and the sender counts those. The first form returned true regardless
     * and left that counter unable to move. It is measured with the rest of the upload. */
    mp_meter_second_add(&session->upload, w.at + bytes, session->now_ms);
    mp_meter_second_add(&peer->meter.second, w.at + bytes, session->now_ms);
    return mp_transport_send(session->transport, peer->endpoint, buffer, w.at + bytes);
}

bool mp_session_read_bulk(mp_session_t *session, size_t peer_index, void *buffer, size_t capacity,
                          size_t *bytes)
{
    mp_peer_t         *peer;
    mp_session_bulk_t *oldest;

    if (session == NULL || peer_index >= MP_SESSION_MAX_PEERS) {
        return false;
    }
    peer = &session->peers[peer_index];
    if (peer->state != MP_PEER_CONNECTED || peer->bulk_count == 0u) {
        return false;
    }
    oldest = &peer->bulk_ring[peer->bulk_head];
    if (oldest->bytes > capacity) {
        return false;   /* whole or not at all: a torn piece would hash to nothing */
    }
    memcpy(buffer, oldest->data, oldest->bytes);
    if (bytes != NULL) {
        *bytes = oldest->bytes;
    }
    peer->bulk_head = (peer->bulk_head + 1u) % MP_SESSION_BULK_RING;
    --peer->bulk_count;
    return true;
}

uint32_t mp_session_bulk_overrun(const mp_session_t *session)
{
    return session->bulk_overrun;
}
