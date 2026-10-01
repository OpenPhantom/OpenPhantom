/* mp_relay_link_leg.c: a leg to the relay that stands: refresh rounds, the sealed traffic on it
 * and the goodbye. See mp_relay_link.h for the rules and mp_relay_link_int.h for the split.
 */
#include "mp_relay_link_int.h"

#include "mp_relay_inner.h"
#include "mp_relay_noise.h"
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t mp_relay_link_keep_interval(const mp_relay_link_t *link)
{
    uint32_t ms = link->role == MP_RELAY_LINK_HOST ? link->registered.refresh_ms
                                                   : link->joined.member_refresh_ms;

    if (ms < MP_RELAY_KEEP_MIN_MS) {
        return MP_RELAY_KEEP_MIN_MS;
    }
    return ms > MP_RELAY_KEEP_MAX_MS ? MP_RELAY_KEEP_MAX_MS : ms;
}

bool mp_relay_link_leg_alive(const mp_relay_link_t *link, uint32_t now)
{
    return link->opened_once && now - link->last_opened < MP_RELAY_QUIET_GAP_MS;
}

static bool may_reconnect(const mp_relay_link_t *link, uint32_t now)
{
    return !link->reconnected_once || now - link->last_reconnect >= MP_RELAY_RECONNECT_GAP_MS;
}

static size_t seal_control(mp_relay_link_t *link, const uint8_t *plain, size_t bytes,
                           uint8_t *out, size_t capacity)
{
    mp_relay_sealed_header_t header;

    memset(&header, 0, sizeof header);
    if (link->role == MP_RELAY_LINK_HOST) {
        header.type  = (uint8_t)MP_RELAY_TYPE_HOST_TO_RELAY_CONTROL;
        header.index = link->registered.session_index;
    } else {
        header.type  = (uint8_t)MP_RELAY_TYPE_MEMBER_TO_RELAY_CONTROL;
        header.gen   = link->joined.gen;
        header.index = link->joined.member_index;
    }
    return mp_relay_leg_seal(&link->leg, &header, plain, bytes, out, capacity);
}

static size_t keep_datagram(mp_relay_link_t *link, uint8_t *out, size_t capacity)
{
    uint8_t plain[MP_RELAY_REFRESH_BYTES];
    size_t  bytes;

    if (link->role == MP_RELAY_LINK_HOST) {
        bytes = mp_relay_refresh_encode(link->epoch,
                                        link->listed ? (uint8_t)MP_RELAY_REFRESH_LISTED : 0u,
                                        link->announce, plain, sizeof plain);
    } else {
        bytes = mp_relay_member_refresh_encode(plain, sizeof plain);
    }
    if (bytes == 0u) {
        return 0u;
    }
    ++link->refreshes_sent;
    return seal_control(link, plain, bytes, out, capacity);
}

/* A refresh round ended without its answer. A new leg follows when the way it ended calls for
 * one and the last new leg is five seconds back; otherwise the next round comes three seconds
 * on. */
static void keep_round_failed(mp_relay_link_t *link, bool calls_for_leg, uint32_t now)
{
    link->keeping      = false;
    link->keep_refused = false;
    if (calls_for_leg && may_reconnect(link, now) && link->have_key) {
        mp_relay_link_renew(link, now);
        return;
    }
    link->next_keep_at = now + MP_RELAY_KEEP_TRY_MS;
}

static void keep_round_answered(mp_relay_link_t *link, uint32_t now)
{
    ++link->refreshes_answered;
    link->keeping      = false;
    link->keep_refused = false;
    link->next_keep_at = now + mp_relay_link_keep_interval(link);
}

bool mp_relay_link_leg_carries(const mp_relay_link_t *link, uint32_t now)
{
    return link->opened_once &&
           now - link->last_opened <
               mp_relay_link_keep_interval(link) + MP_RELAY_KEEP_TRIES * MP_RELAY_KEEP_TRY_MS;
}

