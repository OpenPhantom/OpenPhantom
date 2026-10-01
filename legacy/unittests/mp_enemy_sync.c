/* mp_enemy_sync.c: the enemy block, and the ways a malformed or misplaced one could do damage.
 *
 * SIZE NOTE: over six hundred lines, because the pure rules of the binding that the receiving
 * half asks, the slot and the life a kept corpse belongs to, are held here beside the block they
 * decide for. They are one section each and share nothing but the harness; the next section that
 * grows this file takes them to a test of their own. How a replica's body ends is tested with the
 * body field, in mp_enemy_body.c.
 *
 * There is no engine behind this, so no actor exists and none can be created. That is not a gap in
 * the test: it is the case a client is in for every enemy the host has and it does not, and the
 * block still has to be parsed correctly, refused correctly, and remembered correctly.
 *
 * What would be silent if it were wrong:
 *
 *   a block that claims more records than it carries, applied as far as it goes, leaves half the
 *   enemies of a level at the host's positions and half at their own, and nothing records which;
 *
 *   a record whose length is misread walks the cursor into the middle of the next one, so every
 *   enemy after it gets somebody else's fields with no complaint;
 *
 *   a placement with no local actor, if it stopped the walk, would silently discard every enemy
 *   behind it in the block;
 *
 *   a block from another level, applied, moves bodies that happen to share an index, and now that
 *   a block can create bodies it would create the wrong ones;
 *
 *   and a mirror that only advanced when a body was written decoded every later delta for a
 *   missing body against nothing, so the body that turned up later inherited zeros.
 */
#include "unittest.h"

#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The level both ends of these tests are in, and one they are not. */
#define HERE  57u
#define AWAY  100u

static uint8_t s_block[2048];

/* The substep that carried a block. A real stream never hands the same one twice and never goes
 * backwards, so the helpers count for the tests that do not care which number it is. */
static uint32_t s_wire_tick;

static bool take_block(const uint8_t *block, size_t bytes)
{
    return mp_enemy_sync_apply(block, bytes, ++s_wire_tick);
}

static void full_record(mp_enemy_record_t *record, uint8_t index, uint8_t generation, uint32_t x,
                        int32_t health)
{
    uint32_t packed = 0;

    memset(record, 0, sizeof *record);
    record->value[MP_ENEMY_F_INDEX]      = index;
    record->value[MP_ENEMY_F_GENERATION] = generation;
    (void)mp_enemy_wire_put_position((float)x, &packed);
    record->value[MP_ENEMY_F_POS_X]  = packed;
    /* A host names all three axes, and a first sighting goes whole (put_record), because a receiver
     * with nothing to read a record against takes only a whole one. */
    (void)mp_enemy_wire_put_position(5.0f, &packed);
    record->value[MP_ENEMY_F_POS_Y]  = packed;
    (void)mp_enemy_wire_put_position(7.0f, &packed);
    record->value[MP_ENEMY_F_POS_Z]  = packed;
    record->value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(health);
}

/* One record's worth of bytes, as the sender would lay them out: index, generation, then the
 * delta against `baseline`, or for a first sighting (no baseline) the whole record, which is what
 * the host sends for a key its receiver does not hold. */
static size_t put_record(uint8_t *out, size_t capacity, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *baseline)
{
    size_t bytes = 0;
    bool   wrote;

    out[0] = (uint8_t)record->value[MP_ENEMY_F_INDEX];
    out[1] = (uint8_t)record->value[MP_ENEMY_F_GENERATION];
    wrote  = baseline != NULL
                 ? mp_enemy_wire_encode(record, baseline, out + 2, capacity - 2, &bytes)
                 : mp_enemy_wire_encode_whole(record, out + 2, capacity - 2, &bytes);
    if (!wrote) {
        return 0;
    }
    return 2u + bytes;
}

static void begin_block(uint8_t count, uint16_t level)
{
    memset(s_block, 0, sizeof s_block);
    s_block[0] = count;
    s_block[1] = (uint8_t)(level & 0xFFu);
    s_block[2] = (uint8_t)(level >> 8);
}

static void mark_present(uint8_t index)
{
    s_block[1u + MP_ENEMY_SYNC_LEVEL_BYTES + (index >> 3)] |= (uint8_t)(1u << (index & 7u));
}

