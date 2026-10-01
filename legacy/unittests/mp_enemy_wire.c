/* mp_enemy_wire.c: the enemy record, and the budget it has to fit in.
 *
 * The properties here are the ones a mistake would hide rather than announce. A field that is
 * encoded and never decoded produces an actor in the wrong pose, not a crash; a bias that clamps
 * the wrong way produces an actor that is alive when it should be dead; and a tail whose count is
 * trusted produces a read past the end of a packet a stranger sent.
 *
 * The budget check is a test rather than a comment because the whole design rests on it: an
 * actor that walks and swings costs 24 bytes a substep and the field measured 37 actors alive at
 * once, and if those two numbers ever stop fitting into one packet the design has changed and
 * nobody would otherwise notice.
 *
 * SIZE NOTE: a little over 600 lines, one check function per property of the record, and the last
 * one added is the cost of the body and the masks, which belongs beside the budget it spends.
 */
#include "unittest.h"

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The packet budget a snapshot has to live inside, from the channel's own arithmetic: 1200 bytes
 * per packet less the 13 byte header. */
#define PAYLOAD_BYTES 1187u

/* A field run read `engine peak 37` out of the engine's own high water mark. */
#define MEASURED_PEAK 37u

static void fill_plain(mp_enemy_record_t *r)
{
    memset(r, 0, sizeof *r);
    r->value[MP_ENEMY_F_INDEX]      = 42u;
    r->value[MP_ENEMY_F_GENERATION] = 3u;
    r->value[MP_ENEMY_F_POS_X]      = 12345u;
    r->value[MP_ENEMY_F_POS_Y]      = 23456u;
    r->value[MP_ENEMY_F_POS_Z]      = 3456u;
    r->value[MP_ENEMY_F_HEADING]    = 40000u;
    r->value[MP_ENEMY_F_STATE]      = 7u;
    r->value[MP_ENEMY_F_HEALTH]     = mp_enemy_wire_put_health(100);
    r->value[MP_ENEMY_F_CLIP]       = 14u;
    r->value[MP_ENEMY_F_HEAD]       = 320u;
}

static void check_a_record_survives_the_round_trip(void)
{
    mp_enemy_record_t sent;
    mp_enemy_record_t got;
    uint8_t           buffer[256];
    size_t            wrote = 0;
    size_t            read = 0;
    size_t            i;

    ut_section("a full record comes back exactly as it went");

    fill_plain(&sent);
    ut_check(mp_enemy_wire_encode(&sent, NULL, buffer, sizeof buffer, &wrote), "it encodes");
    ut_check(mp_enemy_wire_decode(buffer, wrote, NULL, &got, &read), "and it decodes");
    ut_checkf(read == wrote, "over exactly the bytes it wrote (%u against %u)",
              (unsigned)read, (unsigned)wrote);
    for (i = 0; i < MP_ENEMY_FIELD_COUNT; ++i) {
        ut_checkf(got.value[i] == sent.value[i], "field %u came back (%u against %u)",
                  (unsigned)i, (unsigned)got.value[i], (unsigned)sent.value[i]);
    }
}

