/* mp_enemy_wire.c: the enemy record, and the tail the field set deliberately does not carry.
 *
 * Everything here is arithmetic on values a caller has already quantised. The one judgement this
 * file makes is where a value is clamped, and it makes it in one place per value so that no call
 * site has to remember a bias.
 */
#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One row per field, in the order the enum names them. The widths are the census: a placement
 * index fits a byte because the largest shipped level holds 255 placements. The engine bounds
 * nothing here: the 0x100 its delete asserts against is the reveal ids a placement lists, and
 * the 0xFF pushed when the object pool is built is that pool's capacity. A clip ordinal fits a byte
 * because the largest model in the shipped set carries 114 clips, and every angle is a u16 over
 * the whole turn, which is 0.0055 degrees a step. The shield is sixteen bits so its radius has room
 * beside what the byte carried, and each mask is a word, as the engine keeps it: the largest rig of
 * the shipped set has 48 nodes, so the high words are needed. */
static const mp_field_t FIELDS[MP_ENEMY_FIELD_COUNT] = {
    { "index",        MP_FIELD_U8  },
    { "generation",   MP_FIELD_U8  },
    { "pos_x",        MP_FIELD_U16 },
    { "pos_y",        MP_FIELD_U16 },
    { "pos_z",        MP_FIELD_U16 },
    { "heading",      MP_FIELD_U16 },
    { "state",        MP_FIELD_U16 },
    { "health",       MP_FIELD_U16 },
    { "clip",         MP_FIELD_U8  },
    { "head",         MP_FIELD_U16 },
    { "overlay_clip", MP_FIELD_U8  },
    { "overlay_head", MP_FIELD_U16 },
    { "pitch",        MP_FIELD_U16 },
    { "roll",         MP_FIELD_U16 },
    { "twists",       MP_FIELD_U8  },
    { "shield",       MP_FIELD_U16 },
    { "body",         MP_FIELD_U32 },
    { "nodes_lo",     MP_FIELD_U32 },
    { "nodes_hi",     MP_FIELD_U32 },
    { "meshes_lo",    MP_FIELD_U32 },
    { "meshes_hi",    MP_FIELD_U32 },
};

/* A 25th field is not wrong, it is expensive: the mask would grow to a fourth byte, and every
 * record of every enemy in every substep would carry it. */
_Static_assert(MP_ENEMY_FIELD_COUNT <= 24u,
               "a 25th enemy field costs every record a fourth mask byte; decide that on purpose");

static const mp_fieldset_t SET = { "enemy", FIELDS, MP_ENEMY_FIELD_COUNT };

const mp_fieldset_t *mp_enemy_wire_set(void)
{
    return &SET;
}

size_t mp_enemy_wire_max_bytes(void)
{
    return mp_fieldset_max_bytes(&SET) + MP_ENEMY_MAX_TWISTS * MP_ENEMY_TWIST_BYTES;
}

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

/* A position is refused rather than clamped, which is the opposite of what health does, and the
 * difference is deliberate. Health beyond the range means the extreme of health and nothing is
 * lost by saying so; a position beyond the range means the level is not one these numbers were
 * measured on, and the nearest representable point is a place the body is not. */
bool mp_enemy_wire_put_position(float world, uint32_t *out)
{
    float scaled;

    if (out == NULL) {
        return false;
    }
    /* Not finite, tested without <math.h> so this file stays free of it: only a NaN differs from
     * itself, and an infinity survives being halved.
     *
     * Zero also survives being halved, and leaving it out of the test made this refuse the
     * origin. That is not a corner: a coordinate of exactly 0 is what an actor standing on an axis
     * has, and the whole record would have been dropped without a word. */
    if (world != world || (world != 0.0f && world * 0.5f == world)) {
        return false;
    }
    if (world < MP_ENEMY_POS_MIN || world > MP_ENEMY_POS_MAX) {
        return false;
    }
    scaled = (world + MP_ENEMY_POS_BIAS) * MP_ENEMY_POS_SCALE + 0.5f;
    *out = (uint32_t)scaled;
    if (*out > 65535u) {
        *out = 65535u;   /* the rounding, and only the rounding, may reach the far edge */
    }
    return true;
}

