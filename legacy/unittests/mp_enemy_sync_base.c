/* mp_enemy_sync_base.c: no partial description without a confirmed base.
 *
 * SIZE NOTE: over 600 lines. Each section drives both ends of one rule through the same table, and
 * the helpers that lay out a row and a block are shared by all of them; split, they would be
 * copied.
 *
 * A record on the enemy wire leaves out every field its sender believes the receiver holds, and
 * the receiver reads what is left out from its own mirror, or as zero when it has none. Both ends
 * have to agree about that base. A field run showed where they did not: a client
 * still behind the host's movie refused the host's first seven payloads, the host had already
 * moved its belief over them, and the first payload the client took was a delta against nothing.
 * Its enemies stood at the bottom of the range, -128 on three axes, until they moved.
 *
 * The model is Quake 3's: a delta only against a snapshot the client has acknowledged, a full one
 * while there is none, and a client without the base drops a delta rather than applying it. Held
 * here:
 *
 *   a whole record names every field and reads the same against any baseline or none, which a
 *   record against nothing does not;
 *   a view no acknowledgement has confirmed gets whole records, and one that has gets deltas;
 *   the confirming acknowledgement opens again the keys no payload from it on carried, and a key
 *   a confirmed view opened again goes out whole, so a field that fell to zero arrives;
 *   a confirmed view whose acknowledgements stop for a whole ring is given up and starts whole;
 *   the sequence of that field run, end to end, reads real coordinates at the first write;
 *   and a receiver refuses a record that has nothing to be read against and is not whole; a test
 *   of "names no position" is not enough, since a delta of a body that moved on all three axes
 *   names its position and still has nothing to be read against.
 *
 * Host and client share one table in this process, as they never do in a session; each check asks
 * the receiving half only about what the sending half does not write.
 */
#include "unittest.h"

#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_fieldset.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The level both ends of these tests are in. */
#define HERE 57u

#define HEADER MP_ENEMY_SYNC_HEADER_BYTES

static uint8_t s_block[2048];

/* The substep that carried a block, for the checks that do not care which number it is. */
static uint32_t s_wire_tick;

/* A record as a host reads one off a standing body: every axis named, a heading, a state with a
 * playhead, a health and a clip, and nothing of the parts a ground actor has not got. */
static void standing_record(mp_enemy_record_t *record, uint8_t index, uint8_t generation, float x,
                            float y, float z)
{
    uint32_t packed = 0;

    memset(record, 0, sizeof *record);
    record->value[MP_ENEMY_F_INDEX]      = index;
    record->value[MP_ENEMY_F_GENERATION] = generation;
    (void)mp_enemy_wire_put_position(x, &packed);
    record->value[MP_ENEMY_F_POS_X] = packed;
    (void)mp_enemy_wire_put_position(y, &packed);
    record->value[MP_ENEMY_F_POS_Y] = packed;
    (void)mp_enemy_wire_put_position(z, &packed);
    record->value[MP_ENEMY_F_POS_Z]   = packed;
    record->value[MP_ENEMY_F_HEADING] = 12000u;
    record->value[MP_ENEMY_F_STATE]   = 3u | MP_ENEMY_HAS_HEAD;
    record->value[MP_ENEMY_F_HEALTH]  = mp_enemy_wire_put_health(60);
    record->value[MP_ENEMY_F_CLIP]    = 4u;
    record->value[MP_ENEMY_F_HEAD]    = 48u;
}

static bool same_record(const mp_enemy_record_t *a, const mp_enemy_record_t *b)
{
    size_t f;
    size_t i;

    for (f = 0; f < MP_ENEMY_FIELD_COUNT; ++f) {
        if (a->value[f] != b->value[f]) {
            return false;
        }
    }
    for (i = 0; i < a->value[MP_ENEMY_F_TWISTS] && i < MP_ENEMY_MAX_TWISTS; ++i) {
        if (a->twist[i].node != b->twist[i].node || a->twist[i].pitch != b->twist[i].pitch ||
            a->twist[i].yaw != b->twist[i].yaw) {
            return false;
        }
    }
    return true;
}

/* How long one record is on its own, whole. */
static size_t whole_bytes(const mp_enemy_record_t *record)
{
    uint8_t scratch[128];
    size_t  bytes = 0;

    return mp_enemy_wire_encode_whole(record, scratch, sizeof scratch, &bytes) ? bytes : 0u;
}

