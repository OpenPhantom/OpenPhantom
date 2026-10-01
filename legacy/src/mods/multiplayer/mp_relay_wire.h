/* mp_relay_wire.h: the relay protocol's open messages and handshake bodies, byte for byte.
 *
 * Layer 1, pure. Version 2 of mp-relay (its PROTOCOL.md). Every multi-byte field is little
 * endian. Every encoder writes its message's exact size with every reserved byte and all padding
 * zero, and every decoder refuses any other size, an unknown flag bit and a reserved byte that is
 * not zero, so a message that decodes encodes back to the bytes it came from. The relay drops
 * anything else without a word, and a client that sent it would only ever see an answer missing.
 *
 * The sealed half, the 16 byte header and the inner control messages, is mp_relay_inner.h.
 */
#ifndef MULTIPLAYER_MP_RELAY_WIRE_H
#define MULTIPLAYER_MP_RELAY_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_VERSION              2u
#define MP_RELAY_DATAGRAM_MAX         1232u   /* 16 header + 1200 game packet + 16 tag */
#define MP_RELAY_GAME_PACKET_MAX      1200u
#define MP_RELAY_CONTROL_HEADER_BYTES 8u      /* type, "MPRL", version, two zero bytes */

/* Open control types. */
#define MP_RELAY_TYPE_HELLO      0x20u
#define MP_RELAY_TYPE_COOKIE     0x21u
#define MP_RELAY_TYPE_REGISTER   0x22u
#define MP_RELAY_TYPE_REGISTERED 0x23u
#define MP_RELAY_TYPE_NUDGE      0x26u
#define MP_RELAY_TYPE_JOIN       0x2Cu
#define MP_RELAY_TYPE_JOINED     0x2Du
#define MP_RELAY_TYPE_LIST_QUERY 0x31u
#define MP_RELAY_TYPE_LIST_PAGE  0x32u
#define MP_RELAY_TYPE_NACK       0x3Fu

/* Their sizes. The requests a stranger can send are padded; their answers are smaller. */
#define MP_RELAY_HELLO_BYTES             256u
#define MP_RELAY_COOKIE_BYTES            40u
#define MP_RELAY_REQUEST_BYTES           256u   /* a Register and a Join */
#define MP_RELAY_REGISTERED_BYTES        120u
#define MP_RELAY_JOINED_BYTES            116u
#define MP_RELAY_NUDGE_BYTES             16u
#define MP_RELAY_LIST_QUERY_BYTES        1200u
#define MP_RELAY_LIST_PAGE_HEADER_BYTES  16u
#define MP_RELAY_LIST_ENTRY_BYTES        44u    /* a code and an announce */
#define MP_RELAY_LIST_PAGE_ENTRIES       24u
#define MP_RELAY_NACK_BYTES              24u
#define MP_RELAY_NACK_MIN_REQUEST        64u    /* the smallest request an open Nack answers */

/* The handshake request: its first 40 bytes are the head, which both sides bind as the prologue
 * behind an eight byte label; suite 0 continues with two zero bytes, the initiator's ephemeral key
 * and the body sealed under es. An answer carries the relay's ephemeral key after its header. */
#define MP_RELAY_HEAD_BYTES              40u
#define MP_RELAY_PROLOGUE_BYTES          48u
#define MP_RELAY_EPHEMERAL_OFFSET        40u
#define MP_RELAY_REQUEST_BODY_OFFSET     72u
#define MP_RELAY_ANSWER_EPHEMERAL_OFFSET 8u
#define MP_RELAY_ANSWER_BODY_OFFSET      40u

#define MP_RELAY_REGISTER_BODY_BYTES   60u
#define MP_RELAY_REGISTERED_BODY_BYTES 64u
#define MP_RELAY_JOIN_BODY_BYTES       40u
#define MP_RELAY_JOINED_BODY_BYTES     60u

