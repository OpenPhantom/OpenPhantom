/* mp_relay_noise.c: the relay handshake, the legs and the proofs.
 *
 * The relay's vector file carries two whole Noise NK exchanges with fixed ephemeral keys, a host's
 * Register and a player's Join, with every byte that crossed the wire and both keys and the hash
 * each side held afterwards; two sealed datagrams whose counters are not symmetric, so a big endian
 * nonce shows; and the two proofs. A client that reproduces all of them speaks the protocol. The
 * window's edges and a forged answer come after, because both are rules the vectors cannot show.
 */
#include "unittest.h"

#include "mp_relay_noise.h"
#include "mp_relay_vectors.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define EPOCH   15000000u
#define NONCE   0x0807060504030201ull

static const mp_relay_vector_t *vector(const char *name)
{
    size_t i;

    for (i = 0; i < MP_RELAY_VECTOR_COUNT; ++i) {
        if (strcmp(MP_RELAY_VECTORS[i].name, name) == 0) {
            return &MP_RELAY_VECTORS[i];
        }
    }
    ut_check(false, name);
    return NULL;
}

static bool same(const char *name, const uint8_t *bytes, size_t size)
{
    const mp_relay_vector_t *v = vector(name);

    return v != NULL && v->size == size && memcmp(v->bytes, bytes, size) == 0;
}

static void run_of(uint8_t *out, size_t bytes, uint8_t first)
{
    size_t i;

    for (i = 0; i < bytes; ++i) {
        out[i] = (uint8_t)(first + i);
    }
}

static mp_relay_cookie_t the_cookie(uint8_t role)
{
    mp_relay_cookie_t cookie;

    memset(&cookie, 0, sizeof cookie);
    cookie.role  = role;
    cookie.nonce = NONCE;
    cookie.epoch = EPOCH;
    run_of(cookie.cookie, sizeof cookie.cookie, 0xA0u);
    return cookie;
}

static void test_the_register_handshake(void)
{
    const mp_relay_vector_t   *answer = vector("handshake_registered_answer");
    const mp_relay_vector_t   *relay  = vector("handshake_static_public");
    mp_relay_cookie_t          cookie = the_cookie((uint8_t)MP_RELAY_ROLE_HOST);
    mp_relay_register_body_t   body;
    mp_relay_registered_body_t registered;
    mp_relay_nk_t              nk;
    mp_relay_keys_t            keys;
    uint8_t                    plain[MP_RELAY_REGISTER_BODY_BYTES];
    uint8_t                    e_private[MP_RELAY_KEY_BYTES];
    uint8_t                    request[MP_RELAY_REQUEST_BYTES];
    uint8_t                    got[MP_RELAY_REGISTERED_BODY_BYTES];
    uint8_t                    forged[MP_RELAY_REGISTERED_BYTES];
    size_t                     got_bytes = 0;
    size_t                     n;

    ut_section("a host's Register, the whole exchange");
    memset(&body, 0, sizeof body);
    body.flags = (uint8_t)MP_RELAY_REGISTER_HAS_KEY;
    body.seats = 3u;
    run_of(body.key, sizeof body.key, 0x40u);
    (void)mp_relay_register_body_encode(&body, plain, sizeof plain);
    run_of(e_private, sizeof e_private, 0x20u);
    n = mp_relay_build_request((uint8_t)MP_RELAY_TYPE_REGISTER, &cookie, NONCE, 4u, relay->bytes,
                               plain, sizeof plain, e_private, &nk, request, sizeof request);
    ut_check(n == MP_RELAY_REQUEST_BYTES && same("handshake_register_request", request, n),
             "the request is the relay's, byte for byte");

    memcpy(forged, answer->bytes, answer->size);
    forged[100] = (uint8_t)(forged[100] ^ 0x01u);
    ut_check(!mp_relay_nk_read2(&nk, forged + MP_RELAY_ANSWER_EPHEMERAL_OFFSET,
                                answer->size - MP_RELAY_ANSWER_EPHEMERAL_OFFSET, got, sizeof got,
                                &got_bytes, &keys),
             "an answer changed by one bit does not open");
    ut_check(mp_relay_nk_read2(&nk, answer->bytes + MP_RELAY_ANSWER_EPHEMERAL_OFFSET,
                               answer->size - MP_RELAY_ANSWER_EPHEMERAL_OFFSET, got, sizeof got,
                               &got_bytes, &keys),
             "and the real answer still opens after it: the forged one changed nothing");
    ut_check(got_bytes == MP_RELAY_REGISTERED_BODY_BYTES &&
                 mp_relay_registered_body_decode(got, got_bytes, &registered) &&
                 registered.session_index == 5u && registered.code[0] == 0x12u &&
                 registered.refresh_ms == 20000u,
             "the Registered body inside is the one the relay sealed");
    ut_check(same("handshake_key_initiator_to_responder", keys.initiator_to_responder, 32u) &&
                 same("handshake_key_responder_to_initiator", keys.responder_to_initiator, 32u),
             "both leg keys");
    ut_check(same("handshake_hash", keys.hash, 32u), "and the handshake hash");
}

