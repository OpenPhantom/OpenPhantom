/* mp_session_mailbox.c: two sessions over a mailbox the test drives packet by packet.
 *
 * SIZE NOTE: a little over 600 lines, because the mailbox lets the test choose the order
 * packets arrive in and forge them, and every handshake gate is exercised through it: a payload
 * leaves in the substep it was set, whatever the frame cadence around it; two packets in one
 * receive lose nothing; an older packet arriving late replaces nothing; a client that restarts
 * from its own address is back in under a second; a keepalive goes out only after silence; the
 * mode, password and capacity gates refuse what they must and nothing else. The loopback half,
 * the content gate included, is mp_session.c.
 *
 * A session starts its own clock at zero, and a first update far past the handshake timeout is
 * read as a handshake that has already failed, so a sub-test that builds a fresh pair puts the
 * clock back to zero first or stays well inside that timeout.
 */
#include "unittest.h"

#include "mp_session.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_session_t s_host;
static mp_session_t s_client;

/* A network the test controls to the packet: everything sent is queued in order; a receive at an
 * endpoint takes the oldest packet addressed to it, or the newest when the test asks for the
 * order reversed; the test can drop a forged packet in with any source it likes. */
typedef struct mailbox_packet {
    uint32_t to;
    uint32_t from;
    size_t   bytes;
    uint8_t  data[MP_CHANNEL_PACKET_BYTES];
} mailbox_packet_t;

#define MAILBOX_SLOTS 64u

typedef struct mailbox {
    mailbox_packet_t queue[MAILBOX_SLOTS];
    size_t           count;
    bool             reversed[2];    /* deliver the newest packet to this endpoint first */
    uint32_t         sent_to[2];     /* packets ever addressed to each endpoint */
} mailbox_t;

typedef struct mailbox_binding {
    mailbox_t *box;
    uint32_t   endpoint;
} mailbox_binding_t;

static mailbox_t         s_box;

/* A tap on the wire: whether any packet the mailbox ever carried held these bytes. */
static const char *s_tap_for;
static bool        s_tap_seen;
static mailbox_binding_t s_box_binding[2];
static mp_transport_t    s_box_transport[2];

static void mailbox_inject(mailbox_t *box, uint32_t from, uint32_t to, const uint8_t *bytes,
                           size_t len)
{
    mailbox_packet_t *packet;

    if (box->count == MAILBOX_SLOTS || len > sizeof packet->data) {
        return;
    }
    if (s_tap_for != NULL && len >= strlen(s_tap_for)) {
        size_t at;

        for (at = 0; at + strlen(s_tap_for) <= len; ++at) {
            if (memcmp(bytes + at, s_tap_for, strlen(s_tap_for)) == 0) {
                s_tap_seen = true;
            }
        }
    }
    packet = &box->queue[box->count++];
    packet->to    = to;
    packet->from  = from;
    packet->bytes = len;
    memcpy(packet->data, bytes, len);
    ++box->sent_to[to & 1u];
}

static bool mailbox_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    mailbox_binding_t *binding = (mailbox_binding_t *)context;

    mailbox_inject(binding->box, binding->endpoint, endpoint, (const uint8_t *)packet, bytes);
    return true;
}

static size_t mailbox_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    mailbox_binding_t *binding = (mailbox_binding_t *)context;
    mailbox_t         *box = binding->box;
    size_t             index;
    size_t             found = MAILBOX_SLOTS;

    for (index = 0; index < box->count; ++index) {
        if (box->queue[index].to == binding->endpoint) {
            found = index;
            if (!box->reversed[binding->endpoint & 1u]) {
                break;
            }
        }
    }
    if (found == MAILBOX_SLOTS || box->queue[found].bytes > capacity) {
        return 0;
    }
    {
        size_t bytes = box->queue[found].bytes;

        memcpy(buffer, box->queue[found].data, bytes);
        *from = box->queue[found].from;
        memmove(&box->queue[found], &box->queue[found + 1u],
                (box->count - found - 1u) * sizeof box->queue[0]);
        --box->count;
        return bytes;
    }
}

/* The two sessions over the mailbox, built but NOT connected. Split out because anything the
 * handshake itself compares has to be set BEFORE the request goes out, and a helper that connects
 * leaves a caller no moment to do that in. */
