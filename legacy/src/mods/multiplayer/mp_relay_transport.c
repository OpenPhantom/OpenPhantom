/* mp_relay_transport.c: a public session's transport. See the header.
 *
 * SIZE NOTE: over 600 lines. One object with four jobs that each hand the next its input: the fetch
 * gives the link a key and the sockets an address, the sockets give the link datagrams, the link
 * gives the seats their notices and the session its packets. Each job is a screen of code; split
 * apart they would share every field of the struct through a header of their own, which is the
 * price mp_relay_link_int.h pays for a seam that is real. This one is not.
 */
#include "mp_relay_transport.h"

#include "mp_relay_crypto.h"
#include "mp_relay_inner.h"
#include "mp_relay_wire.h"
#include "mp_wallclock.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const mp_relay_services_t REAL_SERVICES = {
    mp_relay_fetch_begin,
    mp_relay_fetch_running,
    mp_relay_fetch_take,
    mp_relay_state_load,
    mp_relay_state_save,
    mp_relay_unix_now,
    mp_wallclock_ms,
    mp_relay_random,
};

const mp_relay_services_t *mp_relay_services_real(void)
{
    return &REAL_SERVICES;
}

static bool reached(uint32_t now, uint32_t at)
{
    return now - at < 0x80000000u;
}

static void fail(mp_relay_transport_t *t, const char *why)
{
    if (t->failed) {
        return;
    }
    t->failed = true;
    (void)text_format(t->failure, sizeof t->failure, "%s", why);
    mp_relay_link_fail(&t->link, t->link.failure);
    log_warning("the relay transport gives up: %s", why);
}

/* ---- sending ------------------------------------------------------------------------------- */

static bool send_on(mp_relay_transport_t *t, int family, const uint8_t *data, size_t bytes)
{
    return family >= 0 && mp_relay_socket_send(&t->socket, (mp_relay_family_t)family, data, bytes);
}

/* A Hello begins a race of the families when its nonce is new; IPv4 joins after the head start,
 * or at once when IPv6 cannot reach the relay. */
static void send_hello(mp_relay_transport_t *t, const uint8_t *hello, uint32_t now)
{
    bool ipv6  = mp_relay_socket_usable(&t->socket, MP_RELAY_IPV6);
    bool fresh = t->link.nonce != t->race_nonce || t->hello_family >= 0;

    if (fresh) {
        t->race_nonce     = t->link.nonce;
        t->race_started   = now;
        t->race_ipv4_sent = false;
        t->hello_family   = -1;
        t->race_pinned    = !t->host && t->link.renewing && t->leg_family >= 0 &&
                         t->pinned_attempts < MP_RELAY_PINNED_ATTEMPTS;
        t->pinned_attempts += t->race_pinned ? 1u : 0u;
    }
    memcpy(t->hello, hello, sizeof t->hello);
    if (t->race_pinned) {
        t->race_ipv4_sent = true;   /* no race: the new leg asks where the old one stands */
        (void)send_on(t, t->leg_family, hello, MP_RELAY_HELLO_BYTES);
        return;
    }
    if (ipv6) {
        (void)send_on(t, MP_RELAY_IPV6, hello, MP_RELAY_HELLO_BYTES);
    }
    if (!ipv6 || now - t->race_started >= MP_RELAY_FAMILY_HEADSTART_MS) {
        t->race_ipv4_sent = send_on(t, MP_RELAY_IPV4, hello, MP_RELAY_HELLO_BYTES) ||
                            t->race_ipv4_sent;
    }
}

static void send_datagram(mp_relay_transport_t *t, const uint8_t *data, size_t bytes,
                          uint32_t now)
{
    switch (data[0]) {
    case MP_RELAY_TYPE_HELLO:
        send_hello(t, data, now);
        return;
    case MP_RELAY_TYPE_REGISTER:
    case MP_RELAY_TYPE_JOIN:
        (void)send_on(t, t->hello_family, data, bytes);
        return;
    default:
        /* The Leave for a seat the relay gave a rejoin instead of the old one leaves from where
         * that rejoin came, the only address the new seat answers. */
        (void)send_on(t, t->link.seat_lost || t->leg_family < 0 ? t->hello_family : t->leg_family,
                      data, bytes);
        return;
    }
}

