/* mp_relay_transport.h: a public session's transport: the session's packets through the relay.
 *
 * Layer 0. It fills the same vtable as mp_udp, so the session drives it unchanged, and underneath
 * it is everything a PC needs to reach the relay: the two sockets (mp_relay_socket), the lookup of
 * the relay and the fetch of its key (mp_relay_keyfetch, under the rules of mp_relay_keysource),
 * the link (mp_relay_link) and, for a host, the players' endpoints (mp_relay_seats).
 *
 * Nothing here waits. The work is done inside the session's own reads: the first read of a pump
 * takes what the fetch thread finished and begins what is due, every read feeds the relay's
 * datagrams to the link and hands the game packets among them to the session, and the read that
 * finds nothing more sends what the link has due. mp_relay_transport_pump does the same without a
 * read, for a moment in which the session is not pumped.
 *
 * Endpoints. A player's session has one peer, the host, at endpoint 1. A host's session has one
 * per seated player, as mp_relay_seats numbers them.
 *
 * The address family. Both sockets say Hello when both have an address: IPv6 at once and IPv4
 * 500 ms later, and the first cookie decides the family of the handshake. A leg stays in the
 * family it was made in, its refreshes and game packets included. A player's new leg asks in that
 * family only, for two attempts, because its seat is bound to the address it was taken from and a
 * rejoin from the other family would be a new seat; after two unanswered attempts it races again,
 * and a rejoin that comes through the other family ends the link cleanly as a lost seat. A host's
 * resume races from the start: the relay moves a host to the address its proof comes from.
 *
 * The services are the calls that reach outside the process (the fetch, the file, the clocks, the
 * generator). mp_relay_services_real is the game's; a test hands in its own and a relay on the
 * loopback.
 */
#ifndef MULTIPLAYER_MP_RELAY_TRANSPORT_H
#define MULTIPLAYER_MP_RELAY_TRANSPORT_H

#include "mp_relay_keyfetch.h"
#include "mp_relay_keysource.h"
#include "mp_relay_link.h"
#include "mp_relay_seats.h"
#include "mp_relay_socket.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long IPv4 waits behind IPv6 for the first cookie. */
#define MP_RELAY_FAMILY_HEADSTART_MS 500u

/* Attempts a player's new leg makes in its old leg's family before it races again. */
#define MP_RELAY_PINNED_ATTEMPTS 2u

/* A lookup that found no address, or a key fetch that failed with nothing to stand in, is tried
 * again after these. */
#define MP_RELAY_LOOKUP_RETRY_MS 5000u
#define MP_RELAY_FETCH_RETRY_MS  10000u

/* A leg that stands has a key that works: a fetch for the next one that failed waits longer. */
#define MP_RELAY_FETCH_RETRY_READY_MS 300000u

/* A fetch another part of the program began is waited for this long between looks. */
#define MP_RELAY_FETCH_BUSY_MS 250u

/* Datagrams one read takes off without handing the session a packet, before it gives the game its
 * thread back; the rest wait for the next read. */
#define MP_RELAY_READ_BUDGET 256u

typedef struct mp_relay_services {
    bool     (*fetch_begin)(bool want_key);
    bool     (*fetch_running)(void);
    bool     (*fetch_take)(mp_relay_fetch_result_t *out);
    bool     (*state_load)(mp_relay_key_t *key, int64_t *at);
    bool     (*state_save)(const mp_relay_key_t *key, int64_t at);
    int64_t  (*unix_now)(void);
    uint32_t (*now_ms)(void);
    bool     (*random)(uint8_t *out, size_t bytes);
} mp_relay_services_t;

typedef enum mp_relay_state {
    MP_RELAY_STATE_OFF = 0,
    MP_RELAY_STATE_LOOKING_UP,   /* the relay's addresses or its key are on their way */
    MP_RELAY_STATE_CONNECTING,   /* a handshake is under way */
    MP_RELAY_STATE_READY,        /* a host has its code, a player its seat */
    MP_RELAY_STATE_WAITING,      /* an attempt failed and the next one waits */
    MP_RELAY_STATE_FAILED        /* given up: mp_relay_transport_failure says why */
} mp_relay_state_t;