static void mailbox_build(void)
{
    int tick;

    memset(&s_box, 0, sizeof s_box);
    for (tick = 0; tick < 2; ++tick) {
        s_box_binding[tick].box      = &s_box;
        s_box_binding[tick].endpoint = (uint32_t)tick;
        memset(&s_box_transport[tick], 0, sizeof s_box_transport[tick]);
        s_box_transport[tick].context = &s_box_binding[tick];
        s_box_transport[tick].send    = &mailbox_send;
        s_box_transport[tick].recv    = &mailbox_recv;
    }
    mp_session_init(&s_host, MP_SESSION_HOST, &s_box_transport[0], 0x777u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_box_transport[1], 0x888u);
}

/* Runs both sides until the client is connected, or until the cap. Returns whether it connected. */
static bool mailbox_settle(uint32_t *now, int ticks)
{
    int tick;

    for (tick = 0; tick < ticks; ++tick) {
        *now += 16u;
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

/* A host and a client connected over the mailbox. */
static void mailbox_pair(uint32_t *now)
{
    mailbox_build();
    mp_session_connect(&s_client, 0u);
    (void)mailbox_settle(now, 100);
}

/* Every payload is one dword, so the test can tell them apart and count them. */
static void set_numbered_payload(mp_session_t *session, uint32_t number)
{
    uint8_t          payload[4];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, payload, sizeof payload);
    mp_wire_put_u32(&w, number);
    (void)mp_session_set_payload(session, 0, payload, sizeof payload);
}

static bool read_numbered_payload(mp_session_t *session, uint32_t *number)
{
    uint8_t          payload[MP_SESSION_PAYLOAD_BYTES];
    size_t           bytes = 0;
    mp_wire_reader_t r;

    if (!mp_session_read_payload(session, 0, payload, sizeof payload, &bytes) || bytes != 4u) {
        return false;
    }
    mp_wire_reader_init(&r, payload, bytes);
    return mp_wire_get_u32(&r, number);
}

/* The bridge's cadence: a payload set once per 31.25 ms substep and serviced there, with a frame
 * hook pumping receive and service every 22.2 ms besides. Before the gate was fixed, two frames
 * inside one substep spent the send gate and the next substep's payload overwrote an unsent one. */
static void check_frame_cadence(void)
{
    uint32_t now = 0;
    uint32_t base_us;
    uint32_t next_substep_us;
    uint32_t next_frame_us;
    uint32_t set = 0;
    uint32_t got = 0;
    uint32_t expected = 1;
    bool     in_order = true;
    uint32_t number;

    ut_section("a 45 fps frame cadence against 32 Hz substeps loses no payload over ten seconds");

    mailbox_pair(&now);
    ut_check(mp_session_is_connected(&s_client), "the pair connected over the mailbox");

    /* The cadence starts where the handshake left the clocks; a clock that jumped back would
     * read as thirty seconds of idle and drop the peer on the first service. */
    base_us         = now * 1000u;
    next_substep_us = base_us;
    next_frame_us   = base_us;
    while (next_substep_us < base_us + 10000000u) {
        bool     substep = next_substep_us <= next_frame_us;
        uint32_t at_us = substep ? next_substep_us : next_frame_us;

        now = at_us / 1000u;
        if (substep) {
            set_numbered_payload(&s_host, ++set);
            mp_session_service(&s_host, now);
            next_substep_us += 31250u;
        } else {
            mp_session_receive(&s_host, now);
            mp_session_service(&s_host, now);
            mp_session_receive(&s_client, now);
            while (read_numbered_payload(&s_client, &number)) {
                if (number != expected) {
                    in_order = false;
                }
                expected = number + 1u;
                ++got;
            }
            mp_session_service(&s_client, now);
            next_frame_us += 22222u;
        }
    }
    mp_session_receive(&s_client, now + 1u);
    while (read_numbered_payload(&s_client, &number)) {
        ++got;
    }
    ut_checkf(got == set, "every one of the %u payloads set left exactly once (%u arrived)",
              (unsigned)set, (unsigned)got);
    ut_check(in_order, "in the order they were set, none repeated");
    ut_check(s_host.payloads_reordered == 0u && s_client.payloads_reordered == 0u &&
             s_host.payloads_overrun == 0u && s_client.payloads_overrun == 0u,
             "and nothing was refused on a link that reorders nothing and is drained every frame");
    ut_check(mp_session_drops(&s_host) == 0u && mp_session_drops(&s_client) == 0u &&
             mp_session_joins(&s_host) == 1u,
             "with no drop and no rejoin along the way");
}

static void check_two_packets_one_receive(void)
{
    uint32_t now = 0;
    uint32_t first = 0;
    uint32_t second = 0;
    uint32_t third = 0;

    ut_section("two packets arriving in one receive lose no sample");

    mailbox_pair(&now);
    set_numbered_payload(&s_host, 10u);
    mp_session_service(&s_host, now + 1u);
    set_numbered_payload(&s_host, 11u);
    mp_session_service(&s_host, now + 2u);
    ut_check(s_box.count == 2u, "both packets left in consecutive services, no gate between");

    mp_session_receive(&s_client, now + 3u);
    ut_check(read_numbered_payload(&s_client, &first) && first == 10u, "the older comes out first");
    ut_check(read_numbered_payload(&s_client, &second) && second == 11u, "then the newer");
    ut_check(!read_numbered_payload(&s_client, &third), "and then the ring is empty");

    ut_section("a reordered older packet does not overwrite the newer one");

    set_numbered_payload(&s_host, 20u);
    mp_session_service(&s_host, now + 4u);
    set_numbered_payload(&s_host, 21u);
    mp_session_service(&s_host, now + 5u);
    s_box.reversed[1] = true;
    mp_session_receive(&s_client, now + 6u);
    s_box.reversed[1] = false;
    ut_check(read_numbered_payload(&s_client, &first) && first == 21u,
             "the newer packet, delivered first, is what the ring holds");
    ut_check(!read_numbered_payload(&s_client, &second),
             "the older one, delivered late, entered nothing");
    ut_check(s_client.payloads_reordered == 1u && s_client.payloads_overrun == 0u,
             "and was counted as reordered, which is not the counter a full ring moves");

    ut_section("arrivals that outrun the drain are counted apart from a reordering");

    {
        uint32_t sent;
        uint32_t number = 0;
        uint32_t seen = 0;

        /* One more than the ring is deep, with no read in between: the oldest has to give way. */
        for (sent = 0u; sent < MP_SESSION_PAYLOAD_RING + 1u; ++sent) {
            set_numbered_payload(&s_host, 30u + sent);
            mp_session_service(&s_host, now + 7u + sent);
        }
        mp_session_receive(&s_client, now + 20u);
        ut_check(s_client.payloads_overrun == 1u && s_client.payloads_reordered == 1u,
                 "the ring dropped exactly one, and the reordering counter did not move");
        while (read_numbered_payload(&s_client, &number)) {
            if (seen == 0u) {
                ut_check(number == 31u, "the oldest is the one that went, not the newest");
            }
            ++seen;
        }
        ut_checkf(seen == MP_SESSION_PAYLOAD_RING, "and %u payloads survived", (unsigned)seen);
    }
}

/* The wire's own constants, copied so the test can forge a request and a response. */
#define WIRE_MAGIC    0x4F504D53u
#define WIRE_REQUEST  1u
#define WIRE_RESPONSE 3u

static size_t forge(uint8_t *out, uint8_t type, uint64_t value, size_t len)
{
    mp_wire_writer_t w;

    memset(out, 0, len);
    mp_wire_writer_init(&w, out, len);
    mp_wire_put_u32(&w, WIRE_MAGIC);
    mp_wire_put_u8(&w, type);
    mp_wire_put_u32(&w, (uint32_t)(value >> 32));
    mp_wire_put_u32(&w, (uint32_t)value);
    if (type == WIRE_REQUEST) {
        mp_wire_put_u32(&w, MP_WIRE_VERSION);
    }
    return len;
}

static void check_rejoin_from_connected_address(void)
{
    uint32_t now = 0;
    uint64_t old_id;
    int      tick;

    ut_section("a client that restarts from its own address is back inside a second");

    mailbox_pair(&now);
    old_id = mp_session_peer(&s_host, 0)->connection_id;

    /* The client process dies and starts again: a fresh session, a fresh salt, the same address.
     * The host's clock keeps running and its old slot is still connected. */
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_box_transport[1], 0x999u);
    mp_session_connect(&s_client, 0u);
    for (tick = 0; tick < 60 && !mp_session_is_connected(&s_client); ++tick) {
        now += 16u;
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    ut_checkf(mp_session_is_connected(&s_client), "the client is connected again after %u ms",
              (unsigned)(tick * 16));
    ut_check(mp_session_peer_count(&s_host) == 1u, "the host still holds exactly one peer");
    ut_check(mp_session_joins(&s_host) == 2u && mp_session_replaced(&s_host) == 1u,
             "which it counts as a second join and one replacement");
    ut_check(mp_session_drops(&s_host) == 0u, "with no drop and no timeout waited out");
    ut_check(mp_session_peer(&s_host, 0)->connection_id != old_id &&
             mp_session_peer(&s_host, 0)->connection_id ==
                 mp_session_peer(&s_client, 0)->connection_id,
             "under a fresh id both sides share");
}

static void check_forger_replaces_nothing(void)
{
    static uint8_t bytes[MP_SESSION_REQUEST_BYTES];
    uint32_t       now = 0;
    uint64_t       id;
    uint32_t       last_recv;
    uint32_t       joins;
    size_t         len;
    int            tick;

    ut_section("a forger at the client's address without the host's cookie replaces nothing");

    mailbox_pair(&now);
    id        = mp_session_peer(&s_host, 0)->connection_id;
    joins     = mp_session_joins(&s_host);
    now += 500u;
    last_recv = mp_session_peer(&s_host, 0)->last_recv_ms;

    len = forge(bytes, (uint8_t)WIRE_REQUEST, 0xF0F0F0F0F0F0F0F0ull, MP_SESSION_REQUEST_BYTES);
    mailbox_inject(&s_box, 1u, 0u, bytes, len);
    mp_session_receive(&s_host, now);
    ut_check(mp_session_peer(&s_host, 0)->last_recv_ms == last_recv,
             "and refreshes nothing about the connected peer, not even its idle stamp");
    ut_check(mp_session_peer(&s_host, 0)->connection_id == id &&
             mp_session_peer(&s_host, 0)->state == MP_PEER_CONNECTED,
             "the connection stands as it was");

    len = forge(bytes, (uint8_t)WIRE_RESPONSE, 0x1234567890ABCDEFull, MP_SESSION_REQUEST_BYTES);
    mailbox_inject(&s_box, 1u, 0u, bytes, len);
    mp_session_receive(&s_host, now + 1u);
    ut_check(mp_session_peer(&s_host, 0)->connection_id == id &&
             mp_session_joins(&s_host) == joins && mp_session_replaced(&s_host) == 0u,
             "a response whose cookie the host never made replaces nothing");
    ut_check(mp_session_denied(&s_host) == 1u && s_host.cookies_refused == 1u,
             "and is counted as a forgery");

    for (tick = 0; tick < 400; ++tick) {
        now += 16u;
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    ut_check(mp_session_peer_count(&s_host) == 1u && mp_session_is_connected(&s_client),
             "and the real client was connected throughout");
}

/* The game mode in the handshake.
 *
 * Co-op and deathmatch are two different agreements about who owns what, so two players who picked
 * differently must not end up in one session. The refusal has a reason of its own rather than
 * being folded into the content mismatch, because the two send a reader to different places: one
 * says "your files differ from mine" and the other says "you picked a different game".
 */
static void check_the_game_mode_gate(void)
{
    uint32_t now = 0;

    ut_section("two sides that picked different games do not join");

    mailbox_pair(&now);
    ut_check(mp_session_is_connected(&s_client), "a pair with no mode named joins as before");

    /* Now with modes, and they disagree. The modes are set BEFORE the connect, because the
     * request that carries one is the first packet the client sends. */
    mailbox_build();
    mp_session_set_mode(&s_host, 1u);
    mp_session_set_mode(&s_client, 2u);
    mp_session_connect(&s_client, 0u);
    (void)mailbox_settle(&now, 200);
    ut_check(!mp_session_is_connected(&s_client), "the client is refused");
    ut_checkf(mp_session_last_deny(&s_client) == MP_DENY_MODE,
              "and the reason is the MODE, not the content (%u)",
              (unsigned)mp_session_last_deny(&s_client));
    ut_check(mp_session_peer_count(&s_host) == 0u, "and the host spent no slot on it");

    ut_section("the same game on both sides joins");

    mailbox_build();
    mp_session_set_mode(&s_host, 2u);
    mp_session_set_mode(&s_client, 2u);
    mp_session_connect(&s_client, 0u);
    ut_check(mailbox_settle(&now, 200), "they join");

    ut_section("a side that names no mode stands aside rather than refusing");

    mailbox_build();
    mp_session_set_mode(&s_host, 0u);      /* a test, or a build that does not care */
    mp_session_set_mode(&s_client, 2u);
    mp_session_connect(&s_client, 0u);
    ut_check(mailbox_settle(&now, 200),
             "an unset mode skips the comparison, the same way an unset fingerprint does");
}

/* The password in the handshake.
 *
 * A host with a password is a closed door and a host without one is an open one; what a client
 * offers matters only at the closed door. The comparison is exact bytes, so "jedi" and "Jedi" are
 * two passwords, which is what a player expects of one.
 */
static void check_the_password_gate(void)
{
    uint32_t now = 0;

    ut_section("a closed host refuses the wrong password and takes the right one");

    mailbox_build();
    mp_session_set_password(&s_host, "jedi1999");
    mp_session_set_password(&s_client, "sith1999");
    mp_session_connect(&s_client, 0u);
    (void)mailbox_settle(&now, 200);
    ut_check(!mp_session_is_connected(&s_client), "the wrong password is refused");
    ut_checkf(mp_session_last_deny(&s_client) == MP_DENY_PASSWORD,
              "and the reason says so (%u)", (unsigned)mp_session_last_deny(&s_client));
    ut_check(mp_session_peer_count(&s_host) == 0u, "and no slot was spent on it");

    now = 0;
    mailbox_build();
    mp_session_set_password(&s_host, "jedi1999");
    mp_session_set_password(&s_client, "");
    mp_session_connect(&s_client, 0u);
    (void)mailbox_settle(&now, 200);
    ut_check(!mp_session_is_connected(&s_client) &&
                 mp_session_last_deny(&s_client) == MP_DENY_PASSWORD,
             "offering none to a closed host is refused the same way");

    now = 0;
    mailbox_build();
    mp_session_set_password(&s_host, "jedi1999");
    mp_session_set_password(&s_client, "jedi1999");
    s_tap_for  = "jedi1999";
    s_tap_seen = false;
    mp_session_connect(&s_client, 0u);
    ut_check(mailbox_settle(&now, 200), "the right password joins");
    ut_check(!s_tap_seen,
             "and it never crossed the wire in the clear: the response proves it");
    s_tap_for = NULL;

    ut_section("an open host takes anybody, whatever they offered");

    now = 0;
    mailbox_build();
    mp_session_set_password(&s_host, "");
    mp_session_set_password(&s_client, "whatever");
    mp_session_connect(&s_client, 0u);
    ut_check(mailbox_settle(&now, 200), "a password offered to an open host is simply ignored");

    ut_section("a password is cut to its field and kept printable");

    mp_session_set_password(&s_host, "0123456789ABCDEFGHIJ");
    ut_check(s_host.password[MP_SESSION_PASSWORD_MAX - 1u] == '\0',
             "a long password is cut with a terminator inside the field");
    mp_session_set_password(&s_host, "a\001b");
    ut_check(s_host.password[1] == '?', "a byte with no glyph becomes a question mark");
}

/* The refusal a player can answer.
 *
 * A join made without a password is refused with the reason, and the menu asks for one THEN
 * rather than in advance (mp_menu_screens_lobby). That rests on two things this checks: a host
 * spends no slot on a refusal, and the same address may ask again, with the password, and be
 * taken. The second request is a handshake like any other, cookie and all.
 */
static void check_a_refusal_can_be_answered(void)
{
    uint32_t now = 0;

    ut_section("a join refused for the password is made again with one, from that address");

    mailbox_build();
    mp_session_set_password(&s_host, "jedi1999");
    mp_session_set_password(&s_client, "");
    mp_session_connect(&s_client, 0u);
    (void)mailbox_settle(&now, 200);
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_PASSWORD,
             "the join that offered none is refused, and says why");
    ut_check(mp_session_peer_count(&s_host) == 0u, "and the host spent no slot on it");

    mp_session_set_password(&s_client, "jedi1999");
    mp_session_connect(&s_client, 0u);
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_NONE,
             "asking again clears the refusal: it belonged to the request it answered");
    ut_check(mailbox_settle(&now, 200), "and the second join is taken");
    ut_check(mp_session_peer_count(&s_host) == 1u,
             "as one player, not as a second one beside the refused first");
}