/* ---- what the link says -------------------------------------------------------------------- */

static void tell_code(mp_relay_transport_t *t)
{
    char    text[MP_RELAY_CODE_TEXT_BYTES];
    uint8_t code[MP_RELAY_CODE_BYTES];

    if (!mp_relay_link_code(&t->link, code)) {
        return;
    }
    mp_relay_code_text(code, text);
    if (t->handshakes_told == 0u) {
        log_info("the relay registered this session: code %s, %u seat(s), refreshed every %u ms, "
                 "over %s", text, (unsigned)t->link.seats,
                 (unsigned)t->link.registered.refresh_ms,
                 mp_relay_family_name((mp_relay_family_t)t->leg_family));
    } else if (memcmp(code, t->code_told, sizeof code) != 0) {
        log_warning("the relay no longer knew this session and registered it afresh: the code is "
                    "now %s, and every player has to join again with it", text);
        mp_relay_seats_close_all(&t->seats);
    }
    memcpy(t->code_told, code, sizeof code);
}

static void on_ready(mp_relay_transport_t *t)
{
    t->leg_family      = t->hello_family;
    t->pinned_attempts = 0u;
    t->key_given_up    = false;
    if (!t->failed) {
        t->failure[0] = '\0';
    }
    if (t->host) {
        tell_code(t);
    } else if (t->handshakes_told == 0u) {
        log_info("the relay seated this player: seat index %u, generation %u, refreshed every "
                 "%u ms, over %s", (unsigned)t->link.joined.member_index,
                 (unsigned)t->link.joined.gen, (unsigned)t->link.joined.member_refresh_ms,
                 mp_relay_family_name((mp_relay_family_t)t->leg_family));
    }
    if (t->handshakes_told != 0u) {
        log_info("a new leg to the relay stands (handshake %u, over %s); the session goes on",
                 (unsigned)t->link.handshakes,
                 mp_relay_family_name((mp_relay_family_t)t->leg_family));
    }
    t->handshakes_told = t->link.handshakes;
}

static void on_member(mp_relay_transport_t *t, const mp_relay_link_event_t *event)
{
    uint32_t endpoint;

    if (!t->host) {
        return;
    }
    if (event->kind == MP_RELAY_LINK_EVENT_MEMBER_OPEN) {
        uint32_t continued = t->seats.continued;

        endpoint = mp_relay_seats_open(&t->seats, event->slot, event->gen, event->flags, event->r,
                                       t->link.handshakes, event->counter);
        if (endpoint == 0u) {
            log_warning("the relay seated a player on slot %u (generation %u) and it is not taken: "
                        "%s", (unsigned)event->slot, (unsigned)event->gen,
                        t->seats.full != 0u ? "no endpoint is left" : "the notice is an old one");
            return;
        }
        log_info("the relay seated a player on slot %u (generation %u) as endpoint %u%s",
                 (unsigned)event->slot, (unsigned)event->gen, (unsigned)endpoint,
                 t->seats.continued != continued ? ", continuing the seat it had" : "");
        return;
    }
    endpoint = mp_relay_seats_close(&t->seats, event->slot, event->gen, t->link.handshakes,
                                    event->counter);
    log_info("the relay says the player on slot %u (generation %u) is gone (%s)%s",
             (unsigned)event->slot, (unsigned)event->gen,
             event->reason == MP_RELAY_CLOSE_LEFT      ? "left"
             : event->reason == MP_RELAY_CLOSE_TIMEOUT ? "timed out"
             : event->reason == MP_RELAY_CLOSE_KICKED  ? "kicked"
                                                       : "no reason given",
             endpoint == 0u ? "; no open seat had it" : "");
}