typedef struct mp_relay_transport {
    const mp_relay_services_t *services;
    bool                       up;
    bool                       host;
    mp_relay_socket_t          socket;
    mp_relay_link_t            link;
    mp_relay_seats_t           seats;
    mp_relay_keysource_t       keys;

    /* The lookup and the key. */
    bool           addresses_known;
    bool           fetching;         /* a fetch this transport began is out */
    bool           fetch_wants_key;
    uint32_t       next_fetch_at;
    bool           refetch;          /* the relay refused the key: a new one is wanted */
    bool           refetch_granted;  /* the key source allowed it; the fetch has not begun yet */
    bool           refetch_unproven;
    mp_relay_key_t refused_key;

    /* The address family. */
    int      hello_family;           /* -1 until a cookie decides this handshake's */
    int      leg_family;             /* the family of the leg in use, -1 before the first */
    uint64_t race_nonce;
    uint32_t race_started;
    bool     race_ipv4_sent;
    uint32_t pinned_attempts;        /* a player's new-leg attempts in the old leg's family */
    bool     race_pinned;            /* this attempt's Hellos go to the old leg's family only */
    bool     key_given_up;           /* the refused key was given back to a leg; said once */
    uint8_t  hello[MP_RELAY_HELLO_BYTES];

    /* What the reads are in the middle of, and what the log has been told. */
    bool                  draining;
    mp_relay_link_phase_t phase_told;
    uint32_t              handshakes_told;
    uint8_t               code_told[MP_RELAY_CODE_BYTES];

    bool     failed;                 /* a failure of the transport's own, not the link's */
    char     failure[128];
    uint32_t game_in;
    uint32_t game_out;
    uint32_t game_unsent;
    uint32_t attempts_failed;
    uint8_t  datagram[MP_RELAY_DATAGRAM_MAX];
} mp_relay_transport_t;

const mp_relay_services_t *mp_relay_services_real(void);

/* Opens the sockets and begins: a host session with `seats` seats, or a seat in the session
 * `code`. False when no socket opened, with the reason in mp_relay_transport_failure. */
bool mp_relay_transport_start_host(mp_relay_transport_t *t, const mp_relay_services_t *services,
                                   uint8_t seats);
bool mp_relay_transport_start_join(mp_relay_transport_t *t, const mp_relay_services_t *services,
                                   const uint8_t code[MP_RELAY_CODE_BYTES]);

/* The transport face. Its context points at *t, which must outlive it. */
mp_transport_t mp_relay_transport_face(mp_relay_transport_t *t);

/* The work of a read without the read: what the fetch finished, what is due. */
void mp_relay_transport_pump(mp_relay_transport_t *t);

/* The listing a host's refreshes carry: whether the public list shows the session, and the
 * announce it shows. */
void mp_relay_transport_set_listing(mp_relay_transport_t *t, bool listed,
                                    const uint8_t announce[MP_RELAY_ANNOUNCE_BYTES]);

mp_relay_state_t mp_relay_transport_state(const mp_relay_transport_t *t);

/* The session's code: a host's once registered, a player's from the start. */
bool mp_relay_transport_code(const mp_relay_transport_t *t, uint8_t code[MP_RELAY_CODE_BYTES]);

/* Why the transport failed, or why the last attempt did; "" when neither. */
const char *mp_relay_transport_failure(const mp_relay_transport_t *t);

/* The goodbye to the relay: a host's Close, a player's Leave, sealed on the leg in use. */
void mp_relay_transport_farewell(mp_relay_transport_t *t);

/* Closes the sockets and wipes the keys. Idempotent. */
void mp_relay_transport_close(mp_relay_transport_t *t);

/* The counters, as lines in the log. */
void mp_relay_transport_report(const mp_relay_transport_t *t);

const char *mp_relay_state_name(mp_relay_state_t state);

#endif /* MULTIPLAYER_MP_RELAY_TRANSPORT_H */
