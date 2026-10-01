/* mp_relay_link.c: one PC's link to the relay, the attempts half. See mp_relay_link.h for the
 * rules and mp_relay_link_int.h for the split.
 *
 * SIZE NOTE: a little over 600 lines. One attempt from its Hello to its answer, and every refusal a
 * relay can give one: the refusals are a table the reference client spreads over its callers, and
 * this build decides them in one place, which is the reason this half is the longer one.
 */
#include "mp_relay_link.h"
#include "mp_relay_link_int.h"

#include "mp_relay_crypto.h"
#include "mp_relay_inner.h"
#include "mp_relay_noise.h"
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

bool mp_relay_link_reached(uint32_t now, uint32_t at)
{
    return now - at < 0x80000000u;
}

static uint8_t cookie_role(const mp_relay_link_t *link)
{
    return link->role == MP_RELAY_LINK_HOST ? (uint8_t)MP_RELAY_ROLE_HOST
                                            : (uint8_t)MP_RELAY_ROLE_MEMBER;
}

void mp_relay_link_init(mp_relay_link_t *link, mp_relay_random_fn random)
{
    memset(link, 0, sizeof *link);
    link->random = random;
}

void mp_relay_link_fail(mp_relay_link_t *link, mp_relay_link_failure_t failure)
{
    link->phase    = MP_RELAY_LINK_FAILED;
    link->failure  = failure;
    link->renewing = false;
    link->keeping  = false;
    mp_relay_nk_wipe(&link->nk);
    mp_relay_leg_wipe(&link->leg);
}

/* A Hello under a fresh nonce, with the attempt's clocks from the start. */
static void begin_cookie(mp_relay_link_t *link, uint32_t now)
{
    uint8_t nonce[8];
    size_t  i;

    if (!link->random(nonce, sizeof nonce)) {
        mp_relay_link_fail(link, MP_RELAY_LINK_CRYPTO);
        return;
    }
    link->nonce = 0u;
    for (i = 0; i < sizeof nonce; ++i) {
        link->nonce |= (uint64_t)nonce[i] << (8u * i);
    }
    (void)mp_relay_hello_encode(cookie_role(link), link->nonce, link->hello, sizeof link->hello);
    link->phase        = MP_RELAY_LINK_COOKIE;
    link->tries        = 0u;
    link->next_send_at = now;
    link->unopened     = false;
    link->refused      = false;
    link->seat_held    = false;
}

/* A new attempt: a key first, then a cookie. */
static void begin_attempt(mp_relay_link_t *link, uint32_t now)
{
    link->cookie_retries = 0u;
    link->retry_cookie   = false;
    if (!link->have_key) {
        link->phase = MP_RELAY_LINK_NEEDS_KEY;
        return;
    }
    begin_cookie(link, now);
}

void mp_relay_link_renew(mp_relay_link_t *link, uint32_t now)
{
    link->reconnected_once = true;
    link->last_reconnect   = now;
    link->renewing         = true;
    link->keeping          = false;
    link->keep_refused     = false;
    link->reconnect_due    = false;
    ++link->renewals;
    begin_attempt(link, now);
}

void mp_relay_link_host(mp_relay_link_t *link, uint8_t seats)
{
    link->role  = MP_RELAY_LINK_HOST;
    link->seats = seats;
    link->phase = MP_RELAY_LINK_NEEDS_KEY;
}

void mp_relay_link_join(mp_relay_link_t *link, const uint8_t code[MP_RELAY_CODE_BYTES])
{
    link->role = MP_RELAY_LINK_MEMBER;
    memcpy(link->code, code, MP_RELAY_CODE_BYTES);
    link->phase = MP_RELAY_LINK_NEEDS_KEY;
}

void mp_relay_link_give_key(mp_relay_link_t *link, const mp_relay_key_t *key, uint32_t now)
{
    link->key      = *key;
    link->have_key = true;
    if (link->phase == MP_RELAY_LINK_NEEDS_KEY) {
        begin_attempt(link, now);
    }
}

void mp_relay_link_set_listing(mp_relay_link_t *link, bool listed,
                               const uint8_t announce[MP_RELAY_ANNOUNCE_BYTES])
{
    if (listed != link->listed ||
        (listed && memcmp(announce, link->announce, MP_RELAY_ANNOUNCE_BYTES) != 0)) {
        link->listing_dirty = true;
    }
    link->listed = listed;
    memcpy(link->announce, announce, MP_RELAY_ANNOUNCE_BYTES);
}