static void on_event(mp_relay_transport_t *t, const mp_relay_link_event_t *event)
{
    switch (event->kind) {
    case MP_RELAY_LINK_EVENT_READY:
        on_ready(t);
        return;
    case MP_RELAY_LINK_EVENT_MEMBER_OPEN:
    case MP_RELAY_LINK_EVENT_MEMBER_CLOSED:
        on_member(t, event);
        return;
    case MP_RELAY_LINK_EVENT_KEY_REFUSED:
        t->refetch          = true;
        t->refetch_unproven = event->unproven;
        t->refused_key      = t->link.key;
        log_warning("the relay refused its key (%s): a new one is fetched when the rules allow",
                    mp_relay_link_failure_text(t->link.failure));
        return;
    case MP_RELAY_LINK_EVENT_NONE:
    case MP_RELAY_LINK_EVENT_GAME:
    default:
        return;
    }
}

/* What the log is told when the link changes phase: an attempt that failed, and a give-up. */
static void tell_phase(mp_relay_transport_t *t)
{
    mp_relay_link_phase_t phase = t->link.phase;

    if (phase == t->phase_told) {
        return;
    }
    if (phase == MP_RELAY_LINK_BACKOFF) {
        ++t->attempts_failed;
        log_warning("an attempt to reach the relay failed (%s); the next follows",
                    mp_relay_link_failure_text(t->link.failure));
    } else if (phase == MP_RELAY_LINK_FAILED && !t->failed) {
        log_warning("the relay link gives up: %s", mp_relay_link_failure_text(t->link.failure));
    }
    t->phase_told = phase;
}

static void pump_out(mp_relay_transport_t *t)
{
    uint32_t              now = t->services->now_ms();
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    mp_relay_link_event_t event;
    int                   i;

    for (i = 0; i < 8; ++i) {
        size_t bytes = mp_relay_link_tick(&t->link, now, out, sizeof out, &event);

        on_event(t, &event);
        if (bytes == 0u) {
            break;
        }
        send_datagram(t, out, bytes, now);
    }
    if (t->link.phase == MP_RELAY_LINK_COOKIE && t->hello_family < 0 && !t->race_ipv4_sent &&
        now - t->race_started >= MP_RELAY_FAMILY_HEADSTART_MS) {
        t->race_ipv4_sent = send_on(t, MP_RELAY_IPV4, t->hello, sizeof t->hello);
    }
    tell_phase(t);
}

/* ---- the lookup and the key ---------------------------------------------------------------- */

/* A key the relay keeps refusing, or one that could not be fetched again. A link with a leg in use
 * goes on with that leg and the key it has, as the reference client's Keep does; one without a leg
 * has nothing left to try. */
static void give_up_on_key(mp_relay_transport_t *t, const char *why, uint32_t now)
{
    t->refetch         = false;
    t->refetch_granted = false;
    if (!mp_relay_link_ready(&t->link) || !mp_relay_link_leg_carries(&t->link, now)) {
        fail(t, why);
        return;
    }
    if (!t->key_given_up) {
        t->key_given_up = true;
        log_warning("%s; the leg in use goes on", why);
    }
    mp_relay_link_give_key(&t->link, t->keys.have ? &t->keys.key : &t->refused_key, now);
}

static void give_key(mp_relay_transport_t *t, uint32_t now)
{
    if (t->refetch) {
        t->refetch = false;
        if (t->refetch_unproven && memcmp(&t->keys.key, &t->refused_key, sizeof t->keys.key) == 0) {
            give_up_on_key(t, "the relay's answers never opened under its published key, and the "
                              "key fetched again is the same one", now);
            return;
        }
    }
    mp_relay_link_give_key(&t->link, &t->keys.key, now);
}

