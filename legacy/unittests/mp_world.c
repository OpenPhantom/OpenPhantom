/* mp_world.c: the map's own message and the measurement built on it, driven over their edges.
 *
 * Two things here can silently do the wrong thing. The digest is a variable length message on a
 * channel that already carries four fixed length events, a one byte world slot note and a four
 * byte acknowledgement, so the recogniser is driven over all of them: a digest read as an event
 * would open a door nobody opened, and an event read as a digest would be counted as a
 * measurement and lost. And the deviation is the whole point of the instrument, so its two
 * shapes are pinned: the free runner's pose wraps and its two ends are neighbours, every other
 * type is clamped and a difference of nearly one whole travel is a mover in the opposite state.
 *
 * The engine half is not exercised here, because there is no game in this process. What is
 * checked of it is the one thing that must hold with nothing resolved: it refuses, and an event
 * that arrives anyway is counted rather than handed to a null opener.
 */
#include "unittest.h"

#include "mp_events.h"
#include "mp_world.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The wire quantises the pose to one part in 65535 of the travel, sixteen bits, so a round trip
 * is worth this much and no more. */
#define POSE_ERROR (1.0f / 65535.0f)

/* By its bit pattern rather than by an expression, because the compiler folds the obvious
 * expression and warns about it while doing so. */
static float make_nan(void)
{
    const uint32_t bits = 0x7FC00000u;
    float          value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static void check_digest_round_trip(void)
{
    mp_world_digest_t sent;
    mp_world_digest_t got;
    uint8_t           buffer[MP_WORLD_DIGEST_MAX_BYTES];
    size_t            bytes;

    ut_section("a digest goes out and comes back, with its tick, its types and its poses");

    mp_world_digest_init(&sent, 0xFEEDF00Du);
    ut_check(sent.count == 0u && sent.tick == 0xFEEDF00Du, "an empty digest carries only its tick");

    ut_check(mp_world_digest_add(&sent, 7u, 2u, 1u, 1u, 3.0f, 12.0f, 0.0f) == MP_WORLD_ADD_OK,
             "a door a quarter open goes in");
    ut_check(mp_world_digest_add(&sent, 300u, 0u, 0u, 1u, 9.0f, 10.0f, 0.0f) == MP_WORLD_ADD_OK,
             "so does a free runner with an id past a byte");
    ut_check(mp_world_digest_add(&sent, 1u, 6u, 5u, 1u, 0.0f, 4.0f, 0.0f) == MP_WORLD_ADD_OK,
             "and a button at rest");

    bytes = mp_world_digest_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_WORLD_DIGEST_HEADER_BYTES + 3u * MP_WORLD_DIGEST_ENTRY_BYTES,
             "the encoded length is the header plus seven bytes an entry");
    ut_check(bytes == 29u, "which for three entries is twenty nine bytes");
    ut_check(buffer[0] == MP_WORLD_DIGEST_TAG, "the tag comes first");
    ut_check(buffer[1] == 0x0Du && buffer[2] == 0xF0u && buffer[3] == 0xEDu && buffer[4] == 0xFEu,
             "then the tick, least significant byte first");
    ut_check(buffer[5] == 0u && buffer[6] == 0u,
             "then the level, which a hand built digest leaves at zero");
    ut_check(buffer[7] == 3u, "then the entry count");

    ut_check(mp_world_is_digest(buffer, bytes), "and it is recognised as a digest");

    ut_section("the level a digest describes travels with it");

    {
        mp_world_digest_t stamped;
        mp_world_digest_t back = { 0 };
        uint8_t           stamped_bytes[64];
        size_t            n;

        mp_world_digest_init(&stamped, 7u);
        mp_world_digest_set_level(&stamped, 78u);
        (void)mp_world_digest_add(&stamped, 3u, 0u, 0u, 1u, 1.0f, 29.0f, 0.0f);
        n = mp_world_digest_encode(&stamped, stamped_bytes, sizeof stamped_bytes);
        ut_check(n != 0u && mp_world_digest_decode(stamped_bytes, n, &back),
                 "a stamped digest round trips");
        ut_checkf(back.level == 78u, "and says which level it describes (%u)",
                  (unsigned)back.level);
        ut_check(back.tick == 7u && back.count == 1u,
                 "without disturbing the tick or the count around it");
    }

    ut_check(mp_world_digest_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.tick == 0xFEEDF00Du && got.count == 3u, "with its tick and its count");
    ut_check(got.entry[0].id == 7u && got.entry[0].type == 2u && got.entry[0].dir == 1u,
             "the first entry's id, type and direction survive");
    ut_near(got.entry[0].pose, 0.25f, POSE_ERROR, "and its pose as a quarter of the travel");
    ut_check(got.entry[1].id == 300u && got.entry[1].type == 0u,
             "an id past a byte survives, which is why the id is two bytes");
    ut_near(got.entry[1].pose, 0.9f, POSE_ERROR, "the free runner's pose survives");
    ut_check(got.entry[2].type == 6u && got.entry[2].dir == 5u,
             "the highest direction the engine has survives the three bits it travels in");
    ut_near(got.entry[2].pose, 0.0f, POSE_ERROR, "and a pose of zero stays zero");
    ut_check(got.entry[0].active == 1u && got.entry[1].active == 1u && got.entry[2].active == 1u,
             "and so does the bit that says the engine still ticks these movers, which is what a "
             "door that has latched open and one that has finished closing differ in");
}

