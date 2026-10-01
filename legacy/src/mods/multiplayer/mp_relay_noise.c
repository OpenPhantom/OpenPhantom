/* mp_relay_noise.c: the relay handshake, the legs, the proofs. See the header. */
#include "mp_relay_noise.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Exactly 32 bytes, so the Noise framework copies it into the hash rather than hashing it. */
static const char PROTOCOL_NAME[MP_RELAY_HASH_BYTES] = {
    'N', 'o', 'i', 's', 'e', '_', 'N', 'K', '_', '2', '5', '5', '1', '9', '_', 'C',
    'h', 'a', 'C', 'h', 'a', 'P', 'o', 'l', 'y', '_', 'S', 'H', 'A', '2', '5', '6'
};

static const uint8_t MAC_LABEL[8]    = { 'M', 'P', 'R', 'L', 'M', 'A', 'C', '1' };
static const uint8_t RESUME_LABEL[8] = { 'M', 'P', 'R', 'L', 'R', 'S', 'M', '2' };
static const uint8_t SEAT_LABEL[8]   = { 'M', 'P', 'R', 'L', 'M', 'R', 'S', 'M' };

/* The message both proofs sign: label, epoch, cookie, nonce, key id. */
#define PROOF_MESSAGE_BYTES (8u + 4u + MP_RELAY_COOKIE_MAC_BYTES + 8u + 1u)

static bool mix_hash(uint8_t h[MP_RELAY_HASH_BYTES], const uint8_t *data, size_t bytes)
{
    uint8_t next[MP_RELAY_HASH_BYTES];

    if (!mp_relay_sha256(next, h, MP_RELAY_HASH_BYTES, data, bytes)) {
        return false;
    }
    memcpy(h, next, sizeof next);
    return true;
}

/* The Diffie-Hellman mixed into the chaining key, and the cipher key it yields. */
static bool mix_dh(uint8_t ck[MP_RELAY_HASH_BYTES], uint8_t key[MP_RELAY_KEY_BYTES],
                   const uint8_t e_private[MP_RELAY_KEY_BYTES],
                   const uint8_t peer[MP_RELAY_KEY_BYTES])
{
    uint8_t shared[MP_RELAY_KEY_BYTES];
    uint8_t next_ck[MP_RELAY_HASH_BYTES];
    bool    done;

    done = mp_relay_x25519_shared(shared, e_private, peer) &&
           mp_relay_hkdf2(next_ck, key, ck, shared, sizeof shared);
    if (done) {
        memcpy(ck, next_ck, sizeof next_ck);
    }
    mp_relay_wipe(shared, sizeof shared);
    mp_relay_wipe(next_ck, sizeof next_ck);
    return done;
}

bool mp_relay_nk_begin(mp_relay_nk_t *nk, const uint8_t prologue[MP_RELAY_PROLOGUE_BYTES],
                       const uint8_t rs[MP_RELAY_KEY_BYTES],
                       const uint8_t e_private[MP_RELAY_KEY_BYTES])
{
    if (nk == NULL || prologue == NULL || rs == NULL || e_private == NULL) {
        return false;
    }
    memset(nk, 0, sizeof *nk);
    memcpy(nk->h, PROTOCOL_NAME, sizeof PROTOCOL_NAME);
    memcpy(nk->ck, nk->h, sizeof nk->ck);
    memcpy(nk->rs, rs, sizeof nk->rs);
    memcpy(nk->e_private, e_private, sizeof nk->e_private);
    mp_relay_x25519_public(nk->e_public, nk->e_private);
    return mix_hash(nk->h, prologue, MP_RELAY_PROLOGUE_BYTES) &&
           mix_hash(nk->h, nk->rs, sizeof nk->rs);
}

