/* model_wear_note.c: the two records between the multiplayer and the overlay. See the header. */
#include "model_wear_note.h"

#include "shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The version the records carry, spelled once in the header so that both sides and their tests
 * read the same number. */
#define NOTE_VERSION MODEL_WEAR_NOTE_VERSION

/* The bounds a worn asset's own scale is taken inside. The shipped actors carry 0.4 to 1.3. */
#define SCALE_MIN 0.1f
#define SCALE_MAX 10.0f

/* Filed through a shared note and read by another DLL, so every layout is a contract. */
#define FIELD_AT(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), "a moved field is a new NOTE_VERSION")
#define SIZE_IS(type, size) \
    _Static_assert(sizeof(type) == (size), "a new size is a new NOTE_VERSION")

FIELD_AT(model_wear_want_bank_t, serial, 0u);
FIELD_AT(model_wear_want_bank_t, object, 4u);
FIELD_AT(model_wear_want_bank_t, block, 8u);
FIELD_AT(model_wear_want_bank_t, slot, 12u);
FIELD_AT(model_wear_want_bank_t, model, 16u);
SIZE_IS(model_wear_want_bank_t, 48u);
FIELD_AT(model_wear_want_record_t, bank, 4u);
SIZE_IS(model_wear_want_record_t, 4u + 48u * MODEL_WEAR_BANKS);

FIELD_AT(model_wear_done_bank_t, serial, 0u);
FIELD_AT(model_wear_done_bank_t, state, 4u);
FIELD_AT(model_wear_done_bank_t, reason, 5u);
FIELD_AT(model_wear_done_bank_t, weapon, 6u);
FIELD_AT(model_wear_done_bank_t, scale, 8u);
FIELD_AT(model_wear_done_bank_t, model, 12u);
SIZE_IS(model_wear_done_bank_t, 44u);
FIELD_AT(model_wear_done_record_t, ready, 2u);
FIELD_AT(model_wear_done_record_t, bank, 4u);
SIZE_IS(model_wear_done_record_t, 4u + 44u * MODEL_WEAR_BANKS);

_Static_assert(sizeof(model_wear_want_record_t) <= SHARED_NOTE_BYTES,
               "the wish record for every far bank must fit one shared note");
_Static_assert(sizeof(model_wear_done_record_t) <= SHARED_NOTE_BYTES,
               "the answer record for every far bank must fit one shared note");

