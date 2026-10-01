/* mp_fuzz.c: every decoder fed what no honest peer sends.
 *
 * SIZE NOTE: over 600 lines because every decoder of the network layer is fuzzed from
 * this one file, each with its own valid inputs to mutate. The seam, should it grow, is the
 * session half with its hostile transport, which shares nothing with the codec halves.
 *
 * The other network tests drive the codecs with packets a correct sender built. This one drives
 * them with random bytes, with valid packets after bit flips, truncations and byte runs copied
 * over one another, and with packets an attacker who knows the wire format but not the salts
 * would forge. Nothing here proves a decoder right; what it proves is that no input makes one read
 * past its buffer, hand back a half applied result, or let a stranger into a session. The random
 * source is seeded, so a failure reproduces.
 *
 * The session part speaks the wire format itself, magic and type bytes included, because that is
 * what a forger has. Those constants are copied here on purpose: if the format moves, the padded
 * request below stops drawing a challenge and the amplification check fails loudly.
 */
#include "unittest.h"

#include "mp_fuzz_input.h"

#include "mp_channel.h"
#include "mp_command.h"
#include "mp_events.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool finite3(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/* ===================================== mp_wire ================================================ */

static void check_wire(void)
{
    uint8_t        bytes[64];
    mp_wire_reader_t r;
    mp_wire_body_t body;
    size_t         len;
    unsigned       i;
    bool           in_bounds = true;
    bool           finite_on_success = true;

    ut_section("the body reader never runs past its buffer");

    for (i = 0; i < ROUNDS; ++i) {
        len = random_below(sizeof bytes + 1);
        fill_random(bytes, len);
        mp_wire_reader_init(&r, bytes, len);
        memset(&body, 0, sizeof body);
        if (mp_wire_get_body(&r, &body)) {
            finite_on_success = finite_on_success && finite3(body.position) &&
                                finite3(body.orientation);
        } else {
            in_bounds = in_bounds && r.overran;
        }
        in_bounds = in_bounds && r.at <= r.size;
    }
    ut_check(in_bounds, "the read position stays inside the buffer and a refusal sets overran");
    ut_check(finite_on_success, "every accepted body is finite");
}

/* ===================================== mp_snapshot ============================================ */

static void a_body(mp_wire_body_t *body, float x)
{
    memset(body, 0, sizeof *body);
    body->position[0] = x;
    body->position[1] = 2.0f * x;
    body->position[2] = -x;
    body->orientation[1] = 45.0f;
    body->alive = true;
    body->weapon = 2;
    body->hero = 1;
    body->anim.clip[0] = 7;
    body->anim.track[0] = 120;
    body->anim.channel_mask = 3;
}

static void check_snapshot(void)
{
    static mp_snapshot_t baseline;
    static mp_snapshot_t current;
    static mp_snapshot_t out;
    static uint8_t full[512];
    static uint8_t delta[512];
    static uint8_t bytes[1024];
    mp_wire_body_t body;
    size_t         full_len = 0;
    size_t         delta_len = 0;
    size_t         len;
    unsigned       i;
    bool           refusal_clears = true;
    bool           accepted_finite = true;

    ut_section("the snapshot decoder refuses cleanly and accepts only finite bodies");

    mp_snapshot_clear(&baseline);
    mp_snapshot_clear(&current);
    baseline.tick = 40;
    current.tick = 41;
    a_body(&body, 10.0f);
    mp_snapshot_set_body(&baseline, 0, &body);
    mp_snapshot_set_body(&current, 0, &body);
    a_body(&body, 20.0f);
    mp_snapshot_set_body(&baseline, 2, &body);
    body.position[0] = 21.0f;
    mp_snapshot_set_body(&current, 2, &body);
    a_body(&body, 30.0f);
    mp_snapshot_set_body(&current, 5, &body);

    ut_check(mp_snapshot_encode(&current, NULL, full, sizeof full, &full_len), "a full encodes");
    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &delta_len),
             "and a delta encodes");

    for (i = 0; i < ROUNDS; ++i) {
        const mp_snapshot_t *against;

        switch (next_random() % 4u) {
        case 0:
            memcpy(bytes, full, full_len);
            len = mutate(bytes, full_len, sizeof bytes);
            break;
        case 1:
            memcpy(bytes, delta, delta_len);
            len = mutate(bytes, delta_len, sizeof bytes);
            break;
        default:
            len = random_below(sizeof bytes + 1);
            fill_random(bytes, len);
            break;
        }
        against = (next_random() & 1u) ? &baseline : NULL;

        if (mp_snapshot_decode(bytes, len, against, &out)) {
            size_t slot;

            for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
                if (mp_snapshot_has_body(&out, slot)) {
                    accepted_finite = accepted_finite && finite3(out.body[slot].position) &&
                                      finite3(out.body[slot].orientation);
                }
            }
        } else {
            refusal_clears = refusal_clears && out.present_mask == 0 && out.tick == 0;
        }
    }
    ut_check(refusal_clears, "a refused packet leaves the output cleared, never half applied");
    ut_check(accepted_finite, "every body an accepted packet carries is finite");
}