/* The seats a host offers.
 *
 * A menu lets a host say how many may sit; the session answers the next request past that with
 * the same FULL a session with no free slot answers, and spends nothing on it. Zero is every
 * slot, which is what a build with no menu asks for.
 */
static mp_session_t      s_client2;
static mailbox_binding_t s_binding2;
static mp_transport_t    s_transport2;

static void build_second_client(void)
{
    s_binding2.box      = &s_box;
    s_binding2.endpoint = 2u;
    memset(&s_transport2, 0, sizeof s_transport2);
    s_transport2.context = &s_binding2;
    s_transport2.send    = &mailbox_send;
    s_transport2.recv    = &mailbox_recv;
    mp_session_init(&s_client2, MP_SESSION_CLIENT, &s_transport2, 0x999u);
}

static void run_three(uint32_t *now, int ticks)
{
    int tick;

    for (tick = 0; tick < ticks; ++tick) {
        *now += 16u;
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        mp_session_update(&s_client2, *now);
    }
}

static void check_the_capacity_gate(void)
{
    uint32_t now = 0;

    ut_section("a host with one seat takes one client and refuses the next as FULL");
    mailbox_build();
    build_second_client();
    mp_session_set_capacity(&s_host, 1u);
    mp_session_connect(&s_client, 0u);
    ut_check(mailbox_settle(&now, 200), "the first client joins");
    mp_session_connect(&s_client2, 0u);
    run_three(&now, 200);
    ut_check(!mp_session_is_connected(&s_client2), "the second is refused");
    ut_checkf(mp_session_last_deny(&s_client2) == MP_DENY_FULL, "as FULL (%u)",
              (unsigned)mp_session_last_deny(&s_client2));
    ut_check(mp_session_peer_count(&s_host) == 1u, "and the first is still in, alone");

    ut_section("zero is every slot");
    now = 0;
    mailbox_build();
    build_second_client();
    mp_session_set_capacity(&s_host, 0u);
    mp_session_connect(&s_client, 0u);
    mp_session_connect(&s_client2, 0u);
    run_three(&now, 200);
    ut_check(mp_session_is_connected(&s_client) && mp_session_is_connected(&s_client2),
             "both join");
    ut_check(mp_session_peer_count(&s_host) == 2u, "and the host holds two");

    ut_section("a number past the slots is the slots");
    mp_session_set_capacity(&s_host, 200u);
    ut_check(s_host.capacity == MP_SESSION_MAX_PEERS, "clamped");
}