/* The Register or Join body for the cookie in hand. A handshake that replaces a leg proves the
 * session or the seat it continues. 0 when a proof could not be made. */
static size_t request_body(mp_relay_link_t *link, uint8_t body[MP_RELAY_REGISTER_BODY_BYTES])
{
    size_t bytes  = 0u;
    bool   proved = true;

    if (link->role == MP_RELAY_LINK_HOST) {
        mp_relay_register_body_t reg;

        memset(&reg, 0, sizeof reg);
        reg.seats = link->seats;
        if (link->renewing && link->registered_known) {
            reg.flags     = (uint8_t)MP_RELAY_REGISTER_RESUME;
            reg.resume_id = link->registered.session_id;
            proved = mp_relay_resume_proof(link->registered.host_secret, link->cookie.epoch,
                                           link->cookie.cookie, link->nonce, link->key.id,
                                           reg.resume_proof);
        }
        if (proved) {
            bytes = mp_relay_register_body_encode(&reg, body, MP_RELAY_REGISTER_BODY_BYTES);
        }
        mp_relay_wipe(&reg, sizeof reg);
    } else {
        mp_relay_join_body_t join;

        memset(&join, 0, sizeof join);
        memcpy(join.code, link->code, sizeof join.code);
        if (link->renewing && link->joined_known) {
            join.flags = (uint8_t)MP_RELAY_JOIN_HAS_PROOF;
            memcpy(join.r, link->joined.r, sizeof join.r);
            proved = mp_relay_seat_proof(link->joined.seat_secret, link->cookie.epoch,
                                         link->cookie.cookie, link->nonce, link->key.id,
                                         join.proof);
        }
        if (proved) {
            bytes = mp_relay_join_body_encode(&join, body, MP_RELAY_REGISTER_BODY_BYTES);
        }
        mp_relay_wipe(&join, sizeof join);
    }
    return bytes;
}

static void build_request(mp_relay_link_t *link, uint32_t now)
{
    uint8_t body[MP_RELAY_REGISTER_BODY_BYTES];
    uint8_t e_private[MP_RELAY_KEY_BYTES];
    uint8_t type = link->role == MP_RELAY_LINK_HOST ? (uint8_t)MP_RELAY_TYPE_REGISTER
                                                    : (uint8_t)MP_RELAY_TYPE_JOIN;
    size_t  body_bytes = 0u;
    bool    built      = false;

    if (link->random(e_private, sizeof e_private)) {
        body_bytes = request_body(link, body);
    }
    if (body_bytes != 0u) {
        built = mp_relay_build_request(type, &link->cookie, link->nonce, link->key.id,
                                       link->key.public_key, body, body_bytes, e_private,
                                       &link->nk, link->request, sizeof link->request) ==
                MP_RELAY_REQUEST_BYTES;
    }
    mp_relay_wipe(body, sizeof body);
    mp_relay_wipe(e_private, sizeof e_private);
    if (!built) {
        mp_relay_link_fail(link, MP_RELAY_LINK_CRYPTO);
        return;
    }
    link->phase        = MP_RELAY_LINK_REQUEST;
    link->tries        = 0u;
    link->next_send_at = now;
    link->unopened     = false;
    link->refused      = false;
    link->seat_held    = false;
}

/* The attempt ended without a leg. A link that still has one goes on with it and tries again at
 * its next keep; one that has none waits and starts over. */
static void attempt_failed(mp_relay_link_t *link, mp_relay_link_failure_t failure, uint32_t now)
{
    mp_relay_nk_wipe(&link->nk);
    link->failure = failure;
    if (link->renewing && link->leg.live) {
        link->phase        = MP_RELAY_LINK_READY;
        link->renewing     = false;
        link->keeping      = false;
        link->next_keep_at = now + MP_RELAY_KEEP_TRY_MS;
        return;
    }
    link->renewing      = false;
    link->phase         = MP_RELAY_LINK_BACKOFF;
    link->backoff_until = now + MP_RELAY_BACKOFF_MS;
}

