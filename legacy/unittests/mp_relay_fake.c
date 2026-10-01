/* mp_relay_fake.c: a relay for the link tests. See the header. */
#include "mp_relay_fake.h"

#include "mp_relay_crypto.h"
#include "mp_relay_inner.h"

#include <string.h>

bool fake_random_fails = false;

static uint8_t random_next = 1u;

static const uint8_t MAGIC[4] = { 'M', 'P', 'R', 'L' };

static const char PROTOCOL_NAME[MP_RELAY_HASH_BYTES] = {
    'N', 'o', 'i', 's', 'e', '_', 'N', 'K', '_', '2', '5', '5', '1', '9', '_', 'C',
    'h', 'a', 'C', 'h', 'a', 'P', 'o', 'l', 'y', '_', 'S', 'H', 'A', '2', '5', '6'
};

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *at, uint32_t value)
{
    put16(at, (uint16_t)value);
    put16(at + 2, (uint16_t)(value >> 16));
}

static void put64(uint8_t *at, uint64_t value)
{
    put32(at, (uint32_t)value);
    put32(at + 4, (uint32_t)(value >> 32));
}

static uint32_t get32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

static uint64_t get64(const uint8_t *at)
{
    return (uint64_t)get32(at) | ((uint64_t)get32(at + 4) << 32);
}

static void control_header(uint8_t *out, size_t size, uint8_t type)
{
    memset(out, 0, size);
    out[0] = type;
    memcpy(out + 1, MAGIC, sizeof MAGIC);
    out[5] = (uint8_t)MP_RELAY_VERSION;
}

void fake_run_of(uint8_t *out, size_t bytes, uint8_t first)
{
    size_t i;

    for (i = 0; i < bytes; ++i) {
        out[i] = (uint8_t)(first + i);
    }
}

bool fake_random(uint8_t *out, size_t bytes)
{
    size_t i;

    if (fake_random_fails) {
        return false;
    }
    for (i = 0; i < bytes; ++i) {
        out[i] = random_next++;
    }
    return true;
}

void fake_relay_init(fake_relay_t *relay)
{
    static const uint8_t code[MP_RELAY_CODE_BYTES] = { 0x12u, 0x34u, 0x56u, 0x78u, 0x9Au };

    memset(relay, 0, sizeof *relay);
    fake_run_of(relay->static_private, sizeof relay->static_private, 0xE0u);
    mp_relay_x25519_public(relay->static_public, relay->static_private);
    relay->key_id         = 4u;
    relay->epoch          = 15000000u;
    relay->next_ephemeral = 0x60u;

    relay->registered.session_index = 5u;
    relay->registered.session_id    = 0x1122334455667788ull;
    fake_run_of(relay->registered.host_secret, sizeof relay->registered.host_secret, 0x60u);
    memcpy(relay->registered.code, code, sizeof code);
    relay->registered.refresh_ms        = 20000u;
    relay->registered.member_refresh_ms = 5000u;
    relay->registered.relay_epoch       = relay->epoch;

    relay->joined.member_index      = 44u;
    relay->joined.gen               = 3u;
    relay->joined.seats             = 3u;
    relay->joined.member_refresh_ms = 5000u;
    fake_run_of(relay->joined.r, sizeof relay->joined.r, 0xC0u);
    fake_run_of(relay->joined.seat_secret, sizeof relay->joined.seat_secret, 0x10u);
}

mp_relay_key_t fake_relay_key(const fake_relay_t *relay)
{
    mp_relay_key_t key;

    memset(&key, 0, sizeof key);
    key.id = relay->key_id;
    memcpy(key.public_key, relay->static_public, sizeof key.public_key);
    key.suite = (uint8_t)MP_RELAY_SUITE_NK;
    return key;
}

bool fake_hello_read(const uint8_t *hello, size_t bytes, uint8_t *role, uint64_t *nonce)
{
    if (bytes != MP_RELAY_HELLO_BYTES || hello[0] != MP_RELAY_TYPE_HELLO) {
        return false;
    }
    *role  = hello[8];
    *nonce = get64(hello + 12);
    return true;
}

