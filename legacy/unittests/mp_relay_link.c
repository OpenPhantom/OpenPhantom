/* mp_relay_link.c: the link's handshake, its retries and every refusal a relay can answer with.
 *
 * The fake relay these tests talk to is proven first: fed the relay's own recorded requests and
 * ephemeral keys, it writes the relay's recorded answers byte for byte. Everything after is the
 * link against that relay, on a clock the test turns by hand.
 */
#include "unittest.h"

#include "mp_relay_fake.h"
#include "mp_relay_link.h"
#include "mp_relay_vectors.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define T0 100000u

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

static void test_the_fake_relay_is_the_relay(void)
{
    const mp_relay_vector_t *register_request = vector("handshake_register_request");
    const mp_relay_vector_t *registered       = vector("handshake_registered_answer");
    const mp_relay_vector_t *join_request     = vector("handshake_join_request");
    const mp_relay_vector_t *joined           = vector("handshake_joined_answer");
    const mp_relay_vector_t *seat_secret      = vector("derive_seat_secret");
    fake_relay_t             relay;
    uint8_t                  e_private[MP_RELAY_KEY_BYTES];
    uint8_t                  out[MP_RELAY_DATAGRAM_MAX];
    size_t                   n;

    ut_section("the fake relay answers the recorded requests with the recorded answers");
    fake_relay_init(&relay);
    ut_check(memcmp(relay.static_public, vector("handshake_static_public")->bytes, 32u) == 0,
             "its static key is the vectors' key");
    fake_run_of(e_private, sizeof e_private, 0x60u);
    n = fake_answer_with(&relay, register_request->bytes, register_request->size, e_private, out,
                         sizeof out);
    ut_check(n == registered->size && memcmp(out, registered->bytes, n) == 0,
             "a Register opens and the Registered is the relay's, byte for byte");
    ut_check(memcmp(relay.keys.initiator_to_responder,
                    vector("handshake_key_initiator_to_responder")->bytes, 32u) == 0,
             "and it holds the same leg keys");

    relay.joined.flags = (uint8_t)MP_RELAY_JOINED_CONTINUED;
    memcpy(relay.joined.seat_secret, seat_secret->bytes, 32u);
    fake_run_of(e_private, sizeof e_private, 0x80u);
    n = fake_answer_with(&relay, join_request->bytes, join_request->size, e_private, out,
                         sizeof out);
    ut_check(n == joined->size && memcmp(out, joined->bytes, n) == 0,
             "a Join opens and the Joined is the relay's, byte for byte");
    ut_check(relay.body_bytes == MP_RELAY_JOIN_BODY_BYTES && relay.body[5] == 0x01u,
             "and it kept the Join's body, proof flag and all");
}

/* A host link with its key, at T0. */
static void host_link(mp_relay_link_t *link, fake_relay_t *relay)
{
    mp_relay_key_t key;

    fake_relay_init(relay);
    key = fake_relay_key(relay);
    mp_relay_link_init(link, fake_random);
    mp_relay_link_host(link, 3u);
    mp_relay_link_give_key(link, &key, T0);
}

/* Hello, cookie: the link's request at `now`. */
static size_t to_request(mp_relay_link_t *link, fake_relay_t *relay, uint32_t now, uint8_t *out)
{
    uint8_t               in[MP_RELAY_COOKIE_BYTES];
    mp_relay_link_event_t event;
    uint8_t               role  = 0u;
    uint64_t              nonce = 0u;
    size_t                n     = fake_tick(link, now, out, &event);

    if (!fake_hello_read(out, n, &role, &nonce)) {
        return 0u;
    }
    (void)mp_relay_link_receive(link, now, in, fake_cookie(relay, role, nonce, in), NULL, 0u,
                                &event);
    return fake_tick(link, now, out, &event);
}