/* What went wrong with a fetch, in words: for a refused document, the parser's own reason. */
static void fetch_words(const mp_relay_fetch_result_t *result, char *out, size_t size)
{
    if (result->error == MP_RELAY_FETCH_ERROR_DOCUMENT) {
        (void)text_format(out, size, "%s: %s", mp_relay_fetch_error_text(result->error),
                          mp_relay_keydoc_result_text((mp_relay_keydoc_result_t)result->detail));
    } else {
        (void)text_format(out, size, "%s (detail %d)", mp_relay_fetch_error_text(result->error),
                          result->detail);
    }
}

static void take_key(mp_relay_transport_t *t, const mp_relay_fetch_result_t *result, uint32_t now)
{
    char                 words[96];
    int64_t              unix_now = t->services->unix_now();
    mp_relay_fetch_end_t end = mp_relay_keysource_fetch_ended(&t->keys, result->key_fetched,
                                                              &result->key, unix_now);
    uint32_t             retry;

    switch (end) {
    case MP_RELAY_FETCH_FRESH:
        (void)t->services->state_save(&t->keys.key, unix_now);
        log_info("the relay key: id %u fetched over TLS 1.3 in %u ms, over %s",
                 (unsigned)t->keys.key.id, (unsigned)result->elapsed_ms,
                 result->over_ipv6 ? "IPv6" : "IPv4");
        give_key(t, now);
        return;
    case MP_RELAY_FETCH_STAND_IN:
        fetch_words(result, words, sizeof words);
        log_warning("the relay key could not be fetched (%s); the key kept on disk (id %u) "
                    "stands in", words, (unsigned)t->keys.key.id);
        give_key(t, now);
        return;
    case MP_RELAY_FETCH_FAILED:
    case MP_RELAY_FETCH_FUTURE:
    case MP_RELAY_FETCH_TOO_OLD:
    default:
        break;
    }
    if (t->refetch) {
        give_up_on_key(t, "the relay refused its key, and a new one could not be fetched", now);
        return;
    }
    fetch_words(result, words, sizeof words);
    (void)text_format(t->failure, sizeof t->failure, "the relay key could not be fetched: %s%s",
                      words,
                      end == MP_RELAY_FETCH_FUTURE    ? ", and the kept key is dated in the future"
                      : end == MP_RELAY_FETCH_TOO_OLD ? ", and the kept key is over seven days old"
                                                      : "");
    retry = mp_relay_link_ready(&t->link) ? MP_RELAY_FETCH_RETRY_READY_MS : MP_RELAY_FETCH_RETRY_MS;
    log_warning("%s; the next try in %u s", t->failure, (unsigned)(retry / 1000u));
    t->next_fetch_at = now + retry;
}

static void take_fetch(mp_relay_transport_t *t, const mp_relay_fetch_result_t *result,
                       uint32_t now)
{
    if (result->addresses != 0u) {
        size_t i;
        bool   ipv6 = false;
        bool   ipv4 = false;

        mp_relay_socket_set_relay(&t->socket, result->address, result->addresses);
        for (i = 0; i < result->addresses; ++i) {
            ipv6 = ipv6 || result->address[i].ipv6;
            ipv4 = ipv4 || !result->address[i].ipv6;
        }
        if (!t->addresses_known) {
            log_info("the relay's addresses: %u found (IPv6 %s, IPv4 %s); sockets IPv6 %s, IPv4 %s",
                     (unsigned)result->addresses, ipv6 ? "yes" : "no", ipv4 ? "yes" : "no",
                     t->socket.family[MP_RELAY_IPV6].open ? "open" : "not open",
                     t->socket.family[MP_RELAY_IPV4].open ? "open" : "not open");
        }
        t->addresses_known = true;
        if (!t->failed) {
            t->failure[0] = '\0';
        }
    } else if (!t->addresses_known) {
        (void)text_format(t->failure, sizeof t->failure, "the relay could not be looked up: %s",
                          mp_relay_fetch_error_text(result->error));
        log_warning("%s; the next try in %u s", t->failure,
                    (unsigned)(MP_RELAY_LOOKUP_RETRY_MS / 1000u));
        t->next_fetch_at = now + MP_RELAY_LOOKUP_RETRY_MS;
        return;
    }
    if (t->fetch_wants_key && result->wanted_key) {
        take_key(t, result, now);
    }
}