static void check_digest_refusals(void)
{
    mp_world_digest_t digest;
    uint8_t           buffer[MP_WORLD_DIGEST_MAX_BYTES];
    float             not_a_number = make_nan();
    unsigned          index;
    bool              all_ok = true;

    ut_section("what the digest will not describe");

    mp_world_digest_init(&digest, 1u);
    ut_check(mp_world_digest_add(&digest, 0u, 2u, 0u, 1u, 1.0f, 0.0f, 0.0f) == MP_WORLD_ADD_REFUSED,
             "a mover with no travel is refused rather than divided by");
    ut_check(mp_world_digest_add(&digest, 0u, 2u, 0u, 1u, 1.0f, -4.0f, 0.0f) ==
                 MP_WORLD_ADD_REFUSED,
             "and so is one with a negative travel");
    ut_check(mp_world_digest_add(&digest, 0u, 8u, 0u, 1u, 1.0f, 4.0f, 0.0f) == MP_WORLD_ADD_REFUSED,
             "a type past the engine's eight is refused");
    ut_check(mp_world_digest_add(&digest, 0u, 2u, 8u, 1u, 1.0f, 4.0f, 0.0f) == MP_WORLD_ADD_REFUSED,
             "a direction that would not fit its three bits is refused");
    ut_check(mp_world_digest_add(&digest, 0x10000u, 2u, 0u, 1u, 1.0f, 4.0f, 0.0f) ==
                 MP_WORLD_ADD_REFUSED,
             "an id past what two bytes hold is refused");
    ut_check(digest.count == 0u, "and none of them went in");

    ut_check(mp_world_digest_add(&digest, 0u, 2u, 0u, 1u, not_a_number, 4.0f, 0.0f) ==
                 MP_WORLD_ADD_REFUSED,
             "a pose that is not a number is refused");
    ut_check(mp_world_digest_add(&digest, 0u, 2u, 0u, 1u, 1.0f, not_a_number, 0.0f) ==
                 MP_WORLD_ADD_REFUSED,
             "and so is a travel that is not one");

    ut_section("the cap holds and reports itself");

    for (index = 0; index < MP_WORLD_DIGEST_MAX_ENTRIES; ++index) {
        all_ok = all_ok && mp_world_digest_add(&digest, index, 2u, 1u, 1u, 1.0f, 4.0f, 0.0f) ==
                               MP_WORLD_ADD_OK;
    }
    ut_check(all_ok, "the digest takes exactly its cap");
    ut_check(digest.count == MP_WORLD_DIGEST_MAX_ENTRIES, "and is then full");
    ut_check(mp_world_digest_add(&digest, 999u, 2u, 1u, 1u, 1.0f, 4.0f, 0.0f) == MP_WORLD_ADD_FULL,
             "the next one is refused as full rather than overflowing");
    ut_check(digest.count == MP_WORLD_DIGEST_MAX_ENTRIES, "and the count did not move");
    ut_check(mp_world_digest_encode(&digest, buffer, sizeof buffer) == MP_WORLD_DIGEST_MAX_BYTES,
             "a full digest encodes to exactly the size the header promises");
    ut_check(mp_world_digest_encode(&digest, buffer, MP_WORLD_DIGEST_MAX_BYTES - 1u) == 0u,
             "and into one byte less than that, not at all");

    ut_section("a pose outside its travel is clamped rather than wrapped");

    mp_world_digest_init(&digest, 1u);
    (void)mp_world_digest_add(&digest, 0u, 2u, 0u, 1u, 9.0f, 4.0f, 0.0f);
    (void)mp_world_digest_add(&digest, 1u, 2u, 0u, 1u, -3.0f, 4.0f, 0.0f);
    ut_near(digest.entry[0].pose, 1.0f, 1e-6, "past the end is the end");
    ut_near(digest.entry[1].pose, 0.0f, 1e-6, "and before the start is the start");
}