size_t mp_relay_link_tick_leg(mp_relay_link_t *link, uint32_t now, uint8_t *out, size_t capacity)
{
    if (link->seat_lost) {
        size_t bytes = mp_relay_link_farewell(link, out, capacity);

        mp_relay_link_fail(link, MP_RELAY_LINK_SEAT_LOST);
        return bytes;
    }
    if (!link->keeping) {
        if ((link->reconnect_due || link->leg.next >= MP_RELAY_LEG_KEY_LIMIT) &&
            may_reconnect(link, now) && link->have_key) {
            mp_relay_link_renew(link, now);
            return 0u;
        }
        if (link->listing_dirty && link->role == MP_RELAY_LINK_HOST) {
            link->listing_dirty = false;
            link->next_keep_at  = now;   /* the relay's list shows what the host said last */
        }
        if (!mp_relay_link_reached(now, link->next_keep_at)) {
            return 0u;
        }
        link->keeping            = true;
        link->keep_tries         = 0u;
        link->keep_next_try_at   = now;
        link->keep_first_counter = link->leg.next;
        link->keep_refused       = false;
    }
    if (link->keep_refused) {
        if (mp_relay_link_reached(now, link->keep_grace_until)) {
            /* Open and forgeable: only a leg quiet for a while makes it worth a new one. */
            keep_round_failed(link,
                              link->keep_refusal == MP_RELAY_NACK_UNKNOWN_INDEX &&
                                  !mp_relay_link_leg_alive(link, now),
                              now);
        }
        return 0u;
    }
    if (!mp_relay_link_reached(now, link->keep_next_try_at)) {
        return 0u;
    }
    if (link->keep_tries >= MP_RELAY_KEEP_TRIES) {
        keep_round_failed(link, true, now);
        return 0u;
    }
    ++link->keep_tries;
    link->keep_next_try_at = now + MP_RELAY_KEEP_TRY_MS;
    return keep_datagram(link, out, capacity);
}

void mp_relay_link_keep_refused(mp_relay_link_t *link, uint32_t now, uint8_t reason)
{
    if (link->phase == MP_RELAY_LINK_READY && link->keeping && !link->keep_refused) {
        link->keep_refused     = true;
        link->keep_refusal     = reason;
        link->keep_grace_until = now + MP_RELAY_NACK_GRACE_MS;
    }
}

static bool take_control(mp_relay_link_t *link, uint32_t now,
                         const mp_relay_sealed_header_t *header, const uint8_t *plain,
                         size_t bytes, mp_relay_link_event_t *event)
{
    mp_relay_refresh_ack_t   ack;
    mp_relay_member_open_t   opened;
    mp_relay_member_closed_t closed;
    mp_relay_nack_t          nack;

    if (bytes == 0u || !mp_relay_inner_allowed(header->type, plain[0])) {
        return false;
    }
    switch (plain[0]) {
    case MP_RELAY_INNER_REFRESH_ACK:
        /* Only an answer to this round: an ack for a counter sealed before it is an old one. */
        if (mp_relay_refresh_ack_decode(plain, bytes, &ack) && link->keeping &&
            ack.counter >= link->keep_first_counter) {
            if (ack.epoch > link->epoch) {
                link->epoch = ack.epoch;
            }
            keep_round_answered(link, now);
        }
        return false;
    case MP_RELAY_INNER_MEMBER_ACK:
        if (mp_relay_member_ack_check(plain, bytes) && link->keeping) {
            keep_round_answered(link, now);
        }
        return false;
    case MP_RELAY_INNER_MEMBER_OPEN:
        if (!mp_relay_member_open_decode(plain, bytes, &opened)) {
            return false;
        }
        event->kind    = MP_RELAY_LINK_EVENT_MEMBER_OPEN;
        event->slot    = opened.slot;
        event->gen     = opened.gen;
        event->flags   = opened.flags;
        event->counter = header->counter;
        memcpy(event->r, opened.r, sizeof event->r);
        return true;
    case MP_RELAY_INNER_MEMBER_CLOSED:
        if (!mp_relay_member_closed_decode(plain, bytes, &closed)) {
            return false;
        }
        event->kind    = MP_RELAY_LINK_EVENT_MEMBER_CLOSED;
        event->slot    = closed.slot;
        event->gen     = closed.gen;
        event->reason  = closed.reason;
        event->counter = header->counter;
        return true;
    case MP_RELAY_INNER_NACK:
        if (!mp_relay_leg_nack_decode(plain, bytes, &nack)) {
            return false;
        }
        ++link->nacks_seen;
        if (nack.reason == MP_RELAY_NACK_REVOKED && link->role == MP_RELAY_LINK_HOST) {
            mp_relay_link_fail(link, MP_RELAY_LINK_REVOKED);
            return false;
        }
        if (nack.reason == MP_RELAY_NACK_RECONNECT) {
            link->reconnect_due = true;   /* sealed, so the relay itself said it */
        }
        if (link->keeping) {
            keep_round_failed(link, false, now);
        }
        return false;
    default:
        return false;
    }
}