/* True when a fetch of this transport's is out. */
static bool begin_fetch(mp_relay_transport_t *t, bool want_key, uint32_t now)
{
    mp_relay_fetch_result_t stale;

    if (t->services->fetch_begin(want_key)) {
        t->fetching        = true;
        t->fetch_wants_key = want_key;
        if (want_key) {
            mp_relay_keysource_fetch_began(&t->keys, t->services->unix_now());
        }
        return true;
    }
    /* Refused: another part of the program has a fetch out, or one finished that nobody took.
     * The second waits for nobody, and its addresses are as good as any. */
    if (!t->services->fetch_running() && t->services->fetch_take(&stale)) {
        t->fetch_wants_key = false;
        take_fetch(t, &stale, now);
        return false;
    }
    t->next_fetch_at = now + MP_RELAY_FETCH_BUSY_MS;
    return false;
}

/* What the key and the lookup need next. */
static void service_fetch(mp_relay_transport_t *t, uint32_t now)
{
    mp_relay_fetch_result_t result;
    int64_t                 unix_now = t->services->unix_now();
    bool                    key_due  = mp_relay_keysource_step(&t->keys, unix_now) ==
                                       MP_RELAY_KEY_FETCH;

    if (t->fetching) {
        /* Asked before the take: a fetch that finishes between the two is still in its slot. */
        bool running = t->services->fetch_running();

        if (t->services->fetch_take(&result)) {
            t->fetching = false;
            take_fetch(t, &result, now);
        } else if (!running) {
            t->fetching = false;   /* it finished, and another part of the program took it */
        }
        return;
    }
    if (t->failed || t->link.phase == MP_RELAY_LINK_FAILED || !reached(now, t->next_fetch_at)) {
        return;
    }
    if (t->refetch) {
        /* The key source counts a refetch when it allows one, so it is asked once and the fetch
         * begun as often as it takes to get one out. */
        if (!t->refetch_granted) {
            switch (mp_relay_keysource_refetch(&t->keys, unix_now)) {
            case MP_RELAY_REFETCH_GO:
                t->refetch_granted = true;
                break;
            case MP_RELAY_REFETCH_SPENT:
                give_up_on_key(t, "the relay refused its key, and three new fetches since the "
                                  "start did not change that", now);
                return;
            case MP_RELAY_REFETCH_HOLD:
            case MP_RELAY_REFETCH_NONE:
            default:
                return;   /* a fetch a moment ago: this one waits for its minute */
            }
        }
        if (begin_fetch(t, true, now)) {
            t->refetch_granted = false;
        }
        return;
    }
    if (!t->addresses_known) {
        (void)begin_fetch(t, t->link.phase == MP_RELAY_LINK_NEEDS_KEY && key_due, now);
        return;
    }
    if (t->link.phase == MP_RELAY_LINK_NEEDS_KEY || !t->link.have_key) {
        if (key_due) {
            (void)begin_fetch(t, true, now);
        } else {
            mp_relay_link_give_key(&t->link, &t->keys.key, now);
        }
        return;
    }
    if (key_due && t->link.phase == MP_RELAY_LINK_READY) {
        (void)begin_fetch(t, true, now);   /* over an hour old: the next handshake gets a new one */
    }
}

/* ---- the vtable ---------------------------------------------------------------------------- */

static bool relay_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    mp_relay_transport_t *t    = (mp_relay_transport_t *)context;
    uint8_t               slot = 0u;
    uint16_t              gen  = 0u;
    size_t                sealed;

    if (t == NULL || !t->up || !mp_relay_link_ready(&t->link) ||
        (t->host ? !mp_relay_seats_to(&t->seats, endpoint, &slot, &gen) : endpoint != 1u)) {
        if (t != NULL) {
            ++t->game_unsent;
        }
        return false;
    }
    sealed = mp_relay_link_seal_game(&t->link, slot, gen, (const uint8_t *)packet, bytes,
                                     t->datagram, sizeof t->datagram);
    if (sealed == 0u || !send_on(t, t->leg_family, t->datagram, sealed)) {
        ++t->game_unsent;
        return false;
    }
    ++t->game_out;
    return true;
}