/* A row the census found alive and read, put where the encoder reads it. */
static void poke_row(size_t key, const mp_enemy_record_t *record)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    s->placement[key].current    = *record;
    s->placement[key].current_ok = true;
    s->placement[key].generation = (uint8_t)record->value[MP_ENEMY_F_GENERATION];
}

/* A host in a level with nobody described yet: the level, and a census over an empty pool. */
static void fresh_host(void)
{
    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_enemy_sync_begin_send();
}

static bool encode(size_t view, size_t *bytes)
{
    return mp_enemy_sync_encode_for(view, s_block, sizeof s_block, bytes);
}

static void begin_block(uint8_t count)
{
    memset(s_block, 0, sizeof s_block);
    s_block[0] = count;
    s_block[1] = (uint8_t)(HERE & 0xFFu);
    s_block[2] = (uint8_t)(HERE >> 8);
}

/* One placement's record as a sender lays it out at `at`, against `baseline`, which is nothing
 * for a first sighting. Returns the bytes it took, its identity included. */
static size_t put_record(size_t at, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *baseline)
{
    uint8_t index = (uint8_t)record->value[MP_ENEMY_F_INDEX];
    size_t  bytes = 0;

    s_block[at]      = index;
    s_block[at + 1u] = (uint8_t)record->value[MP_ENEMY_F_GENERATION];
    /* A first sighting goes whole, as the host sends a key its receiver does not hold. */
    if (baseline == NULL
            ? !mp_enemy_wire_encode_whole(record, s_block + at + 2u, sizeof s_block - at - 2u,
                                          &bytes)
            : !mp_enemy_wire_encode(record, baseline, s_block + at + 2u,
                                    sizeof s_block - at - 2u, &bytes)) {
        return 0;
    }
    s_block[1u + MP_ENEMY_SYNC_LEVEL_BYTES + (index >> 3)] |= (uint8_t)(1u << (index & 7u));
    return 2u + bytes;
}

static bool take(size_t bytes)
{
    return mp_enemy_sync_apply(s_block, bytes, ++s_wire_tick);
}

static bool stands_at(uint32_t key, const mp_enemy_record_t *record)
{
    mp_enemy_record_t seen;

    return mp_enemy_sync_mirror(key, &seen) &&
           seen.value[MP_ENEMY_F_POS_X] == record->value[MP_ENEMY_F_POS_X] &&
           seen.value[MP_ENEMY_F_POS_Y] == record->value[MP_ENEMY_F_POS_Y] &&
           seen.value[MP_ENEMY_F_POS_Z] == record->value[MP_ENEMY_F_POS_Z];
}

/* ==============================================================================================
 * The record.
 * ============================================================================================ */