static void test_the_join_handshake(void)
{
    const mp_relay_vector_t *answer = vector("handshake_joined_answer");
    const mp_relay_vector_t *relay  = vector("handshake_static_public");
    const mp_relay_vector_t *proof  = vector("derive_seat_proof_epoch_15000000_key_id_4");
    const mp_relay_vector_t *secret = vector("derive_seat_secret");
    mp_relay_cookie_t        cookie = the_cookie((uint8_t)MP_RELAY_ROLE_MEMBER);
    mp_relay_join_body_t     body;
    mp_relay_joined_body_t   joined;
    mp_relay_nk_t            nk;
    mp_relay_keys_t          keys;
    uint8_t                  plain[MP_RELAY_JOIN_BODY_BYTES];
    uint8_t                  e_private[MP_RELAY_KEY_BYTES];
    uint8_t                  request[MP_RELAY_REQUEST_BYTES];
    uint8_t                  got[MP_RELAY_JOINED_BODY_BYTES];
    size_t                   got_bytes = 0;
    size_t                   n;

    ut_section("a player's Join with a seat proof, the whole exchange");
    memset(&body, 0, sizeof body);
    body.code[0] = 0x12u;
    body.code[1] = 0x34u;
    body.code[2] = 0x56u;
    body.code[3] = 0x78u;
    body.code[4] = 0x9Au;
    body.flags   = (uint8_t)MP_RELAY_JOIN_HAS_PROOF;
    run_of(body.r, sizeof body.r, 0xC0u);
    memcpy(body.proof, proof->bytes, sizeof body.proof);
    (void)mp_relay_join_body_encode(&body, plain, sizeof plain);
    run_of(e_private, sizeof e_private, 0x40u);
    n = mp_relay_build_request((uint8_t)MP_RELAY_TYPE_JOIN, &cookie, NONCE, 4u, relay->bytes, plain,
                               sizeof plain, e_private, &nk, request, sizeof request);
    ut_check(n == MP_RELAY_REQUEST_BYTES && same("handshake_join_request", request, n),
             "the request is the relay's, byte for byte");
    ut_check(mp_relay_nk_read2(&nk, answer->bytes + MP_RELAY_ANSWER_EPHEMERAL_OFFSET,
                               answer->size - MP_RELAY_ANSWER_EPHEMERAL_OFFSET, got, sizeof got,
                               &got_bytes, &keys) &&
                 mp_relay_joined_body_decode(got, got_bytes, &joined) &&
                 joined.member_index == 44u && joined.gen == 3u &&
                 memcmp(joined.seat_secret, secret->bytes, 32u) == 0,
             "the answer opens, and the Joined body carries the derived seat secret");
    ut_check(same("handshake_join_key_initiator_to_responder", keys.initiator_to_responder, 32u) &&
                 same("handshake_join_key_responder_to_initiator", keys.responder_to_initiator,
                      32u) &&
                 same("handshake_join_hash", keys.hash, 32u),
             "both leg keys and the hash");
}

static void test_the_sealed_samples(void)
{
    const mp_relay_vector_t *opened = vector("sealed_member_open_relay_to_host_index_5_counter_"
                                             "0102030405060708");
    const mp_relay_vector_t *inner  = vector("inner_member_open_continues");
    const mp_relay_vector_t *data   = vector("sealed_data_member_to_relay_gen_3_index_44_counter_"
                                             "1112131415161718");
    mp_relay_leg_t           leg;
    mp_relay_sealed_header_t header;
    uint8_t                  plain[64];
    uint8_t                  out[64];
    size_t                   plain_bytes = 0;
    size_t                   n;

    ut_section("the record layer against the two sealed samples");
    memset(&leg, 0, sizeof leg);
    leg.live = true;
    run_of(leg.receive_key, sizeof leg.receive_key, 0x00u);
    ut_check(mp_relay_leg_open(&leg, opened->bytes, opened->size, plain, sizeof plain,
                               &plain_bytes) &&
                 plain_bytes == inner->size && memcmp(plain, inner->bytes, inner->size) == 0,
             "a MemberOpen sealed by the relay opens to the inner message");
    ut_check(!mp_relay_leg_open(&leg, opened->bytes, opened->size, plain, sizeof plain,
                                &plain_bytes),
             "and a second copy of it is a replay");

    memset(&leg, 0, sizeof leg);
    leg.live = true;
    run_of(leg.send_key, sizeof leg.send_key, 0x20u);
    leg.next = 0x1112131415161718ull;
    memset(&header, 0, sizeof header);
    header.type  = (uint8_t)MP_RELAY_TYPE_MEMBER_TO_RELAY;
    header.gen   = 3u;
    header.index = 44u;
    memcpy(plain, "SMPO", 4u);
    run_of(plain + 4, 12u, 0x01u);
    n = mp_relay_leg_seal(&leg, &header, plain, 16u, out, sizeof out);
    ut_check(n == data->size && memcmp(out, data->bytes, n) == 0,
             "a player's game packet seals to the sample, counter and all");
    ut_check(leg.next == 0x1112131415161719ull, "and the counter moved on by one");
}