static void test_a_host_registers(void)
{
    mp_relay_link_t          link;
    fake_relay_t             relay;
    mp_relay_key_t           key;
    mp_relay_link_event_t    event;
    uint8_t                  out[MP_RELAY_DATAGRAM_MAX];
    uint8_t                  first[MP_RELAY_HELLO_BYTES];
    uint8_t                  in[MP_RELAY_DATAGRAM_MAX];
    uint8_t                  code[MP_RELAY_CODE_BYTES];
    uint8_t                  role  = 0u;
    uint64_t                 nonce = 0u;
    size_t                   n;

    ut_section("a host registers");
    fake_relay_init(&relay);
    key = fake_relay_key(&relay);
    mp_relay_link_init(&link, fake_random);
    mp_relay_link_host(&link, 3u);
    ut_check(fake_tick(&link, T0, out, &event) == 0u && link.phase == MP_RELAY_LINK_NEEDS_KEY,
             "without a key nothing goes out");
    mp_relay_link_give_key(&link, &key, T0);
    n = fake_tick(&link, T0, out, &event);
    ut_check(fake_hello_read(out, n, &role, &nonce) && role == MP_RELAY_ROLE_HOST,
             "with one the Hello goes out at once, for the host role");
    memcpy(first, out, sizeof first);
    ut_check(fake_tick(&link, T0 + 999u, out, &event) == 0u, "nothing again within the second");
    n = fake_tick(&link, T0 + 1000u, out, &event);
    ut_check(n == MP_RELAY_HELLO_BYTES && memcmp(out, first, n) == 0,
             "after it the same Hello again");

    n = fake_cookie(&relay, (uint8_t)MP_RELAY_ROLE_MEMBER, nonce, in);
    (void)mp_relay_link_receive(&link, T0 + 1001u, in, n, NULL, 0u, &event);
    n = fake_cookie(&relay, role, nonce + 1u, in);
    (void)mp_relay_link_receive(&link, T0 + 1001u, in, n, NULL, 0u, &event);
    ut_check(link.phase == MP_RELAY_LINK_COOKIE,
             "a cookie for another role or another nonce is not this Hello's");
    n = fake_cookie(&relay, role, nonce, in);
    (void)mp_relay_link_receive(&link, T0 + 1001u, in, n, NULL, 0u, &event);
    n = fake_tick(&link, T0 + 1001u, out, &event);
    ut_check(n == MP_RELAY_REQUEST_BYTES && out[0] == MP_RELAY_TYPE_REGISTER,
             "this Hello's cookie brings the Register at once");
    memcpy(in, out, n);
    ut_check(fake_tick(&link, T0 + 2000u, out, &event) == 0u &&
                 fake_tick(&link, T0 + 2001u, out, &event) == MP_RELAY_REQUEST_BYTES &&
                 memcmp(in, out, MP_RELAY_REQUEST_BYTES) == 0,
             "and a second later the same bytes, so a relay answers a repeat alike");

    n = fake_answer(&relay, out, MP_RELAY_REQUEST_BYTES, in, sizeof in);
    ut_check(n == MP_RELAY_REGISTERED_BYTES && relay.head.key_id == 4u &&
                 relay.head.nonce == nonce && relay.body[0] == 0u && relay.body[1] == 3u,
             "the relay opens it: key id 4, the Hello's nonce, no flags, three seats");
    in[100] = (uint8_t)(in[100] ^ 0x01u);
    ut_check(!mp_relay_link_receive(&link, T0 + 2002u, in, n, NULL, 0u, &event) &&
                 link.phase == MP_RELAY_LINK_REQUEST && link.unopened,
             "an answer changed by a bit is passed over and the wait goes on");
    in[100] = (uint8_t)(in[100] ^ 0x01u);
    ut_check(mp_relay_link_receive(&link, T0 + 2003u, in, n, NULL, 0u, &event) &&
                 event.kind == MP_RELAY_LINK_EVENT_READY && mp_relay_link_ready(&link),
             "the real one opens after it: READY");
    ut_check(mp_relay_link_code(&link, code) && memcmp(code, relay.registered.code, 5u) == 0 &&
                 link.epoch == relay.epoch,
             "the link holds the session's code and the relay's epoch");
    ut_check(memcmp(link.leg.send_key, relay.keys.initiator_to_responder, 32u) == 0 &&
                 memcmp(link.leg.receive_key, relay.keys.responder_to_initiator, 32u) == 0,
             "and the leg sends and receives under the initiator's keys");
    ut_check(link.nk.step == 2u && link.handshakes == 1u,
             "the handshake state is wiped, one handshake counted");
}

static void test_silence(void)
{
    mp_relay_link_t       link;
    fake_relay_t          relay;
    mp_relay_link_event_t event;
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    uint8_t               role   = 0u;
    uint64_t              first  = 0u;
    uint64_t              second = 0u;
    uint32_t              i;
    uint32_t              sent = 0u;

    ut_section("a relay that does not answer");
    host_link(&link, &relay);
    for (i = 0; i <= 4000u; i += 250u) {
        size_t n = fake_tick(&link, T0 + i, out, &event);

        sent += n != 0u ? 1u : 0u;
        if (n != 0u && first == 0u) {
            (void)fake_hello_read(out, n, &role, &first);
        }
    }
    ut_check(sent == 4u, "four Hellos a second apart");
    ut_check(link.phase == MP_RELAY_LINK_BACKOFF && link.failure == MP_RELAY_LINK_NO_ANSWER,
             "then the attempt ends: no answer, and a wait");
    ut_check(fake_tick(&link, T0 + 8999u, out, &event) == 0u, "five seconds of it");
    ut_check(fake_hello_read(out, fake_tick(&link, T0 + 9000u, out, &event), &role, &second) &&
                 second != first,
             "then a new attempt under a new nonce");

    ut_section("a relay that gives a cookie and never answers the request");
    host_link(&link, &relay);
    ut_check(to_request(&link, &relay, T0, out) == MP_RELAY_REQUEST_BYTES, "the request is out");
    sent = 1u;
    for (i = 250u; i <= 4000u; i += 250u) {
        sent += fake_tick(&link, T0 + i, out, &event) != 0u ? 1u : 0u;
    }
    ut_check(sent == 4u && link.phase == MP_RELAY_LINK_BACKOFF &&
                 link.failure == MP_RELAY_LINK_NO_ANSWER,
             "four requests, then no answer");
}