static void check_a_whole_record(void)
{
    const mp_fieldset_t *set = mp_enemy_wire_set();
    mp_enemy_record_t    record;
    mp_enemy_record_t    stale;
    mp_enemy_record_t    got;
    uint8_t              buffer[128];
    size_t               wrote = 0;
    size_t               read  = 0;
    size_t               f;
    bool                 every = true;

    ut_section("a whole record names every field and reads the same against any baseline");

    standing_record(&record, 12u, 1u, 122.15f, 139.30f, 42.00f);
    record.value[MP_ENEMY_F_TWISTS] = 2u;
    record.twist[0].node  = 7u;
    record.twist[0].pitch = 1000u;
    record.twist[0].yaw   = 2000u;
    record.twist[1].node  = 9u;
    record.twist[1].pitch = 3000u;
    record.twist[1].yaw   = 4000u;
    standing_record(&stale, 12u, 1u, 10.0f, 20.0f, 30.0f);
    stale.value[MP_ENEMY_F_OVERLAY_CLIP] = 9u;
    stale.value[MP_ENEMY_F_NODES_HI]     = 1234u;

    ut_check(mp_enemy_wire_encode_whole(&record, buffer, sizeof buffer, &wrote), "it encodes");
    ut_checkf(wrote == mp_fieldset_max_bytes(set) + 2u * MP_ENEMY_TWIST_BYTES,
              "as long as the set allows, %u bytes with two rotations", (unsigned)wrote);
    for (f = 0; f < MP_ENEMY_FIELD_COUNT; ++f) {
        bool named = false;

        every = every && mp_fieldset_changed(set, buffer, wrote, f, &named) && named;
    }
    ut_check(every, "every field is in the mask, the ones that hold zero included");
    ut_check(mp_enemy_wire_decode(buffer, wrote, NULL, &got, &read) && read == wrote &&
                 same_record(&got, &record),
             "read against nothing it is the record that went");
    ut_check(mp_enemy_wire_decode(buffer, wrote, &stale, &got, &read) &&
                 same_record(&got, &record),
             "and read against an older description it is the same record");
    ut_check(!mp_enemy_wire_encode_whole(&record, buffer, wrote - 1u, &wrote),
             "a room one byte short is refused rather than half filled");
    record.value[MP_ENEMY_F_TWISTS] = MP_ENEMY_MAX_TWISTS + 1u;
    ut_check(!mp_enemy_wire_encode_whole(&record, buffer, sizeof buffer, &wrote),
             "and so is a count of rotations the tail cannot carry");

    ut_section("a record against nothing is not whole, and that is why a new view needs whole");
    standing_record(&record, 12u, 1u, 122.15f, 139.30f, 42.00f);
    ut_check(mp_enemy_wire_encode(&record, NULL, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_decode(buffer, wrote, &stale, &got, &read),
             "the same record against nothing encodes, and reads against the older description");
    ut_check(got.value[MP_ENEMY_F_OVERLAY_CLIP] == 9u && got.value[MP_ENEMY_F_NODES_HI] == 1234u,
             "and every field that holds zero was left out and reads as the older value: a "
             "sender that cannot tell whether its receiver holds nothing or something old has to "
             "spell every field out");
}

static void check_the_position_question(void)
{
    mp_enemy_record_t record;
    mp_enemy_record_t moved;
    uint8_t           buffer[128];
    size_t            wrote = 0;

    ut_section("whether a record names its position");

    standing_record(&record, 4u, 1u, 10.0f, 5.0f, 7.0f);
    ut_check(mp_enemy_wire_encode_whole(&record, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_names_position(buffer, wrote),
             "a whole record names it");
    ut_check(mp_enemy_wire_encode(&record, NULL, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_names_position(buffer, wrote),
             "so does one against nothing, because no axis of a shipped level is at the bottom "
             "of the range");
    ut_check(mp_enemy_wire_encode(&record, &record, buffer, sizeof buffer, &wrote) &&
                 !mp_enemy_wire_names_position(buffer, wrote),
             "a delta of a body that stood still names none of it");
    moved = record;
    (void)mp_enemy_wire_put_position(10.5f, &moved.value[MP_ENEMY_F_POS_X]);
    ut_check(mp_enemy_wire_encode(&moved, &record, buffer, sizeof buffer, &wrote) &&
                 !mp_enemy_wire_names_position(buffer, wrote),
             "and one that moved along one axis names only that one, which is not a position");
    ut_check(!mp_enemy_wire_names_position(buffer, 2u),
             "a buffer too short for the mask names nothing");
    ut_check(!mp_enemy_wire_names_position(NULL, 64u), "and no buffer names nothing either");
}

static void check_whether_a_record_is_whole(void)
{
    mp_enemy_record_t record;
    mp_enemy_record_t moved;
    uint8_t           buffer[128];
    size_t            wrote = 0;
    size_t            f;

    ut_section("whether a record is whole, the one form a receiver with nothing may take");

    standing_record(&record, 4u, 1u, 10.0f, 5.0f, 7.0f);
    ut_check(mp_enemy_wire_encode_whole(&record, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_is_whole(buffer, wrote),
             "a whole record is whole");
    ut_check(mp_enemy_wire_encode(&record, NULL, buffer, sizeof buffer, &wrote) &&
                 !mp_enemy_wire_is_whole(buffer, wrote),
             "one against nothing with a field at zero is not: that field is left out");
    for (f = 0; f < MP_ENEMY_FIELD_COUNT; ++f) {
        record.value[f] = 1u;
    }
    record.twist[0].node = 3u;
    ut_check(mp_enemy_wire_encode(&record, NULL, buffer, sizeof buffer, &wrote) &&
                 mp_enemy_wire_is_whole(buffer, wrote),
             "one against nothing with no field at zero is, because it reads the same against "
             "anything");
    moved = record;
    moved.value[MP_ENEMY_F_HEALTH] = 2u;
    ut_check(mp_enemy_wire_encode(&moved, &record, buffer, sizeof buffer, &wrote) &&
                 !mp_enemy_wire_is_whole(buffer, wrote),
             "a delta is not");
    ut_check(mp_enemy_wire_encode_whole(&record, buffer, sizeof buffer, &wrote) &&
                 !mp_enemy_wire_is_whole(buffer, 2u),
             "a whole record handed over shorter than its mask is not");
    ut_check(!mp_enemy_wire_is_whole(NULL, 64u), "and no buffer is not");
}

/* ==============================================================================================
 * The host.
 * ============================================================================================ */

static void check_a_view_nothing_confirmed(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   record;
    size_t              whole;
    size_t              bytes = 0;
    uint32_t            before;
    uint32_t            acked;
    bool                ok;

    ut_section("a view no acknowledgement has confirmed gets every record whole");

    fresh_host();
    standing_record(&record, 7u, 1u, 40.0f, 5.0f, 7.0f);
    poke_row(7u, &record);
    whole  = HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(&record);
    before = s->sent_whole;

    ok = encode(0u, &bytes);
    ut_checkf(ok && bytes == whole, "the first record is whole (%u bytes)", (unsigned)bytes);
    mp_enemy_sync_sent_for(0u, 100u);
    ut_check(s->view[0].known[7] && !s->view[0].confirmed,
             "it went out, so the view believes its peer was sent it, and nothing has said the "
             "peer has it");
    ok = encode(0u, &bytes);
    ut_checkf(ok && bytes == whole,
              "so the second is whole too (%u bytes), not a delta against a payload that may "
              "never have arrived", (unsigned)bytes);
    mp_enemy_sync_abandon_for(0u);
    ut_checkf(s->sent_whole == before + 2u, "and both are counted (%u)",
              (unsigned)(s->sent_whole - before));

    ut_section("an acknowledgement that names none of the view's payloads confirms nothing");
    mp_enemy_sync_acked_for(0u, 99u, 0u);
    ut_check(!s->view[0].confirmed, "a substep that carried no payload of this view does not");
    mp_enemy_sync_acked_for(0u, 0u, 0u);
    ut_check(!s->view[0].confirmed,
             "and nought, which is what a side that holds nothing acknowledges, does not either");
    ut_check(encode(0u, &bytes) && bytes == whole, "so the record is still whole");
    mp_enemy_sync_abandon_for(0u);

    ut_section("the first acknowledgement that names one confirms the view, and deltas follow");
    before = s->views_confirmed;
    acked  = s->acked;
    mp_enemy_sync_acked_for(0u, 100u, 0u);
    ut_check(s->view[0].confirmed && s->views_confirmed == before + 1u && s->acked == acked + 1u,
             "the acknowledgement of 100 confirms it and is counted as moving it on");
    ok = encode(0u, &bytes);
    ut_checkf(ok && bytes < whole,
              "from then on the record is a delta against what the peer holds (%u < %u)",
              (unsigned)bytes, (unsigned)whole);
    mp_enemy_sync_abandon_for(0u);

    ut_section("each view is its own, and a view forgotten starts whole again");
    ut_check(!s->view[1].confirmed && encode(1u, &bytes) && bytes == whole,
             "another peer, which has confirmed nothing, still gets the record whole");
    mp_enemy_sync_abandon_for(1u);
    mp_enemy_sync_forget_view(0u);
    ut_check(!s->view[0].confirmed && encode(0u, &bytes) && bytes == whole,
             "and a new connection on the first seat starts whole");
    mp_enemy_sync_abandon_for(0u);
}

static void check_the_first_confirmation_opens_keys(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   seven;
    mp_enemy_record_t   nine;
    size_t              room;
    size_t              bytes = 0;
    uint32_t            before;

    ut_section("the confirming acknowledgement opens again the keys no payload from it on carried");

    fresh_host();
    standing_record(&seven, 7u, 1u, 40.0f, 5.0f, 7.0f);
    standing_record(&nine, 9u, 1u, 60.0f, 5.0f, 7.0f);
    poke_row(7u, &seven);
    poke_row(9u, &nine);
    room = HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(&seven);

    ut_check(mp_enemy_sync_encode_for(0u, s_block, room, &bytes) && s_block[0] == 1u &&
                 s_block[HEADER] == 7u,
             "a room for one whole record carries placement 7");
    mp_enemy_sync_sent_for(0u, 100u);
    ut_check(mp_enemy_sync_encode_for(0u, s_block, room, &bytes) && s_block[0] == 1u &&
                 s_block[HEADER] == 9u,
             "and the next block carries 9, where the walk stopped");
    mp_enemy_sync_sent_for(0u, 101u);
    ut_check(s->view[0].known[7] && s->view[0].known[9], "both are believed sent");

    before = s->reopened;
    mp_enemy_sync_acked_for(0u, 101u, 0u);
    ut_check(s->view[0].confirmed && s->view[0].known[9],
             "101 confirms the view, and 9, which went out in it, stays known");
    ut_checkf(!s->view[0].known[7] && s->reopened == before + 1u,
              "7 went out only before it, nobody can say whether it arrived, and it is opened "
              "again (%u key(s))", (unsigned)(s->reopened - before));
}

/* Two payloads stepped over by one acknowledgement: the bits say the first was held there and the
 * second was not, so only the second's key opens. With bits of nought, as every call above makes
 * it, both would open. */
static void check_the_bits_keep_what_was_held(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   seven;
    mp_enemy_record_t   nine;
    mp_enemy_record_t   before;
    uint8_t             scratch[128];
    size_t              room;
    size_t              bytes = 0;
    uint32_t            reopened;
    uint32_t            kept;
    uint32_t            unstamped;

    ut_section("an acknowledgement's bits keep the keys of a payload that was held there");
    fresh_host();
    standing_record(&seven, 7u, 1u, 40.0f, 5.0f, 7.0f);
    standing_record(&nine, 9u, 1u, 60.0f, 5.0f, 7.0f);
    poke_row(7u, &seven);
    poke_row(9u, &nine);
    (void)mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &bytes);
    mp_enemy_sync_sent_for(0u, 100u);
    mp_enemy_sync_acked_for(0u, 100u, 0u);
    ut_check(s->view[0].confirmed && s->view[0].known[7] && s->view[0].known[9],
             "100 carried both and confirms the view");

    /* Both turn, and a block has room for one of the two deltas. */
    before = seven;
    seven.value[MP_ENEMY_F_HEADING] = 13000u;
    nine.value[MP_ENEMY_F_HEADING]  = 13000u;
    poke_row(7u, &seven);
    poke_row(9u, &nine);
    ut_check(mp_enemy_wire_encode(&seven, &before, scratch, sizeof scratch, &bytes),
             "(a turn is a delta of its own length)");
    room = HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + bytes;
    ut_check(mp_enemy_sync_encode_for(0u, s_block, room, &bytes) && s_block[HEADER] == 7u,
             "101 carries 7");
    mp_enemy_sync_sent_for(0u, 101u);
    ut_check(mp_enemy_sync_encode_for(0u, s_block, room, &bytes) && s_block[HEADER] == 9u,
             "102 carries 9");
    mp_enemy_sync_sent_for(0u, 102u);
    (void)mp_enemy_sync_encode_for(0u, s_block, room, &bytes);
    mp_enemy_sync_sent_for(0u, 103u);

    reopened  = s->reopened;
    kept      = s->bits_kept;
    unstamped = s->bits_unstamped;
    /* 103 named; bit 1 is 101, held; bit 0 is 102, clear. */
    mp_enemy_sync_acked_for(0u, 103u, 0x2u);
    ut_check(s->view[0].known[7] && s->bits_kept == kept + 1u,
             "101 was held there, so 7 stays known and the bits count it kept");
    ut_checkf(!s->view[0].known[9] && s->reopened == reopened + 1u,
              "102 was not, so 9 is opened again, one key (%u)",
              (unsigned)(s->reopened - reopened));

    mp_enemy_sync_acked_for(0u, 106u, 0x1u);
    ut_check(s->bits_unstamped == unstamped + 1u,
             "a bit for 105, which nothing of this view went out on, is counted and changes "
             "nothing");
}

static void check_a_view_that_hears_nothing(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   record;
    size_t              whole;
    size_t              bytes = 0;
    uint32_t            tick;
    uint32_t            given;
    uint32_t            unheard;

    ut_section("a confirmed view whose acknowledgements stop for a whole ring is given up");

    fresh_host();
    standing_record(&record, 7u, 1u, 40.0f, 5.0f, 7.0f);
    poke_row(7u, &record);
    whole = HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(&record);
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 100u);
    mp_enemy_sync_acked_for(0u, 100u, 0u);
    given   = s->view_given_up;
    unheard = s->given_up_unheard;

    for (tick = 101u; tick <= 100u + MP_ENEMY_SYNC_ACK_RING; ++tick) {
        (void)encode(0u, &bytes);
        mp_enemy_sync_sent_for(0u, tick);
    }
    ut_check(s->view[0].confirmed && s->given_up_unheard == unheard,
             "a ring's worth of payloads with no answer is still inside what the view remembers");
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 101u + MP_ENEMY_SYNC_ACK_RING);
    ut_check(!s->view[0].confirmed && s->view_given_up == given + 1u &&
                 s->given_up_unheard == unheard + 1u,
             "the next would overwrite a payload nobody answered, so the view is given up and "
             "counted as given up at a send");
    ut_check(encode(0u, &bytes) && bytes == whole,
             "and it describes whole again until an acknowledgement confirms it");
    mp_enemy_sync_abandon_for(0u);

    ut_section("acknowledgements that keep up, however late, never give a view up");
    fresh_host();
    poke_row(7u, &record);
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 100u);
    mp_enemy_sync_acked_for(0u, 100u, 0u);
    unheard = s->given_up_unheard;
    given   = s->view_given_up;
    for (tick = 101u; tick < 400u; ++tick) {
        (void)encode(0u, &bytes);
        mp_enemy_sync_sent_for(0u, tick);
        mp_enemy_sync_acked_for(0u, tick - 20u, 0u);
    }
    ut_check(s->view[0].confirmed && s->given_up_unheard == unheard && s->view_given_up == given,
             "an acknowledgement twenty substeps behind every payload is late, not missing");
}

