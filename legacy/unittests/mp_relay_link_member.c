/* mp_relay_link_member.c: a player's link: the Join, holding the seat, and taking it back with its
 * proof after the relay lost the leg, as patiently as a relay restart needs.
 */
#include "unittest.h"

#include "mp_relay_fake.h"
#include "mp_relay_inner.h"
#include "mp_relay_link.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define T0 100000u

typedef struct member_rig {
    mp_relay_link_t          link;
    fake_relay_t             relay;
    mp_relay_link_event_t    event;
    uint8_t                  out[MP_RELAY_DATAGRAM_MAX];
    uint8_t                  in[MP_RELAY_DATAGRAM_MAX];
    uint8_t                  plain[MP_RELAY_DATAGRAM_MAX];
    uint8_t                  inner[64];
    size_t                   plain_bytes;
    mp_relay_sealed_header_t header;
    uint64_t                 nonce;
} member_rig_t;

static const uint8_t CODE[MP_RELAY_CODE_BYTES] = { 0x12u, 0x34u, 0x56u, 0x78u, 0x9Au };

static void member_at(member_rig_t *rig, uint32_t now)
{
    mp_relay_key_t key;

    memset(rig, 0, sizeof *rig);
    fake_relay_init(&rig->relay);
    key = fake_relay_key(&rig->relay);
    mp_relay_link_init(&rig->link, fake_random);
    mp_relay_link_join(&rig->link, CODE);
    mp_relay_link_give_key(&rig->link, &key, now);
}

static size_t tick(member_rig_t *rig, uint32_t now)
{
    return fake_tick(&rig->link, now, rig->out, &rig->event);
}

static bool deliver(member_rig_t *rig, uint32_t now, size_t bytes)
{
    return mp_relay_link_receive(&rig->link, now, rig->in, bytes, rig->plain, sizeof rig->plain,
                                 &rig->event);
}

/* Hello and cookie at `now`: the link's Join is in `rig->out`. */
static bool to_join(member_rig_t *rig, uint32_t now)
{
    uint8_t role = 0u;

    if (!fake_hello_read(rig->out, tick(rig, now), &role, &rig->nonce) ||
        role != MP_RELAY_ROLE_MEMBER) {
        return false;
    }
    (void)deliver(rig, now, fake_cookie(&rig->relay, role, rig->nonce, rig->in));
    return tick(rig, now) == MP_RELAY_REQUEST_BYTES && rig->out[0] == MP_RELAY_TYPE_JOIN;
}

static bool relay_opens(member_rig_t *rig, size_t bytes)
{
    return fake_open(&rig->relay, rig->out, bytes, &rig->header, rig->plain, sizeof rig->plain,
                     &rig->plain_bytes);
}