/* A link whose request is out, with an open Nack of `reason` arriving 10 ms later. */
static void refused_with(mp_relay_link_t *link, fake_relay_t *relay, uint8_t reason,
                         uint8_t *request)
{
    uint8_t               in[MP_RELAY_NACK_BYTES];
    mp_relay_link_event_t event;

    host_link(link, relay);
    (void)to_request(link, relay, T0, request);
    (void)mp_relay_link_receive(link, T0 + 10u, in, fake_open_nack(reason, 0u, in), NULL, 0u,
                                &event);
}

static void test_refusals(void)
{
    mp_relay_link_t       link;
    fake_relay_t          relay;
    mp_relay_link_event_t event;
    uint8_t               request[MP_RELAY_DATAGRAM_MAX];
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    uint8_t               in[MP_RELAY_DATAGRAM_MAX];
    size_t                n;
    uint32_t              round;

    ut_section("an open Nack waits out its grace");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_FULL, request);
    ut_check(fake_tick(&link, T0 + 259u, out, &event) == 0u && link.phase == MP_RELAY_LINK_REQUEST,
             "within 250 ms nothing is decided and nothing is sent");
    n = fake_answer(&relay, request, MP_RELAY_REQUEST_BYTES, in, sizeof in);
    ut_check(mp_relay_link_receive(&link, T0 + 259u, in, n, NULL, 0u, &event) &&
                 event.kind == MP_RELAY_LINK_EVENT_READY,
             "an answer that opens in the grace overturns it");

    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_FULL, request);
    (void)fake_tick(&link, T0 + 260u, out, &event);
    ut_check(link.phase == MP_RELAY_LINK_FAILED && link.failure == MP_RELAY_LINK_FULL,
             "a Nack 3 nothing overturned ends the link: full");
    ut_check(fake_tick(&link, T0 + 99999u, out, &event) == 0u, "and a failed link stays quiet");

    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_CLOSED, request);
    (void)fake_tick(&link, T0 + 260u, out, &event);
    ut_check(link.failure == MP_RELAY_LINK_CLOSED, "a Nack 9 ends it: closed");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_VERSION, request);
    (void)fake_tick(&link, T0 + 260u, out, &event);
    ut_check(link.failure == MP_RELAY_LINK_VERSION, "a Nack 2 ends it: version");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_KEY_REJECTED, request);
    (void)fake_tick(&link, T0 + 260u, out, &event);
    ut_check(link.failure == MP_RELAY_LINK_KEY_REJECTED, "a Nack 5 ends it: key rejected");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_UNKNOWN_CODE, request);
    (void)fake_tick(&link, T0 + 260u, out, &event);
    ut_check(link.phase == MP_RELAY_LINK_FAILED, "a Nack 4 on a first attempt ends it");

    ut_section("a Nack 6 asks for a new cookie, twice");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_BAD_COOKIE, request);
    {
        uint32_t at = T0 + 260u;

        for (round = 0; round < 2u; ++round) {
            uint8_t  role  = 0u;
            uint64_t nonce = 0u;

            (void)fake_tick(&link, at, out, &event);
            ut_check(link.phase == MP_RELAY_LINK_BACKOFF &&
                         fake_tick(&link, at + 199u, out, &event) == 0u,
                     "the refusal is believed after its grace, and a pause of 200 ms follows");
            n = fake_tick(&link, at + 200u, out, &event);
            ut_check(fake_hello_read(out, n, &role, &nonce), "then a Hello for a new cookie");
            (void)mp_relay_link_receive(&link, at + 200u, in, fake_cookie(&relay, role, nonce, in),
                                        NULL, 0u, &event);
            ut_check(fake_tick(&link, at + 200u, out, &event) == MP_RELAY_REQUEST_BYTES,
                     "and a request under it");
            (void)mp_relay_link_receive(&link, at + 210u, in, fake_open_nack(6u, 0u, in), NULL,
                                        0u, &event);
            at += 460u;
        }
        (void)fake_tick(&link, at, out, &event);
        ut_check(link.phase == MP_RELAY_LINK_BACKOFF && link.failure == MP_RELAY_LINK_REFUSED &&
                     link.backoff_until == at + MP_RELAY_BACKOFF_MS,
                 "the third refusal ends the attempt, and the link waits five seconds");
    }

    ut_section("a Nack 7 is waited out twice, the third ends the link");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_RATE_LIMITED, request);
    for (round = 0; round < 3u; ++round) {
        uint32_t at = T0 + round * 10000u;

        if (round != 0u) {
            (void)to_request(&link, &relay, at, request);
            (void)mp_relay_link_receive(&link, at + 10u, in, fake_open_nack(7u, 0u, in), NULL, 0u,
                                        &event);
        }
        (void)fake_tick(&link, at + 260u, out, &event);
    }
    ut_check(link.phase == MP_RELAY_LINK_FAILED && link.failure == MP_RELAY_LINK_RATE_LIMITED,
             "three in a row: rate limited");
}