/* A line whose acknowledgements are steady and gapless but more than a ring behind: the send side
 * gives the view up on the LAG, where the acknowledgement side only ever sees a gap of one. After
 * that the view is never confirmed again, because every acknowledgement names a payload whose
 * stamp the ring has already overwritten, and every record goes out whole for good. Correct, and
 * the field reads it as `view(s) confirmed` staying at nought. */
static void check_a_line_slower_than_the_ring(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   record;
    size_t              whole;
    size_t              bytes = 0;
    uint32_t            tick;
    uint32_t            given;
    uint32_t            unheard;
    uint32_t            confirmed;

    ut_section("acknowledgements without a gap, more than a ring behind, give the view up");

    fresh_host();
    standing_record(&record, 7u, 1u, 40.0f, 5.0f, 7.0f);
    poke_row(7u, &record);
    whole = HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(&record);
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 100u);
    mp_enemy_sync_acked_for(0u, 100u, 0u);
    ut_check(s->view[0].confirmed, "the view is confirmed while the first payload is answered");
    given     = s->view_given_up;
    unheard   = s->given_up_unheard;
    confirmed = s->views_confirmed;

    /* Forty substeps behind every payload, and never a gap: each acknowledgement names the
     * payload after the one before it. */
    for (tick = 101u; tick < 400u; ++tick) {
        (void)encode(0u, &bytes);
        mp_enemy_sync_sent_for(0u, tick);
        mp_enemy_sync_acked_for(0u, tick - 40u, 0u);
    }
    ut_check(!s->view[0].confirmed && s->view_given_up == given + 1u &&
                 s->given_up_unheard == unheard + 1u,
             "the view is given up once, at a send, although no acknowledgement ever skipped one");
    ut_check(s->views_confirmed == confirmed,
             "and nothing confirms it again: every acknowledgement names a payload the ring has "
             "forgotten, which is what a line slower than the ring looks like");
    ut_check(encode(0u, &bytes) && bytes == whole, "so every record goes out whole from then on");
    mp_enemy_sync_abandon_for(0u);
}

