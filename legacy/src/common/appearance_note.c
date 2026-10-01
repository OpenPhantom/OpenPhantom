#include "appearance_note.h"

#include "shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bumped when the record below changes shape. A reader that finds a version it does not know
 * answers false rather than reading a field that has moved, which for this record would mean
 * naming a model nobody chose, or drawing a body at a size nobody asked for.
 *
 * Two, since the scale joined the model. Nothing in the tree publishes version one any more, and
 * a reader of this build refuses it rather than reading a record that is four bytes short. */
#define NOTE_VERSION 2u

typedef struct appearance_record {
    uint32_t version;
    float    scale;
    char     model[APPEARANCE_MODEL_MAX];
} appearance_record_t;

/* Filed through a shared note and read back by another DLL, so the layout is a contract. */
_Static_assert(sizeof(appearance_record_t) == 8u + APPEARANCE_MODEL_MAX,
               "Unexpected appearance_record_t size");

/* A factor this record may carry. Written out rather than left to the reader: a NaN compares
 * false against every bound, so the test is for being INSIDE and not for being outside. */
static bool scale_is_sound(float scale)
{
    return scale >= APPEARANCE_SCALE_MIN && scale <= APPEARANCE_SCALE_MAX;
}

bool appearance_note_publish(const char *model, float scale)
{
    appearance_record_t record;
    size_t              length = (model != NULL) ? strlen(model) : 0u;

    if (length + 1u > sizeof record.model || !scale_is_sound(scale)) {
        return false;
    }
    memset(&record, 0, sizeof record);
    record.version = NOTE_VERSION;
    record.scale   = scale;
    if (length != 0u) {
        memcpy(record.model, model, length);
    }
    return shared_note_publish(APPEARANCE_NOTE_NAME, &record, sizeof record);
}

bool appearance_note_read(char *out, size_t out_size, float *scale)
{
    appearance_record_t record;
    size_t              count = 0;

    if (out == NULL || out_size == 0u) {
        return false;
    }
    if (!shared_note_read(APPEARANCE_NOTE_NAME, &record, sizeof record, &count, NULL)) {
        return false;
    }
    if (count != sizeof record || record.version != NOTE_VERSION ||
        !scale_is_sound(record.scale)) {
        return false;
    }
    if (scale != NULL) {
        *scale = record.scale;
    }
    /* The publisher zeroes the whole field, but the record crossed a boundary this side does not
     * own, so the terminator is put back rather than assumed. */
    record.model[sizeof record.model - 1u] = '\0';
    if (strlen(record.model) + 1u > out_size) {
        return false;
    }
    memcpy(out, record.model, strlen(record.model) + 1u);
    return true;
}