static void check_the_delta_carries_only_what_moved(void)
{
    mp_enemy_record_t baseline;
    mp_enemy_record_t moved;
    mp_enemy_record_t got;
    uint8_t           full[256];
    uint8_t           still[256];
    uint8_t           step[256];
    size_t            full_bytes = 0;
    size_t            still_bytes = 0;
    size_t            step_bytes = 0;
    size_t            read = 0;

    ut_section("a delta costs the mask for what stood still and the value for what moved");

    fill_plain(&baseline);
    ut_check(mp_enemy_wire_encode(&baseline, NULL, full, sizeof full, &full_bytes),
             "the full record encodes");

    ut_check(mp_enemy_wire_encode(&baseline, &baseline, still, sizeof still, &still_bytes),
             "a record identical to its baseline encodes");
    ut_checkf(still_bytes < full_bytes,
              "and costs less than the full one (%u against %u)",
              (unsigned)still_bytes, (unsigned)full_bytes);
    /* The mask PLUS ONE, and the one is the twist count.
     *
     * That field is never left out even when it has not changed, because it says how long the
     * record is. A receiver whose baseline disagreed about it would read the wrong number of tail
     * bytes, land in the middle of the next record and lose the whole block; a field run showed 98
     * blocks going that way in one session before this was forced.
     *
     * So an unchanged actor costs one byte more than the theoretical floor, and that is the price
     * of the block being parsable at all. */
    ut_checkf(still_bytes == mp_fieldset_mask_bytes(mp_enemy_wire_set()) + 1u,
              "in fact the mask plus the twist count, %u byte(s)", (unsigned)still_bytes);
    {
        /* And it really is in the mask every time, which is what the length depends on. Two
         * records whose twist counts agree must still both carry it. */
        mp_enemy_record_t twisted = baseline;
        uint8_t           a[64];
        size_t            an = 0;

        twisted.value[MP_ENEMY_F_TWISTS] = 1u;
        twisted.twist[0].node  = 7u;
        twisted.twist[0].pitch = 100u;
        twisted.twist[0].yaw   = 200u;
        ut_check(mp_enemy_wire_encode(&twisted, &twisted, a, sizeof a, &an),
                 "a record with one rotation, against a baseline that has the same one, encodes");
        ut_checkf(an == mp_fieldset_mask_bytes(mp_enemy_wire_set()) + 1u + MP_ENEMY_TWIST_BYTES,
                  "and carries the count and the whole tail anyway, %u byte(s)", (unsigned)an);

        /* The decoder must reach the same length from a baseline that knows nothing. That is the
         * case the refusals came from: the sender had a mirror and the receiver did not. */
        {
            mp_enemy_record_t blind;
            size_t            blind_read = 0;

            ut_check(mp_enemy_wire_decode(a, an, NULL, &blind, &blind_read),
                     "and a receiver with NO baseline decodes it");
            ut_checkf(blind_read == an, "reading exactly what was written, %u byte(s)",
                      (unsigned)blind_read);
            ut_check(blind.value[MP_ENEMY_F_TWISTS] == 1u && blind.twist[0].node == 7u,
                     "and gets the rotation back");
        }
    }

    /* And the same actor holding two node rotations, which is what a fighter does while it stands.
     * The mask alone is NOT the floor for one of those, because the tail is written whole every
     * time, and this pins the real number. */
    {
        mp_enemy_record_t held = baseline;
        uint8_t           buffer[256];
        size_t            held_bytes = 0;

        held.value[MP_ENEMY_F_TWISTS] = 2u;
        held.twist[0].node = 3u;
        held.twist[1].node = 4u;
        ut_check(mp_enemy_wire_encode(&held, &held, buffer, sizeof buffer, &held_bytes),
                 "a still actor that holds two rotations encodes against itself");
        ut_checkf(held_bytes == still_bytes + 2u * MP_ENEMY_TWIST_BYTES,
                  "and costs the mask plus the whole tail, %u byte(s), not the mask alone",
                  (unsigned)held_bytes);
    }

    moved = baseline;
    moved.value[MP_ENEMY_F_POS_X] = 12400u;
    ut_check(mp_enemy_wire_encode(&moved, &baseline, step, sizeof step, &step_bytes),
             "one axis moved and it encodes");
    ut_checkf(step_bytes == still_bytes + 2u,
              "costing the mask plus one u16 (%u)", (unsigned)step_bytes);

    ut_check(mp_enemy_wire_decode(step, step_bytes, &baseline, &got, &read),
             "and the delta decodes against the baseline");
    ut_check(got.value[MP_ENEMY_F_POS_X] == 12400u, "the axis that moved is the new one");
    ut_check(got.value[MP_ENEMY_F_POS_Y] == baseline.value[MP_ENEMY_F_POS_Y],
             "and the ones that did not came from the baseline");
    ut_check(got.value[MP_ENEMY_F_HEALTH] == baseline.value[MP_ENEMY_F_HEALTH],
             "health included, which is the field a lost baseline would falsify");
}