static void test_a_player_joins(void)
{
    static member_rig_t rig;
    uint8_t             code[MP_RELAY_CODE_BYTES];
    size_t              n;

    ut_section("a player joins by code");
    member_at(&rig, T0);
    ut_check(mp_relay_link_code(&rig.link, code) && memcmp(code, CODE, sizeof code) == 0,
             "a player knows its code before it has a seat");
    ut_check(to_join(&rig, T0), "a Hello for the member role, then the Join");
    n = fake_answer(&rig.relay, rig.out, MP_RELAY_REQUEST_BYTES, rig.in, sizeof rig.in);
    ut_check(n == MP_RELAY_JOINED_BYTES && rig.relay.body_bytes == MP_RELAY_JOIN_BODY_BYTES &&
                 memcmp(rig.relay.body, CODE, 5u) == 0 && rig.relay.body[5] == 0u,
             "the relay opens a Join for the code, without a proof");
    ut_check(deliver(&rig, T0 + 5u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_READY &&
                 mp_relay_link_ready(&rig.link) && rig.link.joined.member_index == 44u &&
                 rig.link.joined.gen == 3u,
             "the Joined gives the seat: index 44, generation 3");

    ut_section("the player holds the seat every MemberRefreshMS");
    ut_check(tick(&rig, T0 + 5004u) == 0u, "nothing before five seconds");
    n = tick(&rig, T0 + 5005u);
    ut_check(relay_opens(&rig, n) && rig.header.type == MP_RELAY_TYPE_MEMBER_TO_RELAY_CONTROL &&
                 rig.header.gen == 3u && rig.header.index == 44u &&
                 rig.plain_bytes == MP_RELAY_MEMBER_REFRESH_BYTES &&
                 rig.plain[0] == MP_RELAY_INNER_MEMBER_REFRESH,
             "a sealed MemberRefresh for index 44, generation 3");
    n = fake_to_member(&rig.relay, rig.inner, fake_member_ack(rig.inner), rig.in);
    (void)deliver(&rig, T0 + 5010u, n);
    ut_check(!rig.link.keeping && rig.link.next_keep_at == T0 + 10010u,
             "a MemberAck ends the round, the next five seconds on");

    ut_section("game packets and the goodbye");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER, 0u, 3u, 44u,
                  (const uint8_t *)"HOST", 4u, rig.in, sizeof rig.in);
    ut_check(deliver(&rig, T0 + 5020u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_GAME &&
                 rig.event.bytes == 4u && memcmp(rig.plain, "HOST", 4u) == 0,
             "the host's packet opens");
    n = fake_seal(&rig.relay, (uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER, 0u, 4u, 44u,
                  (const uint8_t *)"HOST", 4u, rig.in, sizeof rig.in);
    ut_check(!deliver(&rig, T0 + 5021u, n) && rig.link.data_refused == 0u,
             "one for another generation of the seat is not even opened");
    n = mp_relay_link_seal_game(&rig.link, 9u, 9u, (const uint8_t *)"PLAY", 4u, rig.out,
                                sizeof rig.out);
    ut_check(relay_opens(&rig, n) && rig.header.type == MP_RELAY_TYPE_MEMBER_TO_RELAY &&
                 rig.header.slot == 0u && rig.header.gen == 3u && rig.header.index == 44u,
             "the player's packet names its own seat, whatever slot the caller passed");
    n = mp_relay_link_farewell(&rig.link, rig.out, sizeof rig.out);
    ut_check(relay_opens(&rig, n) && rig.plain_bytes == MP_RELAY_LEAVE_BYTES &&
                 rig.plain[0] == MP_RELAY_INNER_LEAVE,
             "a player's farewell is a sealed Leave");

    ut_section("a code no session has");
    member_at(&rig, T0);
    (void)to_join(&rig, T0);
    (void)deliver(&rig, T0 + 10u, fake_open_nack((uint8_t)MP_RELAY_NACK_UNKNOWN_CODE, 0u, rig.in));
    (void)tick(&rig, T0 + 260u);
    ut_check(rig.link.phase == MP_RELAY_LINK_FAILED &&
                 rig.link.failure == MP_RELAY_LINK_UNKNOWN_CODE,
             "ends a first join at once");
}

/* A joined player whose leg the relay has lost: its next keep begins a new leg. */
static void seated_and_lost(member_rig_t *rig)
{
    member_at(rig, T0);
    (void)to_join(rig, T0);
    (void)deliver(rig, T0,
                  fake_answer(&rig->relay, rig->out, MP_RELAY_REQUEST_BYTES, rig->in,
                              sizeof rig->in));
    rig->link.reconnect_due = true;
    (void)tick(rig, T0 + 10u);
}

static void test_a_player_takes_the_seat_back(void)
{
    static member_rig_t rig;
    uint8_t             proof[MP_RELAY_PROOF_BYTES];
    size_t              n;

    ut_section("a rejoin proves the seat");
    seated_and_lost(&rig);
    ut_check(rig.link.renewing && to_join(&rig, T0 + 10u), "a new leg: Hello, then a Join");
    rig.relay.joined.flags = (uint8_t)MP_RELAY_JOINED_CONTINUED;
    n = fake_answer(&rig.relay, rig.out, MP_RELAY_REQUEST_BYTES, rig.in, sizeof rig.in);
    ut_check(n != 0u && rig.relay.body[5] == MP_RELAY_JOIN_HAS_PROOF &&
                 memcmp(rig.relay.body + 8, rig.relay.joined.r, 16u) == 0,
             "the Join carries the proof flag and the seat's value R");
    ut_check(mp_relay_seat_proof(rig.relay.joined.seat_secret, rig.relay.head.epoch,
                                 rig.relay.head.cookie, rig.nonce, 4u, proof) &&
                 memcmp(rig.relay.body + 24, proof, sizeof proof) == 0,
             "and the proof is the seat secret's over this cookie, nonce and key id");
    ut_check(deliver(&rig, T0 + 20u, n) && rig.event.kind == MP_RELAY_LINK_EVENT_READY &&
                 rig.link.joined.flags == MP_RELAY_JOINED_CONTINUED,
             "the answer continues the seat");
}

/* A seated player whose every rejoin is answered with an open Nack of `reason`, for up to
 * `for_ms`, the relay's leg lost again whenever the old one is back in use. True when the link did
 * not fail sooner than `patience` after the first refusal it counted. */
static bool refused_again_and_again(member_rig_t *rig, uint8_t reason, uint32_t for_ms,
                                    uint32_t patience)
{
    uint32_t at    = T0 + 10u;
    bool     early = false;

    seated_and_lost(rig);
    while (at < T0 + for_ms && rig->link.phase != MP_RELAY_LINK_FAILED) {
        if (rig->link.phase == MP_RELAY_LINK_COOKIE) {
            if (to_join(rig, at)) {
                (void)deliver(rig, at + 10u, fake_open_nack(reason, 0u, rig->in));
            }
        } else {
            rig->link.reconnect_due = rig->link.phase == MP_RELAY_LINK_READY;
            (void)tick(rig, at);
        }
        early = early || (rig->link.phase == MP_RELAY_LINK_FAILED &&
                          at - rig->link.first_refusal_at < patience);
        at += 500u;
    }
    return !early;
}

static void test_patience(void)
{
    static member_rig_t rig;

    ut_section("a seat held without its proof: Nack 12 waits each attempt out, for 45 seconds");
    ut_check(refused_again_and_again(&rig, (uint8_t)MP_RELAY_NACK_SEAT_HELD, 90000u,
                                     MP_RELAY_SEAT_HELD_MS),
             "the seat is not given up within 45 seconds of the first refusal");
    ut_check(rig.link.phase == MP_RELAY_LINK_FAILED && rig.link.failure == MP_RELAY_LINK_SEAT_HELD,
             "and after them it is: seat held");
    ut_check(rig.link.renewals > 5u, "with a new attempt every five seconds meanwhile");

    ut_section("a code gone on a rejoin: the host may not be back yet, a minute of patience");
    ut_check(refused_again_and_again(&rig, (uint8_t)MP_RELAY_NACK_UNKNOWN_CODE, 120000u,
                                     MP_RELAY_REJOIN_PATIENCE_MS),
             "the rejoin is not given up within the minute");
    ut_check(rig.link.phase == MP_RELAY_LINK_FAILED &&
                 rig.link.failure == MP_RELAY_LINK_UNKNOWN_CODE,
             "after it the link ends: no session has this code");
}

static void test_a_seat_not_continued(void)
{
    static member_rig_t rig;
    size_t              n;

    ut_section("a rejoin the relay answers with a new seat has lost the old one");
    seated_and_lost(&rig);
    ut_check(to_join(&rig, T0 + 10u), "the rejoin's Join, with its proof");
    rig.relay.joined.flags = 0u;
    rig.relay.joined.gen   = 4u;
    n = fake_answer(&rig.relay, rig.out, MP_RELAY_REQUEST_BYTES, rig.in, sizeof rig.in);
    ut_check(!deliver(&rig, T0 + 20u, n) && !mp_relay_link_ready(&rig.link),
             "the answer is no new leg for the session: nothing is sealed on it");
    n = tick(&rig, T0 + 20u);
    ut_check(relay_opens(&rig, n) && rig.plain_bytes == MP_RELAY_LEAVE_BYTES &&
                 rig.plain[0] == MP_RELAY_INNER_LEAVE,
             "the seat the relay gave instead is left at once, not waited out to its timeout");
    ut_check(rig.link.phase == MP_RELAY_LINK_FAILED &&
                 rig.link.failure == MP_RELAY_LINK_SEAT_LOST,
             "and the link ends: the host would see a stranger on a seat nobody holds a peer on");
}

int main(void)
{
    test_a_player_joins();
    test_a_player_takes_the_seat_back();
    test_a_seat_not_continued();
    test_patience();
    return ut_summary("mp_relay_link_member");
}