/* The defect the whole form closes for a key opened again. A payload that carried the overlay
 * clip falling to zero is lost; the next one is a delta that leaves the clip out, because the host
 * believes it sent; the acknowledgement that steps over the lost one opens the key. Described
 * against zero the clip would be left out a third time and the receiver would keep its 9. */
static void check_an_opened_key_goes_whole(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   record;
    mp_enemy_record_t   seen;
    size_t              bytes = 0;
    uint32_t            before;

    ut_section("a key a confirmed view opened again is described whole, zeros included");

    fresh_host();
    standing_record(&record, 7u, 1u, 40.0f, 5.0f, 7.0f);
    record.value[MP_ENEMY_F_STATE]        |= MP_ENEMY_HAS_OVERLAY;
    record.value[MP_ENEMY_F_OVERLAY_CLIP]  = 9u;
    record.value[MP_ENEMY_F_OVERLAY_HEAD]  = 30u;
    poke_row(7u, &record);
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 100u);
    ut_check(mp_enemy_sync_apply(s_block, bytes, 100u) && mp_enemy_sync_mirror(7u, &seen) &&
                 seen.value[MP_ENEMY_F_OVERLAY_CLIP] == 9u,
             "the receiver takes the first record, overlay clip 9");
    mp_enemy_sync_acked_for(0u, 100u, 0u);

    record.value[MP_ENEMY_F_STATE]        &= ~(uint32_t)MP_ENEMY_HAS_OVERLAY;
    record.value[MP_ENEMY_F_OVERLAY_CLIP]  = 0u;
    record.value[MP_ENEMY_F_OVERLAY_HEAD]  = 0u;
    poke_row(7u, &record);
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 101u);   /* the overlay ends in this payload, and it is lost */
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 102u);
    ut_check(mp_enemy_sync_apply(s_block, bytes, 102u) && mp_enemy_sync_mirror(7u, &seen) &&
                 seen.value[MP_ENEMY_F_OVERLAY_CLIP] == 9u,
             "the next payload leaves the clip out, and the receiver still reads 9: the window "
             "between a loss and its discovery");
    mp_enemy_sync_acked_for(0u, 102u, 0u);
    ut_check(!s->view[0].known[7], "the acknowledgement of 102 steps over 101 and opens the key");

    before = s->opened_whole;
    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 103u);
    ut_check(s->opened_whole == before + 1u, "the opened key goes out whole, and is counted");
    ut_check(mp_enemy_sync_apply(s_block, bytes, 103u) && mp_enemy_sync_mirror(7u, &seen) &&
                 seen.value[MP_ENEMY_F_OVERLAY_CLIP] == 0u &&
                 seen.value[MP_ENEMY_F_OVERLAY_HEAD] == 0u &&
                 (seen.value[MP_ENEMY_F_STATE] & MP_ENEMY_HAS_OVERLAY) == 0u,
             "and the receiver reads the clip and its playhead at zero and the presence bit clear, "
             "where a record against zero would have left the clip and its playhead at 9 and 30");
}