static void check_telling_the_digest_apart(void)
{
    mp_world_digest_t digest;
    mp_event_t        event;
    uint8_t           buffer[MP_WORLD_DIGEST_MAX_BYTES];
    uint8_t           event_bytes[MP_EVENT_MAX_BYTES];
    uint8_t           slot_note[1] = { MP_WORLD_DIGEST_TAG };
    uint8_t           ack[4] = { MP_WORLD_DIGEST_TAG, 0u, 0u, 0u };
    size_t            bytes;
    size_t            which;
    mp_world_digest_t decoded;

    ut_section("a digest, an event, a slot note and an acknowledgement are told apart");

    mp_world_digest_init(&digest, 5u);
    (void)mp_world_digest_add(&digest, 3u, 2u, 1u, 1u, 1.0f, 4.0f, 0.0f);
    bytes = mp_world_digest_encode(&digest, buffer, sizeof buffer);

    ut_check(!mp_event_is_event(buffer, bytes), "a digest is not an event");
    ut_check(!mp_world_is_digest(slot_note, sizeof slot_note),
             "a one byte message is a slot note however it starts");
    ut_check(!mp_world_is_digest(ack, sizeof ack),
             "and a four byte one an acknowledgement, both shorter than a digest's header");

    memset(&event, 0, sizeof event);
    for (which = 0; which < 4u; ++which) {
        static const uint8_t KIND[4] = { MP_EVENT_SHOT, MP_EVENT_PUSH, MP_EVENT_SABRE,
                                         MP_EVENT_MOVER };
        size_t encoded;

        event.kind = KIND[which];
        encoded = mp_event_encode(&event, event_bytes, sizeof event_bytes);
        ut_checkf(encoded != 0u && !mp_world_is_digest(event_bytes, encoded),
                  "event tag %02X is not a digest", (unsigned)KIND[which]);
    }

    ut_section("a torn digest is refused rather than read short");

    ut_check(!mp_world_is_digest(buffer, bytes - 1u),
             "one byte short of an entry does not divide and is refused");
    ut_check(!mp_world_is_digest(buffer, MP_WORLD_DIGEST_HEADER_BYTES),
             "a header whose count says one entry but which carries none is refused");
    buffer[7] = 2u;
    ut_check(!mp_world_is_digest(buffer, bytes),
             "a count byte that disagrees with the length is refused, which is what stops a "
             "truncated digest reading as a shorter valid one");
    ut_check(!mp_world_digest_decode(buffer, bytes, &decoded), "and it does not decode");
    buffer[7] = MP_WORLD_DIGEST_MAX_ENTRIES + 1u;
    ut_check(!mp_world_is_digest(buffer, bytes), "nor does a count past the cap");
    ut_check(!mp_world_is_digest(NULL, 0), "nor does nothing");
}

