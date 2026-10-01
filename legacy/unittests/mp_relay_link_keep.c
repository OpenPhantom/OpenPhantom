/* mp_relay_link_keep.c: a host's link once it stands: refreshing, a new leg when the old one is
 * gone, the notices and the game packets it carries, and the goodbye.
 */
#include "unittest.h"

#include "mp_relay_fake.h"
#include "mp_relay_inner.h"
#include "mp_relay_link.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define T0 100000u

typedef struct host_rig {
    mp_relay_link_t       link;
    fake_relay_t          relay;
    mp_relay_link_event_t event;
    uint8_t               out[MP_RELAY_DATAGRAM_MAX];
    uint8_t               in[MP_RELAY_DATAGRAM_MAX];
    uint8_t               plain[MP_RELAY_DATAGRAM_MAX];
    uint8_t               inner[64];
    size_t                plain_bytes;
    mp_relay_sealed_header_t header;
} host_rig_t;

static bool host_at(host_rig_t *rig, uint32_t now)
{
    mp_relay_key_t key;

    memset(rig, 0, sizeof *rig);
    fake_relay_init(&rig->relay);
    key = fake_relay_key(&rig->relay);
    mp_relay_link_init(&rig->link, fake_random);
    mp_relay_link_host(&rig->link, 3u);
    mp_relay_link_give_key(&rig->link, &key, now);
    return fake_connect(&rig->link, &rig->relay, now);
}

static size_t tick(host_rig_t *rig, uint32_t now)
{
    return fake_tick(&rig->link, now, rig->out, &rig->event);
}

static bool deliver(host_rig_t *rig, uint32_t now, size_t bytes)
{
    return mp_relay_link_receive(&rig->link, now, rig->in, bytes, rig->plain, sizeof rig->plain,
                                 &rig->event);
}

/* The relay seals `bytes` of `rig->inner` toward the host into `rig->in`. */
static size_t to_host(host_rig_t *rig, size_t bytes)
{
    return fake_to_host(&rig->relay, rig->inner, bytes, rig->in);
}

/* The relay opens what the link just sent. */
static bool relay_opens(host_rig_t *rig, size_t bytes)
{
    return fake_open(&rig->relay, rig->out, bytes, &rig->header, rig->plain, sizeof rig->plain,
                     &rig->plain_bytes);
}

static uint32_t le32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

static uint64_t le64(const uint8_t *at)
{
    return (uint64_t)le32(at) | ((uint64_t)le32(at + 4) << 32);
}