/* ==============================================================================================
 * Both ends, over the field run's sequence.
 * ============================================================================================ */

static void check_the_field_sequence(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   record;
    size_t              bytes   = 0;
    size_t              at;
    uint32_t            refused = 0u;
    uint32_t            tick;
    uint32_t            before;

    ut_section("seven payloads refused behind a movie, and the eighth read at real coordinates");

    fresh_host();
    standing_record(&record, 12u, 1u, 122.15f, 139.30f, 42.00f);
    poke_row(12u, &record);
    for (tick = 100u; tick < 107u; ++tick) {
        (void)encode(0u, &bytes);
        mp_enemy_sync_sent_for(0u, tick);
        mp_enemy_sync_set_level(false, 0u);   /* the client: its level opens after the movie */
        refused += mp_enemy_sync_apply(s_block, bytes, tick) ? 0u : 1u;
        mp_enemy_sync_set_level(true, HERE);
    }
    ut_checkf(refused == 7u, "all seven are refused with no level open, none acknowledged (%u)",
              (unsigned)refused);
    ut_check(!s->view[0].confirmed, "so nothing has confirmed the host's view of that client");

    (void)encode(0u, &bytes);
    mp_enemy_sync_sent_for(0u, 107u);
    before = s->baseless_refused;
    ut_check(mp_enemy_sync_apply(s_block, bytes, 107u),
             "the first payload the client takes is taken");
    ut_check(stands_at(12u, &record),
             "and the replica's first write stands where the host has it, 122.15 139.30 42.00, "
             "not at -128");
    ut_check(s->baseless_refused == before, "with nothing refused for want of a base");
    mp_enemy_sync_acked_for(0u, 107u, 0u);
    ut_check(s->view[0].confirmed, "its acknowledgement confirms the view");

    ut_section("a delta against a record the client never took is refused, not read as -128");
    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    begin_block(1u);
    at = HEADER + put_record(HEADER, &record, &record);
    before = s->baseless_refused;
    ut_check(!mp_enemy_sync_apply(s_block, at, 200u),
             "a delta against what the host believed sent, the mask alone, is refused by a "
             "client that never took that payload");
    ut_check(s->baseless_refused == before + 1u && !mp_enemy_sync_mirror(12u, &record),
             "counted, and nothing of it is kept");
}