size_t mp_relay_nk_write1(mp_relay_nk_t *nk, const uint8_t *body, size_t body_bytes, uint8_t *out,
                          size_t capacity)
{
    uint8_t key[MP_RELAY_KEY_BYTES];
    size_t  total = MP_RELAY_KEY_BYTES + body_bytes + MP_RELAY_TAG_BYTES;
    bool    done;

    if (nk == NULL || out == NULL || nk->step != 0u || capacity < total ||
        (body == NULL && body_bytes != 0u)) {
        return 0u;
    }
    memcpy(out, nk->e_public, MP_RELAY_KEY_BYTES);
    done = mix_hash(nk->h, nk->e_public, sizeof nk->e_public) &&
           mix_dh(nk->ck, key, nk->e_private, nk->rs);
    if (done) {
        mp_relay_aead_seal(out + MP_RELAY_KEY_BYTES, out + MP_RELAY_KEY_BYTES + body_bytes, key, 0u,
                           nk->h, sizeof nk->h, body, body_bytes);
        done = mix_hash(nk->h, out + MP_RELAY_KEY_BYTES, body_bytes + MP_RELAY_TAG_BYTES);
    }
    mp_relay_wipe(key, sizeof key);
    if (!done) {
        return 0u;
    }
    nk->step = 1u;
    return total;
}

bool mp_relay_nk_read2(mp_relay_nk_t *nk, const uint8_t *answer, size_t bytes, uint8_t *body,
                       size_t body_capacity, size_t *body_bytes, mp_relay_keys_t *keys)
{
    uint8_t h[MP_RELAY_HASH_BYTES];
    uint8_t ck[MP_RELAY_HASH_BYTES];
    uint8_t key[MP_RELAY_KEY_BYTES];
    size_t  sealed;
    bool    done;

    if (nk == NULL || answer == NULL || body == NULL || body_bytes == NULL || keys == NULL ||
        nk->step != 1u || bytes < MP_RELAY_KEY_BYTES + MP_RELAY_TAG_BYTES) {
        return false;
    }
    sealed = bytes - MP_RELAY_KEY_BYTES - MP_RELAY_TAG_BYTES;
    if (body_capacity < sealed) {
        return false;
    }
    /* Copies, so a failed open leaves the handshake as it was. */
    memcpy(h, nk->h, sizeof h);
    memcpy(ck, nk->ck, sizeof ck);
    done = mix_hash(h, answer, MP_RELAY_KEY_BYTES) && mix_dh(ck, key, nk->e_private, answer) &&
           mp_relay_aead_open(body, key, 0u, h, sizeof h, answer + MP_RELAY_KEY_BYTES, sealed,
                              answer + MP_RELAY_KEY_BYTES + sealed) &&
           mix_hash(h, answer + MP_RELAY_KEY_BYTES, sealed + MP_RELAY_TAG_BYTES) &&
           mp_relay_hkdf2(keys->initiator_to_responder, keys->responder_to_initiator, ck, NULL, 0u);
    mp_relay_wipe(key, sizeof key);
    if (done) {
        memcpy(keys->hash, h, sizeof keys->hash);
        *body_bytes = sealed;
        mp_relay_nk_wipe(nk);
        nk->step = 2u;
    }
    mp_relay_wipe(ck, sizeof ck);
    mp_relay_wipe(h, sizeof h);
    return done;
}

void mp_relay_nk_wipe(mp_relay_nk_t *nk)
{
    if (nk != NULL) {
        mp_relay_wipe(nk, sizeof *nk);
    }
}