/* The relay refused the key, outright or by answers that never opened under it. A new leg gives
 * way to the leg in use, which keeps being refreshed; without one the link waits for a key. */
static void key_refused(mp_relay_link_t *link, mp_relay_link_failure_t failure, uint32_t now,
                        mp_relay_link_event_t *event)
{
    mp_relay_nk_wipe(&link->nk);
    link->failure  = failure;
    link->have_key = false;
    if (link->renewing && link->leg.live) {
        link->phase        = MP_RELAY_LINK_READY;
        link->renewing     = false;
        link->keeping      = false;
        link->next_keep_at = now + MP_RELAY_KEEP_TRY_MS;
    } else {
        link->phase = MP_RELAY_LINK_NEEDS_KEY;
    }
    if (event != NULL) {
        event->kind     = MP_RELAY_LINK_EVENT_KEY_REFUSED;
        event->unproven = failure == MP_RELAY_LINK_UNPROVEN;
    }
}

/* A retry for a while from the first refusal of its kind, then a failure. */
static void patient(mp_relay_link_t *link, uint32_t patience, mp_relay_link_failure_t failure,
                    uint32_t now)
{
    if (!link->refusal_clock || link->patience_for != failure) {
        link->refusal_clock    = true;
        link->first_refusal_at = now;
        link->patience_for     = failure;
    }
    if (now - link->first_refusal_at >= patience) {
        mp_relay_link_fail(link, failure);
        return;
    }
    attempt_failed(link, failure, now);
}

/* A host whose resume the relay does not know: the relay's secret changed and the old code went
 * with it, so the host registers afresh and gets a new one. */
static void register_afresh(mp_relay_link_t *link, uint32_t now)
{
    link->registered_known = false;
    link->renewing         = false;
    mp_relay_leg_wipe(&link->leg);
    mp_relay_nk_wipe(&link->nk);
    begin_attempt(link, now);
}

/* An open Nack nothing overturned within its grace, or Nack 12 held through a patient attempt. */
static void believe_refusal(mp_relay_link_t *link, uint32_t now, mp_relay_link_event_t *event)
{
    link->refused = false;
    switch (link->refusal.reason) {
    case MP_RELAY_NACK_BAD_COOKIE:
        if (link->cookie_retries >= MP_RELAY_COOKIE_RETRIES) {
            attempt_failed(link, MP_RELAY_LINK_REFUSED, now);
            return;
        }
        ++link->cookie_retries;
        mp_relay_nk_wipe(&link->nk);
        link->retry_cookie  = true;
        link->phase         = MP_RELAY_LINK_BACKOFF;
        link->backoff_until = now + MP_RELAY_COOKIE_RETRY_MS;
        return;
    case MP_RELAY_NACK_UNKNOWN_KEY:
        key_refused(link, MP_RELAY_LINK_UNKNOWN_KEY, now, event);
        return;
    case MP_RELAY_NACK_UNKNOWN_CODE:
        if (link->role == MP_RELAY_LINK_MEMBER && link->renewing) {
            patient(link, MP_RELAY_REJOIN_PATIENCE_MS, MP_RELAY_LINK_UNKNOWN_CODE, now);
        } else {
            mp_relay_link_fail(link, MP_RELAY_LINK_UNKNOWN_CODE);
        }
        return;
    case MP_RELAY_NACK_RATE_LIMITED:
        if (++link->rate_attempts >= MP_RELAY_RATE_TRIES) {
            mp_relay_link_fail(link, MP_RELAY_LINK_RATE_LIMITED);
        } else {
            attempt_failed(link, MP_RELAY_LINK_RATE_LIMITED, now);
        }
        return;
    case MP_RELAY_NACK_SEAT_HELD:
        patient(link, MP_RELAY_SEAT_HELD_MS, MP_RELAY_LINK_SEAT_HELD, now);
        return;
    case MP_RELAY_NACK_UNKNOWN_INDEX:
        if (link->role == MP_RELAY_LINK_HOST && link->renewing && link->registered_known &&
            !mp_relay_link_leg_alive(link, now)) {
            register_afresh(link, now);
        } else {
            attempt_failed(link, MP_RELAY_LINK_REFUSED, now);
        }
        return;
    case MP_RELAY_NACK_FULL:
        mp_relay_link_fail(link, MP_RELAY_LINK_FULL);
        return;
    case MP_RELAY_NACK_KEY_REJECTED:
        mp_relay_link_fail(link, MP_RELAY_LINK_KEY_REJECTED);
        return;
    case MP_RELAY_NACK_CLOSED:
        mp_relay_link_fail(link, MP_RELAY_LINK_CLOSED);
        return;
    case MP_RELAY_NACK_VERSION:
        mp_relay_link_fail(link, MP_RELAY_LINK_VERSION);
        return;
    default:
        attempt_failed(link, MP_RELAY_LINK_REFUSED, now);
        return;
    }
}