/* ===================================== mp_command ============================================= */

static void check_command(void)
{
    mp_command_t commands[MP_COMMAND_REDUNDANCY];
    mp_command_t out[MP_COMMAND_REDUNDANCY];
    uint8_t      valid[256];
    uint8_t      bytes[512];
    size_t       valid_len = 0;
    size_t       len;
    size_t       count;
    size_t       max;
    unsigned     i;
    bool         within_max = true;
    bool         accepted_finite = true;

    ut_section("the command decoder never claims more than the buffer holds");

    for (i = 0; i < MP_COMMAND_REDUNDANCY; ++i) {
        memset(&commands[i], 0, sizeof commands[i]);
        commands[i].tick = 1000u + i;
        commands[i].turn = 12.5f;
        commands[i].move = -0.25f;
        commands[i].buttons = MP_CMD_ATTACK | MP_CMD_RUN;
    }
    ut_check(mp_command_encode(commands, MP_COMMAND_REDUNDANCY, valid, sizeof valid, &valid_len),
             "a full redundant packet encodes");

    for (i = 0; i < ROUNDS; ++i) {
        if (next_random() & 1u) {
            memcpy(bytes, valid, valid_len);
            len = mutate(bytes, valid_len, sizeof bytes);
        } else {
            len = random_below(sizeof bytes + 1);
            fill_random(bytes, len);
        }
        max = 1 + random_below(MP_COMMAND_REDUNDANCY);
        count = 0;
        memset(out, 0, sizeof out);
        if (mp_command_decode(bytes, len, out, max, &count)) {
            size_t k;

            within_max = within_max && count <= max;
            for (k = 0; k < count && k < max; ++k) {
                accepted_finite = accepted_finite && isfinite(out[k].turn) &&
                                  isfinite(out[k].move);
            }
        }
    }
    ut_check(within_max, "an accepted packet never reports more commands than the caller's max");
    ut_check(accepted_finite, "and every accepted axis is finite");
}

/* ===================================== mp_events ============================================== */

