/* mp_host_settings_rule.c: the 0xAB codec and the host's own reading of its settings. See the
 * header.
 */
#include "mp_host_settings_rule.h"

#include "common/host_settings_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AT_VERSION 1u
#define AT_CHEATS  2u
#define AT_PRESENT 3u

/* One bit per setting of the table. */
#define KNOWN_SETTINGS ((1u << HOST_SETTING_COUNT) - 1u)

/* The largest value two bytes of hundredths can carry. The note has no sign, and every range of the
 * table starts at 0 or above, so a negative value is one the note cannot say and is refused. */
#define MOST_HUNDREDTHS 65535.0f

static const host_setting_key_t *key_of(size_t id)
{
    return &host_settings_keys(NULL)[id];
}

/* Whether a value in hundredths is one this setting may carry: inside the table's range, and a
 * whole number where the setting's mod takes only whole numbers. */
static bool hundredths_are_admitted(size_t id, uint16_t hundredths)
{
    if (key_of(id)->whole_numbers && hundredths % 100u != 0u) {
        return false;
    }
    return host_settings_value_is_admitted((host_setting_id_t)id, (float)hundredths / 100.0f);
}

size_t mp_host_settings_encode(const mp_host_settings_note_t *note, uint8_t *out,
                               size_t capacity)
{
    uint8_t bytes[MP_HOST_SETTINGS_BYTES];
    size_t  id;

    if (note == NULL || out == NULL || capacity < MP_HOST_SETTINGS_BYTES ||
        (note->cheats & ~MP_HOST_SETTINGS_CHEATS_KNOWN) != 0u ||
        (note->present & ~KNOWN_SETTINGS) != 0u) {
        return 0u;
    }
    memset(bytes, 0, sizeof bytes);
    bytes[0]              = (uint8_t)MP_HOST_SETTINGS_TAG;
    bytes[AT_VERSION]     = (uint8_t)MP_HOST_SETTINGS_VERSION;
    bytes[AT_CHEATS]      = note->cheats;
    bytes[AT_PRESENT]     = (uint8_t)(note->present & 0xFFu);
    bytes[AT_PRESENT + 1] = (uint8_t)(note->present >> 8);
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        float    value = note->values[id];
        uint16_t hundredths;

        if ((note->present & (1u << id)) == 0u) {
            continue;   /* written as 0, which is what the decoder asks of an unnamed setting */
        }
        if (!isfinite(value) || value < 0.0f || value * 100.0f + 0.5f >= MOST_HUNDREDTHS) {
            return 0u;
        }
        hundredths = (uint16_t)(value * 100.0f + 0.5f);
        if (!hundredths_are_admitted(id, hundredths)) {
            return 0u;
        }
        bytes[MP_HOST_SETTINGS_HEAD_BYTES + 2u * id]      = (uint8_t)(hundredths & 0xFFu);
        bytes[MP_HOST_SETTINGS_HEAD_BYTES + 2u * id + 1u] = (uint8_t)(hundredths >> 8);
    }
    memcpy(out, bytes, sizeof bytes);
    return sizeof bytes;
}

mp_host_settings_verdict_t mp_host_settings_decode(const uint8_t *in, size_t bytes,
                                                   mp_host_settings_note_t *out)
{
    mp_host_settings_note_t note;
    size_t                  id;

    if (in == NULL || bytes == 0u || in[0] != (uint8_t)MP_HOST_SETTINGS_TAG) {
        return MP_HOST_SETTINGS_NOT_THIS_NOTE;
    }
    if (bytes != MP_HOST_SETTINGS_BYTES || in[AT_VERSION] != (uint8_t)MP_HOST_SETTINGS_VERSION) {
        return MP_HOST_SETTINGS_WRONG_SHAPE;
    }
    memset(&note, 0, sizeof note);
    note.cheats  = in[AT_CHEATS];
    note.present = (uint16_t)(in[AT_PRESENT] | (in[AT_PRESENT + 1] << 8));
    if ((note.cheats & ~MP_HOST_SETTINGS_CHEATS_KNOWN) != 0u ||
        (note.present & ~KNOWN_SETTINGS) != 0u) {
        return MP_HOST_SETTINGS_UNKNOWN_BIT;
    }
    for (id = 0; id < HOST_SETTING_COUNT; ++id) {
        uint16_t hundredths = (uint16_t)(in[MP_HOST_SETTINGS_HEAD_BYTES + 2u * id] |
                                         (in[MP_HOST_SETTINGS_HEAD_BYTES + 2u * id + 1u] << 8));

        if ((note.present & (1u << id)) == 0u) {
            if (hundredths != 0u) {
                return MP_HOST_SETTINGS_OUT_OF_RANGE;
            }
            continue;
        }
        if (!hundredths_are_admitted(id, hundredths)) {
            return MP_HOST_SETTINGS_OUT_OF_RANGE;
        }
        note.values[id] = (float)hundredths / 100.0f;
    }
    if (out != NULL) {
        *out = note;
    }
    return MP_HOST_SETTINGS_TAKEN;
}

float mp_host_settings_own_value(host_setting_id_t id, float raw)
{
    const host_setting_key_t *key;
    float                     whole;

    if ((unsigned)id >= (unsigned)HOST_SETTING_COUNT) {
        return 0.0f;
    }
    key = key_of((size_t)id);
    if (!isfinite(raw)) {
        return key->default_value;
    }
    if (!key->whole_numbers) {
        if (raw < key->minimum) {
            return key->minimum;
        }
        return raw > key->maximum ? key->maximum : raw;
    }
    whole = truncf(raw);
    if (key->minimum == 0.0f && key->maximum == 1.0f) {
        return whole != 0.0f ? 1.0f : 0.0f;
    }
    if (whole < key->minimum || whole > key->maximum) {
        return key->default_value;
    }
    return whole;
}
