/* The table driven wire record, driven over its edges with no game and no wire.
 *
 * What has to hold: a record round trips exactly; a body that did not move costs the mask alone; a
 * field that changed costs the mask plus that field and nothing else; a value too wide for its row
 * is refused before a single byte is written, because a truncated pose is worse than no pose; a
 * truncated buffer is refused rather than read past; and the mask can be inspected without
 * decoding, which is how a report says what a class actually sends.
 *
 * The set used below is deliberately awkward: eight fields, so the mask is exactly one byte and
 * the ninth field in the second set proves the mask grows; mixed widths, so an offset error inside
 * the walk cannot cancel itself out.
 */
#include "unittest.h"

#include "mp_fieldset.h"

#include <string.h>

enum { F_ID, F_STATE, F_HEALTH, F_YAW, F_PITCH, F_CLIP, F_HEAD, F_FLAGS, F_COUNT };

static const mp_field_t SET_FIELDS[F_COUNT] = {
    { "id",     MP_FIELD_U8  },
    { "state",  MP_FIELD_U8  },
    { "health", MP_FIELD_U16 },   /* the reason this exists: an enemy has up to 999 hit points */
    { "yaw",    MP_FIELD_U16 },
    { "pitch",  MP_FIELD_U16 },
    { "clip",   MP_FIELD_U16 },
    { "head",   MP_FIELD_U32 },
    { "flags",  MP_FIELD_U8  }
};

static const mp_fieldset_t SET = { "test", SET_FIELDS, F_COUNT };

static void fill(uint32_t *values, uint32_t seed)
{
    values[F_ID]     = 7u + (seed & 0x0Fu);
    values[F_STATE]  = 3u;
    values[F_HEALTH] = 999u;
    values[F_YAW]    = 40000u + seed;
    values[F_PITCH]  = 100u;
    values[F_CLIP]   = 512u;
    values[F_HEAD]   = 0x00ABCDEFu + seed;
    values[F_FLAGS]  = 0x81u;
}

static void check_shape(void)
{
    ut_section("the shape of a record");

    ut_check(mp_fieldset_valid(&SET), "the table is usable");
    ut_checkf(mp_fieldset_mask_bytes(&SET) == 1u, "eight fields need exactly one mask byte (%u)",
              (unsigned)mp_fieldset_mask_bytes(&SET));
    ut_checkf(mp_fieldset_max_bytes(&SET) == 1u + 1u + 1u + 2u + 2u + 2u + 2u + 4u + 1u,
              "the widest record is the mask plus every field (%u)",
              (unsigned)mp_fieldset_max_bytes(&SET));

    {
        static const mp_field_t NINE[9] = {
            { "a", MP_FIELD_U8 }, { "b", MP_FIELD_U8 }, { "c", MP_FIELD_U8 },
            { "d", MP_FIELD_U8 }, { "e", MP_FIELD_U8 }, { "f", MP_FIELD_U8 },
            { "g", MP_FIELD_U8 }, { "h", MP_FIELD_U8 }, { "i", MP_FIELD_U8 }
        };
        static const mp_fieldset_t WIDE = { "nine", NINE, 9u };

        ut_check(mp_fieldset_mask_bytes(&WIDE) == 2u, "and nine fields need two");
    }
}

static void check_round_trip(void)
{
    uint32_t sent[F_COUNT];
    uint32_t got[F_COUNT];
    uint8_t  buffer[64];
    size_t   written = 0;
    size_t   read = 0;

    ut_section("a full record comes back exactly as it went in");

    fill(sent, 0u);
    ut_check(mp_fieldset_encode(&SET, sent, NULL, buffer, sizeof buffer, &written) ==
             MP_FIELDSET_OK, "a record against no baseline encodes");
    ut_checkf(written == mp_fieldset_max_bytes(&SET),
              "and it is the full length, because every field differs from nothing (%u)",
              (unsigned)written);

    memset(got, 0xEE, sizeof got);
    ut_check(mp_fieldset_decode(&SET, buffer, written, NULL, got, &read), "it decodes");
    ut_checkf(read == written, "over exactly the bytes it wrote (%u)", (unsigned)read);
    ut_check(memcmp(sent, got, sizeof sent) == 0, "and every field is what it was");
    ut_check(got[F_HEALTH] == 999u,
             "health past 255 survives, which a byte wide field would have made 231");
}