static void check_the_mover_event(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a mover trigger carries its tick, the mover it names and which way");

    memset(&sent, 0, sizeof sent);
    sent.kind       = MP_EVENT_MOVER;
    sent.tick       = 4242u;
    sent.mover_id   = 0x0123u;
    sent.mover_mode = MP_EVENT_MOVER_OPEN;

    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_MOVER_BYTES && bytes == 8u, "a mover event is eight bytes");
    ut_check(bytes == MP_EVENT_SABRE_BYTES && bytes != MP_EVENT_SHOT_BYTES &&
             bytes != MP_EVENT_PUSH_BYTES && bytes != MP_EVENT_WEAPON_BYTES,
             "the length of a sabre action and no other event's, so the tag tells those two "
             "apart, as it does the push and the weapon change");
    ut_check(mp_event_is_event(buffer, bytes), "and it is recognised as an event");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_MOVER && got.tick == 4242u, "kind and tick survive");
    ut_check(got.mover_id == 0x0123u && got.mover_mode == MP_EVENT_MOVER_OPEN,
             "and so do the mover and the mode");

    sent.mover_id   = 0xFFFFu;
    sent.mover_mode = MP_EVENT_MOVER_CLOSE;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_decode(buffer, bytes, &got) && got.mover_id == 0xFFFFu &&
             got.mover_mode == MP_EVENT_MOVER_CLOSE,
             "the largest id two bytes hold survives, and so does a close");

    sent.mover_mode = MP_EVENT_MOVER_OPEN + 1u;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "a mode the engine has no function for does not encode");

    sent.mover_mode = MP_EVENT_MOVER_OPEN;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    buffer[7] = 200u;   /* the mode byte, forged */
    ut_check(mp_event_is_event(buffer, bytes), "a forged mode still looks like an event");
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "but does not decode, because the engine has two openers and no third");
}

static void check_the_deviation(void)
{
    float not_a_number = make_nan();

    ut_section("the deviation, and the one type whose pose wraps");

    ut_near(mp_world_pose_delta(2u, 0.5f, 0.5f), 0.0f, 1e-6, "two doors in step are zero apart");
    ut_near(mp_world_pose_delta(2u, 0.75f, 0.25f), 0.5f, 1e-6, "half a travel apart is a half");
    ut_near(mp_world_pose_delta(2u, 0.25f, 0.75f), 0.5f, 1e-6, "and it does not care which way");
    ut_near(mp_world_pose_delta(2u, 0.99f, 0.01f), 0.98f, 1e-5,
            "a door at one end and one at the other are nearly a whole travel apart, because a "
            "door is clamped and cannot have wrapped");
    ut_near(mp_world_pose_delta(MP_WORLD_TYPE_ALWAYS_ON, 0.99f, 0.01f), 0.02f, 1e-5,
            "the same two poses on a free runner are a hair apart, because its pose wraps");
    ut_near(mp_world_pose_delta(MP_WORLD_TYPE_ALWAYS_ON, 0.2f, 0.8f), 0.4f, 1e-5,
            "and the free runner still takes the shorter way round when that is the long one");
    ut_near(mp_world_pose_delta(2u, not_a_number, 0.5f), 1.0f, 1e-6,
            "a pose that is not a number reads as the largest disagreement there is");

    ut_section("the histogram buckets");

    ut_check(mp_world_bucket(0.0f) == 0u, "in step falls in the first bucket");
    ut_check(mp_world_bucket(1.0f / 512.0f) == 0u, "and so does half a 256th");
    ut_check(mp_world_bucket(1.0f / 256.0f) == 1u, "a 256th exactly falls in the second");
    ut_check(mp_world_bucket(1.0f / 100.0f) == 1u,
             "a hundredth is still under a 64th, so the second");
    ut_check(mp_world_bucket(1.0f / 32.0f) == 2u, "a 32nd in the third");
    ut_check(mp_world_bucket(1.0f / 8.0f) == 3u, "an eighth in the fourth");
    ut_check(mp_world_bucket(0.25f) == MP_WORLD_BUCKETS - 1u, "a quarter in the last");
    ut_check(mp_world_bucket(1.0f) == MP_WORLD_BUCKETS - 1u, "and so does everything above it");
}