static void test_refreshing(void)
{
    static host_rig_t rig;
    uint8_t           announce[MP_RELAY_ANNOUNCE_BYTES];
    uint64_t          counter;
    size_t            n;

    ut_section("a host refreshes every RefreshMS");
    ut_check(host_at(&rig, T0), "the host is registered");
    ut_check(tick(&rig, T0 + 19999u) == 0u, "nothing before 20 seconds");
    (void)mp_relay_link_seal_game(&rig.link, 1u, 2u, (const uint8_t *)"SMPO", 4u, rig.out,
                                  sizeof rig.out);
    n = tick(&rig, T0 + 20000u);
    ut_check(n != 0u && relay_opens(&rig, n) &&
                 rig.header.type == MP_RELAY_TYPE_HOST_TO_RELAY_CONTROL && rig.header.index == 5u &&
                 rig.plain_bytes == MP_RELAY_REFRESH_BYTES &&
                 rig.plain[0] == MP_RELAY_INNER_REFRESH,
             "at 20 seconds a sealed Refresh for session index 5");
    ut_check(le32(rig.plain + 1) == 15000000u && rig.plain[5] == 0u,
             "naming the epoch of the Registered, unlisted");
    counter = rig.header.counter;
    ut_check(counter == 1u, "under the counter after the game packet sealed before it");

    n = to_host(&rig, fake_refresh_ack(15000009u, counter - 1u, rig.inner));
    (void)deliver(&rig, T0 + 20010u, n);
    ut_check(rig.link.keeping, "an ack for a counter sealed before the round is an old one");
    n = to_host(&rig, fake_refresh_ack(15000001u, counter, rig.inner));
    (void)deliver(&rig, T0 + 20020u, n);
    ut_check(!rig.link.keeping && rig.link.epoch == 15000001u && rig.link.refreshes_answered == 1u,
             "the ack for this round ends it and moves the epoch on");

    ut_check(tick(&rig, T0 + 20030u) == 0u, "the next round waits its 20 seconds");
    memset(announce, 0x5A, sizeof announce);
    mp_relay_link_set_listing(&rig.link, true, announce);
    n = tick(&rig, T0 + 20040u);
    ut_check(n != 0u && relay_opens(&rig, n) && le32(rig.plain + 1) == 15000001u &&
                 rig.plain[5] == MP_RELAY_REFRESH_LISTED && rig.plain[6] == 0x5Au &&
                 rig.plain[44] == 0x5Au,
             "a new listing goes out at once, with the new epoch, the flag and the announce");
    counter = rig.header.counter;
    (void)deliver(&rig, T0 + 20050u, to_host(&rig, fake_refresh_ack(15000001u, counter,
                                                                     rig.inner)));
    mp_relay_link_set_listing(&rig.link, true, announce);
    ut_check(tick(&rig, T0 + 40049u) == 0u && tick(&rig, T0 + 40050u) != 0u,
             "the same listing again changes nothing: the next round comes on time");

    ut_section("a nudge brings the refresh forward and changes no epoch");
    ut_check(host_at(&rig, T0), "a new host");
    n = fake_nudge(6u, 16000000u, rig.in);
    (void)deliver(&rig, T0 + 1000u, n);
    ut_check(tick(&rig, T0 + 1000u) == 0u, "a nudge for another session is not this host's");
    n = fake_nudge(5u, 16000000u, rig.in);
    (void)deliver(&rig, T0 + 1000u, n);
    n = tick(&rig, T0 + 1000u);
    ut_check(n != 0u && relay_opens(&rig, n) && le32(rig.plain + 1) == 15000000u &&
                 rig.link.nudges == 1u,
             "this session's nudge: a Refresh at once, with the epoch heard last");
}

/* Three refreshes unanswered from the round at `at`: the link begins a new leg. */
static void let_three_go_unanswered(host_rig_t *rig, uint32_t at)
{
    (void)tick(rig, at);
    (void)tick(rig, at + 3000u);
    (void)tick(rig, at + 6000u);
    (void)tick(rig, at + 9000u);
}

