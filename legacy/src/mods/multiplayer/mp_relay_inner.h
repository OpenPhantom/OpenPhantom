/* mp_relay_inner.h: the sealed half of the relay protocol's wire: the 16 byte header every sealed
 * datagram starts with, and the control messages sealed behind it.
 *
 * Layer 1, pure, the same rules as mp_relay_wire.h: exact sizes, zero reserved bytes, unknown flag
 * bits refused. A sealed datagram is the header, the ciphertext and a 16 byte tag; the header is
 * the associated data of the seal, and its counter is the nonce. Sealing is mp_relay_noise.h's.
 *
 * Each sealed control message travels one way on one kind of leg, and the four outer types say
 * which: host to relay carries a refresh, a kick or a close; relay to host a refresh ack, a member
 * opened, a member closed or a nack; player to relay a member refresh or a leave; relay to player a
 * member ack or a nack.
 */
#ifndef MULTIPLAYER_MP_RELAY_INNER_H
#define MULTIPLAYER_MP_RELAY_INNER_H

#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_SEALED_HEADER_BYTES 16u
#define MP_RELAY_TAG_OVERHEAD        16u
#define MP_RELAY_SEALED_OVERHEAD     32u

/* Sealed types. */
#define MP_RELAY_TYPE_HOST_TO_RELAY           0x10u
#define MP_RELAY_TYPE_RELAY_TO_HOST           0x11u
#define MP_RELAY_TYPE_MEMBER_TO_RELAY         0x12u
#define MP_RELAY_TYPE_RELAY_TO_MEMBER         0x13u
#define MP_RELAY_TYPE_HOST_TO_RELAY_CONTROL   0x14u
#define MP_RELAY_TYPE_RELAY_TO_HOST_CONTROL   0x15u
#define MP_RELAY_TYPE_MEMBER_TO_RELAY_CONTROL 0x16u
#define MP_RELAY_TYPE_RELAY_TO_MEMBER_CONTROL 0x17u

/* Inner types of sealed control, and their plaintext sizes with the type byte. The refresh and
 * the member refresh are padded so their datagrams reach the size an open Nack may answer: a relay
 * that lost the leg can still say so. */
#define MP_RELAY_INNER_REFRESH        0x24u
#define MP_RELAY_INNER_REFRESH_ACK    0x25u
#define MP_RELAY_INNER_MEMBER_OPEN    0x28u
#define MP_RELAY_INNER_MEMBER_CLOSED  0x29u
#define MP_RELAY_INNER_KICK           0x2Au
#define MP_RELAY_INNER_CLOSE          0x2Bu
#define MP_RELAY_INNER_MEMBER_REFRESH 0x2Eu
#define MP_RELAY_INNER_MEMBER_ACK     0x2Fu
#define MP_RELAY_INNER_LEAVE          0x30u
#define MP_RELAY_INNER_NACK           0x3Fu

#define MP_RELAY_REFRESH_BYTES        48u
#define MP_RELAY_REFRESH_ACK_BYTES    13u
#define MP_RELAY_MEMBER_OPEN_BYTES    37u
#define MP_RELAY_MEMBER_CLOSED_BYTES  5u
#define MP_RELAY_KICK_BYTES           4u
#define MP_RELAY_CLOSE_BYTES          9u
#define MP_RELAY_MEMBER_REFRESH_BYTES 32u
#define MP_RELAY_MEMBER_ACK_BYTES     1u
#define MP_RELAY_LEAVE_BYTES          1u
#define MP_RELAY_LEG_NACK_BYTES       6u

#define MP_RELAY_REFRESH_LISTED       0x01u
#define MP_RELAY_MEMBER_OPEN_CONTINUES 0x01u

/* Why a player left, as MemberClosed says it. */
#define MP_RELAY_CLOSE_LEFT    1u
#define MP_RELAY_CLOSE_TIMEOUT 2u
#define MP_RELAY_CLOSE_KICKED  3u

typedef struct mp_relay_sealed_header {
    uint8_t  type;
    uint8_t  slot;
    uint16_t gen;
    uint32_t index;
    uint64_t counter;
} mp_relay_sealed_header_t;

typedef struct mp_relay_refresh_ack {
    uint32_t epoch;
    uint64_t counter;
} mp_relay_refresh_ack_t;

typedef struct mp_relay_member_open {
    uint8_t  slot;
    uint8_t  flags;
    uint16_t gen;
    uint8_t  handle[MP_RELAY_HANDLE_BYTES];
    uint8_t  r[MP_RELAY_SEAT_VALUE_BYTES];
} mp_relay_member_open_t;

typedef struct mp_relay_member_closed {
    uint8_t  slot;
    uint8_t  reason;
    uint16_t gen;
} mp_relay_member_closed_t;

void mp_relay_sealed_header_put(const mp_relay_sealed_header_t *header,
                                uint8_t out[MP_RELAY_SEALED_HEADER_BYTES]);

/* The header of a sealed datagram of `bytes`: false when it is shorter than a header and a tag,
 * longer than any datagram, or not of a sealed type. */
bool mp_relay_sealed_header_read(const uint8_t *in, size_t bytes, mp_relay_sealed_header_t *out);

bool   mp_relay_is_sealed_type(uint8_t type);
/* Whether a sealed control type may carry an inner type. */
bool   mp_relay_inner_allowed(uint8_t outer, uint8_t inner);

size_t mp_relay_refresh_encode(uint32_t epoch, uint8_t flags,
                               const uint8_t announce[MP_RELAY_ANNOUNCE_BYTES], uint8_t *out,
                               size_t capacity);
bool   mp_relay_refresh_ack_decode(const uint8_t *in, size_t bytes, mp_relay_refresh_ack_t *out);
bool   mp_relay_member_open_decode(const uint8_t *in, size_t bytes, mp_relay_member_open_t *out);
bool   mp_relay_member_closed_decode(const uint8_t *in, size_t bytes,
                                     mp_relay_member_closed_t *out);
size_t mp_relay_close_encode(uint64_t session_id, uint8_t *out, size_t capacity);
size_t mp_relay_member_refresh_encode(uint8_t *out, size_t capacity);
bool   mp_relay_member_ack_check(const uint8_t *in, size_t bytes);
size_t mp_relay_leave_encode(uint8_t *out, size_t capacity);
bool   mp_relay_leg_nack_decode(const uint8_t *in, size_t bytes, mp_relay_nack_t *out);

#endif /* MULTIPLAYER_MP_RELAY_INNER_H */