static size_t tick_request(mp_relay_link_t *link, uint32_t now, uint8_t *out,
                           mp_relay_link_event_t *event)
{
    if (link->refused) {
        if (mp_relay_link_reached(now, link->wait_until)) {
            believe_refusal(link, now, event);
        }
        return 0u;
    }
    if (!mp_relay_link_reached(now, link->next_send_at)) {
        return 0u;
    }
    if (link->tries < MP_RELAY_TRIES) {
        ++link->tries;
        link->next_send_at = now + MP_RELAY_TRY_MS;
        memcpy(out, link->request, MP_RELAY_REQUEST_BYTES);
        return MP_RELAY_REQUEST_BYTES;
    }
    if (link->seat_held) {
        link->refusal.reason = (uint8_t)MP_RELAY_NACK_SEAT_HELD;
        believe_refusal(link, now, event);
    } else if (link->unopened) {
        key_refused(link, MP_RELAY_LINK_UNPROVEN, now, event);
    } else {
        attempt_failed(link, MP_RELAY_LINK_NO_ANSWER, now);
    }
    return 0u;
}

size_t mp_relay_link_tick(mp_relay_link_t *link, uint32_t now, uint8_t *out, size_t capacity,
                          mp_relay_link_event_t *event)
{
    if (event != NULL) {
        memset(event, 0, sizeof *event);
    }
    if (out == NULL || capacity < MP_RELAY_REQUEST_BYTES) {
        return 0u;
    }
    switch (link->phase) {
    case MP_RELAY_LINK_BACKOFF:
        if (!mp_relay_link_reached(now, link->backoff_until)) {
            return 0u;
        }
        if (link->retry_cookie) {
            link->retry_cookie = false;
            begin_cookie(link, now);
        } else {
            begin_attempt(link, now);
        }
        return link->phase == MP_RELAY_LINK_COOKIE
                   ? mp_relay_link_tick(link, now, out, capacity, event)
                   : 0u;
    case MP_RELAY_LINK_COOKIE:
        if (!mp_relay_link_reached(now, link->next_send_at)) {
            return 0u;
        }
        if (link->tries >= MP_RELAY_TRIES) {
            attempt_failed(link, MP_RELAY_LINK_NO_ANSWER, now);
            return 0u;
        }
        ++link->tries;
        link->next_send_at = now + MP_RELAY_TRY_MS;
        memcpy(out, link->hello, MP_RELAY_HELLO_BYTES);
        return MP_RELAY_HELLO_BYTES;
    case MP_RELAY_LINK_REQUEST:
        return tick_request(link, now, out, event);
    case MP_RELAY_LINK_READY:
        return mp_relay_link_tick_leg(link, now, out, capacity);
    case MP_RELAY_LINK_IDLE:
    case MP_RELAY_LINK_NEEDS_KEY:
    case MP_RELAY_LINK_FAILED:
    default:
        return 0u;
    }
}

static void take_cookie(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram, size_t bytes)
{
    mp_relay_cookie_t cookie;

    if (link->phase == MP_RELAY_LINK_COOKIE && mp_relay_cookie_decode(datagram, bytes, &cookie) &&
        cookie.nonce == link->nonce && cookie.role == cookie_role(link)) {
        link->cookie = cookie;
        build_request(link, now);
    }
}