static void test_a_new_leg(void)
{
    static host_rig_t rig;
    uint8_t           proof[MP_RELAY_PROOF_BYTES];
    uint8_t           old_send[MP_RELAY_KEY_BYTES];
    uint8_t           role  = 0u;
    uint64_t          nonce = 0u;
    uint32_t          at    = T0 + 29000u;
    size_t            n;

    ut_section("three refreshes unanswered: a new leg that resumes the session");
    ut_check(host_at(&rig, T0), "the host is registered");
    let_three_go_unanswered(&rig, T0 + 20000u);
    ut_check(rig.link.renewing && rig.link.renewals == 1u && rig.link.leg.live &&
                 mp_relay_link_ready(&rig.link),
             "a new leg is under way and the old one still carries");
    memcpy(old_send, rig.link.leg.send_key, sizeof old_send);
    n = tick(&rig, at);
    ut_check(fake_hello_read(rig.out, n, &role, &nonce), "a Hello goes out");
    n = mp_relay_link_seal_game(&rig.link, 1u, 2u, (const uint8_t *)"SMPO", 4u, rig.out,
                                sizeof rig.out);
    ut_check(n == 36u && relay_opens(&rig, n) && memcmp(rig.plain, "SMPO", 4u) == 0,
             "game packets go on under the old leg meanwhile");
    (void)deliver(&rig, at, fake_cookie(&rig.relay, role, nonce, rig.in));
    n = tick(&rig, at);
    n = fake_answer(&rig.relay, rig.out, n, rig.in, sizeof rig.in);
    ut_check(n != 0u && rig.relay.body[0] == MP_RELAY_REGISTER_RESUME &&
                 le64(rig.relay.body + 36) == 0x1122334455667788ull,
             "the Register resumes the session by its id");
    ut_check(mp_relay_resume_proof(rig.relay.registered.host_secret, rig.relay.head.epoch,
                                   rig.relay.head.cookie, nonce, 4u, proof) &&
                 memcmp(rig.relay.body + 44, proof, sizeof proof) == 0,
             "and proves it with the host secret over this cookie, nonce and key id");
    ut_check(deliver(&rig, at + 5u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_READY &&
                 !rig.link.renewing && memcmp(rig.link.leg.send_key, old_send, 32u) != 0,
             "the answer brings a new leg");
    n = mp_relay_link_seal_game(&rig.link, 1u, 2u, (const uint8_t *)"SMPO", 4u, rig.out,
                                sizeof rig.out);
    ut_check(relay_opens(&rig, n) && rig.header.counter == 0u,
             "and the game packets after it go under the new leg from counter 0");

    ut_section("a new leg that fails leaves the old one in use");
    ut_check(host_at(&rig, T0), "a new host");
    let_three_go_unanswered(&rig, T0 + 20000u);
    (void)tick(&rig, at);
    (void)tick(&rig, at + 1000u);
    (void)tick(&rig, at + 2000u);
    (void)tick(&rig, at + 3000u);
    (void)tick(&rig, at + 4000u);
    ut_check(rig.link.phase == MP_RELAY_LINK_READY && rig.link.leg.live &&
                 rig.link.failure == MP_RELAY_LINK_NO_ANSWER &&
                 rig.link.next_keep_at == at + 4000u + MP_RELAY_KEEP_TRY_MS,
             "four Hellos unanswered: back on the old leg, the next keep three seconds on");

    ut_section("never more than one new leg in five seconds");
    n = to_host(&rig, fake_leg_nack((uint8_t)MP_RELAY_NACK_RECONNECT, 5u, rig.inner));
    (void)deliver(&rig, at + 4001u, n);
    ut_check(rig.link.reconnect_due, "a sealed Nack 11 asks for a new leg");
    (void)tick(&rig, at + 4999u);
    ut_check(!rig.link.renewing, "within five seconds of the last one it waits");
    (void)tick(&rig, at + 5000u);
    ut_check(rig.link.renewing && rig.link.renewals == 2u, "at five seconds the new leg begins");
}

static void test_open_nack_1(void)
{
    static host_rig_t rig;
    uint8_t           old_code[MP_RELAY_CODE_BYTES];
    uint8_t           code[MP_RELAY_CODE_BYTES];
    uint8_t           role  = 0u;
    uint64_t          nonce = 0u;
    uint32_t          at    = T0 + 20000u;
    size_t            n;

    ut_section("an open Nack 1 in a refresh round");
    ut_check(host_at(&rig, T0), "the host is registered");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST, 1u, 2u, 5u,
                  (const uint8_t *)"SMPO", 4u, rig.in, sizeof rig.in);
    ut_check(deliver(&rig, at - 1000u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_GAME,
             "a game packet opened a second before");
    (void)tick(&rig, at);
    (void)deliver(&rig, at + 10u,
                  fake_open_nack((uint8_t)MP_RELAY_NACK_UNKNOWN_INDEX, 5u, rig.in));
    ut_check(tick(&rig, at + 259u) == 0u && rig.link.keeping, "is weighed after its grace");
    (void)tick(&rig, at + 260u);
    ut_check(!rig.link.keeping && !rig.link.renewing &&
                 rig.link.next_keep_at == at + 260u + MP_RELAY_KEEP_TRY_MS,
             "and with the leg alive it is passed over: the next round in three seconds");
    (void)tick(&rig, at + 3260u);
    (void)deliver(&rig, at + 3270u,
                  fake_open_nack((uint8_t)MP_RELAY_NACK_UNKNOWN_INDEX, 5u, rig.in));
    (void)tick(&rig, at + 3520u);
    ut_check(rig.link.renewing, "with nothing opened for three seconds it calls for a new leg");

    ut_section("a Nack 1 on the resume: the relay lost the session, the host registers afresh");
    memcpy(old_code, rig.relay.registered.code, sizeof old_code);
    n = tick(&rig, at + 3520u);
    ut_check(fake_hello_read(rig.out, n, &role, &nonce), "the resume's Hello");
    (void)deliver(&rig, at + 3520u, fake_cookie(&rig.relay, role, nonce, rig.in));
    ut_check(tick(&rig, at + 3520u) == MP_RELAY_REQUEST_BYTES, "its Register");
    (void)deliver(&rig, at + 3530u,
                  fake_open_nack((uint8_t)MP_RELAY_NACK_UNKNOWN_INDEX, 0u, rig.in));
    (void)tick(&rig, at + 3780u);
    n = tick(&rig, at + 3780u);
    ut_check(!rig.link.leg.live && !rig.link.registered_known &&
                 fake_hello_read(rig.out, n, &role, &nonce),
             "the old leg and session are dropped and a fresh attempt begins at once");
    (void)deliver(&rig, at + 3780u, fake_cookie(&rig.relay, role, nonce, rig.in));
    n = tick(&rig, at + 3780u);
    rig.relay.registered.code[4] = 0x9Bu;
    n = fake_answer(&rig.relay, rig.out, n, rig.in, sizeof rig.in);
    ut_check(n != 0u && rig.relay.body[0] == 0u, "a Register without the resume flag");
    ut_check(deliver(&rig, at + 3790u, n) && mp_relay_link_code(&rig.link, code) &&
                 memcmp(code, old_code, 4u) == 0 && code[4] == 0x9Bu,
             "and the session stands again under the new code");
}

/* The Hello and the Register of a new leg that the key limit asked for, at `at`. */
static bool new_leg_request(host_rig_t *rig, uint32_t at)
{
    uint8_t  role  = 0u;
    uint64_t nonce = 0u;

    rig->link.leg.next = MP_RELAY_LEG_KEY_LIMIT;
    (void)tick(rig, at);
    if (!rig->link.renewing || !fake_hello_read(rig->out, tick(rig, at), &role, &nonce)) {
        return false;
    }
    (void)deliver(rig, at, fake_cookie(&rig->relay, role, nonce, rig->in));
    return tick(rig, at) == MP_RELAY_REQUEST_BYTES;
}

static void test_a_new_leg_refused(void)
{
    static host_rig_t rig;
    mp_relay_key_t    key;
    uint8_t           code[MP_RELAY_CODE_BYTES];
    uint8_t           announce[MP_RELAY_ANNOUNCE_BYTES];
    size_t            n;

    ut_section("the relay refuses its key during a new leg: the old leg goes on");
    ut_check(host_at(&rig, T0), "the host is registered");
    ut_check(new_leg_request(&rig, T0 + 10u), "a new leg asks with its Register");
    (void)deliver(&rig, T0 + 20u, fake_open_nack((uint8_t)MP_RELAY_NACK_UNKNOWN_KEY, 0u, rig.in));
    (void)tick(&rig, T0 + 270u);
    ut_check(rig.event.kind == MP_RELAY_LINK_EVENT_KEY_REFUSED &&
                 rig.link.phase == MP_RELAY_LINK_READY && !rig.link.renewing &&
                 rig.link.leg.live && !rig.link.have_key,
             "the refusal is reported, and the link is back on the leg it had");
    n = tick(&rig, T0 + 3270u);
    ut_check(n != 0u && relay_opens(&rig, n) && rig.plain[0] == MP_RELAY_INNER_REFRESH &&
                 !rig.link.renewing,
             "it keeps refreshing that leg, and begins no new one without a key");
    (void)deliver(&rig, T0 + 3280u, to_host(&rig, fake_refresh_ack(15000000u,
                                                                     rig.header.counter,
                                                                     rig.inner)));
    ut_check(tick(&rig, T0 + 5500u) == 0u && !rig.link.renewing,
             "five seconds on a new leg would be due, and without a key none begins");
    key = fake_relay_key(&rig.relay);
    mp_relay_link_give_key(&rig.link, &key, T0 + 6000u);
    ut_check(tick(&rig, T0 + 6000u) == 0u && rig.link.renewing,
             "with a key again, the new leg the full one needs begins");

    ut_section("a listed host's new leg says its listing at once: the relay may have forgotten it");
    ut_check(host_at(&rig, T0), "a new host");
    memset(announce, 0x5A, sizeof announce);
    mp_relay_link_set_listing(&rig.link, true, announce);
    n = tick(&rig, T0 + 10u);
    ut_check(n != 0u && relay_opens(&rig, n), "the listing goes out");
    (void)deliver(&rig, T0 + 20u, to_host(&rig, fake_refresh_ack(15000000u, rig.header.counter,
                                                                  rig.inner)));
    ut_check(new_leg_request(&rig, T0 + 30u), "a new leg asks with its Register");
    n = fake_answer(&rig.relay, rig.out, MP_RELAY_REQUEST_BYTES, rig.in, sizeof rig.in);
    ut_check(deliver(&rig, T0 + 40u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_READY,
             "the resume is answered");
    n = tick(&rig, T0 + 40u);
    ut_check(n != 0u && relay_opens(&rig, n) && rig.plain[0] == MP_RELAY_INNER_REFRESH &&
                 rig.plain[5] == MP_RELAY_REFRESH_LISTED,
             "and the first thing on the new leg is a listed Refresh");

    ut_section("an open Nack 1 on a resume while the old leg still carries is not believed");
    ut_check(host_at(&rig, T0), "a new host");
    ut_check(new_leg_request(&rig, T0 + 10u), "a new leg asks with its Register");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST, 1u, 2u, 5u,
                  (const uint8_t *)"SMPO", 4u, rig.in, sizeof rig.in);
    ut_check(deliver(&rig, T0 + 15u, n), "a game packet opens on the old leg");
    (void)deliver(&rig, T0 + 20u,
                  fake_open_nack((uint8_t)MP_RELAY_NACK_UNKNOWN_INDEX, 0u, rig.in));
    (void)tick(&rig, T0 + 270u);
    ut_check(rig.link.registered_known && rig.link.leg.live &&
                 rig.link.phase == MP_RELAY_LINK_READY && mp_relay_link_code(&rig.link, code) &&
                 memcmp(code, rig.relay.registered.code, sizeof code) == 0,
             "the session and its code stand: anybody on the path can send an open Nack");
}

static void test_limits_and_revocation(void)
{
    static host_rig_t rig;
    size_t            n;

    ut_section("2^28 packets sealed on a leg: a new one");
    ut_check(host_at(&rig, T0), "the host is registered");
    rig.link.leg.next = MP_RELAY_LEG_KEY_LIMIT - 1u;
    (void)tick(&rig, T0 + 10u);
    ut_check(!rig.link.renewing, "one short of the limit the leg goes on");
    rig.link.leg.next = MP_RELAY_LEG_KEY_LIMIT;
    (void)tick(&rig, T0 + 20u);
    ut_check(rig.link.renewing, "at the limit a new leg begins");

    ut_section("a sealed Nack 8 ends the link");
    ut_check(host_at(&rig, T0), "a new host");
    n = to_host(&rig, fake_leg_nack((uint8_t)MP_RELAY_NACK_REVOKED, 5u, rig.inner));
    (void)deliver(&rig, T0 + 10u, n);
    ut_check(rig.link.phase == MP_RELAY_LINK_FAILED && rig.link.failure == MP_RELAY_LINK_REVOKED &&
                 !rig.link.leg.live && !mp_relay_link_ready(&rig.link),
             "revoked, and the leg's keys are gone");
    ut_check(mp_relay_link_seal_game(&rig.link, 0u, 0u, (const uint8_t *)"x", 1u, rig.out,
                                     sizeof rig.out) == 0u &&
                 mp_relay_link_farewell(&rig.link, rig.out, sizeof rig.out) == 0u,
             "nothing more is sealed");
}

static void test_what_the_leg_carries(void)
{
    static host_rig_t rig;
    uint8_t           r[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t           copy[MP_RELAY_DATAGRAM_MAX];
    uint8_t           game[4];
    size_t            n;

    ut_section("a host hears who comes and goes");
    ut_check(host_at(&rig, T0), "the host is registered");
    fake_run_of(r, sizeof r, 0x33u);
    n = to_host(&rig, fake_member_open(2u, 1u, 7u, r, rig.inner));
    ut_check(deliver(&rig, T0 + 10u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_MEMBER_OPEN &&
                 rig.event.slot == 2u && rig.event.gen == 7u && rig.event.flags == 1u &&
                 memcmp(rig.event.r, r, sizeof r) == 0 && rig.event.counter == 0u,
             "a MemberOpen: slot, generation, flags, seat value and the counter it came under");
    n = to_host(&rig, fake_member_closed(2u, 3u, 7u, rig.inner));
    ut_check(deliver(&rig, T0 + 20u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_MEMBER_CLOSED &&
                 rig.event.slot == 2u && rig.event.gen == 7u && rig.event.reason == 3u &&
                 rig.event.counter == 1u,
             "a MemberClosed: slot, generation, reason, counter");

    ut_section("game packets both ways");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST, 2u, 7u, 5u,
                  (const uint8_t *)"SMPO-GAME", 9u, rig.in, sizeof rig.in);
    memcpy(copy, rig.in, n);
    ut_check(deliver(&rig, T0 + 30u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_GAME &&
                 rig.event.slot == 2u && rig.event.gen == 7u && rig.event.bytes == 9u &&
                 memcmp(rig.plain, "SMPO-GAME", 9u) == 0,
             "a player's packet opens with its slot and generation");
    ut_check(!deliver(&rig, T0 + 31u, n) && rig.link.data_refused == 1u,
             "the same datagram again is a replay");
    memcpy(rig.in, copy, n);
    rig.in[20] = (uint8_t)(rig.in[20] ^ 0x01u);
    rig.in[8]  = (uint8_t)(rig.in[8] + 1u);
    ut_check(!deliver(&rig, T0 + 32u, n) && rig.link.data_refused == 2u,
             "a changed one does not open");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST, 2u, 7u, 6u,
                  (const uint8_t *)"SMPO", 4u, rig.in, sizeof rig.in);
    ut_check(!deliver(&rig, T0 + 33u, n) && rig.link.data_refused == 2u,
             "one for another session index is not even opened");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST, 2u, 7u, 5u,
                  (const uint8_t *)"SMPO-GAME", 9u, rig.in, sizeof rig.in);
    ut_check(!mp_relay_link_receive(&rig.link, T0 + 34u, rig.in, n, game, sizeof game,
                                    &rig.event),
             "a packet larger than the caller's buffer is not handed over");

    n = mp_relay_link_seal_game(&rig.link, 2u, 7u, (const uint8_t *)"HOST", 4u, rig.out,
                                sizeof rig.out);
    ut_check(relay_opens(&rig, n) && rig.header.type == MP_RELAY_TYPE_HOST_TO_RELAY &&
                 rig.header.slot == 2u && rig.header.gen == 7u && rig.header.index == 5u &&
                 rig.plain_bytes == 4u && memcmp(rig.plain, "HOST", 4u) == 0,
             "the host's packet is sealed for the player's slot and generation");
    ut_check(mp_relay_link_seal_game(&rig.link, 2u, 7u, rig.plain,
                                     MP_RELAY_GAME_PACKET_MAX + 1u, rig.out, sizeof rig.out) == 0u,
             "a packet over 1200 bytes is not sealed");

    ut_section("the goodbye");
    n = mp_relay_link_farewell(&rig.link, rig.out, sizeof rig.out);
    ut_check(relay_opens(&rig, n) && rig.header.type == MP_RELAY_TYPE_HOST_TO_RELAY_CONTROL &&
                 rig.plain_bytes == MP_RELAY_CLOSE_BYTES &&
                 rig.plain[0] == MP_RELAY_INNER_CLOSE &&
                 le64(rig.plain + 1) == 0x1122334455667788ull,
             "a host's farewell is a sealed Close naming its session id");
}

static void test_the_clock_wraps(void)
{
    static host_rig_t rig;
    uint32_t          start = 0xFFFFF000u;

    ut_section("the millisecond clock wraps");
    ut_check(host_at(&rig, start), "a host registered 4 seconds before the wrap");
    ut_check(tick(&rig, start + 19999u) == 0u, "no refresh a millisecond early, past the wrap");
    ut_check(tick(&rig, start + 20000u) != 0u, "the refresh on time");
}

int main(void)
{
    test_refreshing();
    test_a_new_leg();
    test_open_nack_1();
    test_a_new_leg_refused();
    test_limits_and_revocation();
    test_what_the_leg_carries();
    test_the_clock_wraps();
    return ut_summary("mp_relay_link_keep");
}