/* ==============================================================================================
 * The receiver.
 * ============================================================================================ */

static void check_the_receiver(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_record_t   first;
    mp_enemy_record_t   moved;
    mp_enemy_record_t   next;
    mp_enemy_record_t   copy;
    size_t              at;
    size_t              bytes = 0;
    uint32_t            torn;
    uint32_t            baseless;
    uint32_t            positioned;
    uint32_t            copies;

    ut_section("a record with nothing to be read against is refused unless it is whole");

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    standing_record(&first, 4u, 1u, 10.0f, 5.0f, 7.0f);
    moved = first;
    moved.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(40);

    begin_block(1u);
    at       = HEADER + put_record(HEADER, &moved, &first);
    torn     = s->refused;
    baseless = s->baseless_refused;
    ut_check(!take(at), "a delta against a first sighting this side never had is refused");
    ut_check(s->baseless_refused == baseless + 1u && s->refused == torn,
             "counted as a record with nothing to be read against, not as a torn block");
    ut_check(!mp_enemy_sync_mirror(4u, &next), "and nothing of it is kept");

    /* A delta of a body that walked on all three axes names its position; a rule that asked only
     * for the position would take it with every other field at zero, the life among them. */
    standing_record(&next, 4u, 1u, 11.0f, 6.0f, 8.0f);
    next.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(40);
    begin_block(1u);
    at       = HEADER + put_record(HEADER, &next, &first);
    baseless   = s->baseless_refused;
    positioned = s->baseless_positioned;
    ut_check(!take(at),
             "a delta that names its position is refused all the same when it is not whole");
    ut_check(s->baseless_refused == baseless + 1u && s->baseless_positioned == positioned + 1u,
             "counted as a record with nothing to be read against, and as one naming a position");
    ut_check(!mp_enemy_sync_mirror(4u, &copy), "and nothing of it is kept, the life included");

    begin_block(1u);
    at = HEADER + put_record(HEADER, &first, NULL);
    ut_check(take(at) && stands_at(4u, &first), "the first sighting goes whole and is taken");
    begin_block(1u);
    at = HEADER + put_record(HEADER, &moved, &first);
    ut_check(take(at) && stands_at(4u, &first),
             "and the same delta is taken once there is a base to read it against");

    ut_section("a new life has nothing to be read against, whatever this side still holds");
    next = moved;
    next.value[MP_ENEMY_F_GENERATION] = 2u;
    begin_block(1u);
    at       = HEADER + put_record(HEADER, &next, &moved);
    baseless = s->baseless_refused;
    ut_check(!take(at) && s->baseless_refused == baseless + 1u,
             "a record of life 2 that leaves its position out is refused, although life 1 had one");

    ut_section("a copy's record is held to the same rule");
    standing_record(&copy, 2u, 1u, 30.0f, 5.0f, 7.0f);
    begin_block(0u);
    at            = HEADER;
    s_block[at++] = 1u;      /* a bitmap of one byte */
    s_block[at++] = 0x04u;   /* copy 2 */
    s_block[at++] = 1u;      /* one record */
    s_block[at++] = 2u;      /* k, low byte first */
    s_block[at++] = 0u;
    s_block[at++] = 1u;      /* its generation */
    ut_check(mp_enemy_wire_encode(&copy, &copy, s_block + at, sizeof s_block - at, &bytes),
             "a copy's delta that names nothing encodes");
    at      += bytes;
    baseless = s->baseless_refused;
    copies   = s->copy_refused;
    ut_check(!take(at) && s->baseless_refused == baseless + 1u && s->copy_refused == copies,
             "and it is refused for the same reason, not as a torn copies' part");
}

int main(void)
{
    check_a_whole_record();
    check_the_position_question();
    check_whether_a_record_is_whole();
    check_a_view_nothing_confirmed();
    check_the_first_confirmation_opens_keys();
    check_the_bits_keep_what_was_held();
    check_a_view_that_hears_nothing();
    check_a_line_slower_than_the_ring();
    check_an_opened_key_goes_whole();
    check_the_field_sequence();
    check_the_receiver();

    ut_section("the report runs");
    mp_enemy_sync_report();
    ut_check(true, "reporting the base counters is not a fault");

    return ut_summary("mp_enemy_sync_base");
}