/* The body of an answer that opened. False when it does not read. */
static bool take_body(mp_relay_link_t *link, const uint8_t *body, size_t bytes)
{
    bool decoded;

    if (link->role == MP_RELAY_LINK_HOST) {
        mp_relay_registered_body_t registered;

        decoded = mp_relay_registered_body_decode(body, bytes, &registered);
        if (decoded) {
            link->registered       = registered;
            link->registered_known = true;
            memcpy(link->code, registered.code, sizeof link->code);
            if (registered.relay_epoch > link->epoch) {
                link->epoch = registered.relay_epoch;
            }
        }
        mp_relay_wipe(&registered, sizeof registered);
    } else {
        mp_relay_joined_body_t joined;

        decoded = mp_relay_joined_body_decode(body, bytes, &joined);
        if (decoded) {
            link->joined       = joined;
            link->joined_known = true;
        }
        mp_relay_wipe(&joined, sizeof joined);
    }
    return decoded;
}

static bool take_answer(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram, size_t bytes,
                        mp_relay_link_event_t *event)
{
    bool            host   = link->role == MP_RELAY_LINK_HOST;
    uint8_t         wanted = host ? (uint8_t)MP_RELAY_TYPE_REGISTERED
                                  : (uint8_t)MP_RELAY_TYPE_JOINED;
    uint8_t         body[MP_RELAY_REGISTERED_BODY_BYTES];
    size_t          body_bytes = 0u;
    mp_relay_keys_t keys;
    bool            decoded;

    if (link->phase != MP_RELAY_LINK_REQUEST || datagram[0] != wanted) {
        return false;
    }
    /* An answer that does not open is passed over: it may be forged, and the real one may
     * follow. */
    if (!mp_relay_check_control(datagram, bytes, wanted,
                                host ? MP_RELAY_REGISTERED_BYTES : MP_RELAY_JOINED_BYTES) ||
        !mp_relay_nk_read2(&link->nk, datagram + MP_RELAY_ANSWER_EPHEMERAL_OFFSET,
                           bytes - MP_RELAY_ANSWER_EPHEMERAL_OFFSET, body, sizeof body,
                           &body_bytes, &keys)) {
        link->unopened = true;
        ++link->unopened_answers;
        return false;
    }
    decoded = take_body(link, body, body_bytes);
    mp_relay_wipe(body, sizeof body);
    if (!decoded) {
        /* It opened, so the relay sent it: a relay that says something this build cannot read. */
        mp_relay_wipe(&keys, sizeof keys);
        mp_relay_link_fail(link, MP_RELAY_LINK_BAD_ANSWER);
        return false;
    }
    mp_relay_leg_wipe(&link->leg);
    mp_relay_leg_init(&link->leg, &keys);
    if (!host && link->renewing &&
        (link->joined.flags & (uint8_t)MP_RELAY_JOINED_CONTINUED) == 0u) {
        /* The proof did not carry, from another address or for an ended seat, and the relay seated
         * this player anew. The host knows that seat as a stranger's. The new leg is kept for one
         * datagram, the Leave that frees the seat, and the next tick ends the link. */
        mp_relay_wipe(&keys, sizeof keys);
        mp_relay_nk_wipe(&link->nk);
        link->seat_lost = true;
        link->failure   = MP_RELAY_LINK_SEAT_LOST;
        link->phase     = MP_RELAY_LINK_READY;
        link->renewing  = false;
        link->keeping   = false;
        return false;
    }
    mp_relay_wipe(&keys, sizeof keys);
    ++link->handshakes;
    link->phase         = MP_RELAY_LINK_READY;
    link->failure       = MP_RELAY_LINK_FINE;
    link->renewing      = false;
    link->reconnect_due = false;
    link->keeping       = false;
    link->keep_refused  = false;
    link->refused       = false;
    link->refusal_clock = false;
    link->rate_attempts = 0u;
    link->opened_once   = true;
    link->last_opened   = now;
    link->next_keep_at  = now + mp_relay_link_keep_interval(link);
    /* A new session, or a relay that restarted, knows nothing of the listing. */
    link->listing_dirty = link->listing_dirty || (host && link->listed);
    event->kind         = MP_RELAY_LINK_EVENT_READY;
    return true;
}

