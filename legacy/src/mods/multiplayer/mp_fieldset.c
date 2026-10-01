/* mp_fieldset.c: the table walk, both ways.
 *
 * Little endian on the wire, like everything else in this feature, and written a byte at a time
 * rather than through a cast: the buffer is not aligned and the record does not begin on a
 * boundary of anything.
 *
 * The two walks are deliberately the same shape. Encode reads the mask bit it just wrote; decode
 * reads the mask bit it just read; both then advance by the same width from the same row. A field
 * that is written and not read is not a bug that can be introduced here, because there is one
 * loop over one table.
 */
#include "mp_fieldset.h"

#include <string.h>

static uint32_t width_limit(mp_field_width_t width)
{
    switch (width) {
    case MP_FIELD_U8:
        return 0xFFu;
    case MP_FIELD_U16:
        return 0xFFFFu;
    case MP_FIELD_U32:
    default:
        return 0xFFFFFFFFu;
    }
}

bool mp_fieldset_valid(const mp_fieldset_t *set)
{
    size_t index;

    if (set == NULL || set->field == NULL || set->count == 0u ||
        set->count > MP_FIELDSET_MAX_FIELDS) {
        return false;
    }
    for (index = 0; index < set->count; ++index) {
        const mp_field_t *field = &set->field[index];

        if (field->width != MP_FIELD_U8 && field->width != MP_FIELD_U16 &&
            field->width != MP_FIELD_U32) {
            return false;
        }
        if (field->name == NULL) {
            return false;   /* a row with no name cannot be reported, and a report is the point */
        }
    }
    return true;
}

size_t mp_fieldset_mask_bytes(const mp_fieldset_t *set)
{
    if (!mp_fieldset_valid(set)) {
        return 0u;
    }
    return (set->count + 7u) / 8u;
}

size_t mp_fieldset_max_bytes(const mp_fieldset_t *set)
{
    size_t total = mp_fieldset_mask_bytes(set);
    size_t index;

    if (total == 0u) {
        return 0u;
    }
    for (index = 0; index < set->count; ++index) {
        total += (size_t)set->field[index].width;
    }
    return total;
}

static void put_value(uint8_t *out, uint32_t value, mp_field_width_t width)
{
    size_t byte;

    for (byte = 0; byte < (size_t)width; ++byte) {
        out[byte] = (uint8_t)((value >> (8u * byte)) & 0xFFu);
    }
}

static uint32_t get_value(const uint8_t *in, mp_field_width_t width)
{
    uint32_t value = 0;
    size_t   byte;

    for (byte = 0; byte < (size_t)width; ++byte) {
        value |= (uint32_t)in[byte] << (8u * byte);
    }
    return value;
}

mp_fieldset_result_t mp_fieldset_encode(const mp_fieldset_t *set, const uint32_t *values,
                                        const uint32_t *baseline, uint8_t *out, size_t capacity,
                                        size_t *bytes)
{
    size_t mask_bytes;
    size_t at;
    size_t index;

    if (!mp_fieldset_valid(set) || values == NULL || out == NULL || bytes == NULL) {
        return MP_FIELDSET_BAD_SET;
    }
    mask_bytes = mp_fieldset_mask_bytes(set);
    if (capacity < mask_bytes) {
        return MP_FIELDSET_NO_ROOM;
    }

    /* Every value is checked against its width BEFORE a byte is written, so a refusal leaves the
     * buffer untouched rather than half filled. A caller that gets one has a sampling step that
     * clamped nothing, and the honest answer is to send nothing rather than a truncated pose.
     * The alternative was measured: the player's health clamp goes to 0..255 because a player has
     * 0..100, and a census of the 2250 shipped enemy placements found a maximum of 999 hit points
     * with 252 records above 255. A table that inherited that clamp would turn a tank with 999
     * into one with 231 and nothing would say so; hence a width per row and a refusal past it. */
    for (index = 0; index < set->count; ++index) {
        if (values[index] > width_limit(set->field[index].width)) {
            return MP_FIELDSET_VALUE_TOO_WIDE;
        }
    }

    memset(out, 0, mask_bytes);
    at = mask_bytes;
    for (index = 0; index < set->count; ++index) {
        const mp_field_t *field = &set->field[index];

        if (baseline != NULL && baseline[index] == values[index]) {
            continue;
        }
        if (at + (size_t)field->width > capacity) {
            return MP_FIELDSET_NO_ROOM;
        }
        out[index / 8u] |= (uint8_t)(1u << (index % 8u));
        put_value(out + at, values[index], field->width);
        at += (size_t)field->width;
    }

    *bytes = at;
    return MP_FIELDSET_OK;
}

bool mp_fieldset_decode(const mp_fieldset_t *set, const uint8_t *buffer, size_t available,
                        const uint32_t *baseline, uint32_t *values, size_t *bytes)
{
    size_t mask_bytes;
    size_t at;
    size_t index;

    if (!mp_fieldset_valid(set) || buffer == NULL || values == NULL || bytes == NULL) {
        return false;
    }
    mask_bytes = mp_fieldset_mask_bytes(set);
    if (available < mask_bytes) {
        return false;
    }

    at = mask_bytes;
    for (index = 0; index < set->count; ++index) {
        const mp_field_t *field   = &set->field[index];
        bool              present = (buffer[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0u;

        if (!present) {
            values[index] = baseline != NULL ? baseline[index] : 0u;
            continue;
        }
        if (at + (size_t)field->width > available) {
            return false;   /* truncated: the mask promises a field the buffer does not hold */
        }
        values[index] = get_value(buffer + at, field->width);
        at += (size_t)field->width;
    }

    *bytes = at;
    return true;
}

bool mp_fieldset_changed(const mp_fieldset_t *set, const uint8_t *buffer, size_t available,
                         size_t field_index, bool *changed)
{
    size_t mask_bytes = mp_fieldset_mask_bytes(set);

    if (mask_bytes == 0u || buffer == NULL || changed == NULL || available < mask_bytes ||
        field_index >= set->count) {
        return false;
    }
    *changed = (buffer[field_index / 8u] & (uint8_t)(1u << (field_index % 8u))) != 0u;
    return true;
}

bool mp_fieldset_is_whole(const mp_fieldset_t *set, const uint8_t *buffer, size_t available)
{
    size_t mask_bytes = mp_fieldset_mask_bytes(set);
    size_t field_index;

    if (mask_bytes == 0u || buffer == NULL || available < mask_bytes) {
        return false;
    }
    /* Only the bits below the count: the ones past it in the last mask byte are never set, whole or
     * not. */
    for (field_index = 0; field_index < set->count; ++field_index) {
        if ((buffer[field_index / 8u] & (uint8_t)(1u << (field_index % 8u))) == 0u) {
            return false;
        }
    }
    return true;
}