/* A block of `n` first sightings at level `level`, one record per index. */
static size_t build_at(uint8_t count, const uint8_t *indices, size_t n, uint16_t level)
{
    size_t at;
    size_t i;

    begin_block(count, level);
    at = MP_ENEMY_SYNC_HEADER_BYTES;
    for (i = 0; i < n; ++i) {
        mp_enemy_record_t record;
        size_t            wrote;

        full_record(&record, indices[i], 1u, 10u + (uint32_t)i, 100);
        wrote = put_record(s_block + at, sizeof s_block - at, &record, NULL);
        if (wrote == 0) {
            return 0;
        }
        mark_present(indices[i]);
        at += wrote;
    }
    return at;
}

static size_t build(uint8_t count, const uint8_t *indices, size_t n)
{
    return build_at(count, indices, n, HERE);
}

/* A host's block for its first peer, as the world send builds one: the census once, then the
 * block against that peer's view. */
static bool encode_block(uint8_t *out, size_t capacity, size_t *bytes)
{
    return mp_enemy_sync_begin_send() && mp_enemy_sync_encode_for(0u, out, capacity, bytes);
}

/* Three placements a block names in the tests that do not care which. */
static const uint8_t THREE[] = { 4u, 17u, 200u };

static void check_off_and_the_shape(void)
{
    ut_section("off by default, and off means off in BOTH directions");
    {
        /* The module is off unless a real network role switches it on, because the loopback is one
         * process being both sides and the two halves would run over one pool of actors. A guard
         * that only stopped the sending half would still have the receiving one parking the very
         * actors the sender reads. */
        size_t n = 0;

        mp_enemy_sync_set_enabled(false);
        mp_enemy_sync_set_level(true, HERE);
        ut_check(!encode_block(s_block, sizeof s_block, &n),
                 "a module that is off describes nothing");
        ut_check(!take_block(s_block, 64u), "and applies nothing");
    }
    mp_enemy_sync_set_enabled(true);

    ut_section("the shape of a block");
    ut_check(MP_ENEMY_SYNC_BITMAP_BYTES == 32u,
             "the presence bitmap is 32 bytes, one bit per placement");
    ut_check(MP_ENEMY_SYNC_LEVEL_BYTES == 2u,
             "the level is two bytes, the same identity the map's digest carries");
    ut_check(MP_ENEMY_SYNC_HEADER_BYTES == 40u,
             "so a block header is the count, the level, the bitmap and the five bytes of the "
             "world events' head");
    ut_check(MP_ENEMY_SYNC_MAX_PLACEMENTS == 256u,
             "and 256 placements is what a u8 index reaches; the largest shipped level has 255");
}

static void check_an_encode_with_no_engine(void)
{
    size_t bytes;

    ut_section("an encode with no engine behind it");
    mp_enemy_sync_reset();
    ut_check(!encode_block(s_block, sizeof s_block, &bytes),
             "a host with no level open builds no block: there is nothing to describe and no "
             "name to put on it");
    mp_enemy_sync_set_level(true, HERE);
    ut_check(encode_block(s_block, sizeof s_block, &bytes),
             "a host in a level with no actors still produces a block");
    ut_checkf(bytes == MP_ENEMY_SYNC_HEADER_BYTES,
              "and it is the header alone, %u bytes, with nothing behind it", (unsigned)bytes);
    ut_check(s_block[0] == 0u, "the count is zero");
    ut_check(s_block[1] == (uint8_t)(HERE & 0xFFu) && s_block[2] == (uint8_t)(HERE >> 8),
             "the level is the one this side was told, low byte first");
    {
        size_t i;
        bool   any = false;

        for (i = 1u + MP_ENEMY_SYNC_LEVEL_BYTES; i < MP_ENEMY_SYNC_HEADER_BYTES; ++i) {
            any = any || (s_block[i] != 0u);
        }
        ut_check(!any, "and no bit is set, so a receiver lets go of every replica it holds");
    }
    ut_check(!encode_block(NULL, sizeof s_block, &bytes), "a null buffer is refused");
    ut_check(mp_enemy_sync_begin_send() &&
                 !mp_enemy_sync_encode_for(MP_ENEMY_SYNC_VIEWS, s_block, sizeof s_block, &bytes),
             "a view past the peers a host seats is refused");
    ut_check(!encode_block(s_block, 4u, &bytes),
             "and a buffer too small for the header is refused rather than half filled");
}