/* The discard counter alone says only that payloads were thrown away. These two runs are its two
 * meanings, side by side: the same number of discards, from the same ring, asking for opposite
 * fixes. */
static void check_a_discard_says_when(void)
{
    uint32_t now = 0;
    uint32_t sent;
    uint32_t spare = 4u;

    ut_section("a burst inside one receive is a discard the drain was awake for");

    mailbox_pair(&now);
    for (sent = 0u; sent < MP_SESSION_PAYLOAD_RING + spare; ++sent) {
        set_numbered_payload(&s_host, 100u + sent);
        mp_session_service(&s_host, now + 1u + sent);
    }
    mp_session_receive(&s_client, now + 100u);
    ut_checkf(s_client.payloads_overrun == spare,
              "%u payloads over a ring of %u gave up %u",
              (unsigned)(MP_SESSION_PAYLOAD_RING + spare),
              (unsigned)MP_SESSION_PAYLOAD_RING, (unsigned)s_client.payloads_overrun);
    ut_checkf(s_client.payloads_overrun_fresh == spare,
              "and all %u of them entered and left inside the same receive, so the drain was "
              "awake and was outrun anyway", (unsigned)s_client.payloads_overrun_fresh);
    ut_checkf(s_client.deepest_backlog == MP_SESSION_PAYLOAD_RING,
              "the ring stood full, backlog %u", (unsigned)s_client.deepest_backlog);
    ut_checkf(s_client.longest_wait_ms == 0u,
              "and nothing waited, longest wait %u ms", (unsigned)s_client.longest_wait_ms);

    ut_section("the same discards spread over a stall are a different finding");

    mailbox_pair(&now);
    /* One packet per receive, forty milliseconds apart, and nothing reads: this is the shape of a
     * level load, where the receiving half of the pump runs and the draining half does not. */
    for (sent = 0u; sent < MP_SESSION_PAYLOAD_RING + spare; ++sent) {
        set_numbered_payload(&s_host, 200u + sent);
        mp_session_service(&s_host, now + 1u + sent * 40u);
        mp_session_receive(&s_client, now + 2u + sent * 40u);
    }
    ut_checkf(s_client.payloads_overrun == spare,
              "the same %u payloads gave up the same %u",
              (unsigned)(MP_SESSION_PAYLOAD_RING + spare),
              (unsigned)s_client.payloads_overrun);
    ut_checkf(s_client.payloads_overrun_fresh == 0u,
              "but not one of them with the drain awake (%u), because each had waited a ring's "
              "worth of arrivals", (unsigned)s_client.payloads_overrun_fresh);
    ut_checkf(s_client.longest_wait_ms >= MP_SESSION_DRAIN_STALL_MS,
              "and the longest wait, %u ms, is past the stall line of %u",
              (unsigned)s_client.longest_wait_ms, (unsigned)MP_SESSION_DRAIN_STALL_MS);

    ut_section("a payload that is read late moves the wait as well as one that is discarded");

    mailbox_pair(&now);
    set_numbered_payload(&s_host, 300u);
    mp_session_service(&s_host, now + 1u);
    mp_session_receive(&s_client, now + 2u);
    mp_session_receive(&s_client, now + 502u);   /* the clock moves, nothing new arrives */
    {
        uint32_t number = 0;

        ut_check(read_numbered_payload(&s_client, &number) && number == 300u,
                 "the payload is still there to be read");
    }
    ut_checkf(s_client.longest_wait_ms == 500u,
              "and its five hundred millisecond wait was measured, not only a discard's (%u)",
              (unsigned)s_client.longest_wait_ms);
    ut_check(s_client.payloads_overrun == 0u && s_client.deepest_backlog == 1u,
             "with nothing discarded and the ring one deep");
}

