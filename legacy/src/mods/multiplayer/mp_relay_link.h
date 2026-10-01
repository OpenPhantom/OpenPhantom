/* mp_relay_link.h: one PC's link to the relay, as a host or as a player: the handshake, the leg it
 * leaves, keeping the leg, and a new leg when the old one is gone.
 *
 * Layer 1, pure. No socket, no clock, no thread: the transport hands in the time in milliseconds
 * and every datagram from the relay's address, and sends whatever the link hands out. The rules are
 * the relay's reference client's (internal/client/client.go), turned from a client that waits on
 * its socket into one that is asked every few milliseconds what is due:
 *
 *   - Hello up to four times a second apart; a Cookie counts only with this Hello's nonce and role.
 *   - The Register or Join up to four times a second apart, the same bytes each time, so the relay
 *     answers a repeat with the same answer and no second key set exists. An answer that does not
 *     open is passed over and the wait goes on; an open Nack is believed only if nothing opens
 *     within 250 ms after it. Nack 6 asks for a new cookie at most twice, 200 ms apart.
 *   - A host refreshes every RefreshMS and a player every MemberRefreshMS, each round up to three
 *     tries three seconds apart, each sealed under a new counter. A new leg follows three
 *     unanswered tries, a sealed Nack 11, an open Nack 1 after three seconds with nothing opened,
 *     or 2^28 packets sealed, and never more than one new leg in five seconds. The old leg carries
 *     until the new answer opens.
 *   - A host that is nudged refreshes at once and takes no epoch from the nudge.
 *   - A refresh round ends on its answer, on a sealed Nack, or on an open Nack nothing overturned
 *     within 250 ms; only an open Nack 1 with the leg quiet for three seconds calls for a new leg.
 *
 * What the relay's reference client leaves to its caller is decided here: a Nack 3, 5, 9 or 2
 * ends the link; a Nack 7 or 12 is tried again after five seconds for a while; a Nack 4 ends a
 * fresh join and is tried again for a minute on a rejoin, because the host may not have come back
 * yet after a relay restart; a Nack 1 on a resume registers afresh, under a new code, unless the
 * old leg still carries, in which case the Nack is one anybody could have sent. A new leg that
 * fails while the old one stands leaves the old one in use and tries again at the next keep, and so
 * does one whose key the relay refused. A rejoin answered with a new seat instead of the old one
 * leaves that seat and ends the link: the host would see a stranger. A host's listing goes out as
 * soon as it changes, and again on every new leg.
 */
#ifndef MULTIPLAYER_MP_RELAY_LINK_H
#define MULTIPLAYER_MP_RELAY_LINK_H

#include "mp_relay_keydoc.h"
#include "mp_relay_noise.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_TRY_MS             1000u
#define MP_RELAY_TRIES              4u
#define MP_RELAY_NACK_GRACE_MS      250u
#define MP_RELAY_COOKIE_RETRIES     2u
#define MP_RELAY_COOKIE_RETRY_MS    200u
#define MP_RELAY_KEEP_TRY_MS        3000u
#define MP_RELAY_KEEP_TRIES         3u
#define MP_RELAY_QUIET_GAP_MS       3000u
#define MP_RELAY_RECONNECT_GAP_MS   5000u
#define MP_RELAY_BACKOFF_MS         5000u
#define MP_RELAY_RATE_TRIES         3u
#define MP_RELAY_SEAT_HELD_MS       45000u
#define MP_RELAY_REJOIN_PATIENCE_MS 60000u
#define MP_RELAY_LEG_KEY_LIMIT      (1ull << 28)

/* The refresh intervals a Registered or Joined body may name, clamped into this range. */
#define MP_RELAY_KEEP_MIN_MS 1000u
#define MP_RELAY_KEEP_MAX_MS 60000u

typedef enum mp_relay_link_role {
    MP_RELAY_LINK_HOST = 1,
    MP_RELAY_LINK_MEMBER = 2
} mp_relay_link_role_t;

typedef enum mp_relay_link_phase {
    MP_RELAY_LINK_IDLE = 0,     /* nothing asked of it */
    MP_RELAY_LINK_NEEDS_KEY,    /* a handshake is wanted and waits for the relay's key */
    MP_RELAY_LINK_COOKIE,       /* Hellos are out */
    MP_RELAY_LINK_REQUEST,      /* the Register or Join is out */
    MP_RELAY_LINK_BACKOFF,      /* waiting before the next attempt */
    MP_RELAY_LINK_READY,        /* a leg stands */
    MP_RELAY_LINK_FAILED        /* given up; `failure` says why */
} mp_relay_link_phase_t;