static size_t relay_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    mp_relay_transport_t *t     = (mp_relay_transport_t *)context;
    uint32_t              taken;

    if (t == NULL || !t->up) {
        return 0u;
    }
    if (!t->draining) {
        service_fetch(t, t->services->now_ms());
        t->draining = true;
    }
    for (taken = 0u; taken < MP_RELAY_READ_BUDGET; ++taken) {
        mp_relay_link_event_t event;
        mp_relay_link_phase_t before = t->link.phase;
        int                   family;
        size_t                bytes = 0u;

        for (family = 0; family < (int)MP_RELAY_FAMILIES && bytes == 0u; ++family) {
            bytes = mp_relay_socket_recv(&t->socket, (mp_relay_family_t)family, t->datagram,
                                         sizeof t->datagram);
        }
        if (bytes == 0u) {
            break;
        }
        --family;
        (void)mp_relay_link_receive(&t->link, t->services->now_ms(), t->datagram, bytes,
                                    (uint8_t *)buffer, capacity, &event);
        if (before == MP_RELAY_LINK_COOKIE && t->link.phase == MP_RELAY_LINK_REQUEST) {
            t->hello_family = family;   /* the first cookie decides */
        }
        on_event(t, &event);
        if (event.kind == MP_RELAY_LINK_EVENT_GAME) {
            uint32_t endpoint = t->host ? mp_relay_seats_from(&t->seats, event.slot, event.gen)
                                        : 1u;

            if (endpoint != 0u) {
                if (from != NULL) {
                    *from = endpoint;
                }
                ++t->game_in;
                return event.bytes;
            }
        }
    }
    t->draining = false;
    pump_out(t);
    return 0u;
}

static void relay_hold(void *context, uint32_t endpoint, bool held)
{
    mp_relay_transport_t *t = (mp_relay_transport_t *)context;

    if (t != NULL && t->host) {
        mp_relay_seats_hold(&t->seats, endpoint, held);
    }
}

static uint64_t relay_address(void *context, uint32_t endpoint)
{
    const mp_relay_transport_t *t = (const mp_relay_transport_t *)context;

    if (t == NULL) {
        return 0u;
    }
    return t->host ? mp_relay_seats_address(&t->seats, endpoint) : (endpoint == 1u ? 1u : 0u);
}

mp_transport_t mp_relay_transport_face(mp_relay_transport_t *t)
{
    mp_transport_t face;

    face.context = t;
    face.send    = relay_send;
    face.recv    = relay_recv;
    face.hold    = relay_hold;
    face.address = relay_address;
    return face;
}

/* ---- starting and ending ------------------------------------------------------------------- */

static bool start(mp_relay_transport_t *t, const mp_relay_services_t *services, bool host)
{
    mp_relay_key_t kept;
    int64_t        kept_at = 0;
    const char    *broken;

    mp_relay_wipe(t, sizeof *t);
    t->services     = services;
    t->host         = host;
    t->hello_family = -1;
    t->leg_family   = -1;
    broken          = mp_relay_crypto_self_test();
    if (broken != NULL) {
        (void)text_format(t->failure, sizeof t->failure,
                          "the relay's cryptography failed its own test (%s)", broken);
        t->failed = true;
        log_error("the relay transport cannot start: %s", t->failure);
        return false;
    }
    if (!mp_relay_socket_open(&t->socket)) {
        (void)text_format(t->failure, sizeof t->failure,
                          "no UDP socket to the relay would open (error %d)", t->socket.last_error);
        t->failed = true;
        log_warning("the relay transport cannot start: %s", t->failure);
        return false;
    }
    mp_relay_keysource_init(&t->keys);
    if (services->state_load(&kept, &kept_at)) {
        mp_relay_keysource_set_cache(&t->keys, &kept, kept_at);
    }
    mp_relay_link_init(&t->link, services->random);
    mp_relay_seats_init(&t->seats);
    t->next_fetch_at = services->now_ms();
    t->up            = true;
    return true;
}