size_t mp_relay_build_request(uint8_t type, const mp_relay_cookie_t *cookie, uint64_t nonce,
                              uint8_t key_id, const uint8_t relay_public[MP_RELAY_KEY_BYTES],
                              const uint8_t *body, size_t body_bytes,
                              const uint8_t e_private[MP_RELAY_KEY_BYTES], mp_relay_nk_t *nk,
                              uint8_t *out, size_t capacity)
{
    mp_relay_head_t head;
    uint8_t         prologue[MP_RELAY_PROLOGUE_BYTES];

    if (cookie == NULL || out == NULL || capacity < MP_RELAY_REQUEST_BYTES ||
        MP_RELAY_REQUEST_BODY_OFFSET + body_bytes + MP_RELAY_TAG_BYTES > MP_RELAY_REQUEST_BYTES) {
        return 0u;
    }
    memset(&head, 0, sizeof head);
    head.epoch  = cookie->epoch;
    memcpy(head.cookie, cookie->cookie, sizeof head.cookie);
    head.nonce  = nonce;
    head.key_id = key_id;
    head.suite  = (uint8_t)MP_RELAY_SUITE_NK;
    if (mp_relay_head_encode(&head, type, out, capacity) != MP_RELAY_REQUEST_BYTES) {
        return 0u;
    }
    mp_relay_prologue(out, prologue);
    if (!mp_relay_nk_begin(nk, prologue, relay_public, e_private) ||
        mp_relay_nk_write1(nk, body, body_bytes, out + MP_RELAY_EPHEMERAL_OFFSET,
                           MP_RELAY_REQUEST_BYTES - MP_RELAY_EPHEMERAL_OFFSET) == 0u) {
        mp_relay_nk_wipe(nk);
        return 0u;
    }
    return MP_RELAY_REQUEST_BYTES;
}

/* ==============================================================================================
 * The window.
 * ============================================================================================ */

static uint64_t bit_of(uint64_t counter)
{
    return 1ull << (counter & 63u);
}

static size_t word_of(uint64_t counter)
{
    return (size_t)((counter >> 6) % MP_RELAY_WINDOW_WORDS);
}

bool mp_relay_window_check(const mp_relay_window_t *window, uint64_t counter)
{
    if (!window->seen || counter > window->top) {
        return true;
    }
    if (window->top - counter >= MP_RELAY_WINDOW_SPAN) {
        return false;
    }
    return (window->bits[word_of(counter)] & bit_of(counter)) == 0u;
}

bool mp_relay_window_accept(mp_relay_window_t *window, uint64_t counter)
{
    if (!window->seen) {
        window->seen = true;
        window->top  = counter;
        memset(window->bits, 0, sizeof window->bits);
    } else if (counter > window->top) {
        uint64_t current = window->top >> 6;
        uint64_t next    = counter >> 6;

        if (next - current >= MP_RELAY_WINDOW_WORDS) {
            memset(window->bits, 0, sizeof window->bits);
        } else {
            uint64_t word;

            for (word = current + 1u; word <= next; ++word) {
                window->bits[word % MP_RELAY_WINDOW_WORDS] = 0u;
            }
        }
        window->top = counter;
    } else if (window->top - counter >= MP_RELAY_WINDOW_SPAN) {
        return false;
    } else if ((window->bits[word_of(counter)] & bit_of(counter)) != 0u) {
        return false;
    }
    window->bits[word_of(counter)] |= bit_of(counter);
    return true;
}

/* ==============================================================================================
 * The leg.
 * ============================================================================================ */

void mp_relay_leg_init(mp_relay_leg_t *leg, const mp_relay_keys_t *keys)
{
    memset(leg, 0, sizeof *leg);
    memcpy(leg->send_key, keys->initiator_to_responder, sizeof leg->send_key);
    memcpy(leg->receive_key, keys->responder_to_initiator, sizeof leg->receive_key);
    leg->live = true;
}

size_t mp_relay_leg_seal(mp_relay_leg_t *leg, const mp_relay_sealed_header_t *header,
                         const uint8_t *plain, size_t bytes, uint8_t *out, size_t capacity)
{
    mp_relay_sealed_header_t stamped;
    size_t                   total = MP_RELAY_SEALED_OVERHEAD + bytes;

    if (leg == NULL || !leg->live || header == NULL || out == NULL || capacity < total ||
        total > MP_RELAY_DATAGRAM_MAX || (plain == NULL && bytes != 0u)) {
        return 0u;
    }
    stamped         = *header;
    stamped.counter = leg->next++;
    mp_relay_sealed_header_put(&stamped, out);
    mp_relay_aead_seal(out + MP_RELAY_SEALED_HEADER_BYTES,
                       out + MP_RELAY_SEALED_HEADER_BYTES + bytes, leg->send_key, stamped.counter,
                       out, MP_RELAY_SEALED_HEADER_BYTES, plain, bytes);
    return total;
}