typedef enum mp_relay_link_failure {
    MP_RELAY_LINK_FINE = 0,
    MP_RELAY_LINK_NO_ANSWER,      /* the last attempt went unanswered; the link tries again */
    MP_RELAY_LINK_UNPROVEN,       /* answers came and none opened under the published key */
    MP_RELAY_LINK_FULL,           /* Nack 3 */
    MP_RELAY_LINK_UNKNOWN_CODE,   /* Nack 4: no session has this code */
    MP_RELAY_LINK_KEY_REJECTED,   /* Nack 5 */
    MP_RELAY_LINK_RATE_LIMITED,   /* Nack 7, three times */
    MP_RELAY_LINK_REVOKED,        /* sealed Nack 8 */
    MP_RELAY_LINK_CLOSED,         /* Nack 9 */
    MP_RELAY_LINK_VERSION,        /* Nack 2 */
    MP_RELAY_LINK_SEAT_HELD,      /* Nack 12 for 45 seconds */
    MP_RELAY_LINK_UNKNOWN_KEY,    /* Nack 10: the relay does not know the key id */
    MP_RELAY_LINK_REFUSED,        /* another open Nack; `refusal` holds it */
    MP_RELAY_LINK_BAD_ANSWER,     /* an answer opened and its body did not read */
    MP_RELAY_LINK_SEAT_LOST,      /* a rejoin was answered with a new seat, not the old one */
    MP_RELAY_LINK_CRYPTO,         /* no randomness, or a handshake that could not be built */
    MP_RELAY_LINK_FAILURE_COUNT
} mp_relay_link_failure_t;

typedef enum mp_relay_link_event_kind {
    MP_RELAY_LINK_EVENT_NONE = 0,
    MP_RELAY_LINK_EVENT_GAME,          /* a game packet opened; host: from `slot` and `gen` */
    MP_RELAY_LINK_EVENT_MEMBER_OPEN,   /* host: a player took `slot` under `gen`, seat value `r` */
    MP_RELAY_LINK_EVENT_MEMBER_CLOSED, /* host: the player on `slot` and `gen` is gone */
    MP_RELAY_LINK_EVENT_READY,         /* a handshake finished, the first one or a new leg */
    MP_RELAY_LINK_EVENT_KEY_REFUSED    /* the relay refused the key; the link waits for one */
} mp_relay_link_event_kind_t;

typedef struct mp_relay_link_event {
    mp_relay_link_event_kind_t kind;
    uint8_t                    slot;
    uint16_t                   gen;
    uint8_t                    flags;     /* MemberOpen's flags */
    uint8_t                    reason;    /* MemberClosed's reason */
    uint64_t                   counter;   /* the sealed counter the notice arrived under */
    uint8_t                    r[MP_RELAY_SEAT_VALUE_BYTES];
    size_t                     bytes;     /* a game packet's size, in the caller's buffer */
    bool                       unproven;  /* KEY_REFUSED because answers never opened */
} mp_relay_link_event_t;

typedef bool (*mp_relay_random_fn)(uint8_t *out, size_t bytes);

typedef struct mp_relay_link {
    mp_relay_link_role_t    role;
    mp_relay_link_phase_t   phase;
    mp_relay_link_failure_t failure;
    mp_relay_random_fn      random;

    bool           have_key;
    mp_relay_key_t key;

    /* The handshake under way. */
    bool              renewing;       /* a leg stands and the handshake replaces it */
    uint64_t          nonce;
    uint8_t           hello[MP_RELAY_HELLO_BYTES];
    mp_relay_cookie_t cookie;
    uint32_t          cookie_retries;
    uint8_t           request[MP_RELAY_REQUEST_BYTES];
    mp_relay_nk_t     nk;
    uint32_t          tries;
    uint32_t          next_send_at;
    uint32_t          wait_until;
    bool              unopened;
    bool              refused;
    mp_relay_nack_t   refusal;
    bool              seat_held;
    uint32_t          backoff_until;
    uint32_t          rate_attempts;
    uint32_t          first_refusal_at;
    bool              refusal_clock;
    mp_relay_link_failure_t patience_for;   /* the refusal the clock above is running for */
    bool              retry_cookie;   /* the backoff ends in a new cookie for the same attempt */

    /* What the session is. */
    uint8_t                    seats;
    uint8_t                    code[MP_RELAY_CODE_BYTES];
    bool                       registered_known;
    mp_relay_registered_body_t registered;
    bool                       joined_known;
    mp_relay_joined_body_t     joined;
    uint32_t                   epoch;

    /* The leg and keeping it. */
    mp_relay_leg_t leg;
    bool           opened_once;
    uint32_t       last_opened;
    bool           reconnected_once;
    uint32_t       last_reconnect;
    bool           reconnect_due;     /* a sealed Nack 11 arrived */
    uint32_t       next_keep_at;
    bool           keeping;
    uint32_t       keep_tries;
    uint32_t       keep_next_try_at;
    uint64_t       keep_first_counter;
    bool           keep_refused;
    uint8_t        keep_refusal;
    uint32_t       keep_grace_until;

    /* The listing a host's refresh carries. */
    bool    listed;
    bool    listing_dirty;   /* changed since the last refresh went out */

    /* A rejoin was seated anew: the next tick leaves that seat and ends the link. */
    bool seat_lost;
    uint8_t announce[MP_RELAY_ANNOUNCE_BYTES];

    /* Counters for the report. */
    uint32_t handshakes;
    uint32_t renewals;
    uint32_t refreshes_sent;
    uint32_t refreshes_answered;
    uint32_t unopened_answers;
    uint32_t data_refused;
    uint32_t nacks_seen;
    uint32_t nudges;
} mp_relay_link_t;