static void check_blocks_taken_and_refused(void)
{
    size_t bytes;

    ut_section("a well formed block with records nobody here can place");
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    bytes = build(3u, THREE, 3u);
    ut_check(bytes > MP_ENEMY_SYNC_HEADER_BYTES, "the block was built");
    ut_check(take_block(s_block, bytes),
             "three records for placements with no local actor are parsed and the block is "
             "accepted: a missing actor is counted, not a parse failure");
    ut_checkf(mp_enemy_sync_pending() == 3u,
              "and all three are remembered as wanted (%u), to be created from a substep",
              (unsigned)mp_enemy_sync_pending());
    ut_check(mp_enemy_sync_spawn_pending() == 0u,
             "with no engine behind it nothing is created");
    ut_check(mp_enemy_sync_pending() == 0u,
             "and the wish is not kept: the host lists them again in its next block, so a wish "
             "that could not be granted is dropped rather than retried forever");

    ut_section("a block that lies about how many records it carries");
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    bytes = build(3u, THREE, 3u);
    s_block[0] = 9u;
    {
        uint32_t torn     = mp_enemy_sync_state()->refused;
        uint32_t baseless = mp_enemy_sync_state()->baseless_refused;

        ut_check(!take_block(s_block, bytes),
                 "nine records claimed and three carried is refused WHOLE, because half a level at "
                 "the host's positions and half at its own is worse than none");
        ut_check(mp_enemy_sync_state()->refused == torn + 1u &&
                     mp_enemy_sync_state()->baseless_refused == baseless,
                 "and it is counted as torn");
    }

    ut_section("a truncated block");
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    bytes = build(3u, THREE, 3u);
    ut_check(!take_block(s_block, bytes - 1u),
             "one byte short of the records it claims is refused");
    ut_check(!take_block(s_block, MP_ENEMY_SYNC_HEADER_BYTES - 1u),
             "and so is a block too short to hold its own header");
    ut_check(!take_block(NULL, 64u), "a null block is refused");

    ut_section("a header with no records is legitimate");
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    bytes = build(0u, NULL, 0u);
    ut_check(take_block(s_block, bytes),
             "a count of zero is a level whose enemies have nothing new to say, not an error");

    ut_section("a block about another level, and a block with no level open here");
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    bytes = build_at(3u, THREE, 3u, AWAY);
    ut_check(!take_block(s_block, bytes),
             "a well formed block from a level this side is not in is refused whole");
    ut_check(mp_enemy_sync_pending() == 0u,
             "and nothing in it is wished for, because its indices name other placements here");
    mp_enemy_sync_set_level(false, 0u);
    bytes = build(3u, THREE, 3u);
    ut_check(!take_block(s_block, bytes),
             "with no level open here, which is a menu or a load, the block is refused rather "
             "than applied to a world that is being built");
    mp_enemy_sync_set_level(true, HERE);
    ut_check(take_block(s_block, bytes), "and the same bytes are taken once one is");
    ut_check(mp_enemy_sync_pending() == 3u, "with the three wished for");
    mp_enemy_sync_set_level(true, AWAY);
    ut_check(mp_enemy_sync_pending() == 0u,
             "a level change forgets every wish, because a wish was about the level before");
    ut_check(mp_enemy_sync_spawn_pending() == 0u, "and nothing is created for the old one");
}