static void check_delta(void)
{
    uint32_t base[F_COUNT];
    uint32_t now[F_COUNT];
    uint32_t got[F_COUNT];
    uint8_t  buffer[64];
    size_t   written = 0;
    size_t   read = 0;
    bool     changed = true;

    ut_section("what a body that did not move costs");

    fill(base, 0u);
    memcpy(now, base, sizeof now);
    ut_check(mp_fieldset_encode(&SET, now, base, buffer, sizeof buffer, &written) ==
             MP_FIELDSET_OK, "an unchanged record encodes");
    ut_checkf(written == mp_fieldset_mask_bytes(&SET), "as the mask alone, one byte (%u)",
              (unsigned)written);
    ut_check(buffer[0] == 0u, "with every bit clear");

    ut_check(mp_fieldset_decode(&SET, buffer, written, base, got, &read),
             "and it decodes against the baseline");
    ut_check(memcmp(base, got, sizeof base) == 0, "back to the baseline, field for field");

    ut_section("what one moved field costs");

    now[F_YAW] = base[F_YAW] + 1000u;
    ut_check(mp_fieldset_encode(&SET, now, base, buffer, sizeof buffer, &written) ==
             MP_FIELDSET_OK, "one field changed encodes");
    ut_checkf(written == mp_fieldset_mask_bytes(&SET) + 2u,
              "as the mask plus that field's two bytes and nothing else (%u)", (unsigned)written);
    ut_check(mp_fieldset_changed(&SET, buffer, written, F_YAW, &changed) && changed,
             "the mask names the field that moved");
    ut_check(mp_fieldset_changed(&SET, buffer, written, F_HEALTH, &changed) && !changed,
             "and does not name one that did not");
    ut_check(mp_fieldset_decode(&SET, buffer, written, base, got, &read) &&
             got[F_YAW] == now[F_YAW] && got[F_HEALTH] == base[F_HEALTH],
             "the moved field comes from the wire and the still one from the baseline");
}

static void check_refusals(void)
{
    uint32_t values[F_COUNT];
    uint32_t got[F_COUNT];
    uint8_t  buffer[64];
    uint8_t  guard[64];
    size_t   written = 0;
    size_t   read = 0;

    ut_section("what is refused, and that a refusal writes nothing");

    fill(values, 0u);
    values[F_STATE] = 256u;                     /* one past a byte */
    memset(buffer, 0xA5, sizeof buffer);
    memcpy(guard, buffer, sizeof guard);
    ut_check(mp_fieldset_encode(&SET, values, NULL, buffer, sizeof buffer, &written) ==
             MP_FIELDSET_VALUE_TOO_WIDE, "a value too wide for its row is refused");
    ut_check(memcmp(buffer, guard, sizeof guard) == 0,
             "and the buffer is untouched, so a refusal cannot ship half a record");

    fill(values, 0u);
    ut_check(mp_fieldset_encode(&SET, values, NULL, buffer, 4u, &written) == MP_FIELDSET_NO_ROOM,
             "a buffer too small for the record is refused");
    ut_check(mp_fieldset_encode(&SET, values, NULL, buffer, 0u, &written) == MP_FIELDSET_NO_ROOM,
             "and so is one too small for the mask");

    ut_check(mp_fieldset_encode(NULL, values, NULL, buffer, sizeof buffer, &written) ==
             MP_FIELDSET_BAD_SET, "no table, no record");

    ut_section("a truncated record is refused rather than read past");

    ut_check(mp_fieldset_encode(&SET, values, NULL, buffer, sizeof buffer, &written) ==
             MP_FIELDSET_OK, "a full record to cut down");
    ut_check(!mp_fieldset_decode(&SET, buffer, written - 1u, NULL, got, &read),
             "one byte short and the decode refuses");
    ut_check(!mp_fieldset_decode(&SET, buffer, 0u, NULL, got, &read),
             "and nothing at all is not a record either");
}

static void check_bad_tables(void)
{
    static const mp_field_t NO_NAME[1] = { { NULL, MP_FIELD_U8 } };
    static const mp_field_t BAD_WIDTH[1] = { { "x", (mp_field_width_t)3 } };
    static const mp_fieldset_t NAMELESS = { "nameless", NO_NAME, 1u };
    static const mp_fieldset_t WIDTH = { "width", BAD_WIDTH, 1u };
    static const mp_fieldset_t EMPTY = { "empty", NO_NAME, 0u };
    static const mp_fieldset_t TOO_MANY = { "many", NO_NAME, MP_FIELDSET_MAX_FIELDS + 1u };

    ut_section("a table that cannot be walked is rejected once, not every substep");

    ut_check(!mp_fieldset_valid(&NAMELESS), "a row with no name cannot be reported");
    ut_check(!mp_fieldset_valid(&WIDTH), "a width that is not 1, 2 or 4 is not a width");
    ut_check(!mp_fieldset_valid(&EMPTY), "a set with no fields is not a set");
    ut_check(!mp_fieldset_valid(&TOO_MANY), "and one past what the mask can name is not either");
    ut_check(mp_fieldset_mask_bytes(&EMPTY) == 0u && mp_fieldset_max_bytes(&EMPTY) == 0u,
             "an invalid set has no size rather than a misleading one");
}

int main(void)
{
    check_shape();
    check_round_trip();
    check_delta();
    check_refusals();
    check_bad_tables();

    return ut_summary("mp_fieldset");
}