/* The witness for the escape. A reliable message that no packet with a payload can seat has one
 * way out: a packet built with no payload, which the service builds when the channel holds queued
 * messages and the pacing interval has passed. That path existed and nothing counted how often it
 * was taken, so "in a level nothing large rides the channel" was an argument and not a reading.
 * The two counters make it a reading; the third section is the channel's own account of a
 * message the payload keeps out, summed by the session, and the once-only line that goes with it.
 * */
static void check_the_two_kinds_of_packet(void)
{
    static uint8_t big[800];
    static uint8_t full[MP_SESSION_PAYLOAD_BYTES];
    uint8_t        note[64];
    uint32_t       now = 0;
    uint32_t       with_before;
    uint32_t       without_before;
    int            tick;

    ut_section("a session that sends only reliable messages counts packets without a payload");

    memset(big, 0x77, sizeof big);
    memset(full, 0x99, sizeof full);
    memset(note, 0x11, sizeof note);

    mailbox_pair(&now);
    for (tick = 0; tick < 20; ++tick) {   /* settle the handshake's last packets */
        now += 16u;
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    with_before    = mp_session_packets_with_payload(&s_host);
    without_before = mp_session_packets_without_payload(&s_host);
    for (tick = 0; tick < 20; ++tick) {
        now += 16u;
        (void)mp_session_send_reliable(&s_host, 0, note, sizeof note);
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    ut_checkf(mp_session_packets_without_payload(&s_host) - without_before >= 8u,
              "twenty ticks of queued messages and no payload left at least 8 packets without one, "
              "paced rather than kept to the keepalive; %u did",
              (unsigned)(mp_session_packets_without_payload(&s_host) - without_before));
    ut_checkf(mp_session_packets_with_payload(&s_host) == with_before,
              "and none with a payload, because none was set (%u more)",
              (unsigned)(mp_session_packets_with_payload(&s_host) - with_before));

    ut_section("a session with a payload set every tick counts packets with one");

    with_before    = mp_session_packets_with_payload(&s_host);
    without_before = mp_session_packets_without_payload(&s_host);
    for (tick = 0; tick < 20; ++tick) {
        now += 16u;
        set_numbered_payload(&s_host, (uint32_t)tick);
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    ut_checkf(mp_session_packets_with_payload(&s_host) - with_before == 20u,
              "twenty payloads set, twenty packets carrying one, not %u",
              (unsigned)(mp_session_packets_with_payload(&s_host) - with_before));
    ut_checkf(mp_session_packets_without_payload(&s_host) == without_before,
              "and none without in that stretch, because a payload packet is also the keepalive "
              "(%u more)",
              (unsigned)(mp_session_packets_without_payload(&s_host) - without_before));

    /* The payload takes the seat in its own packet and the count says so, and the second packet
     * of the same service carries the message at once, while the payload is still full. */
    ut_section("a message the full payload keeps out rides the second packet of the same service");

    ut_check(mp_session_seats_lost_to_payload(&s_host) == 0u &&
                 mp_session_longest_seat_wait_ms(&s_host) == 0u,
             "so far no seat was lost to a payload and nothing waited for one");
    ut_check(mp_session_send_reliable(&s_host, 0, big, sizeof big),
             "an 800 byte message is queued");
    {
        uint8_t buffer[MP_CHANNEL_MESSAGE_BYTES];
        size_t  bytes = 0;
        bool    got = false;

        for (tick = 0; tick < 80; ++tick) {   /* 1280 ms of a payload that leaves it no room */
            now += 16u;
            (void)mp_session_set_payload(&s_host, 0, full, sizeof full);
            mp_session_update(&s_host, now);
            mp_session_update(&s_client, now);
            while (mp_session_read_reliable(&s_client, 0, buffer, sizeof buffer, &bytes)) {
                got = got || (bytes == sizeof big);
            }
        }
        ut_checkf(mp_session_seats_lost_to_payload(&s_host) >= 1u,
                  "the payload still took its seat and it is counted, %u time(s)",
                  (unsigned)mp_session_seats_lost_to_payload(&s_host));
        ut_check(got, "and the message crossed whole while the payload was still full");
    }
    ut_checkf(mp_session_longest_seat_wait_ms(&s_host) < 100u && !s_host.seat_wait_said,
              "so no message waited long for a seat (%u ms) and nothing had to be said",
              (unsigned)mp_session_longest_seat_wait_ms(&s_host));
    ut_check(s_host.peers[0].overflow_packets >= 1u,
             "the host counts the second packet it sent for it");
}

static void check_keepalive_cadence(void)
{
    uint32_t now = 0;
    uint32_t before;
    uint32_t after;
    int      tick;

    ut_section("with nothing to send, a keepalive goes out only after a hundred milliseconds");

    mailbox_pair(&now);
    for (tick = 0; tick < 20; ++tick) {   /* settle the handshake's last packets */
        now += 16u;
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    before = s_box.sent_to[1];
    for (tick = 0; tick < 200; ++tick) {
        now += 5u;
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    after = s_box.sent_to[1];
    ut_checkf(after - before >= 9u && after - before <= 11u,
              "two hundred services over one second sent %u keepalives, not two hundred",
              (unsigned)(after - before));
    ut_check(mp_session_peer_count(&s_host) == 1u && mp_session_is_connected(&s_client),
             "and the keepalives held the connection");
}

int main(void)
{
    check_frame_cadence();
    check_two_packets_one_receive();
    check_rejoin_from_connected_address();
    check_forger_replaces_nothing();
    check_keepalive_cadence();
    check_the_two_kinds_of_packet();
    check_the_game_mode_gate();
    check_the_password_gate();
    check_a_refusal_can_be_answered();
    check_the_capacity_gate();
    check_a_discard_says_when();

    ut_section("the names cross the handshake, cleaned, and the round trip is measured");
    {
        uint32_t now = 0;
        int      tick;

        mailbox_build();
        mp_session_set_name(&s_host, "Padme");
        mp_session_set_name(&s_client, "  Gr\xfc\xdf  ");
        mp_session_connect(&s_client, 0u);
        ut_check(mailbox_settle(&now, 100), "the pair connects");
        ut_check(strcmp(mp_session_peer_name(&s_host, 0), "Gr??") == 0,
                 "the host knows the client by the name in its request, cleaned");
        ut_check(strcmp(mp_session_peer_name(&s_client, 0), "Padme") == 0,
                 "and the client knows the host by the name in the accept");
        ut_check(strcmp(mp_session_peer_name(&s_host, 3), "Player") == 0,
                 "a slot nobody took answers the default rather than nothing");
        /* A packet every tick both ways, which is what a payload buys; an idle session sends only
         * its keepalives and would measure nothing inside this window. */
        for (tick = 0; tick < 40; ++tick) {
            now += 16u;
            set_numbered_payload(&s_host, (uint32_t)tick);
            set_numbered_payload(&s_client, (uint32_t)tick);
            mp_session_update(&s_host, now);
            mp_session_update(&s_client, now);
        }
        ut_checkf(mp_session_peer_rtt_ms(&s_host, 0) >= 16u &&
                      mp_session_peer_rtt_ms(&s_host, 0) <= 64u,
                  "over a mailbox that delivers on the next 16 ms tick the host "
                  "measures a round trip "
                  "of one to four ticks, and reads %u ms",
                  (unsigned)mp_session_peer_rtt_ms(&s_host, 0));
        ut_checkf(mp_session_peer_rtt_ms(&s_client, 0) >= 16u &&
                      mp_session_peer_rtt_ms(&s_client, 0) <= 64u,
                  "and so does the client, reading %u ms",
                  (unsigned)mp_session_peer_rtt_ms(&s_client, 0));
    }

    return ut_summary("mp_session_mailbox");
}