static void check_the_mirror(void)
{
    ut_section("the mirror follows the sender, body or no body");
    {
        mp_enemy_record_t first;
        mp_enemy_record_t delta;
        mp_enemy_record_t seen;
        size_t            at;

        mp_enemy_sync_reset();
        mp_enemy_sync_set_level(true, HERE);

        /* A first sighting of placement 4 at x = 10 with 100 health. */
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        full_record(&first, 4u, 1u, 10u, 100);
        at += put_record(s_block + at, sizeof s_block - at, &first, NULL);
        mark_present(4u);
        ut_check(take_block(s_block, at), "the first sighting is taken");
        ut_check(mp_enemy_sync_mirror(4u, &seen), "and remembered although no body exists here");
        ut_check(seen.value[MP_ENEMY_F_POS_X] == first.value[MP_ENEMY_F_POS_X],
                 "with the position the host sent");

        /* Then only the health changes, encoded as the host would: against the first record. */
        delta = first;
        delta.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(40);
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        at += put_record(s_block + at, sizeof s_block - at, &delta, &first);
        mark_present(4u);
        ut_check(take_block(s_block, at), "a delta against the first sighting is taken");
        ut_check(mp_enemy_sync_mirror(4u, &seen), "and the mirror still answers");
        ut_check(seen.value[MP_ENEMY_F_HEALTH] == delta.value[MP_ENEMY_F_HEALTH],
                 "it carries the new health");
        ut_check(seen.value[MP_ENEMY_F_POS_X] == first.value[MP_ENEMY_F_POS_X],
                 "AND the position from the first sighting, which the delta did not repeat: the "
                 "body created later gets where the host has it, not the axis");

        /* A new generation is a new life and starts from nothing. */
        full_record(&first, 4u, 2u, 20u, 100);
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        at += put_record(s_block + at, sizeof s_block - at, &first, NULL);
        mark_present(4u);
        ut_check(take_block(s_block, at), "a new generation is taken");
        ut_check(mp_enemy_sync_mirror(4u, &seen) &&
                     seen.value[MP_ENEMY_F_POS_X] == first.value[MP_ENEMY_F_POS_X],
                 "and its mirror is the new life's, not the old one's");
        ut_check(!mp_enemy_sync_mirror(5u, &seen),
                 "a placement nothing was said about has no mirror");
    }

    ut_section("a placement named twice is torn too");
    {
        mp_enemy_record_t record;
        mp_enemy_record_t seen;
        size_t            at;

        mp_enemy_sync_reset();
        mp_enemy_sync_set_level(true, HERE);
        begin_block(2u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        full_record(&record, 4u, 1u, 10u, 100);
        at += put_record(s_block + at, sizeof s_block - at, &record, NULL);
        at += put_record(s_block + at, sizeof s_block - at, &record, NULL);
        mark_present(4u);
        {
            uint32_t baseless = mp_enemy_sync_state()->baseless_refused;

            ut_check(!take_block(s_block, at) && !mp_enemy_sync_mirror(4u, &seen),
                     "refused whole, and nothing applied");
            ut_check(mp_enemy_sync_state()->baseless_refused == baseless,
                     "as torn, not as a record with nothing to be read against: both copies are "
                     "whole");
        }
    }
}

static void check_a_new_life(void)
{
    ut_section("a delta of a new life is not read against the old life's mirror");
    {
        mp_enemy_record_t first;
        mp_enemy_record_t second;
        mp_enemy_record_t seen;
        uint32_t          packed = 0;
        size_t            at;

        mp_enemy_sync_reset();
        mp_enemy_sync_set_level(true, HERE);
        full_record(&first, 4u, 1u, 10u, 100);
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        at += put_record(s_block + at, sizeof s_block - at, &first, NULL);
        mark_present(4u);
        ut_check(take_block(s_block, at), "the first life is taken whole");

        /* The second life moved on all three axes and kept its health, so a delta against the first
         * names a position and leaves the life's other fields to a base this side does not hold. */
        full_record(&second, 4u, 2u, 30u, 100);
        (void)mp_enemy_wire_put_position(6.0f, &packed);
        second.value[MP_ENEMY_F_POS_Y] = packed;
        (void)mp_enemy_wire_put_position(8.0f, &packed);
        second.value[MP_ENEMY_F_POS_Z] = packed;
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        at += put_record(s_block + at, sizeof s_block - at, &second, &first);
        mark_present(4u);
        {
            uint32_t baseless   = mp_enemy_sync_state()->baseless_refused;
            uint32_t positioned = mp_enemy_sync_state()->baseless_positioned;

            ut_check(!take_block(s_block, at),
                     "refused: a new generation has no base here, and the record is not whole");
            ut_check(mp_enemy_sync_state()->baseless_refused == baseless + 1u &&
                         mp_enemy_sync_state()->baseless_positioned == positioned + 1u,
                     "counted as a record with nothing to be read against, one that named a "
                     "position, which the rule before this one took with the life and the health "
                     "at zero");
        }
        ut_check(mp_enemy_sync_mirror(4u, &seen) &&
                     seen.value[MP_ENEMY_F_GENERATION] == 1u &&
                     seen.value[MP_ENEMY_F_POS_X] == first.value[MP_ENEMY_F_POS_X],
                 "and the mirror is still the first life's, untouched");
    }

    ut_section("a new life goes whole, and reads as sent even where a field drops to zero");
    {
        mp_enemy_record_t first;
        mp_enemy_record_t second;
        mp_enemy_record_t seen;
        size_t            at;

        mp_enemy_sync_reset();
        mp_enemy_sync_set_level(true, HERE);
        full_record(&first, 4u, 1u, 10u, 100);
        first.value[MP_ENEMY_F_HEAD] = 100u;
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        at += put_record(s_block + at, sizeof s_block - at, &first, NULL);
        mark_present(4u);
        ut_check(take_block(s_block, at), "the first life, with a playhead of 100");

        /* The second life's playhead is 0, which a whole record names and a delta leaves out. */
        full_record(&second, 4u, 2u, 10u, 100);
        begin_block(1u, HERE);
        at = MP_ENEMY_SYNC_HEADER_BYTES;
        at += put_record(s_block + at, sizeof s_block - at, &second, NULL);
        mark_present(4u);
        ut_check(take_block(s_block, at), "the second life is taken");
        ut_check(mp_enemy_sync_mirror(4u, &seen) && seen.value[MP_ENEMY_F_HEAD] == 0u,
                 "and its playhead is 0, not the dead life's 100: the whole record replaces the "
                 "old life's mirror field for field (that a delta of it is refused is pinned by "
                 "the section before)");
    }
}

static void check_the_peak_against_the_packet(void)
{
    ut_section("the measured peak against the packet");
    {
        /* The number the whole design rests on. The field runs measured 37 actors alive at once,
         * and a packet is 1200 bytes. If a steady state block for 37 does not fit alongside the
         * bodies, every enemy cannot travel every substep and the rate has to be cut.
         *
         * Steady state means a delta against what the far side already holds: an actor that walks
         * and swings changes its position, its playhead and little else. That is what is built
         * here, one record at a time, exactly as the sender lays them out. */
        mp_enemy_record_t moving;
        mp_enemy_record_t mirror;
        size_t            total = MP_ENEMY_SYNC_HEADER_BYTES + 2u;   /* header plus the length */
        size_t            one   = 0;
        size_t            i;
        uint32_t          packed = 0;

        memset(&mirror, 0, sizeof mirror);
        mirror.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(100);
        (void)mp_enemy_wire_put_position(50.0f, &packed);
        mirror.value[MP_ENEMY_F_POS_X] = packed;
        (void)mp_enemy_wire_put_position(60.0f, &packed);
        mirror.value[MP_ENEMY_F_POS_Z] = packed;
        mirror.value[MP_ENEMY_F_CLIP]  = 3u;
        mirror.value[MP_ENEMY_F_HEAD]  = 100u;

        moving = mirror;
        (void)mp_enemy_wire_put_position(50.25f, &packed);
        moving.value[MP_ENEMY_F_POS_X]  = packed;
        moving.value[MP_ENEMY_F_HEADING] = 4000u;
        moving.value[MP_ENEMY_F_HEAD]    = 116u;

        ut_check(mp_enemy_wire_encode(&moving, &mirror, s_block, sizeof s_block, &one),
                 "a walking actor encodes against what the far side holds");
        one += MP_ENEMY_SYNC_IDENTITY_BYTES;
        ut_checkf(one <= 32u, "and one steady record with its identity is %u bytes",
                  (unsigned)one);

        for (i = 0; i < 37u; ++i) {
            total += one;
        }
        ut_checkf(total <= 1100u,
                  "37 of them plus the block header come to %u bytes, which leaves room for the "
                  "two bodies inside a 1200 byte packet", (unsigned)total);
    }
}

static void check_liveness(void)
{
    ut_section("liveness is asked of the actor's own record, with its key");
    {
        const uintptr_t     actor = 0x00A01000u;
        mp_enemy_liveness_t seen;

        memset(&seen, 0, sizeof seen);
        seen.read      = true;
        seen.index     = 7u;
        seen.record    = 0x00B02000u;
        seen.live_word = (uint32_t)actor;
        ut_check(mp_enemy_liveness_holds(&seen, actor, 7u),
                 "a live actor on its own record and key is live");
        ut_check(!mp_enemy_liveness_holds(&seen, actor, 9u),
                 "a slot another placement took is not the placement asked about, although "
                 "its record names the slot: the directory's answer would have been yes");
        seen.live_word = 0u;
        ut_check(!mp_enemy_liveness_holds(&seen, actor, 7u),
                 "a record whose live word a delete cleared is gone");
        seen.live_word = 0x00A01204u;
        ut_check(!mp_enemy_liveness_holds(&seen, actor, 7u),
                 "a record that already names another actor is gone for this one");
        seen.live_word = (uint32_t)actor;
        seen.record    = 0u;
        ut_check(!mp_enemy_liveness_holds(&seen, actor, 7u), "an actor with no record is gone");
        seen.record = 0x00B02000u;
        seen.read   = false;
        ut_check(!mp_enemy_liveness_holds(&seen, actor, 7u),
                 "words that could not all be read answer gone, never live");
        ut_check(!mp_enemy_liveness_holds(NULL, actor, 7u), "no words is not a fault");
        ut_check(!mp_enemy_bind_is_live(actor, 7u, &seen) && !seen.read,
                 "with no engine bound nothing is read and nothing is live");
    }
}

static void check_the_slot(void)
{
    ut_section("the slot is asked whether it is still the actor, which is not whether it lives");
    {
        /* The defect: a droid the host killed stood on in its last clip on the client. Its
         * script removed it as a corpse that stays, which clears the record's live word and
         * leaves the body linked; the host went on describing the corpse, and the client asked
         * "alive" before every write and so never wrote it again. */
        const uintptr_t     actor = 0x00A01000u;
        mp_enemy_liveness_t seen;
        int                 slot;

        memset(&seen, 0, sizeof seen);
        seen.read      = true;
        seen.link      = 0x00A01204u;
        seen.index     = 7u;
        seen.record    = 0x00B02000u;
        seen.live_word = (uint32_t)actor;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_LIVE &&
                     mp_enemy_slot_is_actor(MP_ENEMY_SLOT_LIVE),
                 "a live actor on its own record and key is alive and is its placement's actor");
        seen.link = 0u;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_LIVE,
                 "the last node of the chain links to 0, and it is linked");

        seen.link      = 0x00A01204u;
        seen.live_word = 0u;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_KEPT,
                 "a cleared live word on a slot still linked is a corpse a removal kept");
        ut_check(mp_enemy_slot_is_actor(mp_enemy_slot_of(&seen, actor, 7u)),
                 "and a kept corpse is still the actor: the host lists it, so it is written on "
                 "and the host's later removal of it is performed");
        ut_check(!mp_enemy_liveness_holds(&seen, actor, 7u),
                 "while it is not alive, which is what deciding to build a body asks");

        seen.link = 0xFFFFFFFFu;
        ut_check(!mp_enemy_slot_is_actor(mp_enemy_slot_of(&seen, actor, 7u)),
                 "a slot the pool took back is not the actor, although its bytes still name the "
                 "placement: a removal there would free a body twice");
        seen.live_word = (uint32_t)actor;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_FREED,
                 "the free mark decides before the record does");

        seen.link  = 0x00A01204u;
        seen.index = 9u;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_OTHER &&
                     !mp_enemy_slot_is_actor(MP_ENEMY_SLOT_OTHER),
                 "a slot another placement took is another's, written to by nobody here");
        seen.index     = 7u;
        seen.live_word = 0x00A01408u;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_OTHER,
                 "a record that names another actor has moved on to another life");
        seen.live_word = (uint32_t)actor;
        seen.record    = 0u;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_OTHER,
                 "an actor with no record is nobody's");
        seen.read = false;
        ut_check(mp_enemy_slot_of(&seen, actor, 7u) == MP_ENEMY_SLOT_UNREAD &&
                     !mp_enemy_slot_is_actor(MP_ENEMY_SLOT_UNREAD),
                 "words that could not all be read are nothing, never the actor");
        ut_check(mp_enemy_slot_of(NULL, actor, 7u) == MP_ENEMY_SLOT_UNREAD, "no words either");
        for (slot = MP_ENEMY_SLOT_UNREAD; slot <= MP_ENEMY_SLOT_LIVE; ++slot) {
            ut_checkf(slot != MP_ENEMY_SLOT_LIVE || mp_enemy_slot_is_actor((mp_enemy_slot_t)slot),
                      "one rule, two questions: an actor that lives is its actor (slot %d)",
                      slot);
        }
        ut_check(mp_enemy_bind_slot(actor, 7u, &seen) == MP_ENEMY_SLOT_UNREAD && !seen.read,
                 "with no engine bound nothing is read and the slot is nothing");
    }
}