bool mp_relay_transport_start_host(mp_relay_transport_t *t, const mp_relay_services_t *services,
                                   uint8_t seats)
{
    if (!start(t, services, true)) {
        return false;
    }
    mp_relay_link_host(&t->link, seats);
    t->phase_told = t->link.phase;
    log_info("the transport: PUBLIC, through the relay %s as host of %u seat(s); the LAN socket "
             "and the LAN announce are not used", MP_RELAY_HOST, (unsigned)seats);
    return true;
}

bool mp_relay_transport_start_join(mp_relay_transport_t *t, const mp_relay_services_t *services,
                                   const uint8_t code[MP_RELAY_CODE_BYTES])
{
    char text[MP_RELAY_CODE_TEXT_BYTES];

    if (!start(t, services, false)) {
        return false;
    }
    mp_relay_link_join(&t->link, code);
    t->phase_told = t->link.phase;
    mp_relay_code_text(code, text);
    log_info("the transport: PUBLIC, through the relay %s to the session %s; the LAN socket is "
             "not used", MP_RELAY_HOST, text);
    return true;
}

void mp_relay_transport_pump(mp_relay_transport_t *t)
{
    if (t == NULL || !t->up || t->draining) {
        return;
    }
    service_fetch(t, t->services->now_ms());
    pump_out(t);
}

void mp_relay_transport_set_listing(mp_relay_transport_t *t, bool listed,
                                    const uint8_t announce[MP_RELAY_ANNOUNCE_BYTES])
{
    if (t != NULL && t->up) {
        mp_relay_link_set_listing(&t->link, listed, announce);
    }
}

mp_relay_state_t mp_relay_transport_state(const mp_relay_transport_t *t)
{
    if (t == NULL || (!t->up && !t->failed)) {
        return MP_RELAY_STATE_OFF;
    }
    if (t->failed || t->link.phase == MP_RELAY_LINK_FAILED) {
        return MP_RELAY_STATE_FAILED;
    }
    if (mp_relay_link_ready(&t->link)) {
        /* A leg in name only, with a new one under way, carries nothing a player could use. */
        return t->link.renewing && !mp_relay_link_leg_carries(&t->link, t->services->now_ms())
                   ? MP_RELAY_STATE_CONNECTING
                   : MP_RELAY_STATE_READY;
    }
    switch (t->link.phase) {
    case MP_RELAY_LINK_COOKIE:
    case MP_RELAY_LINK_REQUEST:
        return MP_RELAY_STATE_CONNECTING;
    case MP_RELAY_LINK_BACKOFF:
        return MP_RELAY_STATE_WAITING;
    case MP_RELAY_LINK_IDLE:
    case MP_RELAY_LINK_NEEDS_KEY:
    case MP_RELAY_LINK_READY:
    case MP_RELAY_LINK_FAILED:
    default:
        return t->failure[0] != '\0' ? MP_RELAY_STATE_WAITING : MP_RELAY_STATE_LOOKING_UP;
    }
}

bool mp_relay_transport_code(const mp_relay_transport_t *t, uint8_t code[MP_RELAY_CODE_BYTES])
{
    return t != NULL && (t->up || t->failed) && mp_relay_link_code(&t->link, code);
}

const char *mp_relay_transport_failure(const mp_relay_transport_t *t)
{
    if (t == NULL) {
        return "";
    }
    if (t->failed || t->failure[0] != '\0') {
        return t->failure;
    }
    if (t->link.phase == MP_RELAY_LINK_FAILED || t->link.phase == MP_RELAY_LINK_BACKOFF) {
        return mp_relay_link_failure_text(t->link.failure);
    }
    return "";
}