/* The nine valid events the mangled ones are made from, one of every kind the decoder takes. */
static void encode_valid_events(uint8_t valid[9][MP_EVENT_MAX_BYTES], size_t valid_len[9])
{
    mp_event_t event;

    memset(&event, 0, sizeof event);
    event.kind = MP_EVENT_SHOT;
    event.tick = 77u;
    event.shot_kind = 3;
    event.origin[0] = 0.3f;
    event.origin[1] = 0.5f;
    event.origin[2] = 1.2f;
    event.pitch = -10.0f;
    event.yaw = 350.0f;
    valid_len[0] = mp_event_encode(&event, valid[0], sizeof valid[0]);
    event.kind = MP_EVENT_PUSH;
    event.charge = 0.5f;
    valid_len[1] = mp_event_encode(&event, valid[1], sizeof valid[1]);
    event.kind = MP_EVENT_SABRE;
    event.action = MP_SABRE_BLOCK;
    event.operand = 0x62;
    valid_len[2] = mp_event_encode(&event, valid[2], sizeof valid[2]);
    event.kind = MP_EVENT_WEAPON;
    event.weapon_slot = 2u;
    valid_len[3] = mp_event_encode(&event, valid[3], sizeof valid[3]);
    event.kind = MP_EVENT_MOVER;
    event.mover_id = 41u;
    event.mover_mode = MP_EVENT_MOVER_OPEN;
    valid_len[4] = mp_event_encode(&event, valid[4], sizeof valid[4]);
    /* The two actor messages have to be SEEDED, not left to the random path. A control pass
     * measured what leaving them out cost: over twenty thousand rounds a random buffer lands on
     * the spawn's tag and length about 0.6 times and on the despawn's about 0.012, so the
     * assertions written for them would have run essentially never. A whitelist that is never
     * exercised is a whitelist that passes. */
    event.kind = MP_EVENT_SPAWN;
    event.level_id = 528u;
    event.actor_index = 61u;
    event.actor_generation = 2u;
    event.actor_script = 300u;
    valid_len[5] = mp_event_encode(&event, valid[5], sizeof valid[5]);
    event.kind = MP_EVENT_DESPAWN;
    event.actor_reason = MP_EVENT_REMOVE_OUT_OF_RANGE;
    valid_len[6] = mp_event_encode(&event, valid[6], sizeof valid[6]);
    event.kind = MP_EVENT_SKIN;
    event.skin_kind = MP_SKIN_CHARACTER;
    event.skin_hero = 3u;
    memset(event.skin_asset, 0, sizeof event.skin_asset);
    memcpy(event.skin_asset, "mace.baf", 8u);
    valid_len[7] = mp_event_encode(&event, valid[7], sizeof valid[7]);
    event.kind = MP_EVENT_PICKUP;
    event.pickup_kind = MP_PICKUP_KIND_MIN;
    valid_len[8] = mp_event_encode(&event, valid[8], sizeof valid[8]);
}

/* Whether an event the decoder accepted is of a kind it knows, with every field of it one the
 * decoder can name. */
static bool known_event(const mp_event_t *out)
{
    bool known = out->kind == MP_EVENT_SHOT || out->kind == MP_EVENT_PUSH ||
                 out->kind == MP_EVENT_SABRE || out->kind == MP_EVENT_WEAPON ||
                 out->kind == MP_EVENT_MOVER || out->kind == MP_EVENT_SPAWN ||
                 out->kind == MP_EVENT_DESPAWN || out->kind == MP_EVENT_SKIN ||
                 out->kind == MP_EVENT_PICKUP;

    if (out->kind == MP_EVENT_SHOT || out->kind == MP_EVENT_PUSH ||
        out->kind == MP_EVENT_SABRE || out->kind == MP_EVENT_WEAPON ||
        out->kind == MP_EVENT_PICKUP) {
        /* The receiver picks a far bank or a claimant by it. */
        known = known && out->source_slot < MP_SNAPSHOT_MAX_BODIES;
    }
    if (out->kind == MP_EVENT_SABRE) {
        known = known && out->action <= MP_SABRE_ACTION_MAX;
    }
    if (out->kind == MP_EVENT_WEAPON) {
        known = known && out->weapon_slot < MP_EVENT_WEAPON_SLOTS;
    }
    if (out->kind == MP_EVENT_MOVER) {
        known = known && out->mover_mode <= MP_EVENT_MOVER_OPEN;
    }
    if (out->kind == MP_EVENT_PICKUP) {
        /* Two values inside the band reach a stack defect in the retail handler and are placed
         * nowhere in the shipped data. A fuzzed one must never come back accepted. */
        known = known && out->pickup_kind >= MP_PICKUP_KIND_MIN &&
                out->pickup_kind <= MP_PICKUP_KIND_MAX &&
                out->pickup_kind != MP_PICKUP_KIND_UNPLACED_LO &&
                out->pickup_kind != MP_PICKUP_KIND_UNPLACED_HI;
    }
    if (out->kind == MP_EVENT_SKIN) {
        /* The name goes to a resource loader whose miss ends the program, so a fuzzed one must
         * never come back accepted. */
        size_t at;
        bool   terminated = false;

        known = known && out->skin_kind <= MP_SKIN_KIND_MAX;
        for (at = 0; at < MP_EVENT_ASSET_MAX; ++at) {
            if (out->skin_asset[at] == '\0') {
                terminated = true;
                break;
            }
        }
        known = known && terminated && at > 0u;
    }
    if (out->kind == MP_EVENT_DESPAWN) {
        /* The removal switches on this and has no arm for anything else. A reason the fuzzer can
         * invent must never reach it. */
        known = known && (out->actor_reason == MP_EVENT_REMOVE_OUT_OF_RANGE ||
                          out->actor_reason == MP_EVENT_REMOVE_DELETED ||
                          out->actor_reason == MP_EVENT_REMOVE_HOST_RELEASE ||
                          out->actor_reason == MP_EVENT_REMOVE_LEVEL_END ||
                          out->actor_reason == MP_EVENT_REMOVE_LEAVE_CORPSE);
    }
    return known;
}