static void check_what_a_removal_leaves(void)
{
    ut_section("a kept corpse is its placement's actor only for the life it was kept for");
    {
        /* A new life of a placement is described by the host while the old corpse still lies
         * here waiting for the host's removal of it. Written onto the corpse, the new life
         * would stand a living actor up in a body with no class. */
        const uintptr_t corpse = 0x00A01000u;

        ut_check(mp_enemy_slot_for_life(MP_ENEMY_SLOT_KEPT, corpse, 3u, corpse, 3u) ==
                     MP_ENEMY_SLOT_KEPT,
                 "a corpse this side kept for life 3 is the actor of life 3");
        ut_check(mp_enemy_slot_for_life(MP_ENEMY_SLOT_KEPT, corpse, 4u, corpse, 3u) ==
                     MP_ENEMY_SLOT_OTHER,
                 "and not the actor of life 4, which waits for a body of its own");
        ut_check(mp_enemy_slot_for_life(MP_ENEMY_SLOT_KEPT, corpse, 3u, 0u, 3u) ==
                     MP_ENEMY_SLOT_OTHER,
                 "a corpse no removal here kept is nobody's replica, as before");
        ut_check(mp_enemy_slot_for_life(MP_ENEMY_SLOT_KEPT, corpse, 3u, 0x00A01204u, 3u) ==
                     MP_ENEMY_SLOT_OTHER,
                 "and neither is a corpse at another address than the one kept");
        ut_check(mp_enemy_slot_for_life(MP_ENEMY_SLOT_LIVE, corpse, 4u, corpse, 3u) ==
                     MP_ENEMY_SLOT_LIVE &&
                     mp_enemy_slot_for_life(MP_ENEMY_SLOT_FREED, corpse, 3u, corpse, 3u) ==
                         MP_ENEMY_SLOT_FREED,
                 "a live actor and a freed slot answer as the binding says");
    }

    ut_section("what a removal leaves behind is one rule, for a placement and a copy alike");
    {
        /* A copy's removal that keeps its body used to leave no mark, so the copy's corpse
         * was nobody's for every life and its death clip never reached it. */
        enemy_sync_state_t *s     = mp_enemy_sync_state();
        const uint32_t      copy  = MP_WIRE_KEY_COPY_BASE + 5u;
        const uint32_t      place = 200u;
        const uintptr_t     body  = 0x00A03000u;

        mp_enemy_sync_performed(copy, body, 7u, 0x0Eu);
        ut_check(s->placement[copy].kept_actor == body &&
                     s->placement[copy].kept_generation == 7u,
                 "a copy's corpse kept here is remembered for the life the removal named");
        mp_enemy_sync_performed(place, body, 3u, 0x0Eu);
        ut_check(s->placement[place].kept_actor == body &&
                     s->placement[place].kept_generation == 3u,
                 "and so is a placement's");
        mp_enemy_sync_performed(place, body, 3u, 1u);
        ut_check(s->placement[place].kept_actor == 0u && s->placement[place].actor == 0u,
                 "a removal that frees the body leaves nothing of it");
        mp_enemy_sync_performed(copy, body, 7u, 1u);
        ut_check(s->placement[copy].kept_actor == 0u, "for a copy too");
        mp_enemy_sync_performed(MP_ENEMY_SYNC_KEYS, body, 7u, 0x0Eu);
        ut_check(s->placement[MP_ENEMY_SYNC_KEYS - 1u].kept_actor != body,
                 "a key past the table is ignored, and the last row is not written instead");
    }
}