void mp_relay_link_init(mp_relay_link_t *link, mp_relay_random_fn random);

/* A host session with `seats`, or a seat in the session `code`. The link then asks for a key. */
void mp_relay_link_host(mp_relay_link_t *link, uint8_t seats);
void mp_relay_link_join(mp_relay_link_t *link, const uint8_t code[MP_RELAY_CODE_BYTES]);

/* The relay's key, for the handshake that waits on it or the next one. After KEY_REFUSED the link
 * has no key: without a leg it waits in NEEDS_KEY, with one it goes on with that leg, and it
 * begins no handshake until this hands it a key again or `fail` ends it. Whether to fetch, and
 * whether a fetched key is worth another try, is the transport's call. */
void mp_relay_link_give_key(mp_relay_link_t *link, const mp_relay_key_t *key, uint32_t now);

/* Ends the link for good with a failure the transport decided, such as a key it may not fetch. */
void mp_relay_link_fail(mp_relay_link_t *link, mp_relay_link_failure_t failure);

/* The listing a host's next refresh carries. */
void mp_relay_link_set_listing(mp_relay_link_t *link, bool listed,
                               const uint8_t announce[MP_RELAY_ANNOUNCE_BYTES]);

/* The next datagram due for the relay, or 0; call until it answers 0. A decision that waited on
 * the clock, such as a refusal believed after its grace, can end in an event, which `*event` then
 * carries whether or not a datagram is due as well. */
size_t mp_relay_link_tick(mp_relay_link_t *link, uint32_t now, uint8_t *out, size_t capacity,
                          mp_relay_link_event_t *event);

/* A datagram from the relay's address. Answers true with `*event` filled when something came of
 * it; a game packet is copied into `game` of `game_capacity`. */
bool mp_relay_link_receive(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram,
                           size_t bytes, uint8_t *game, size_t game_capacity,
                           mp_relay_link_event_t *event);

/* A game packet sealed for the relay: a host's for `slot` and `gen`, a player's for its host. */
size_t mp_relay_link_seal_game(mp_relay_link_t *link, uint8_t slot, uint16_t gen,
                               const uint8_t *packet, size_t bytes, uint8_t *out,
                               size_t capacity);

/* The goodbye: a host's Close, a player's Leave. 0 when no leg stands. */
size_t mp_relay_link_farewell(mp_relay_link_t *link, uint8_t *out, size_t capacity);

bool mp_relay_link_ready(const mp_relay_link_t *link);

/* Whether anything opened on the leg in the last three seconds. */
bool mp_relay_link_leg_alive(const mp_relay_link_t *link, uint32_t now);

/* Whether the leg has carried anything within a refresh interval and a whole round of retries: a
 * leg that stands in name only, its relay gone, answers false. */
bool mp_relay_link_leg_carries(const mp_relay_link_t *link, uint32_t now);

/* The session's code, once a host has registered or a player has been told one. */
bool mp_relay_link_code(const mp_relay_link_t *link, uint8_t code[MP_RELAY_CODE_BYTES]);

const char *mp_relay_link_failure_text(mp_relay_link_failure_t failure);

#endif /* MULTIPLAYER_MP_RELAY_LINK_H */