static void check_the_measured_peak_fits_the_budget(void)
{
    mp_enemy_record_t fighting;
    uint8_t           buffer[256];
    size_t            bytes = 0;
    size_t            i;

    ut_section("thirty seven fighting actors fit into one packet");

    /* A fighting ground actor as the census describes it: the overlay channel live and two node
     * rotations held, which is the waist to the target and the neck to look at it. */
    fill_plain(&fighting);
    fighting.value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_OVERLAY;
    fighting.value[MP_ENEMY_F_OVERLAY_CLIP] = 9u;
    fighting.value[MP_ENEMY_F_OVERLAY_HEAD] = 48u;
    fighting.value[MP_ENEMY_F_TWISTS] = 2u;
    for (i = 0; i < 2u; ++i) {
        fighting.twist[i].node  = (uint8_t)(3u + i);
        fighting.twist[i].pitch = 1000u;
        fighting.twist[i].yaw   = 2000u;
    }
    ut_check(mp_enemy_wire_encode(&fighting, NULL, buffer, sizeof buffer, &bytes),
             "the fighting record encodes as a first record, which is against zero");
    /* A first record is what an actor costs the once, when it is introduced. Thirty seven of them
     * at once do NOT fit, and that is a property of the design rather than a fault in it: an actor
     * is introduced when it spawns, and a level does not spawn its whole population into one
     * substep. What the budget has to carry every substep is the steady state below. */
    ut_checkf(bytes < mp_enemy_wire_max_bytes(),
              "and it costs %u rather than the %u a field-by-field record would",
              (unsigned)bytes, (unsigned)mp_enemy_wire_max_bytes());
    {
        /* The numbers the header's comment quotes: 34 against zero, 60 whole. */
        size_t whole = 0;

        ut_checkf(bytes == 34u, "against zero the fighter is 34 bytes (%u)", (unsigned)bytes);
        ut_checkf(mp_enemy_wire_encode_whole(&fighting, buffer, sizeof buffer, &whole) &&
                      whole == 60u,
                  "whole, with a flyer pose, a shield, a body and four mask words it does not "
                  "have, 60 (%u)", (unsigned)whole);
    }

    /* And the steady state, which is what every substep after the first one actually carries: the
     * actor walks and swings, so its position, its heading and both playheads move and everything
     * else stands. This is the number the design rests on. */
    {
        mp_enemy_record_t moved = fighting;
        size_t            delta = 0;

        moved.value[MP_ENEMY_F_POS_X] += 40u;
        moved.value[MP_ENEMY_F_POS_Y] += 12u;
        moved.value[MP_ENEMY_F_HEADING] += 300u;
        moved.value[MP_ENEMY_F_HEAD] += 16u;
        moved.value[MP_ENEMY_F_OVERLAY_HEAD] += 16u;
        ut_check(mp_enemy_wire_encode(&moved, &fighting, buffer, sizeof buffer, &delta),
                 "a substep of movement encodes as a delta");
        ut_checkf(delta == 24u, "of 24 bytes, the number the header quotes (%u)",
                  (unsigned)delta);
        ut_checkf(delta * MEASURED_PEAK <= PAYLOAD_BYTES,
                  "%u moving actors at %u bytes each is %u against a budget of %u",
                  (unsigned)MEASURED_PEAK, (unsigned)delta, (unsigned)(delta * MEASURED_PEAK),
                  (unsigned)PAYLOAD_BYTES);
    }

    /* What does NOT fit, asserted rather than remarked, so that the requirement it implies cannot
     * be forgotten the day somebody adds a field: introducing a whole level's population is a
     * SWEEP ACROSS PACKETS and never one packet. Both the realistic first record and the
     * theoretical maximum are over the budget at the measured peak. */
    ut_checkf(bytes * MEASURED_PEAK > PAYLOAD_BYTES,
              "%u first records at %u bytes each is %u, past the budget of %u: the introduction "
              "of a level's actors is a sweep",
              (unsigned)MEASURED_PEAK, (unsigned)bytes, (unsigned)(bytes * MEASURED_PEAK),
              (unsigned)PAYLOAD_BYTES);
    ut_checkf(mp_enemy_wire_max_bytes() * MEASURED_PEAK > PAYLOAD_BYTES,
              "and so is the theoretical maximum, %u * %u against %u",
              (unsigned)mp_enemy_wire_max_bytes(), (unsigned)MEASURED_PEAK,
              (unsigned)PAYLOAD_BYTES);
}

/* What the body, the masks and the wider shield cost, asserted rather than remarked, because every
 * field added to the table spends the margin the steady state leaves under a packet. */