static void test_the_key(void)
{
    mp_relay_link_t       link;
    fake_relay_t          relay;
    mp_relay_link_event_t event;
    mp_relay_key_t        key;
    uint8_t               request[MP_RELAY_DATAGRAM_MAX];
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    uint8_t               in[MP_RELAY_DATAGRAM_MAX];
    uint32_t              i;
    size_t                n;

    ut_section("a Nack 10: the relay does not know the key");
    refused_with(&link, &relay, (uint8_t)MP_RELAY_NACK_UNKNOWN_KEY, request);
    (void)fake_tick(&link, T0 + 260u, out, &event);
    ut_check(event.kind == MP_RELAY_LINK_EVENT_KEY_REFUSED && !event.unproven &&
                 link.phase == MP_RELAY_LINK_NEEDS_KEY && !link.have_key &&
                 link.failure == MP_RELAY_LINK_UNKNOWN_KEY,
             "the link says so once and waits for a key");
    ut_check(fake_tick(&link, T0 + 20000u, out, &event) == 0u, "and sends nothing while it waits");
    key = fake_relay_key(&relay);
    mp_relay_link_give_key(&link, &key, T0 + 20000u);
    ut_check(fake_tick(&link, T0 + 20000u, out, &event) == MP_RELAY_HELLO_BYTES,
             "a key given brings a new Hello at once");

    ut_section("answers that never open under the key");
    host_link(&link, &relay);
    (void)to_request(&link, &relay, T0, request);
    relay.static_private[0] = (uint8_t)(relay.static_private[0] ^ 0x40u);
    relay.static_public[0]  = (uint8_t)(relay.static_public[0] ^ 0x01u);
    for (i = 0; i <= 4000u; i += 500u) {
        n = fake_tick(&link, T0 + i, out, &event);
        if (i < 4000u) {
            n = fake_answer(&relay, request, MP_RELAY_REQUEST_BYTES, in, sizeof in);
            ut_check(n == 0u || !mp_relay_link_receive(&link, T0 + i, in, n, NULL, 0u, &event),
                     "an answer under another key does not open");
            memset(in, 0, sizeof in);
            memcpy(in, request, 8u);
            in[0] = (uint8_t)MP_RELAY_TYPE_REGISTERED;
            (void)mp_relay_link_receive(&link, T0 + i, in, MP_RELAY_REGISTERED_BYTES, NULL, 0u,
                                        &event);
        }
    }
    ut_check(event.kind == MP_RELAY_LINK_EVENT_KEY_REFUSED && event.unproven &&
                 link.failure == MP_RELAY_LINK_UNPROVEN && link.phase == MP_RELAY_LINK_NEEDS_KEY,
             "after four tries the key is refused as unproven, and the link waits for one");
    ut_check(link.unopened_answers != 0u, "the answers that did not open were counted");
}

static void test_no_randomness(void)
{
    mp_relay_link_t       link;
    fake_relay_t          relay;
    mp_relay_link_event_t event;
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    int                   i;
    bool                  named = true;

    ut_section("no randomness, no handshake");
    fake_random_fails = true;
    host_link(&link, &relay);
    fake_random_fails = false;
    ut_check(link.phase == MP_RELAY_LINK_FAILED && link.failure == MP_RELAY_LINK_CRYPTO &&
                 fake_tick(&link, T0, out, &event) == 0u,
             "a generator that refuses ends the link before anything is sent");

    for (i = 0; i < (int)MP_RELAY_LINK_FAILURE_COUNT; ++i) {
        const char *text = mp_relay_link_failure_text((mp_relay_link_failure_t)i);

        named = named && text != NULL && strcmp(text, "an unnamed failure") != 0;
    }
    ut_check(named, "every failure has words of its own for the log");
}

int main(void)
{
    test_the_fake_relay_is_the_relay();
    test_a_host_registers();
    test_silence();
    test_refusals();
    test_the_key();
    test_no_randomness();
    return ut_summary("mp_relay_link");
}