static void check_the_counting(void)
{
    mp_world_stats_t stats;
    mp_world_entry_t wire;
    mp_world_entry_t local;

    ut_section("a compared pair lands in the row of the type the sender named");

    memset(&stats, 0, sizeof stats);
    wire.id   = 4u;
    wire.type = 2u;
    wire.dir  = 1u;
    wire.pose = 0.5f;
    local = wire;
    mp_world_note(&stats, &wire, &local);
    ut_check(stats.type[2].compared == 1u, "the door row counted it");
    ut_check(stats.type[2].bucket[0] == 1u, "in the closest bucket");
    ut_check(stats.type[2].worst_milli == 0u, "with nothing to report as the worst");
    ut_check(stats.type[2].dir_mismatch == 0u && stats.type[2].type_mismatch == 0u,
             "and no disagreement");

    local.pose = 0.4f;
    local.dir  = 3u;
    mp_world_note(&stats, &wire, &local);
    ut_check(stats.type[2].compared == 2u, "a second pair is counted");
    ut_check(stats.type[2].dir_mismatch == 1u, "a direction that differs is counted");
    ut_check(stats.type[2].worst_milli == 100u,
             "and a tenth of the travel apart reports as a hundred thousandths");
    ut_check(stats.type[2].bucket[MP_WORLD_BUCKETS - 1u] == 0u &&
             stats.type[2].bucket[3] == 1u, "a tenth is under a quarter, so the fourth bucket");

    local.pose = 0.5f;
    local.dir  = 1u;
    local.type = 4u;
    mp_world_note(&stats, &wire, &local);
    ut_check(stats.type[2].type_mismatch == 1u && stats.type[2].compared == 3u,
             "a type that differs is counted in the row the sender named, not in its own");
    ut_check(stats.type[4].compared == 0u, "so the local type's row stays empty");
    ut_check(stats.type[2].worst_milli == 100u, "and the worst is not walked back");

    wire.type = MP_WORLD_MOVER_TYPES;
    mp_world_note(&stats, &wire, &local);
    ut_check(stats.type[2].compared == 3u,
             "a type past the engine's eight is dropped rather than indexed with");
}

/* One reliable message out of this module, captured rather than sent. */
static uint8_t s_sent[MP_WORLD_DIGEST_MAX_BYTES];
static size_t  s_sent_bytes;
static unsigned s_sends;

static bool capture(const uint8_t *bytes, size_t count)
{
    ++s_sends;
    if (count <= sizeof s_sent) {
        memcpy(s_sent, bytes, count);
        s_sent_bytes = count;
    }
    return true;
}