/* The event decoder against random bytes, cut buffers and mangled valid events. A decoder that
 * accepted an unknown kind would hand the puppet a moment it has no starter for; one that
 * accepted a shot with a NaN offset would spawn a bolt nowhere. */
static void check_events(void)
{
    uint8_t    valid[9][MP_EVENT_MAX_BYTES];
    size_t     valid_len[9];
    uint8_t    bytes[64];
    mp_event_t out;
    size_t     len;
    unsigned   i;
    bool       known_kind = true;
    bool       finite_shot = true;
    bool       agrees = true;

    ut_section("the event decoder accepts only known kinds and finite shots");

    encode_valid_events(valid, valid_len);
    ut_check(valid_len[0] == MP_EVENT_SHOT_BYTES && valid_len[1] == MP_EVENT_PUSH_BYTES &&
             valid_len[2] == MP_EVENT_SABRE_BYTES && valid_len[3] == MP_EVENT_WEAPON_BYTES &&
             valid_len[4] == MP_EVENT_MOVER_BYTES && valid_len[5] == MP_EVENT_SPAWN_BYTES &&
             valid_len[6] == MP_EVENT_DESPAWN_BYTES && valid_len[7] == MP_EVENT_SKIN_BYTES &&
             valid_len[8] == MP_EVENT_PICKUP_BYTES,
             "the nine valid events encode");

    for (i = 0; i < ROUNDS; ++i) {
        if (next_random() & 1u) {
            size_t which = random_below(9);

            memcpy(bytes, valid[which], valid_len[which]);
            len = mutate(bytes, valid_len[which], sizeof bytes);
        } else {
            len = random_below(sizeof bytes + 1);
            fill_random(bytes, len);
        }
        memset(&out, 0, sizeof out);
        if (mp_event_decode(bytes, len, &out)) {
            agrees = agrees && mp_event_is_event(bytes, len);
            known_kind = known_kind && known_event(&out);
            if (out.kind == MP_EVENT_SHOT) {
                finite_shot = finite_shot && finite3(out.origin) && isfinite(out.pitch) &&
                              isfinite(out.yaw);
            }
        } else {
            /* What the recogniser lets through and the decoder still refuses: a body's moment
             * naming a slot past the snapshot's table, a sabre action with no starter behind
             * it, a weapon slot outside the engine's ammo table, a mover mode the engine has no
             * function for, a removal reason it has no arm for, a name it could not load and a
             * pickup kind nobody placed. Only a spawn has nothing to refuse. */
            agrees = agrees && (!mp_event_is_event(bytes, len) || bytes[0] != MP_EVENT_SPAWN);
        }
    }
    ut_check(known_kind,
             "every accepted event is of a kind the decoder knows, with every field of it one "
             "the decoder can name");
    ut_check(finite_shot, "every accepted shot has a finite offset and finite angles");
    ut_check(agrees, "the recogniser and the decoder agree, except on an operand out of range");
}

/* ===================================== mp_channel ============================================= */

static mp_channel_t s_sender;
static mp_channel_t s_receiver;