bool mp_relay_leg_open(mp_relay_leg_t *leg, const uint8_t *datagram, size_t bytes, uint8_t *plain,
                       size_t capacity, size_t *plain_bytes)
{
    mp_relay_sealed_header_t header;
    size_t                   sealed;

    if (leg == NULL || !leg->live || plain == NULL || plain_bytes == NULL ||
        !mp_relay_sealed_header_read(datagram, bytes, &header)) {
        return false;
    }
    sealed = bytes - MP_RELAY_SEALED_OVERHEAD;
    if (capacity < sealed || !mp_relay_window_check(&leg->window, header.counter)) {
        return false;
    }
    if (!mp_relay_aead_open(plain, leg->receive_key, header.counter, datagram,
                            MP_RELAY_SEALED_HEADER_BYTES, datagram + MP_RELAY_SEALED_HEADER_BYTES,
                            sealed, datagram + MP_RELAY_SEALED_HEADER_BYTES + sealed)) {
        ++leg->failed;
        return false;
    }
    if (!mp_relay_window_accept(&leg->window, header.counter)) {
        return false;
    }
    *plain_bytes = sealed;
    return true;
}

void mp_relay_leg_wipe(mp_relay_leg_t *leg)
{
    if (leg != NULL) {
        mp_relay_wipe(leg, sizeof *leg);
    }
}

/* ==============================================================================================
 * The proofs.
 * ============================================================================================ */

bool mp_relay_mac16(const uint8_t key[MP_RELAY_SECRET_BYTES], const uint8_t *message, size_t bytes,
                    uint8_t out[MP_RELAY_PROOF_BYTES])
{
    uint8_t full[MP_RELAY_HASH_BYTES];

    if (!mp_relay_hmac_sha256(full, key, MP_RELAY_SECRET_BYTES, MAC_LABEL, sizeof MAC_LABEL,
                              message, bytes)) {
        return false;
    }
    memcpy(out, full, MP_RELAY_PROOF_BYTES);
    mp_relay_wipe(full, sizeof full);
    return true;
}

static bool proof(const uint8_t label[8], const uint8_t secret[MP_RELAY_SECRET_BYTES],
                  uint32_t epoch, const uint8_t cookie[MP_RELAY_COOKIE_MAC_BYTES], uint64_t nonce,
                  uint8_t key_id, uint8_t out[MP_RELAY_PROOF_BYTES])
{
    uint8_t message[PROOF_MESSAGE_BYTES];
    size_t  i;

    memcpy(message, label, 8u);
    for (i = 0; i < 4u; ++i) {
        message[8u + i] = (uint8_t)(epoch >> (8u * i));
    }
    memcpy(message + 12u, cookie, MP_RELAY_COOKIE_MAC_BYTES);
    for (i = 0; i < 8u; ++i) {
        message[28u + i] = (uint8_t)(nonce >> (8u * i));
    }
    message[36] = key_id;
    return mp_relay_mac16(secret, message, sizeof message, out);
}

bool mp_relay_resume_proof(const uint8_t host_secret[MP_RELAY_SECRET_BYTES], uint32_t epoch,
                           const uint8_t cookie[MP_RELAY_COOKIE_MAC_BYTES], uint64_t nonce,
                           uint8_t key_id, uint8_t out[MP_RELAY_PROOF_BYTES])
{
    return proof(RESUME_LABEL, host_secret, epoch, cookie, nonce, key_id, out);
}

bool mp_relay_seat_proof(const uint8_t seat_secret[MP_RELAY_SECRET_BYTES], uint32_t epoch,
                         const uint8_t cookie[MP_RELAY_COOKIE_MAC_BYTES], uint64_t nonce,
                         uint8_t key_id, uint8_t out[MP_RELAY_PROOF_BYTES])
{
    return proof(SEAT_LABEL, seat_secret, epoch, cookie, nonce, key_id, out);
}