static void take_open_nack(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram,
                           size_t bytes)
{
    mp_relay_nack_t nack;

    if (!mp_relay_nack_decode(datagram, bytes, &nack)) {
        return;
    }
    ++link->nacks_seen;
    if (link->phase != MP_RELAY_LINK_REQUEST) {
        mp_relay_link_keep_refused(link, now, nack.reason);
    } else if (link->role == MP_RELAY_LINK_MEMBER && link->renewing &&
               nack.reason == MP_RELAY_NACK_SEAT_HELD) {
        link->seat_held = true;   /* a rejoin with its proof sits the whole attempt out */
    } else if (!link->refused) {
        link->refused    = true;
        link->refusal    = nack;
        link->wait_until = now + MP_RELAY_NACK_GRACE_MS;
    }
}

static void take_nudge(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram, size_t bytes)
{
    mp_relay_nudge_t nudge;

    if (link->role == MP_RELAY_LINK_HOST && link->leg.live && link->registered_known &&
        mp_relay_nudge_decode(datagram, bytes, &nudge) &&
        nudge.session_index == link->registered.session_index) {
        ++link->nudges;
        if (link->phase == MP_RELAY_LINK_READY && !link->keeping) {
            link->next_keep_at = now;
        }
    }
}

bool mp_relay_link_receive(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram,
                           size_t bytes, uint8_t *game, size_t game_capacity,
                           mp_relay_link_event_t *event)
{
    if (event == NULL) {
        return false;
    }
    memset(event, 0, sizeof *event);
    if (datagram == NULL || bytes < MP_RELAY_CONTROL_HEADER_BYTES ||
        link->phase == MP_RELAY_LINK_FAILED) {
        return false;
    }
    switch (datagram[0]) {
    case MP_RELAY_TYPE_COOKIE:
        take_cookie(link, now, datagram, bytes);
        return false;
    case MP_RELAY_TYPE_REGISTERED:
    case MP_RELAY_TYPE_JOINED:
        return take_answer(link, now, datagram, bytes, event);
    case MP_RELAY_TYPE_NACK:
        take_open_nack(link, now, datagram, bytes);
        return false;
    case MP_RELAY_TYPE_NUDGE:
        take_nudge(link, now, datagram, bytes);
        return false;
    default:
        return mp_relay_link_take_sealed(link, now, datagram, bytes, game, game_capacity, event);
    }
}

bool mp_relay_link_code(const mp_relay_link_t *link, uint8_t code[MP_RELAY_CODE_BYTES])
{
    bool known = link->role == MP_RELAY_LINK_HOST ? link->registered_known
                                                  : link->role == MP_RELAY_LINK_MEMBER;

    if (known) {
        memcpy(code, link->code, MP_RELAY_CODE_BYTES);
    }
    return known;
}

const char *mp_relay_link_failure_text(mp_relay_link_failure_t failure)
{
    switch (failure) {
    case MP_RELAY_LINK_FINE:         return "no failure";
    case MP_RELAY_LINK_NO_ANSWER:    return "the relay did not answer";
    case MP_RELAY_LINK_UNPROVEN:     return "the relay's answers did not open under its key";
    case MP_RELAY_LINK_FULL:         return "the relay or the session is full (reason 3)";
    case MP_RELAY_LINK_UNKNOWN_CODE: return "no session has this code (reason 4)";
    case MP_RELAY_LINK_KEY_REJECTED: return "the relay rejected the invitation key (reason 5)";
    case MP_RELAY_LINK_RATE_LIMITED: return "the relay is braking this address (reason 7)";
    case MP_RELAY_LINK_REVOKED:      return "the session's access was revoked (reason 8)";
    case MP_RELAY_LINK_CLOSED:       return "the session is closed (reason 9)";
    case MP_RELAY_LINK_VERSION:      return "the relay speaks another protocol version (reason 2)";
    case MP_RELAY_LINK_SEAT_HELD:    return "the seat at this address is held (reason 12)";
    case MP_RELAY_LINK_UNKNOWN_KEY:  return "the relay does not know the key id (reason 10)";
    case MP_RELAY_LINK_REFUSED:      return "the relay refused the attempt";
    case MP_RELAY_LINK_BAD_ANSWER:   return "the relay's answer opened and did not read";
    case MP_RELAY_LINK_SEAT_LOST:    return "a rejoin was given a new seat; the old one is lost";
    case MP_RELAY_LINK_CRYPTO:       return "the handshake could not be built";
    case MP_RELAY_LINK_FAILURE_COUNT:
    default:                         return "an unnamed failure";
    }
}
