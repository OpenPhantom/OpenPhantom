/* mp_relay_fake.h: a relay for the link tests, the side of every exchange a PC never plays.
 *
 * It answers a Hello with a cookie, opens a Register or a Join under its static key as the
 * responder of Noise NK, seals the Registered or Joined it was given, and then holds the relay's
 * side of the leg: it sends under the responder-to-initiator key and opens under the other. It
 * encodes what only a relay sends (cookies, open Nacks, nudges, refresh acks, member notices and
 * sealed Nacks), which the mod has decoders for and no encoders.
 *
 * The responder is proven against the relay's own vectors before any test leans on it.
 */
#ifndef UNITTESTS_MP_RELAY_FAKE_H
#define UNITTESTS_MP_RELAY_FAKE_H

#include "mp_relay_link.h"
#include "mp_relay_noise.h"
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct fake_relay {
    uint8_t static_private[MP_RELAY_KEY_BYTES];
    uint8_t static_public[MP_RELAY_KEY_BYTES];
    uint8_t key_id;
    uint32_t epoch;
    uint8_t  next_ephemeral;   /* the first byte of the next answer's ephemeral key */

    /* What the last request that opened carried. */
    uint8_t         request_type;
    mp_relay_head_t head;
    uint8_t         body[MP_RELAY_REGISTER_BODY_BYTES];
    size_t          body_bytes;
    uint32_t        answers;

    /* What the next answer seals. */
    mp_relay_registered_body_t registered;
    mp_relay_joined_body_t     joined;

    mp_relay_leg_t leg;
    mp_relay_keys_t keys;   /* as the initiator holds them */
} fake_relay_t;

/* A relay with the vectors' static key under id 4, a session index 5 with code 12 34 56 78 9A and
 * refreshes every 20 s, and a seat index 44 generation 3 refreshed every 5 s. */
void fake_relay_init(fake_relay_t *relay);

mp_relay_key_t fake_relay_key(const fake_relay_t *relay);

/* A deterministic generator for the link: a running byte, or a refusal while
 * `fake_random_fails`. */
extern bool fake_random_fails;
bool        fake_random(uint8_t *out, size_t bytes);

void fake_run_of(uint8_t *out, size_t bytes, uint8_t first);

bool   fake_hello_read(const uint8_t *hello, size_t bytes, uint8_t *role, uint64_t *nonce);
size_t fake_cookie(const fake_relay_t *relay, uint8_t role, uint64_t nonce, uint8_t *out);

/* Opens a Register or a Join, keeps what it carried, and writes the answer under the ephemeral key
 * `e_private`, leaving the relay's side of the new leg in `relay->leg`. 0 when it does not open. */
size_t fake_answer_with(fake_relay_t *relay, const uint8_t *request, size_t bytes,
                        const uint8_t e_private[MP_RELAY_KEY_BYTES], uint8_t *out,
                        size_t capacity);
/* The same under the relay's next ephemeral key. */
size_t fake_answer(fake_relay_t *relay, const uint8_t *request, size_t bytes, uint8_t *out,
                   size_t capacity);

size_t fake_open_nack(uint8_t reason, uint32_t ref, uint8_t *out);
size_t fake_nudge(uint32_t index, uint32_t epoch, uint8_t *out);

/* A datagram sealed toward the PC under the relay's leg, and one from the PC opened. */
size_t fake_seal(fake_relay_t *relay, uint8_t type, uint8_t slot, uint16_t gen, uint32_t index,
                 const uint8_t *plain, size_t bytes, uint8_t *out, size_t capacity);
bool   fake_open(fake_relay_t *relay, const uint8_t *datagram, size_t bytes,
                 mp_relay_sealed_header_t *header, uint8_t *plain, size_t capacity,
                 size_t *plain_bytes);

/* Sealed control toward a host, or toward a player. */
size_t fake_to_host(fake_relay_t *relay, const uint8_t *plain, size_t bytes, uint8_t *out);
size_t fake_to_member(fake_relay_t *relay, const uint8_t *plain, size_t bytes, uint8_t *out);

size_t fake_refresh_ack(uint32_t epoch, uint64_t counter, uint8_t *out);
size_t fake_member_open(uint8_t slot, uint8_t flags, uint16_t gen,
                        const uint8_t r[MP_RELAY_SEAT_VALUE_BYTES], uint8_t *out);
size_t fake_member_closed(uint8_t slot, uint8_t reason, uint16_t gen, uint8_t *out);
size_t fake_member_ack(uint8_t *out);
size_t fake_leg_nack(uint8_t reason, uint32_t ref, uint8_t *out);

/* The link's tick at `now` into a buffer of MP_RELAY_DATAGRAM_MAX. */
size_t fake_tick(mp_relay_link_t *link, uint32_t now, uint8_t *out,
                 mp_relay_link_event_t *event);

/* A whole handshake at `now`: the Hello, its cookie, the request and the answer. True when the link
 * reported READY. */
bool fake_connect(mp_relay_link_t *link, fake_relay_t *relay, uint32_t now);

#endif /* UNITTESTS_MP_RELAY_FAKE_H */