float mp_enemy_wire_get_position(uint32_t wire)
{
    return (float)(wire & 0xFFFFu) / MP_ENEMY_POS_SCALE - MP_ENEMY_POS_BIAS;
}

uint32_t mp_enemy_wire_put_health(int32_t health)
{
    return (uint32_t)(clamp_i32(health, MP_ENEMY_HEALTH_MIN, MP_ENEMY_HEALTH_MAX) +
                      MP_ENEMY_HEALTH_BIAS);
}

int32_t mp_enemy_wire_health(const mp_enemy_record_t *record)
{
    return (int32_t)record->value[MP_ENEMY_F_HEALTH] - MP_ENEMY_HEALTH_BIAS;
}

bool mp_enemy_wire_reports_death(const mp_enemy_record_t *record)
{
    uint32_t state;

    if (record == NULL) {
        return false;
    }
    state = record->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK;
    return state >= MP_ENEMY_STATE_DEATH && state <= MP_ENEMY_STATE_CORPSE;
}

bool mp_enemy_wire_health_known(const mp_enemy_record_t *record)
{
    return record != NULL && record->value[MP_ENEMY_F_HEALTH] != 0u;
}

bool mp_enemy_wire_death_begins(const mp_enemy_record_t *record, bool health_was_up)
{
    if (mp_enemy_wire_reports_death(record)) {
        return true;
    }
    return health_was_up && mp_enemy_wire_health_known(record) &&
           mp_enemy_wire_health(record) <= 0;
}

bool mp_enemy_wire_has(const mp_enemy_record_t *record, uint16_t presence_bit)
{
    return (record->value[MP_ENEMY_F_STATE] & presence_bit) != 0u;
}

/* The tail. Five bytes each, big endian for no reason other than that the rest of this wire is,
 * and a reader that meets a count it cannot serve refuses the whole record rather than reading
 * what it can: half a set of rotations is a body with one shoulder from this tick and one from
 * the last, which looks like a physics fault and is not one. */
static void put_twist(uint8_t *out, const mp_enemy_twist_t *twist)
{
    out[0] = twist->node;
    out[1] = (uint8_t)(twist->pitch >> 8);
    out[2] = (uint8_t)(twist->pitch & 0xffu);
    out[3] = (uint8_t)(twist->yaw >> 8);
    out[4] = (uint8_t)(twist->yaw & 0xffu);
}

static void get_twist(const uint8_t *in, mp_enemy_twist_t *twist)
{
    twist->node  = in[0];
    twist->pitch = (uint16_t)(((uint16_t)in[1] << 8) | in[2]);
    twist->yaw   = (uint16_t)(((uint16_t)in[3] << 8) | in[4]);
}

/* The rotations behind a record of which the field set has already written `written` bytes. */
static bool put_tail(const mp_enemy_record_t *record, uint8_t *out, size_t capacity,
                     size_t written, size_t *bytes)
{
    uint32_t count = record->value[MP_ENEMY_F_TWISTS];
    size_t   i;

    if (written > capacity || capacity - written < count * MP_ENEMY_TWIST_BYTES) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        put_twist(out + written + i * MP_ENEMY_TWIST_BYTES, &record->twist[i]);
    }
    *bytes = written + count * MP_ENEMY_TWIST_BYTES;
    return true;
}