static void check_the_engine_half_refuses(void)
{
    mp_event_t              event;
    const mp_world_stats_t *stats;
    uint8_t                 digest[MP_WORLD_DIGEST_MAX_BYTES];
    mp_world_digest_t       built;
    size_t                  bytes;
    uint32_t                before;

    ut_section("with no game in the process the engine half refuses and counts");

    ut_check(!mp_world_install(true, true),
             "nothing resolves here, so the install answers false rather than hulling a guess");

    memset(&event, 0, sizeof event);
    event.kind       = MP_EVENT_MOVER;
    event.tick       = 1u;
    event.mover_id   = 3u;
    event.mover_mode = MP_EVENT_MOVER_OPEN;
    mp_world_queue_event(1u, &event);
    mp_world_run_due();
    stats = mp_world_statistics();
    ut_check(stats->performed == 0u && stats->perform_refused == 1u,
             "an event that arrives anyway is refused and counted, not handed to a null opener");

    event.kind = MP_EVENT_SHOT;
    mp_world_queue_event(1u, &event);
    mp_world_run_due();
    ut_check(mp_world_statistics()->perform_refused == 1u,
             "and a queue call with something that is not a mover event puts nothing in");

    ut_section("what the module takes off the reliable channel and what it leaves");

    mp_world_digest_init(&built, 9u);
    (void)mp_world_digest_add(&built, 1u, 2u, 1u, 1u, 1.0f, 4.0f, 0.0f);
    bytes = mp_world_digest_encode(&built, digest, sizeof digest);
    ut_check(mp_world_take_message(1u, digest, bytes), "a digest is the map's");

    event.kind = MP_EVENT_MOVER;
    bytes = mp_event_encode(&event, digest, sizeof digest);
    ut_check(mp_world_take_message(1u, digest, bytes), "and so is a mover event");
    digest[7] = 9u;   /* forge the mode so the decode refuses it */
    ut_check(mp_world_take_message(1u, digest, bytes),
             "a torn mover event is still taken, so it cannot reach the puppet as something else");

    event.kind = MP_EVENT_PUSH;
    bytes = mp_event_encode(&event, digest, sizeof digest);
    ut_check(!mp_world_take_message(1u, digest, bytes),
             "a push belongs to the body and is left");

    event.kind = MP_EVENT_SABRE;
    bytes = mp_event_encode(&event, digest, sizeof digest);
    ut_check(bytes == MP_EVENT_MOVER_BYTES && !mp_world_take_message(1u, digest, bytes),
             "and so is a sabre action, which has a mover's length and not its tag");

    ut_section("a mover event is due on its own player's clock and holds nobody else's");

    mp_world_clear();
    before = mp_world_statistics()->perform_refused;
    mp_world_note_render_tick(1u, 100u);
    mp_world_note_render_tick(2u, 50u);
    event.kind = MP_EVENT_MOVER;
    event.tick = 60u;   /* ten ticks ahead of the second player's replay */
    mp_world_queue_event(2u, &event);
    event.tick = 100u;  /* and the first player's, due now */
    mp_world_queue_event(1u, &event);
    mp_world_run_due();
    ut_checkf(mp_world_statistics()->perform_refused == before + 1u,
              "the first player's door comes due and, with no game, is refused, while the "
              "second one's waits for its own replay (%u refused)",
              (unsigned)(mp_world_statistics()->perform_refused - before));
    mp_world_note_render_tick(2u, 60u);
    mp_world_run_due();
    ut_check(mp_world_statistics()->perform_refused == before + 2u,
             "and the second one's when that replay gets there");
    mp_world_queue_event(MP_WORLD_CLOCKS, &event);
    mp_world_run_due();
    ut_check(mp_world_statistics()->perform_refused == before + 2u,
             "a clock past the far banks takes nothing");
    mp_world_clear();

    ut_section("with nothing caught and nothing to measure, nothing is sent");

    s_sends = 0;
    s_sent_bytes = 0;
    mp_world_send(MP_WORLD_DIGEST_TICKS, &capture);
    ut_check(s_sends == 0u, "an empty queue and an unresolved world send no message at all");
}

int main(void)
{
    check_digest_round_trip();
    check_digest_refusals();
    check_telling_the_digest_apart();
    check_the_mover_event();
    check_the_deviation();
    check_the_counting();
    check_the_engine_half_refuses();

    return ut_summary("mp_world");
}