void mp_relay_transport_farewell(mp_relay_transport_t *t)
{
    uint8_t out[MP_RELAY_DATAGRAM_MAX];
    size_t  bytes;

    if (t == NULL || !t->up) {
        return;
    }
    bytes = mp_relay_link_farewell(&t->link, out, sizeof out);
    if (bytes != 0u && send_on(t, t->leg_family, out, bytes)) {
        log_info("the relay was told goodbye: %s", t->host ? "the session is closed"
                                                             : "this player left the session");
    }
}

void mp_relay_transport_close(mp_relay_transport_t *t)
{
    if (t == NULL || !t->up) {
        return;
    }
    mp_relay_socket_close(&t->socket);
    mp_relay_wipe(&t->link, sizeof t->link);
    t->up       = false;
    t->draining = false;
}

void mp_relay_transport_report(const mp_relay_transport_t *t)
{
    const mp_relay_family_socket_t *v6;
    const mp_relay_family_socket_t *v4;

    if (t == NULL || (!t->up && !t->failed)) {
        return;
    }
    v6 = &t->socket.family[MP_RELAY_IPV6];
    v4 = &t->socket.family[MP_RELAY_IPV4];
    log_info("  the relay transport: %s, %s; game packets %u in, %u out, %u not sent",
             t->host ? "host" : "player", mp_relay_state_name(mp_relay_transport_state(t)),
             (unsigned)t->game_in, (unsigned)t->game_out, (unsigned)t->game_unsent);
    log_info("  the relay link: %u handshake(s), %u new leg(s), %u attempt(s) failed, refreshes %u "
             "sent and %u answered, %u answer(s) that did not open, %u sealed packet(s) refused, "
             "%u nack(s), %u nudge(s)", (unsigned)t->link.handshakes, (unsigned)t->link.renewals,
             (unsigned)t->attempts_failed, (unsigned)t->link.refreshes_sent,
             (unsigned)t->link.refreshes_answered, (unsigned)t->link.unopened_answers,
             (unsigned)t->link.data_refused, (unsigned)t->link.nacks_seen,
             (unsigned)t->link.nudges);
    log_info("  the relay sockets: IPv6 %u sent, %u failed, %u in, %u foreign; IPv4 %u sent, %u "
             "failed, %u in, %u foreign; the leg over %s", (unsigned)v6->sent,
             (unsigned)v6->send_failures, (unsigned)v6->received, (unsigned)v6->foreign,
             (unsigned)v4->sent, (unsigned)v4->send_failures, (unsigned)v4->received,
             (unsigned)v4->foreign, mp_relay_family_name((mp_relay_family_t)t->leg_family));
    if (t->host) {
        log_info("  the relay seats: %u open, %u opened, %u continued, %u closed, %u stale "
                 "notice(s), %u with no endpoint left, %u packet(s) from no seat, %u send(s) to "
                 "no seat", (unsigned)mp_relay_seats_open_count(&t->seats),
                 (unsigned)t->seats.opened, (unsigned)t->seats.continued,
                 (unsigned)t->seats.closed, (unsigned)t->seats.stale, (unsigned)t->seats.full,
                 (unsigned)t->seats.unknown_data, (unsigned)t->seats.refused_sends);
    }
    if (mp_relay_transport_failure(t)[0] != '\0') {
        log_info("  the relay's last failure: %s", mp_relay_transport_failure(t));
    }
}

const char *mp_relay_state_name(mp_relay_state_t state)
{
    switch (state) {
    case MP_RELAY_STATE_OFF:        return "off";
    case MP_RELAY_STATE_LOOKING_UP: return "looking the relay up";
    case MP_RELAY_STATE_CONNECTING: return "connecting";
    case MP_RELAY_STATE_READY:      return "ready";
    case MP_RELAY_STATE_WAITING:    return "waiting to try again";
    case MP_RELAY_STATE_FAILED:     return "failed";
    default:                        return "an unnamed state";
    }
}