static void check_the_record_costs(void)
{
    mp_enemy_record_t walking;
    mp_enemy_record_t moved;
    uint8_t           buffer[256];
    size_t            still = 0;
    size_t            bytes = 0;

    ut_section("the largest record, and what a change of the body or the masks adds to a substep");
    ut_check(MP_ENEMY_FIELD_COUNT == 21u,
             "a record has 21 fields");
    ut_checkf(mp_enemy_wire_max_bytes() == 70u,
              "the largest record is 70 bytes: 3 of mask, 47 of fields, 20 of rotations (%u)",
              (unsigned)mp_enemy_wire_max_bytes());

    fill_plain(&walking);
    moved = walking;
    moved.value[MP_ENEMY_F_POS_X] += 40u;
    ut_check(mp_enemy_wire_encode(&moved, &walking, buffer, sizeof buffer, &still),
             "a substep of walking encodes");
    moved.value[MP_ENEMY_F_BODY] = 0x0000FF06u;
    ut_check(mp_enemy_wire_encode(&moved, &walking, buffer, sizeof buffer, &bytes) &&
                 bytes == still + 4u,
             "a body that changes costs its four bytes in that substep");
    moved.value[MP_ENEMY_F_BODY]      = 0u;
    moved.value[MP_ENEMY_F_NODES_LO]  = 1u << 7;
    moved.value[MP_ENEMY_F_MESHES_HI] = 1u << 1;
    ut_check(mp_enemy_wire_encode(&moved, &walking, buffer, sizeof buffer, &bytes) &&
                 bytes == still + 8u,
             "two mask words that change four each");
    moved.value[MP_ENEMY_F_NODES_LO]  = 0u;
    moved.value[MP_ENEMY_F_MESHES_HI] = 0u;
    moved.value[MP_ENEMY_F_SHIELD]    = 0x0123u;
    ut_check(mp_enemy_wire_encode(&moved, &walking, buffer, sizeof buffer, &bytes) &&
                 bytes == still + 2u,
             "and a shield two, sixteen bits of it");
    {
        uint8_t whole[256];
        size_t  n = 0;

        fill_plain(&moved);
        ut_check(mp_enemy_wire_encode_whole(&moved, whole, sizeof whole, &n) && n == 50u,
                 "a whole record with no rotation is 50 bytes");
    }
}

/* The last rows travel both ways: against a baseline that held other values, and against
 * nothing, and the sixteen bits of the shield come back whole. */