static void check_the_acknowledgements(void)
{
    size_t bytes;

    ut_section("a description nobody acknowledged is described again, whole");
    {
        /* The defect this pair exists to close, and it is not a corner: a client that missed one
         * payload kept every enemy of that payload at the bottom of the position range for the
         * rest of the level, because a stationary enemy's position never changes again and so was
         * never offered again. A field run had five of six sampled replicas standing
         * at -128, -128, -128, which is what a position field that never arrived decodes to. */
        enemy_sync_state_t *s = mp_enemy_sync_state();
        size_t              full  = 0;
        size_t              again = 0;
        size_t              delta = 0;

        mp_enemy_sync_reset();
        mp_enemy_sync_set_enabled(true);
        mp_enemy_sync_set_level(true, HERE);
        (void)mp_enemy_sync_begin_send();
        full_record(&s->placement[7].current, 7u, 1u, 40u, 100);
        s->placement[7].current_ok = true;
        s->placement[7].generation = 1u;

        ut_check(mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &full) && full != 0u,
                 "a placement the peer has never held is described whole");
        mp_enemy_sync_sent_for(0u, 100u);
        mp_enemy_sync_acked_for(0u, 100u, 0u);
        ut_check(mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &delta) && delta < full,
                 "once it has gone out it is a delta against what the peer was sent");

        /* The payload of 101 carries that delta and the far side never names 101: its next word is
         * 103. Everything 101 carried has to go again, whole. */
        mp_enemy_sync_sent_for(0u, 101u);
        mp_enemy_sync_acked_for(0u, 103u, 0u);
        ut_check(mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &again) && again == full,
                 "a payload the acknowledgements stepped over opens its keys again, so the "
                 "placement is described whole a second time");

        mp_enemy_sync_sent_for(0u, 104u);
        mp_enemy_sync_acked_for(0u, 104u, 0u);
        ut_check(mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &delta) && delta < full,
                 "and an unbroken chain of acknowledgements leaves it a delta again");
    }

    ut_section("an acknowledgement from further back than the view remembers gives it up whole");
    {
        enemy_sync_state_t *s = mp_enemy_sync_state();
        size_t              full  = 0;
        size_t              after = 0;

        mp_enemy_sync_reset();
        mp_enemy_sync_set_enabled(true);
        mp_enemy_sync_set_level(true, HERE);
        (void)mp_enemy_sync_begin_send();
        full_record(&s->placement[9].current, 9u, 1u, 40u, 100);
        s->placement[9].current_ok = true;
        s->placement[9].generation = 1u;

        (void)mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &full);
        mp_enemy_sync_sent_for(0u, 200u);
        mp_enemy_sync_acked_for(0u, 200u, 0u);
        mp_enemy_sync_sent_for(0u, 201u);
        mp_enemy_sync_acked_for(0u, 201u + MP_ENEMY_SYNC_ACK_RING + 1u, 0u);
        ut_check(mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &after) && after == full,
                 "which of them were missed cannot be told, so the whole view starts over");
    }

    ut_section("a block older than one already taken is refused");
    {
        /* The channel takes a reordered packet back, so this is reachable. Refused rather than
         * ignored: the caller refuses the payload with it, and the sender then hears no
         * acknowledgement for a block this side did not take. */
        mp_enemy_sync_reset();
        mp_enemy_sync_set_enabled(true);
        mp_enemy_sync_set_level(true, HERE);
        bytes = build(3u, THREE, 3u);
        ut_check(mp_enemy_sync_apply(s_block, bytes, 50u), "the block of substep 50 is taken");
        ut_check(!mp_enemy_sync_apply(s_block, bytes, 49u),
                 "one from 49 arriving behind it is refused");
        ut_check(!mp_enemy_sync_apply(s_block, bytes, 50u), "and so is the same substep twice");
        ut_check(mp_enemy_sync_apply(s_block, bytes, 51u), "while the stream itself goes on");
        ut_check(mp_enemy_sync_apply(s_block, bytes, 0x80000031u) &&
                     mp_enemy_sync_apply(s_block, bytes, 0x00000002u),
                 "and a counter that wraps past its maximum is still read as going forward");
    }

    ut_section("the report runs without a session");
    mp_enemy_sync_report();
    ut_check(true, "reporting on a sync that never carried anything is not a fault");
}

int main(void)
{
    check_off_and_the_shape();
    check_an_encode_with_no_engine();
    check_blocks_taken_and_refused();
    check_the_mirror();
    check_a_new_life();
    check_the_peak_against_the_packet();
    check_liveness();
    check_the_slot();
    check_what_a_removal_leaves();
    check_the_acknowledgements();

    return ut_summary("mp_enemy_sync");
}