static void check_channel(void)
{
    static uint8_t packet[MP_CHANNEL_PACKET_BYTES];
    static uint8_t bytes[MP_CHANNEL_PACKET_BYTES];
    static uint8_t message[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t        payload[300];
    size_t         packet_len = 0;
    size_t         len;
    size_t         n;
    unsigned       i;
    uint32_t       now = 0;
    bool           payload_inside = true;
    bool           reads_bounded = true;
    bool           ready_agrees = true;

    ut_section("the channel survives forged and mangled packets");

    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);

    for (i = 0; i < ROUNDS; ++i) {
        const uint8_t       *got = NULL;
        size_t               got_len = 0;
        uint16_t             sequence = 0;
        mp_channel_receipt_t receipt;
        unsigned             reads;

        now += 31u;
        if (i % 7u == 0) {
            mp_channel_send(&s_sender, "reliable", 8u);
        }
        fill_random(payload, sizeof payload);
        if (!mp_channel_packet_build(&s_sender, now, payload, 1 + random_below(sizeof payload),
                                     packet, sizeof packet, &packet_len)) {
            packet_len = 0;
        }
        if ((next_random() & 3u) != 0 && packet_len > 0) {
            memcpy(bytes, packet, packet_len);
            len = mutate(bytes, packet_len, sizeof bytes);
        } else {
            len = random_below(sizeof bytes + 1);
            fill_random(bytes, len);
        }

        if (i % 2u == 0u) {
            if (mp_channel_packet_receive(&s_receiver, bytes, len, &got, &got_len)) {
                payload_inside = payload_inside && got >= bytes && got_len <= len &&
                                 (size_t)(got - bytes) + got_len <= len;
            }
        } else {
            /* The session's receive, which also hands out the payload of a packet refused for
             * the window, and nothing at all for a packet it dropped. */
            receipt = mp_channel_packet_take(&s_receiver, now, bytes, len, &got, &got_len,
                                             &sequence);
            if (receipt == MP_CHANNEL_DROPPED) {
                payload_inside = payload_inside && got == NULL && got_len == 0u;
            } else {
                payload_inside = payload_inside && got >= bytes && got_len <= len &&
                                 (size_t)(got - bytes) + got_len <= len;
            }
        }
        for (reads = 0; reads < 64u; ++reads) {
            size_t ready = 0;
            bool   has = mp_channel_message_ready(&s_receiver, &ready);

            n = 0;
            if (!mp_channel_message_read(&s_receiver, message, sizeof message, &n)) {
                ready_agrees = ready_agrees && !has;
                break;
            }
            reads_bounded = reads_bounded && n <= sizeof message;
            ready_agrees  = ready_agrees && has && ready == n;
        }
        if (i % 2000u == 1999u) {
            mp_channel_init(&s_receiver);   /* a fresh window, so late rounds are not all stale */
        }
    }
    ut_check(payload_inside, "an accepted payload always lies inside the packet it came in");
    ut_check(reads_bounded, "and no message read ever exceeds the message capacity");
    ut_check(ready_agrees, "and the peek at the next message agrees with the read of it");
}

/* ===================================== mp_session ============================================= */

/* The wire's own constants, copied so the test can forge. */
#define WIRE_MAGIC        0x4F504D53u
#define WIRE_REQUEST      1u
#define WIRE_RESPONSE     3u
#define WIRE_DENY         5u

/* A transport that is the attacker: whatever the test puts in `in` is what the session receives
 * next, and everything the session sends is measured per endpoint and otherwise dropped. */
typedef struct hostile {
    uint8_t  in[MP_CHANNEL_PACKET_BYTES];
    size_t   in_len;
    uint32_t in_from;
    size_t   sent_to[16];      /* bytes the session sent, by endpoint modulo 16 */
    size_t   sent_packets;
} hostile_t;

static bool hostile_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    hostile_t *h = (hostile_t *)context;

    (void)packet;
    h->sent_to[endpoint % 16u] += bytes;
    ++h->sent_packets;
    return true;
}

static size_t hostile_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    hostile_t *h = (hostile_t *)context;
    size_t     len = h->in_len;

    if (len == 0 || len > capacity) {
        return 0;
    }
    memcpy(buffer, h->in, len);
    *from = h->in_from;
    h->in_len = 0;
    return len;
}

static void hostile_inject(hostile_t *h, uint32_t from, const uint8_t *bytes, size_t len)
{
    memcpy(h->in, bytes, len);
    h->in_len = len;
    h->in_from = from;
}

/* magic, type, one u64 value, padded with zero to `len`. Both the honest request and the forged
 * short one are built by this. The session writes a u64 as its high word then its low word, and
 * the denial check below only means something if the forged salt is laid out the same way. */
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
        mp_wire_put_u32(&w, MP_WIRE_VERSION);   /* a request naming another version is refused */
    }
    return len;
}

/* Plants the magic and a random type over the head of a random packet, so half the storm gets past
 * the magic check and the type dispatch and the handlers see it. */