static void test_the_proofs(void)
{
    const mp_relay_vector_t *host = vector("derive_host_secret_session_1122334455667788");
    const mp_relay_vector_t *seat = vector("derive_seat_secret");
    uint8_t                  cookie[MP_RELAY_COOKIE_MAC_BYTES];
    uint8_t                  out[MP_RELAY_PROOF_BYTES];

    ut_section("the two proofs");
    run_of(cookie, sizeof cookie, 0xA0u);
    ut_check(mp_relay_resume_proof(host->bytes, EPOCH, cookie, NONCE, 4u, out) &&
                 same("derive_resume_proof_epoch_15000000_key_id_4", out, sizeof out),
             "a resume proof from the host secret");
    ut_check(mp_relay_seat_proof(seat->bytes, EPOCH, cookie, NONCE, 4u, out) &&
                 same("derive_seat_proof_epoch_15000000_key_id_4", out, sizeof out),
             "a seat proof from the seat secret");
    ut_check(mp_relay_seat_proof(seat->bytes, EPOCH, cookie, NONCE, 5u, out) &&
                 !same("derive_seat_proof_epoch_15000000_key_id_4", out, sizeof out),
             "a proof made for another relay key is another proof");
}

static void test_the_window(void)
{
    mp_relay_window_t window;
    uint64_t          n;
    bool              every = true;

    ut_section("the replay window");
    memset(&window, 0, sizeof window);
    ut_check(mp_relay_window_accept(&window, 0u), "the first counter is taken, zero included");
    ut_check(!mp_relay_window_accept(&window, 0u), "and not twice");
    ut_check(mp_relay_window_accept(&window, 5000u), "a counter far ahead moves the window");
    ut_check(!mp_relay_window_check(&window, 5000u - 960u), "960 behind the newest is refused");
    ut_check(mp_relay_window_accept(&window, 5000u - 959u), "959 behind is still taken");
    ut_check(!mp_relay_window_accept(&window, 5000u - 959u), "once");
    for (n = 4100u; n < 4200u; n += 3u) {
        every = every && mp_relay_window_accept(&window, n);
    }
    ut_check(every, "counters arriving out of order inside the window are each taken");
    ut_check(mp_relay_window_accept(&window, 5000u + 64u * 20u),
             "a jump past every word clears the window");
    ut_check(mp_relay_window_check(&window, 5000u + 64u * 20u - 1u),
             "and a counter just behind the new newest is free again");
}

static void test_a_forged_packet_spends_nothing(void)
{
    mp_relay_leg_t           relay;
    mp_relay_leg_t           mine;
    mp_relay_sealed_header_t header;
    uint8_t                  datagram[64];
    uint8_t                  plain[64];
    size_t                   n;
    size_t                   plain_bytes = 0;

    ut_section("a packet that does not open spends no counter");
    memset(&relay, 0, sizeof relay);
    relay.live = true;
    run_of(relay.send_key, sizeof relay.send_key, 0x55u);
    memset(&mine, 0, sizeof mine);
    mine.live = true;
    run_of(mine.receive_key, sizeof mine.receive_key, 0x55u);
    memset(&header, 0, sizeof header);
    header.type  = (uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER;
    header.index = 44u;
    n = mp_relay_leg_seal(&relay, &header, (const uint8_t *)"packet", 6u, datagram,
                          sizeof datagram);
    datagram[n - 1u] = (uint8_t)(datagram[n - 1u] ^ 0x80u);
    ut_check(!mp_relay_leg_open(&mine, datagram, n, plain, sizeof plain, &plain_bytes) &&
                 mine.failed == 1u,
             "a forged copy is refused and counted");
    datagram[n - 1u] = (uint8_t)(datagram[n - 1u] ^ 0x80u);
    ut_check(mp_relay_leg_open(&mine, datagram, n, plain, sizeof plain, &plain_bytes) &&
                 plain_bytes == 6u && memcmp(plain, "packet", 6u) == 0,
             "and the real packet under the same counter still opens");
}

int main(void)
{
    test_the_register_handshake();
    test_the_join_handshake();
    test_the_sealed_samples();
    test_the_proofs();
    test_the_window();
    test_a_forged_packet_spends_nothing();
    return ut_summary("mp_relay_noise");
}