bool mp_enemy_wire_encode(const mp_enemy_record_t *record, const mp_enemy_record_t *baseline,
                          uint8_t *out, size_t capacity, size_t *bytes)
{
    size_t   written = 0;
    uint32_t count;

    if (record == NULL || out == NULL || bytes == NULL) {
        return false;
    }
    count = record->value[MP_ENEMY_F_TWISTS];
    if (count > MP_ENEMY_MAX_TWISTS) {
        return false;   /* a count the tail cannot carry is a caller error, not a wire condition */
    }
    {
        /* Against zero rather than against nothing when no baseline is named. See the header: the
         * difference is 60 bytes against 34 for a fighting actor, and it decides how many packets
         * a level's worth of them takes. */
        static const uint32_t ZERO[MP_ENEMY_FIELD_COUNT] = { 0 };
        uint32_t              against[MP_ENEMY_FIELD_COUNT];
        size_t                f;

        for (f = 0; f < MP_ENEMY_FIELD_COUNT; ++f) {
            against[f] = (baseline != NULL) ? baseline->value[f] : ZERO[f];
        }

        /* The twist count is never deltaed, and this is the one field where that is not a
         * preference but a rule of the format.
         *
         * Every other field can be left out of the mask when it has not changed, and a receiver
         * reads it from its own baseline. The count cannot, because it says how long the record
         * is. A receiver whose baseline disagrees then reads the wrong number of tail bytes, its
         * cursor lands in the middle of the next record, and every record after it in the block is
         * nonsense: the whole block is refused, and a field run showed 98 of them going that way
         * in one session.
         *
         * Forcing the baseline to a value the count can never hold puts it in the mask every time.
         * One byte an actor, against a block. */
        against[MP_ENEMY_F_TWISTS] = 0xFFFFFFFFu;

        if (mp_fieldset_encode(&SET, record->value, against, out, capacity, &written) !=
            MP_FIELDSET_OK) {
            return false;
        }
    }
    return put_tail(record, out, capacity, written, bytes);
}

/* Every field, the twist count included, so the record reads the same against any baseline. The
 * field set writes every field when it is handed no baseline at all, which is the one difference
 * from the encode above: that one hands it zeros. */
bool mp_enemy_wire_encode_whole(const mp_enemy_record_t *record, uint8_t *out, size_t capacity,
                                size_t *bytes)
{
    size_t written = 0;

    if (record == NULL || out == NULL || bytes == NULL ||
        record->value[MP_ENEMY_F_TWISTS] > MP_ENEMY_MAX_TWISTS) {
        return false;
    }
    if (mp_fieldset_encode(&SET, record->value, NULL, out, capacity, &written) !=
        MP_FIELDSET_OK) {
        return false;
    }
    return put_tail(record, out, capacity, written, bytes);
}

bool mp_enemy_wire_decode(const uint8_t *buffer, size_t available,
                          const mp_enemy_record_t *baseline, mp_enemy_record_t *out,
                          size_t *bytes)
{
    size_t   used = 0;
    uint32_t count;
    size_t   i;

    if (buffer == NULL || out == NULL || bytes == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    /* The tail is written WHOLE every time and is never deltaed against the baseline, which is
     * not an omission. The engine's node relax at 004360C0 pulls every node rotation back towards
     * zero in each pre-tick, so a rotation that is being held changes every substep anyway; a
     * delta of it would carry the mask and the value both, and cost more than the value alone. */
    if (!mp_fieldset_decode(&SET, buffer, available, baseline != NULL ? baseline->value : NULL,
                            out->value, &used)) {
        return false;
    }
    count = out->value[MP_ENEMY_F_TWISTS];
    if (count > MP_ENEMY_MAX_TWISTS) {
        return false;   /* a corrupt count must not run the reader off the end */
    }
    if (used > available || available - used < count * MP_ENEMY_TWIST_BYTES) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        get_twist(buffer + used + i * MP_ENEMY_TWIST_BYTES, &out->twist[i]);
    }
    *bytes = used + count * MP_ENEMY_TWIST_BYTES;
    return true;
}

bool mp_enemy_wire_is_whole(const uint8_t *buffer, size_t available)
{
    return mp_fieldset_is_whole(&SET, buffer, available);
}

bool mp_enemy_wire_names_position(const uint8_t *buffer, size_t available)
{
    static const size_t POSITION[] = { MP_ENEMY_F_POS_X, MP_ENEMY_F_POS_Y, MP_ENEMY_F_POS_Z };
    size_t              i;

    for (i = 0; i < sizeof POSITION / sizeof POSITION[0]; ++i) {
        bool named = false;

        if (!mp_fieldset_changed(&SET, buffer, available, POSITION[i], &named) || !named) {
            return false;
        }
    }
    return true;
}