static void plant_magic(uint8_t *bytes, size_t len)
{
    mp_wire_writer_t w;

    if (len < 5) {
        return;
    }
    mp_wire_writer_init(&w, bytes, len);
    mp_wire_put_u32(&w, WIRE_MAGIC);
    mp_wire_put_u8(&w, (uint8_t)(1u + next_random() % 6u));
}

static hostile_t      s_hostile;
static mp_transport_t s_hostile_transport;
static mp_session_t   s_session;

static void hostile_session(mp_session_role_t role)
{
    memset(&s_hostile, 0, sizeof s_hostile);
    s_hostile_transport.context = &s_hostile;
    s_hostile_transport.send = &hostile_send;
    s_hostile_transport.recv = &hostile_recv;
    mp_session_init(&s_session, role, &s_hostile_transport, 0x5151u);
}

static bool session_sane(const mp_session_t *session)
{
    size_t i;
    size_t k;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_peer_t *peer = mp_session_peer(session, i);

        if (peer == NULL || peer->state > MP_PEER_CONNECTED ||
            peer->out_payload_bytes > MP_SESSION_PAYLOAD_BYTES ||
            peer->in_count > MP_SESSION_PAYLOAD_RING || peer->in_head >= MP_SESSION_PAYLOAD_RING) {
            return false;
        }
        for (k = 0; k < peer->in_count; ++k) {
            if (peer->in_ring[(peer->in_head + k) % MP_SESSION_PAYLOAD_RING].bytes >
                MP_SESSION_PAYLOAD_BYTES) {
                return false;
            }
        }
    }
    return true;
}

static void check_session_storm(void)
{
    static uint8_t bytes[MP_CHANNEL_PACKET_BYTES];
    size_t         len;
    unsigned       i;
    uint32_t       now = 0;
    bool           sane = true;

    ut_section("a host under a storm of random and half forged packets lets nobody in");

    hostile_session(MP_SESSION_HOST);
    for (i = 0; i < ROUNDS; ++i) {
        len = random_below(sizeof bytes + 1);
        fill_random(bytes, len);
        if (next_random() & 1u) {
            plant_magic(bytes, len);
        }
        hostile_inject(&s_hostile, 1u + (uint32_t)random_below(12), bytes, len);
        now += 3u;
        mp_session_update(&s_session, now);
        sane = sane && session_sane(&s_session);
    }
    ut_check(sane, "every peer slot stays structurally sane throughout");
    ut_check(mp_session_peer_count(&s_session) == 0 && mp_session_joins(&s_session) == 0,
             "no forged packet reaches connected without the salts");

    ut_section("a joining client under the same storm is neither connected nor talked out of it");

    hostile_session(MP_SESSION_CLIENT);
    mp_session_connect(&s_session, 1u);
    for (i = 0; i < ROUNDS; ++i) {
        len = random_below(sizeof bytes + 1);
        fill_random(bytes, len);
        if (next_random() & 1u) {
            plant_magic(bytes, len);
        }
        hostile_inject(&s_hostile, 1u, bytes, len);   /* all of it from the host's address */
        mp_session_update(&s_session, 100u);          /* time stands still, so no timeout */
        sane = sane && session_sane(&s_session);
    }
    ut_check(sane, "the client's slot stays sane");
    ut_check(!mp_session_is_connected(&s_session), "it is not connected by a forged accept");
    ut_check(mp_session_denied(&s_session) == 0 &&
             mp_session_peer(&s_session, 0)->state == MP_PEER_CONNECTING,
             "and no forged denial without its salt aborts the join");
}

static void check_amplification(void)
{
    static uint8_t bytes[MP_SESSION_REQUEST_BYTES];
    size_t         len;

    ut_section("the host never answers a small packet with a larger one");

    hostile_session(MP_SESSION_HOST);
    len = forge(bytes, (uint8_t)WIRE_REQUEST, 0x1122334455667788ull, 13u);
    hostile_inject(&s_hostile, 7u, bytes, len);
    mp_session_update(&s_session, 10u);
    ut_check(s_hostile.sent_to[7] == 0, "a request below the padded size draws no reply at all");
    ut_check(mp_session_peer_count(&s_session) == 0, "and allocates nothing");

    len = forge(bytes, (uint8_t)WIRE_RESPONSE, 0x1122334455667788ull, 13u);
    hostile_inject(&s_hostile, 7u, bytes, len);
    mp_session_update(&s_session, 20u);
    ut_check(s_hostile.sent_to[7] == 0, "nor does a short response");

    len = forge(bytes, (uint8_t)WIRE_REQUEST, 0x1122334455667788ull, MP_SESSION_REQUEST_BYTES);
    hostile_inject(&s_hostile, 7u, bytes, len);
    mp_session_update(&s_session, 30u);
    ut_check(s_hostile.sent_to[7] > 0 && s_hostile.sent_to[7] < MP_SESSION_REQUEST_BYTES,
             "a padded request draws a challenge smaller than itself");
}