#define MP_RELAY_COOKIE_MAC_BYTES  16u
#define MP_RELAY_PROOF_BYTES       16u
#define MP_RELAY_SECRET_BYTES      32u
#define MP_RELAY_PUBLIC_KEY_BYTES  32u
#define MP_RELAY_CODE_BYTES        5u
#define MP_RELAY_ANNOUNCE_BYTES    39u
#define MP_RELAY_HANDLE_BYTES      16u
#define MP_RELAY_SEAT_VALUE_BYTES  16u

/* Roles a cookie is bound to. */
#define MP_RELAY_ROLE_HOST   1u
#define MP_RELAY_ROLE_MEMBER 2u
#define MP_RELAY_ROLE_LIST   3u

/* Nack reasons. */
#define MP_RELAY_NACK_UNKNOWN_INDEX 1u    /* an unknown index, or a leg the relay no longer holds */
#define MP_RELAY_NACK_VERSION       2u
#define MP_RELAY_NACK_FULL          3u
#define MP_RELAY_NACK_UNKNOWN_CODE  4u
#define MP_RELAY_NACK_KEY_REJECTED  5u
#define MP_RELAY_NACK_BAD_COOKIE    6u    /* or a wrong resume proof */
#define MP_RELAY_NACK_RATE_LIMITED  7u    /* or a full cookie memory */
#define MP_RELAY_NACK_REVOKED       8u
#define MP_RELAY_NACK_CLOSED        9u
#define MP_RELAY_NACK_UNKNOWN_KEY   10u   /* the relay does not know the key id or the suite */
#define MP_RELAY_NACK_RECONNECT     11u   /* the leg reached its limit */
#define MP_RELAY_NACK_SEAT_HELD     12u   /* the seat at this address is held without its proof */

/* Flags. */
#define MP_RELAY_REGISTER_RESUME 0x01u
#define MP_RELAY_REGISTER_HAS_KEY 0x02u
#define MP_RELAY_JOIN_HAS_PROOF  0x01u
#define MP_RELAY_JOINED_CONTINUED 0x01u

/* The only handshake suite: Noise_NK_25519_ChaChaPoly_SHA256. */
#define MP_RELAY_SUITE_NK 0u

/* The code as text: "ABCD-EFGH" and its terminator. */
#define MP_RELAY_CODE_TEXT_BYTES 10u

typedef struct mp_relay_cookie {
    uint8_t  role;
    uint64_t nonce;
    uint32_t epoch;
    uint8_t  cookie[MP_RELAY_COOKIE_MAC_BYTES];
} mp_relay_cookie_t;

typedef struct mp_relay_head {
    uint32_t epoch;
    uint8_t  cookie[MP_RELAY_COOKIE_MAC_BYTES];
    uint64_t nonce;
    uint8_t  key_id;
    uint8_t  suite;
} mp_relay_head_t;

typedef struct mp_relay_register_body {
    uint8_t  flags;
    uint8_t  seats;
    uint8_t  key[MP_RELAY_PUBLIC_KEY_BYTES];
    uint64_t resume_id;
    uint8_t  resume_proof[MP_RELAY_PROOF_BYTES];
} mp_relay_register_body_t;

typedef struct mp_relay_registered_body {
    uint32_t session_index;
    uint64_t session_id;
    uint8_t  host_secret[MP_RELAY_SECRET_BYTES];
    uint8_t  code[MP_RELAY_CODE_BYTES];
    uint32_t refresh_ms;
    uint32_t member_refresh_ms;
    uint32_t relay_epoch;
} mp_relay_registered_body_t;