size_t fake_cookie(const fake_relay_t *relay, uint8_t role, uint64_t nonce, uint8_t *out)
{
    control_header(out, MP_RELAY_COOKIE_BYTES, (uint8_t)MP_RELAY_TYPE_COOKIE);
    out[8] = role;
    put64(out + 12, nonce);
    put32(out + 20, relay->epoch);
    fake_run_of(out + 24, MP_RELAY_COOKIE_MAC_BYTES, 0xA0u);
    return MP_RELAY_COOKIE_BYTES;
}

static bool mix_hash(uint8_t h[MP_RELAY_HASH_BYTES], const uint8_t *data, size_t bytes)
{
    uint8_t next[MP_RELAY_HASH_BYTES];

    if (!mp_relay_sha256(next, h, MP_RELAY_HASH_BYTES, data, bytes)) {
        return false;
    }
    memcpy(h, next, sizeof next);
    return true;
}

static bool mix_dh(uint8_t ck[MP_RELAY_HASH_BYTES], uint8_t key[MP_RELAY_KEY_BYTES],
                   const uint8_t private_key[MP_RELAY_KEY_BYTES],
                   const uint8_t peer[MP_RELAY_KEY_BYTES])
{
    uint8_t shared[MP_RELAY_KEY_BYTES];
    uint8_t next[MP_RELAY_HASH_BYTES];

    if (!mp_relay_x25519_shared(shared, private_key, peer) ||
        !mp_relay_hkdf2(next, key, ck, shared, sizeof shared)) {
        return false;
    }
    memcpy(ck, next, sizeof next);
    return true;
}

static void registered_put(const mp_relay_registered_body_t *body, uint8_t *out)
{
    memset(out, 0, MP_RELAY_REGISTERED_BODY_BYTES);
    put32(out, body->session_index);
    put64(out + 4, body->session_id);
    memcpy(out + 12, body->host_secret, sizeof body->host_secret);
    memcpy(out + 44, body->code, sizeof body->code);
    put32(out + 52, body->refresh_ms);
    put32(out + 56, body->member_refresh_ms);
    put32(out + 60, body->relay_epoch);
}

static void joined_put(const mp_relay_joined_body_t *body, uint8_t *out)
{
    memset(out, 0, MP_RELAY_JOINED_BODY_BYTES);
    put32(out, body->member_index);
    put16(out + 4, body->gen);
    out[6] = body->seats;
    out[7] = body->flags;
    put32(out + 8, body->member_refresh_ms);
    memcpy(out + 12, body->r, sizeof body->r);
    memcpy(out + 28, body->seat_secret, sizeof body->seat_secret);
}