static void check_token_gate(void)
{
    static uint8_t bytes[MP_SESSION_REQUEST_BYTES];
    size_t         len;
    unsigned       i;
    uint32_t       now = 0;

    ut_section("a request holds no slot, and a wrong cookie opens none");

    hostile_session(MP_SESSION_HOST);
    len = forge(bytes, (uint8_t)WIRE_REQUEST, 0xA5A5A5A5A5A5A5A5ull, MP_SESSION_REQUEST_BYTES);
    bytes[13] ^= 0xFFu;   /* the version word follows the salt; a foreign build reads as one */
    hostile_inject(&s_hostile, 9u, bytes, len);
    mp_session_update(&s_session, now);
    ut_check(mp_session_peer(&s_session, 0)->state == MP_PEER_FREE &&
             mp_session_denied(&s_session) == 1u,
             "a request from another protocol version is refused before a slot is spent");
    len = forge(bytes, (uint8_t)WIRE_REQUEST, 0xA5A5A5A5A5A5A5A5ull, MP_SESSION_REQUEST_BYTES);
    hostile_inject(&s_hostile, 9u, bytes, len);
    mp_session_update(&s_session, now);
    ut_check(mp_session_peer(&s_session, 0)->state == MP_PEER_FREE &&
                 s_hostile.sent_to[9] > 0,
             "a padded request is answered and opens no slot, so forged requests cannot hold "
             "the slots for the handshake timeout");

    /* Forged responses from the right address with a cookie that cannot be right, every second,
     * for twice the handshake timeout. None of them may open anything. */
    for (i = 0; i < 10u; ++i) {
        now += 1000u;
        len = forge(bytes, (uint8_t)WIRE_RESPONSE, 0xDEADBEEFull, MP_SESSION_REQUEST_BYTES);
        hostile_inject(&s_hostile, 9u, bytes, len);
        mp_session_update(&s_session, now);
    }
    ut_check(mp_session_peer(&s_session, 0)->state == MP_PEER_FREE,
             "no slot opens under a stream of forged responses");
    ut_check(mp_session_denied(&s_session) > 0, "and each forgery was counted");
}

static void check_deny_forgery(void)
{
    static uint8_t bytes[64];
    uint64_t       salt;
    size_t         len;

    ut_section("a denial without the client's salt is ignored, with it honoured");

    hostile_session(MP_SESSION_CLIENT);
    mp_session_connect(&s_session, 1u);
    mp_session_update(&s_session, 0u);
    salt = mp_session_peer(&s_session, 0)->client_salt;

    len = forge(bytes, (uint8_t)WIRE_DENY, 0u, 13u);
    hostile_inject(&s_hostile, 1u, bytes, len);
    mp_session_update(&s_session, 10u);
    ut_check(mp_session_peer(&s_session, 0)->state == MP_PEER_CONNECTING &&
             mp_session_denied(&s_session) == 0,
             "a zero salt denial from the host's own address changes nothing");

    len = forge(bytes, (uint8_t)WIRE_DENY, salt, 13u);
    hostile_inject(&s_hostile, 1u, bytes, len);
    mp_session_update(&s_session, 20u);
    ut_check(mp_session_peer(&s_session, 0)->state == MP_PEER_FREE &&
             mp_session_denied(&s_session) == 1,
             "the genuine denial, carrying the salt, ends the join");
}

int main(void)
{
    check_wire();
    check_snapshot();
    check_command();
    check_events();
    check_channel();
    check_session_storm();
    check_amplification();
    check_token_gate();
    check_deny_forgery();

    return ut_summary("mp_fuzz");
}