typedef struct mp_relay_join_body {
    uint8_t code[MP_RELAY_CODE_BYTES];
    uint8_t flags;
    uint8_t r[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t proof[MP_RELAY_PROOF_BYTES];
} mp_relay_join_body_t;

typedef struct mp_relay_joined_body {
    uint32_t member_index;
    uint16_t gen;
    uint8_t  seats;
    uint8_t  flags;
    uint32_t member_refresh_ms;
    uint8_t  r[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t  seat_secret[MP_RELAY_SECRET_BYTES];
} mp_relay_joined_body_t;

typedef struct mp_relay_nudge {
    uint32_t session_index;
    uint32_t relay_epoch;
} mp_relay_nudge_t;

typedef struct mp_relay_list_query {
    uint32_t epoch;
    uint8_t  cookie[MP_RELAY_COOKIE_MAC_BYTES];
    uint64_t nonce;
    uint16_t page;
} mp_relay_list_query_t;

typedef struct mp_relay_list_entry {
    uint8_t code[MP_RELAY_CODE_BYTES];
    uint8_t announce[MP_RELAY_ANNOUNCE_BYTES];
} mp_relay_list_entry_t;

typedef struct mp_relay_list_page {
    uint16_t              page;
    uint16_t              pages;
    uint8_t               count;
    mp_relay_list_entry_t entry[MP_RELAY_LIST_PAGE_ENTRIES];
} mp_relay_list_page_t;

typedef struct mp_relay_nack {
    uint8_t  reason;
    uint32_t ref;
} mp_relay_nack_t;

/* Whether `bytes` of `in` are an open control message of `type` and exactly `size` bytes, with
 * the magic, the version and the two reserved header bytes right. */
bool mp_relay_check_control(const uint8_t *in, size_t bytes, uint8_t type, size_t size);

/* Each encoder answers the bytes written, or 0 when `capacity` is short or a field is out of
 * range. Each decoder answers false for anything its message may not be. */
size_t mp_relay_hello_encode(uint8_t role, uint64_t nonce, uint8_t *out, size_t capacity);
bool   mp_relay_cookie_decode(const uint8_t *in, size_t bytes, mp_relay_cookie_t *out);

/* A Register or a Join of its whole padded size, zero but for the head. */
size_t mp_relay_head_encode(const mp_relay_head_t *head, uint8_t type, uint8_t *out,
                            size_t capacity);
/* The label and the request's head, which both sides bind before the handshake. */
void   mp_relay_prologue(const uint8_t request[MP_RELAY_HEAD_BYTES],
                         uint8_t out[MP_RELAY_PROLOGUE_BYTES]);

size_t mp_relay_register_body_encode(const mp_relay_register_body_t *body, uint8_t *out,
                                     size_t capacity);
bool   mp_relay_registered_body_decode(const uint8_t *in, size_t bytes,
                                       mp_relay_registered_body_t *out);
size_t mp_relay_join_body_encode(const mp_relay_join_body_t *body, uint8_t *out,
                                 size_t capacity);
bool   mp_relay_joined_body_decode(const uint8_t *in, size_t bytes, mp_relay_joined_body_t *out);

bool   mp_relay_nudge_decode(const uint8_t *in, size_t bytes, mp_relay_nudge_t *out);
size_t mp_relay_list_query_encode(const mp_relay_list_query_t *query, uint8_t *out,
                                  size_t capacity);
bool   mp_relay_list_page_decode(const uint8_t *in, size_t bytes, mp_relay_list_page_t *out);
bool   mp_relay_nack_decode(const uint8_t *in, size_t bytes, mp_relay_nack_t *out);

/* A join code as a person reads it: the 40 bits as eight Crockford base32 characters grouped four
 * and four, "ABCD-EFGH". Crockford leaves out I, L, O and U, and a code holds no '.', ':', '[' or
 * ']', so it can never be taken for an address. */
void mp_relay_code_text(const uint8_t code[MP_RELAY_CODE_BYTES],
                        char out[MP_RELAY_CODE_TEXT_BYTES]);

/* A code as a person types it: case does not matter, hyphens and spaces are passed over, and I
 * and L read as 1, O as 0. False for any other character or for anything but eight symbols. */
bool mp_relay_code_parse(const char *text, uint8_t code[MP_RELAY_CODE_BYTES]);

#endif /* MULTIPLAYER_MP_RELAY_WIRE_H */