static void check_the_last_fields_travel(void)
{
    mp_enemy_record_t before;
    mp_enemy_record_t after;
    mp_enemy_record_t got;
    uint8_t           buffer[256];
    size_t            wrote = 0;
    size_t            read  = 0;

    ut_section("the body, the masks and the shield come back from a delta and whole");
    fill_plain(&before);
    before.value[MP_ENEMY_F_MESHES_HI] = 0x00001000u;
    after = before;
    after.value[MP_ENEMY_F_MESHES_HI] = 0x80000001u;
    after.value[MP_ENEMY_F_NODES_HI]  = 0x0000FFFFu;
    after.value[MP_ENEMY_F_BODY]      = 0x03FF8006u;
    after.value[MP_ENEMY_F_SHIELD]    = 0xFF01u;
    ut_check(mp_enemy_wire_encode(&after, &before, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_decode(buffer, wrote, &before, &got, &read) && read == wrote &&
                 memcmp(got.value, after.value, sizeof got.value) == 0,
             "against the older values every field arrives as the new one");
    ut_check(mp_enemy_wire_encode_whole(&after, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_decode(buffer, wrote, NULL, &got, &read) && read == wrote &&
                 memcmp(got.value, after.value, sizeof got.value) == 0,
             "and a whole record read against nothing carries them too");
    ut_check(MP_ENEMY_F_MESHES_HI == MP_ENEMY_FIELD_COUNT - 1u,
             "the high mesh word is the last row, so no field before it moved its index");

    /* The room one peer's enemy block really gets, from the send that builds it: the payload is
     * 1168 bytes, two of them the block's length, 24 the snapshot's header, and 66 for each of the
     * three far bodies a four player session puts beside it. The block head with the world
     * events' own, the copies' head, and two whole records of the largest kind have to fit in
     * what is left, or a level could not send its first copy beside a placement. */
    {
        const size_t room  = 1168u - 2u - 24u - 66u * 3u;
        const size_t fixed = 40u + 18u + (3u + mp_enemy_wire_max_bytes()) +
                             (2u + mp_enemy_wire_max_bytes());

        ut_checkf(room == 944u && fixed <= room,
                  "the heads and two of the largest records, %u bytes, fit the %u a four "
                  "player session leaves", (unsigned)fixed, (unsigned)room);
    }
}

/* Random records, whole and against random baselines, come back exactly: the codec asymmetries a
 * hand written case never thinks of show up here. */
static void check_the_round_trip_fuzz(void)
{
    uint32_t seed  = 0x9E3779B9u;
    unsigned trips = 0;
    unsigned i;

    ut_section("random records survive the round trip, whole and as a delta");
    for (i = 0; i < 3000u; ++i) {
        mp_enemy_record_t record;
        mp_enemy_record_t base;
        mp_enemy_record_t got;
        uint8_t           buffer[256];
        size_t            wrote = 0;
        size_t            read  = 0;
        size_t            f;

        memset(&record, 0, sizeof record);
        memset(&base, 0, sizeof base);
        for (f = 0; f < MP_ENEMY_FIELD_COUNT; ++f) {
            uint32_t width = (uint32_t)mp_enemy_wire_set()->field[f].width;
            uint32_t mask  = width == MP_FIELD_U8 ? 0xFFu : width == MP_FIELD_U16 ? 0xFFFFu
                                                                                    : 0xFFFFFFFFu;

            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            record.value[f] = (seed & 3u) == 0u ? 0u : seed & mask;
            base.value[f]   = (seed & 12u) == 0u ? record.value[f] : (seed >> 3) & mask;
        }
        record.value[MP_ENEMY_F_TWISTS] %= MP_ENEMY_MAX_TWISTS + 1u;
        base.value[MP_ENEMY_F_TWISTS] %= MP_ENEMY_MAX_TWISTS + 1u;
        if (mp_enemy_wire_encode(&record, &base, buffer, sizeof buffer, &wrote) &&
            mp_enemy_wire_decode(buffer, wrote, &base, &got, &read) && read == wrote &&
            memcmp(got.value, record.value, sizeof got.value) == 0 &&
            mp_enemy_wire_encode_whole(&record, buffer, sizeof buffer, &wrote) &&
            wrote <= mp_enemy_wire_max_bytes() &&
            mp_enemy_wire_decode(buffer, wrote, NULL, &got, &read) && read == wrote &&
            memcmp(got.value, record.value, sizeof got.value) == 0) {
            ++trips;
        }
    }
    ut_checkf(trips == 3000u, "all of them, the delta and the whole alike (%u of 3000)", trips);
}

static void check_health_goes_negative(void)
{
    mp_enemy_record_t r;

    ut_section("health is signed on the engine's side and survives being so");

    memset(&r, 0, sizeof r);
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(-37);
    ut_checkf(mp_enemy_wire_health(&r) == -37, "minus thirty seven comes back (%d)",
              (int)mp_enemy_wire_health(&r));

    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(999);
    ut_check(mp_enemy_wire_health(&r) == 999,
             "and so do the 999 hit points the placement census found, which a byte would have "
             "turned into 231");

    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(0);
    ut_check(mp_enemy_wire_health(&r) == 0,
             "zero, which is a LIVING actor in this engine and not a dead one");

    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(-99999);
    ut_checkf(mp_enemy_wire_health(&r) == MP_ENEMY_HEALTH_MIN,
              "a value past the floor clamps rather than wrapping (%d)",
              (int)mp_enemy_wire_health(&r));
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(99999);
    ut_check(mp_enemy_wire_health(&r) == MP_ENEMY_HEALTH_MAX, "and so does one past the ceiling");
}

static void check_the_state_word(void)
{
    mp_enemy_record_t r;
    uint32_t          value;

    ut_section("the state word holds seventeen states and does not collide with its flags");

    ut_check(MP_ENEMY_STATE_VALUES <= MP_ENEMY_STATE_MASK + 1u,
             "seventeen states fit the state field, which four bits would not have done");

    memset(&r, 0, sizeof r);
    r.value[MP_ENEMY_F_STATE] = (MP_ENEMY_STATE_VALUES - 1u) | MP_ENEMY_FLAG_BLOCKING |
                                MP_ENEMY_HAS_OVERLAY;
    value = r.value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK;
    ut_checkf(value == MP_ENEMY_STATE_VALUES - 1u,
              "the highest state reads back beside its flags (%u)", (unsigned)value);
    ut_check(mp_enemy_wire_has(&r, MP_ENEMY_HAS_OVERLAY), "the overlay is present");
    ut_check(!mp_enemy_wire_has(&r, MP_ENEMY_HAS_FLYER_POSE), "the flyer pose is not");

    /* Every named bit has to be its own. A collision would be invisible: an actor would block
     * whenever it jumped. */
    {
        uint16_t bits[] = { MP_ENEMY_FLAG_JUMP_UP, MP_ENEMY_FLAG_JUMP_FWD, MP_ENEMY_FLAG_BLOCKING,
                            MP_ENEMY_FLAG_IMPULSE, MP_ENEMY_FLAG_FORCE_THROWN, MP_ENEMY_HAS_OVERLAY,
                            MP_ENEMY_HAS_FLYER_POSE, MP_ENEMY_HAS_HEAD,
                            MP_ENEMY_HAS_NODES, MP_ENEMY_HAS_MESHES };
        size_t   i;
        size_t   j;

        for (i = 0; i < sizeof bits / sizeof bits[0]; ++i) {
            ut_checkf((bits[i] & MP_ENEMY_STATE_MASK) == 0u,
                      "bit %u stands clear of the state field", (unsigned)i);
            for (j = 0; j < i; ++j) {
                ut_checkf((bits[i] & bits[j]) == 0u, "bit %u does not share with bit %u",
                          (unsigned)i, (unsigned)j);
            }
        }
    }
}

/* The two presence bits of the node masks ride in the state word, which is a u16 on the wire, so
 * they have to come back both from a whole record and from a delta that carries nothing but
 * them. */
static void check_the_mask_bits_travel(void)
{
    mp_enemy_record_t sent;
    mp_enemy_record_t base;
    mp_enemy_record_t got;
    uint8_t           buffer[256];
    size_t            wrote = 0;
    size_t            read  = 0;
    const uint32_t    all   = MP_ENEMY_HAS_HEAD | MP_ENEMY_HAS_NODES | MP_ENEMY_HAS_MESHES;

    ut_section("the masks' two presence bits travel beside the playhead's, whole and as a change");

    fill_plain(&sent);
    sent.value[MP_ENEMY_F_STATE] |= all;
    ut_check(mp_enemy_wire_encode(&sent, NULL, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_decode(buffer, wrote, NULL, &got, &read) && read == wrote,
             "a record with the playhead's bit and both of the masks' encodes and decodes");
    ut_checkf(got.value[MP_ENEMY_F_STATE] == sent.value[MP_ENEMY_F_STATE],
              "the state word comes back whole (%04X)", (unsigned)got.value[MP_ENEMY_F_STATE]);
    ut_check(mp_enemy_wire_has(&got, MP_ENEMY_HAS_HEAD) &&
                 mp_enemy_wire_has(&got, MP_ENEMY_HAS_NODES) &&
                 mp_enemy_wire_has(&got, MP_ENEMY_HAS_MESHES),
             "and all three read back as set");

    base = got;
    sent.value[MP_ENEMY_F_STATE] &= ~(uint32_t)MP_ENEMY_HAS_MESHES;
    ut_check(mp_enemy_wire_encode(&sent, &base, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_decode(buffer, wrote, &base, &got, &read) && read == wrote,
             "a mesh array no longer read goes as a delta against the one before");
    ut_check(mp_enemy_wire_has(&got, MP_ENEMY_HAS_NODES) &&
                 !mp_enemy_wire_has(&got, MP_ENEMY_HAS_MESHES) &&
                 mp_enemy_wire_has(&got, MP_ENEMY_HAS_HEAD),
             "and arrives without it, the node bit and the playhead's bit untouched");

    base = got;
    sent.value[MP_ENEMY_F_STATE] &= ~(uint32_t)(MP_ENEMY_HAS_NODES | MP_ENEMY_HAS_MESHES);
    ut_check(mp_enemy_wire_encode(&sent, &base, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_decode(buffer, wrote, &base, &got, &read) &&
                 !mp_enemy_wire_has(&got, MP_ENEMY_HAS_NODES),
             "a sender that stops speaking for the nodes clears the presence bit as a change too");
}

static void check_the_death_rule(void)
{
    mp_enemy_record_t r;
    uint32_t          state;

    ut_section("a death is a death state, the same states in which the engine lays its body down");

    fill_plain(&r);
    r.value[MP_ENEMY_F_STATE] = 1u | MP_ENEMY_HAS_HEAD;
    ut_check(!mp_enemy_wire_reports_death(&r), "an active actor with its health is alive");
    for (state = 0u; state < MP_ENEMY_STATE_VALUES; ++state) {
        bool dead = (state >= 11u && state <= 14u);

        r.value[MP_ENEMY_F_STATE] = state | MP_ENEMY_HAS_NODES;
        ut_checkf(mp_enemy_wire_reports_death(&r) == dead,
                  "state %u with health 100 %s", (unsigned)state,
                  dead ? "is a death: the death clip, the fade, the shatter or the corpse"
                       : "is not one");
    }

    /* The defect the first version of this rule had: health at or below zero counted as a death,
     * and the replica was laid down while the host's body kept its class and its shadow. */
    r.value[MP_ENEMY_F_STATE]  = 1u;
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(0);
    ut_check(!mp_enemy_wire_reports_death(&r),
             "an active actor with no hit points is alive: three shipped placements are authored "
             "that way (a bus, a tank, a child) and live for the whole level");
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(-37);
    ut_check(!mp_enemy_wire_reports_death(&r),
             "and health below zero while still active is a script playing its death clip, whose "
             "body keeps its class and shadow on the host until the script ends it");
    r.value[MP_ENEMY_F_STATE] = 14u;
    ut_check(mp_enemy_wire_reports_death(&r), "a corpse is a corpse whatever its health says");
    ut_check(!mp_enemy_wire_reports_death(NULL), "and no record reports nothing");

    ut_section("where a death begins, for the report that follows deaths");

    fill_plain(&r);
    r.value[MP_ENEMY_F_STATE] = 1u;
    ut_check(mp_enemy_wire_health_known(&r) && !mp_enemy_wire_death_begins(&r, true),
             "an actor with 100 hit points begins no death");
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(0);
    ut_check(!mp_enemy_wire_death_begins(&r, false),
             "a life never seen above zero has nothing to lose: a placement authored with no hit "
             "points takes no place among the deaths followed");
    ut_check(mp_enemy_wire_death_begins(&r, true),
             "a life seen above zero that falls to zero begins its death, before the script "
             "ends it");
    r.value[MP_ENEMY_F_HEALTH] = 0u;
    ut_check(!mp_enemy_wire_health_known(&r) && !mp_enemy_wire_death_begins(&r, true),
             "a health field that never arrived reads -1024 and is no health: a record decoded "
             "against nothing holds zero for every field it did not carry");
    r.value[MP_ENEMY_F_STATE] = 11u;
    ut_check(mp_enemy_wire_death_begins(&r, false),
             "a death state begins a death whatever came before");
    ut_check(!mp_enemy_wire_health_known(NULL) && !mp_enemy_wire_death_begins(NULL, true),
             "and no record begins nothing");
}

static void check_the_tail(void)
{
    mp_enemy_record_t sent;
    mp_enemy_record_t got;
    uint8_t           buffer[256];
    size_t            bytes = 0;
    size_t            read = 0;
    size_t            i;

    ut_section("the node rotations ride behind the record and are counted, not guessed");

    fill_plain(&sent);
    sent.value[MP_ENEMY_F_TWISTS] = MP_ENEMY_MAX_TWISTS;
    for (i = 0; i < MP_ENEMY_MAX_TWISTS; ++i) {
        sent.twist[i].node  = (uint8_t)(10u + i);
        sent.twist[i].pitch = (uint16_t)(1000u + i);
        sent.twist[i].yaw   = (uint16_t)(60000u + i);
    }
    ut_check(mp_enemy_wire_encode(&sent, NULL, buffer, sizeof buffer, &bytes),
             "a full tail encodes");
    ut_check(mp_enemy_wire_decode(buffer, bytes, NULL, &got, &read), "and decodes");
    for (i = 0; i < MP_ENEMY_MAX_TWISTS; ++i) {
        ut_checkf(got.twist[i].node == sent.twist[i].node &&
                  got.twist[i].pitch == sent.twist[i].pitch &&
                  got.twist[i].yaw == sent.twist[i].yaw,
                  "rotation %u came back whole", (unsigned)i);
    }

    ut_section("a count the tail cannot carry is refused on both sides");

    sent.value[MP_ENEMY_F_TWISTS] = MP_ENEMY_MAX_TWISTS + 1u;
    ut_check(!mp_enemy_wire_encode(&sent, NULL, buffer, sizeof buffer, &bytes),
             "the encoder refuses rather than clamping");

    /* And the reader refuses the same record arriving from a stranger. The count is the one field
     * a corrupt packet can use to walk off the end, so it is checked before it is used.
     *
     * The count is the LAST row of the table, so in a record that carries it the byte before the
     * tail is the byte that names it. It is only carried when it differs from the baseline, which
     * is why this record has a full tail rather than none: a zero count is absent from the record
     * entirely and there would be nothing to forge. */
    sent.value[MP_ENEMY_F_TWISTS] = MP_ENEMY_MAX_TWISTS;
    ut_check(mp_enemy_wire_encode(&sent, NULL, buffer, sizeof buffer, &bytes), "a clean record");
    {
        size_t record_end = bytes - MP_ENEMY_MAX_TWISTS * MP_ENEMY_TWIST_BYTES;

        ut_check(buffer[record_end - 1u] == (uint8_t)MP_ENEMY_MAX_TWISTS,
                 "the byte before the tail is the count, as the table order says");
        buffer[record_end - 1u] = (uint8_t)(MP_ENEMY_MAX_TWISTS + 3u);
        ut_check(!mp_enemy_wire_decode(buffer, bytes, NULL, &got, &read),
                 "and a forged count is refused rather than read past the end");
    }
}

static void check_every_truncation_is_refused(void)
{
    mp_enemy_record_t sent;
    mp_enemy_record_t got;
    uint8_t           buffer[256];
    size_t            bytes = 0;
    size_t            read = 0;
    size_t            cut;
    bool              any_accepted = false;

    ut_section("no prefix of a record is mistaken for a record");

    fill_plain(&sent);
    sent.value[MP_ENEMY_F_TWISTS] = 2u;
    sent.twist[0].node = 3u;
    sent.twist[1].node = 4u;
    ut_check(mp_enemy_wire_encode(&sent, NULL, buffer, sizeof buffer, &bytes), "it encodes");

    for (cut = 0; cut < bytes; ++cut) {
        if (mp_enemy_wire_decode(buffer, cut, NULL, &got, &read)) {
            any_accepted = true;
        }
    }
    ut_checkf(!any_accepted, "all %u short reads were refused", (unsigned)bytes);
    ut_check(mp_enemy_wire_decode(buffer, bytes, NULL, &got, &read), "and the whole one is not");
}

static void check_the_table_is_sound(void)
{
    const mp_fieldset_t *set = mp_enemy_wire_set();

    ut_section("the table itself");

    ut_check(mp_fieldset_valid(set), "the field set is usable");
    ut_check(set->count == MP_ENEMY_FIELD_COUNT,
             "and its length is the enum's, so no row was added without a name");
}

int main(void)
{
    check_a_record_survives_the_round_trip();
    check_the_delta_carries_only_what_moved();
    check_the_measured_peak_fits_the_budget();
    check_the_record_costs();
    check_the_last_fields_travel();
    check_the_round_trip_fuzz();
    check_health_goes_negative();
    check_the_state_word();
    check_the_mask_bits_travel();
    check_the_death_rule();
    check_the_tail();
    check_every_truncation_is_refused();
    check_the_table_is_sound();

    ut_section("a position, against the census the range was taken from");
    {
        /* The measured extent of the eleven shipped levels: x 0.5 to 240.0, y -2.1 to 176.7, z
         * 11.0 to 97.0, with 1.0 as one more low z. Each has to survive the round trip, and the
         * negative one is the whole reason the bias exists. */
        static const float REAL[] = { 0.5f, 240.0f, -2.1f, 176.7f, 1.0f, 97.0f, 0.0f, 120.25f };
        size_t   i;
        uint32_t packed = 0;

        for (i = 0; i < sizeof REAL / sizeof REAL[0]; ++i) {
            float back;

            ut_checkf(mp_enemy_wire_put_position(REAL[i], &packed),
                      "%d.%02d is inside the measured range and encodes",
                      (int)REAL[i], (int)((REAL[i] < 0.0f ? -REAL[i] : REAL[i]) * 100.0f) % 100);
            ut_checkf(packed <= 65535u, "and it fits the u16 the field set writes (%u)",
                      (unsigned)packed);
            back = mp_enemy_wire_get_position(packed);
            ut_checkf((back - REAL[i]) < 0.01f && (REAL[i] - back) < 0.01f,
                      "and it comes back within a hundredth of a unit");
        }

        /* Refused, never clamped. A coordinate outside the range means a level these numbers were
         * not measured on, and the nearest representable point is a place the body is not. */
        ut_check(!mp_enemy_wire_put_position(-200.0f, &packed),
                 "a coordinate below the range is refused rather than clamped to the floor");
        ut_check(!mp_enemy_wire_put_position(1000.0f, &packed),
                 "and one above it is refused rather than clamped to the ceiling");
        ut_check(!mp_enemy_wire_put_position(0.0f, NULL), "a null destination is refused");

        /* The far edge of the range is representable, which is what says the arithmetic did not
         * lose the last step to rounding. */
        ut_check(mp_enemy_wire_put_position(MP_ENEMY_POS_MIN, &packed) && packed == 0u,
                 "the bottom of the range is exactly zero on the wire");
        ut_check(mp_enemy_wire_put_position(MP_ENEMY_POS_MAX, &packed) && packed == 65535u,
                 "and the top is exactly the largest u16");
    }

    return ut_summary("mp_enemy_wire");
}