bool mp_relay_link_take_sealed(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram,
                               size_t bytes, uint8_t *game, size_t game_capacity,
                               mp_relay_link_event_t *event)
{
    mp_relay_sealed_header_t header;
    uint8_t                  plain[MP_RELAY_GAME_PACKET_MAX];
    size_t                   plain_bytes = 0u;
    bool                     data;
    bool                     ours;

    if (!link->leg.live || !mp_relay_sealed_header_read(datagram, bytes, &header)) {
        return false;
    }
    /* The header is checked before anything is opened, so a packet for another leg costs no work
     * and counts against nothing. */
    if (link->role == MP_RELAY_LINK_HOST) {
        data = header.type == MP_RELAY_TYPE_RELAY_TO_HOST;
        ours = link->registered_known && header.index == link->registered.session_index &&
               (data || header.type == MP_RELAY_TYPE_RELAY_TO_HOST_CONTROL);
    } else {
        data = header.type == MP_RELAY_TYPE_RELAY_TO_MEMBER;
        ours = link->joined_known && header.index == link->joined.member_index &&
               header.gen == link->joined.gen &&
               (data || header.type == MP_RELAY_TYPE_RELAY_TO_MEMBER_CONTROL);
    }
    if (!ours) {
        return false;
    }
    if (!mp_relay_leg_open(&link->leg, datagram, bytes, plain, sizeof plain, &plain_bytes)) {
        ++link->data_refused;
        return false;
    }
    link->opened_once = true;
    link->last_opened = now;
    if (!data) {
        return take_control(link, now, &header, plain, plain_bytes, event);
    }
    if (game == NULL || plain_bytes > game_capacity) {
        ++link->data_refused;
        return false;
    }
    memcpy(game, plain, plain_bytes);
    event->kind  = MP_RELAY_LINK_EVENT_GAME;
    event->slot  = header.slot;
    event->gen   = header.gen;
    event->bytes = plain_bytes;
    return true;
}

size_t mp_relay_link_seal_game(mp_relay_link_t *link, uint8_t slot, uint16_t gen,
                               const uint8_t *packet, size_t bytes, uint8_t *out,
                               size_t capacity)
{
    mp_relay_sealed_header_t header;

    if (!mp_relay_link_ready(link) || bytes > MP_RELAY_GAME_PACKET_MAX) {
        return 0u;
    }
    memset(&header, 0, sizeof header);
    if (link->role == MP_RELAY_LINK_HOST) {
        header.type  = (uint8_t)MP_RELAY_TYPE_HOST_TO_RELAY;
        header.slot  = slot;
        header.gen   = gen;
        header.index = link->registered.session_index;
    } else {
        header.type  = (uint8_t)MP_RELAY_TYPE_MEMBER_TO_RELAY;
        header.gen   = link->joined.gen;
        header.index = link->joined.member_index;
    }
    return mp_relay_leg_seal(&link->leg, &header, packet, bytes, out, capacity);
}

size_t mp_relay_link_farewell(mp_relay_link_t *link, uint8_t *out, size_t capacity)
{
    uint8_t plain[MP_RELAY_CLOSE_BYTES];
    size_t  bytes;

    if (!link->leg.live || !(link->role == MP_RELAY_LINK_HOST ? link->registered_known
                                                               : link->joined_known)) {
        return 0u;
    }
    bytes = link->role == MP_RELAY_LINK_HOST
                ? mp_relay_close_encode(link->registered.session_id, plain, sizeof plain)
                : mp_relay_leave_encode(plain, sizeof plain);
    return bytes == 0u ? 0u : seal_control(link, plain, bytes, out, capacity);
}

bool mp_relay_link_ready(const mp_relay_link_t *link)
{
    return link->leg.live && !link->seat_lost &&
           (link->role == MP_RELAY_LINK_HOST ? link->registered_known : link->joined_known);
}