size_t fake_answer_with(fake_relay_t *relay, const uint8_t *request, size_t bytes,
                        const uint8_t e_private[MP_RELAY_KEY_BYTES], uint8_t *out,
                        size_t capacity)
{
    bool            host = bytes == MP_RELAY_REQUEST_BYTES && request[0] == MP_RELAY_TYPE_REGISTER;
    size_t          body_bytes = host ? MP_RELAY_REGISTER_BODY_BYTES : MP_RELAY_JOIN_BODY_BYTES;
    size_t          answer_body = host ? MP_RELAY_REGISTERED_BODY_BYTES
                                       : MP_RELAY_JOINED_BODY_BYTES;
    size_t          size = host ? MP_RELAY_REGISTERED_BYTES : MP_RELAY_JOINED_BYTES;
    uint8_t         prologue[MP_RELAY_PROLOGUE_BYTES];
    uint8_t         h[MP_RELAY_HASH_BYTES];
    uint8_t         ck[MP_RELAY_HASH_BYTES];
    uint8_t         key[MP_RELAY_KEY_BYTES];
    uint8_t         e_public[MP_RELAY_KEY_BYTES];
    uint8_t         plain[MP_RELAY_REGISTERED_BODY_BYTES];
    const uint8_t  *ie = request + MP_RELAY_EPHEMERAL_OFFSET;
    mp_relay_keys_t swapped;

    if (bytes != MP_RELAY_REQUEST_BYTES || capacity < size ||
        (request[0] != MP_RELAY_TYPE_REGISTER && request[0] != MP_RELAY_TYPE_JOIN)) {
        return 0u;
    }
    memcpy(h, PROTOCOL_NAME, sizeof h);
    memcpy(ck, h, sizeof ck);
    mp_relay_prologue(request, prologue);
    if (!mix_hash(h, prologue, sizeof prologue) ||
        !mix_hash(h, relay->static_public, sizeof relay->static_public) ||
        !mix_hash(h, ie, MP_RELAY_KEY_BYTES) || !mix_dh(ck, key, relay->static_private, ie) ||
        !mp_relay_aead_open(relay->body, key, 0u, h, sizeof h,
                            request + MP_RELAY_REQUEST_BODY_OFFSET, body_bytes,
                            request + MP_RELAY_REQUEST_BODY_OFFSET + body_bytes) ||
        !mix_hash(h, request + MP_RELAY_REQUEST_BODY_OFFSET, body_bytes + MP_RELAY_TAG_BYTES)) {
        return 0u;
    }
    relay->request_type = request[0];
    relay->body_bytes   = body_bytes;
    relay->head.epoch   = get32(request + 8);
    memcpy(relay->head.cookie, request + 12, sizeof relay->head.cookie);
    relay->head.nonce  = get64(request + 28);
    relay->head.key_id = request[36];
    relay->head.suite  = request[37];

    if (host) {
        registered_put(&relay->registered, plain);
    } else {
        joined_put(&relay->joined, plain);
    }
    mp_relay_x25519_public(e_public, e_private);
    control_header(out, size, host ? (uint8_t)MP_RELAY_TYPE_REGISTERED
                                   : (uint8_t)MP_RELAY_TYPE_JOINED);
    memcpy(out + MP_RELAY_ANSWER_EPHEMERAL_OFFSET, e_public, sizeof e_public);
    if (!mix_hash(h, e_public, sizeof e_public) || !mix_dh(ck, key, e_private, ie)) {
        return 0u;
    }
    mp_relay_aead_seal(out + MP_RELAY_ANSWER_BODY_OFFSET,
                       out + MP_RELAY_ANSWER_BODY_OFFSET + answer_body, key, 0u, h, sizeof h,
                       plain, answer_body);
    if (!mix_hash(h, out + MP_RELAY_ANSWER_BODY_OFFSET, answer_body + MP_RELAY_TAG_BYTES) ||
        !mp_relay_hkdf2(relay->keys.initiator_to_responder, relay->keys.responder_to_initiator,
                        ck, NULL, 0u)) {
        return 0u;
    }
    memcpy(relay->keys.hash, h, sizeof h);
    memcpy(swapped.initiator_to_responder, relay->keys.responder_to_initiator, MP_RELAY_KEY_BYTES);
    memcpy(swapped.responder_to_initiator, relay->keys.initiator_to_responder, MP_RELAY_KEY_BYTES);
    memcpy(swapped.hash, h, sizeof h);
    mp_relay_leg_init(&relay->leg, &swapped);
    ++relay->answers;
    return size;
}

size_t fake_answer(fake_relay_t *relay, const uint8_t *request, size_t bytes, uint8_t *out,
                   size_t capacity)
{
    uint8_t e_private[MP_RELAY_KEY_BYTES];

    fake_run_of(e_private, sizeof e_private, relay->next_ephemeral);
    relay->next_ephemeral = (uint8_t)(relay->next_ephemeral + 0x21u);
    return fake_answer_with(relay, request, bytes, e_private, out, capacity);
}

size_t fake_open_nack(uint8_t reason, uint32_t ref, uint8_t *out)
{
    control_header(out, MP_RELAY_NACK_BYTES, (uint8_t)MP_RELAY_TYPE_NACK);
    out[8] = reason;
    put32(out + 12, ref);
    return MP_RELAY_NACK_BYTES;
}

size_t fake_nudge(uint32_t index, uint32_t epoch, uint8_t *out)
{
    control_header(out, MP_RELAY_NUDGE_BYTES, (uint8_t)MP_RELAY_TYPE_NUDGE);
    put32(out + 8, index);
    put32(out + 12, epoch);
    return MP_RELAY_NUDGE_BYTES;
}

size_t fake_seal(fake_relay_t *relay, uint8_t type, uint8_t slot, uint16_t gen, uint32_t index,
                 const uint8_t *plain, size_t bytes, uint8_t *out, size_t capacity)
{
    mp_relay_sealed_header_t header;

    memset(&header, 0, sizeof header);
    header.type  = type;
    header.slot  = slot;
    header.gen   = gen;
    header.index = index;
    return mp_relay_leg_seal(&relay->leg, &header, plain, bytes, out, capacity);
}