static bool zero_bytes(const void *bytes, size_t count)
{
    const uint8_t *at = (const uint8_t *)bytes;
    size_t         i;

    for (i = 0; i < count; ++i) {
        if (at[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool model_wear_name_is_sound(const char name[MODEL_WEAR_NAME_MAX])
{
    size_t length;
    size_t i;
    size_t stem = 0u;
    size_t tail = 0u;
    bool   dot = false;

    if (name == NULL) {
        return false;
    }
    for (length = 0; length < MODEL_WEAR_NAME_MAX && name[length] != '\0'; ++length) {
    }
    if (length == MODEL_WEAR_NAME_MAX) {
        return false;   /* no terminator inside the field */
    }
    if (!zero_bytes(name + length, MODEL_WEAR_NAME_MAX - length)) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        if (name[i] == '.') {
            if (dot || stem == 0u) {
                return false;
            }
            dot = true;
        } else if (!name_char(name[i])) {
            return false;
        } else if (dot) {
            ++tail;
        } else {
            ++stem;
        }
    }
    return stem <= 8u && tail <= 3u && (!dot || tail != 0u);
}

static bool want_bank_is_sound(const model_wear_want_bank_t *bank)
{
    if (!zero_bytes(bank->reserved, sizeof bank->reserved) ||
        !model_wear_name_is_sound(bank->model)) {
        return false;
    }
    /* A body has a block to be described by; a bank with no body wants nothing. */
    if (bank->object != 0u && bank->block == 0u) {
        return false;
    }
    return bank->object != 0u || bank->model[0] == '\0';
}

bool model_wear_want_is_sound(const model_wear_want_record_t *record)
{
    size_t b;

    if (record == NULL || record->reserved != 0u) {
        return false;
    }
    for (b = 0; b < MODEL_WEAR_BANKS; ++b) {
        if (!want_bank_is_sound(&record->bank[b])) {
            return false;
        }
    }
    return true;
}

/* The weapon flag is a field of its own rather than a borrowed reserved byte, so that the rule
 * "every reserved byte is zero" stays whole instead of gaining an exception. Only a body that
 * wears something can carry a weapon over it, so the flag is 0 for every other state. */
static bool done_bank_is_sound(const model_wear_done_bank_t *bank)
{
    if (bank->reserved != 0u || !model_wear_name_is_sound(bank->model) ||
        bank->state > MODEL_WEAR_STATE_REFUSED || bank->reason > MODEL_WEAR_REASON_MAX ||
        bank->weapon > 1u) {
        return false;
    }
    switch (bank->state) {
    case MODEL_WEAR_STATE_WORN:
        /* Written as the test for being INSIDE, so a NaN is refused as well. */
        return bank->reason == MODEL_WEAR_REASON_NONE && bank->model[0] != '\0' &&
               bank->scale >= SCALE_MIN && bank->scale <= SCALE_MAX;
    case MODEL_WEAR_STATE_REFUSED:
        /* Only a body that wears another model has something to echo. */
        return bank->weapon == 0u && bank->reason != MODEL_WEAR_REASON_NONE &&
               bank->scale == 0.0f &&
               ((bank->reason == MODEL_WEAR_REASON_WEARS_OTHER) == (bank->model[0] != '\0'));
    default:
        return bank->weapon == 0u && bank->reason == MODEL_WEAR_REASON_NONE &&
               bank->scale == 0.0f && bank->model[0] == '\0';
    }
}

bool model_wear_done_is_sound(const model_wear_done_record_t *record)
{
    size_t b;

    if (record == NULL || record->reserved != 0u ||
        ((unsigned)record->ready &
         ~(MODEL_WEAR_READY_LISTENING | MODEL_WEAR_READY_ABLE | MODEL_WEAR_READY_WEAPON)) != 0u) {
        return false;
    }
    for (b = 0; b < MODEL_WEAR_BANKS; ++b) {
        if (!done_bank_is_sound(&record->bank[b])) {
            return false;
        }
    }
    return true;
}

bool model_wear_publish_want(const model_wear_want_record_t *record)
{
    model_wear_want_record_t out;

    if (!model_wear_want_is_sound(record)) {
        return false;
    }
    out = *record;
    out.version = NOTE_VERSION;
    return shared_note_publish(MODEL_WEAR_WANT_NOTE_NAME, &out, sizeof out);
}

bool model_wear_publish_done(const model_wear_done_record_t *record)
{
    model_wear_done_record_t out;

    if (!model_wear_done_is_sound(record)) {
        return false;
    }
    out = *record;
    out.version = NOTE_VERSION;
    return shared_note_publish(MODEL_WEAR_DONE_NOTE_NAME, &out, sizeof out);
}

/* One read into a local record, so that a refusal never touches the caller's. */
static bool read_record(const char *name, void *record, size_t size, uint32_t *serial)
{
    size_t count = 0;

    return shared_note_read(name, record, size, &count, serial) && count == size;
}

bool model_wear_read_want(model_wear_want_record_t *out, uint32_t *serial)
{
    model_wear_want_record_t record;
    uint32_t                 at = 0;

    if (out == NULL || !read_record(MODEL_WEAR_WANT_NOTE_NAME, &record, sizeof record, &at) ||
        record.version != NOTE_VERSION || !model_wear_want_is_sound(&record)) {
        return false;
    }
    *out = record;
    if (serial != NULL) {
        *serial = at;
    }
    return true;
}

bool model_wear_read_done(model_wear_done_record_t *out, uint32_t *serial)
{
    model_wear_done_record_t record;
    uint32_t                 at = 0;

    if (out == NULL || !read_record(MODEL_WEAR_DONE_NOTE_NAME, &record, sizeof record, &at) ||
        record.version != NOTE_VERSION || !model_wear_done_is_sound(&record)) {
        return false;
    }
    *out = record;
    if (serial != NULL) {
        *serial = at;
    }
    return true;
}