bool fake_open(fake_relay_t *relay, const uint8_t *datagram, size_t bytes,
               mp_relay_sealed_header_t *header, uint8_t *plain, size_t capacity,
               size_t *plain_bytes)
{
    return mp_relay_sealed_header_read(datagram, bytes, header) &&
           mp_relay_leg_open(&relay->leg, datagram, bytes, plain, capacity, plain_bytes);
}

size_t fake_to_host(fake_relay_t *relay, const uint8_t *plain, size_t bytes, uint8_t *out)
{
    return fake_seal(relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST_CONTROL, 0u, 0u,
                     relay->registered.session_index, plain, bytes, out, MP_RELAY_DATAGRAM_MAX);
}

size_t fake_to_member(fake_relay_t *relay, const uint8_t *plain, size_t bytes, uint8_t *out)
{
    return fake_seal(relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER_CONTROL, 0u, relay->joined.gen,
                     relay->joined.member_index, plain, bytes, out, MP_RELAY_DATAGRAM_MAX);
}

size_t fake_refresh_ack(uint32_t epoch, uint64_t counter, uint8_t *out)
{
    out[0] = (uint8_t)MP_RELAY_INNER_REFRESH_ACK;
    put32(out + 1, epoch);
    put64(out + 5, counter);
    return MP_RELAY_REFRESH_ACK_BYTES;
}

size_t fake_member_open(uint8_t slot, uint8_t flags, uint16_t gen,
                        const uint8_t r[MP_RELAY_SEAT_VALUE_BYTES], uint8_t *out)
{
    memset(out, 0, MP_RELAY_MEMBER_OPEN_BYTES);
    out[0] = (uint8_t)MP_RELAY_INNER_MEMBER_OPEN;
    out[1] = slot;
    out[2] = flags;
    put16(out + 3, gen);
    fake_run_of(out + 5, MP_RELAY_HANDLE_BYTES, 0x70u);
    memcpy(out + 21, r, MP_RELAY_SEAT_VALUE_BYTES);
    return MP_RELAY_MEMBER_OPEN_BYTES;
}

size_t fake_member_closed(uint8_t slot, uint8_t reason, uint16_t gen, uint8_t *out)
{
    out[0] = (uint8_t)MP_RELAY_INNER_MEMBER_CLOSED;
    out[1] = slot;
    out[2] = reason;
    put16(out + 3, gen);
    return MP_RELAY_MEMBER_CLOSED_BYTES;
}

size_t fake_member_ack(uint8_t *out)
{
    out[0] = (uint8_t)MP_RELAY_INNER_MEMBER_ACK;
    return MP_RELAY_MEMBER_ACK_BYTES;
}

size_t fake_leg_nack(uint8_t reason, uint32_t ref, uint8_t *out)
{
    out[0] = (uint8_t)MP_RELAY_INNER_NACK;
    out[1] = reason;
    put32(out + 2, ref);
    return MP_RELAY_LEG_NACK_BYTES;
}

size_t fake_tick(mp_relay_link_t *link, uint32_t now, uint8_t *out, mp_relay_link_event_t *event)
{
    return mp_relay_link_tick(link, now, out, MP_RELAY_DATAGRAM_MAX, event);
}

bool fake_connect(mp_relay_link_t *link, fake_relay_t *relay, uint32_t now)
{
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    uint8_t               in[MP_RELAY_DATAGRAM_MAX];
    mp_relay_link_event_t event;
    uint8_t               role  = 0u;
    uint64_t              nonce = 0u;
    size_t                n     = fake_tick(link, now, out, &event);

    if (!fake_hello_read(out, n, &role, &nonce)) {
        return false;
    }
    n = fake_cookie(relay, role, nonce, in);
    (void)mp_relay_link_receive(link, now, in, n, NULL, 0u, &event);
    n = fake_tick(link, now, out, &event);
    n = n == MP_RELAY_REQUEST_BYTES ? fake_answer(relay, out, n, in, sizeof in) : 0u;
    return n != 0u && mp_relay_link_receive(link, now, in, n, NULL, 0u, &event) &&
           event.kind == MP_RELAY_LINK_EVENT_READY;
}
