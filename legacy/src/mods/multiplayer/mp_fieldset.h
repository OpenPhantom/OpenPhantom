/* mp_fieldset.h: a wire record described by a table instead of by three pieces of code.
 *
 * Written by hand, every field on the wire is written out three times: once where it is sampled,
 * once where it is encoded and once where it is decoded, and the three have to agree by hand. That
 * works for one replicated body. It does not work for two more classes, and the reason it does not
 * is not the typing: it is that the three places drift, and a field that is encoded and never
 * decoded costs nothing at compile time and produces a body in the wrong pose at run time.
 *
 * So a field set is a table. One row per field, and the encoder and the decoder both walk it, in
 * the same order, from the same data. Adding a field is a row plus a sample and an apply; it
 * cannot be added to one side and forgotten on the other, because there are no longer two sides.
 *
 * What this is not. It is not a serialisation framework and it holds no engine knowledge. Values
 * arrive here as plain integers, already quantised by whoever sampled them, and leave the same
 * way. Turning an angle into a u16 or a position into fixed point is the caller's business,
 * because the scale belongs to the thing being described and not to the table that carries it.
 *
 * WHAT IT COSTS. A record carries a change mask, one bit per field, and then only the fields whose
 * value differs from the baseline. For a body that stands still that is the mask alone. For one
 * that moves it is the mask plus the fields that moved. The mask is the whole reason a record has
 * a variable length, and a variable length is the whole reason thirty seven enemies can be
 * described at all inside a packet budget of 1200 bytes. In numbers: a full enemy record as
 * designed is 37 to 43 bytes and a level holds between eleven and thirty seven live actors, so
 * thirty seven full records are 1369 to 1591 bytes and do not fit one packet at all; with the
 * mask a still actor costs two bytes for a thirteen field set, thirty seven of them 74 bytes, and
 * only the ones that moved pay for what moved.
 *
 * A tail that is not a fixed set of scalars, such as a variable number of node rotations, is NOT a
 * field. The caller appends it after the record and reads it back itself; the mask says nothing
 * about it. That is deliberate: a table whose rows can be variable length is a parser, and a
 * parser is the thing this file exists to avoid.
 */
#ifndef MULTIPLAYER_MP_FIELDSET_H
#define MULTIPLAYER_MP_FIELDSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The mask is a whole number of bytes and every field needs a bit, so this is what decides both.
 * Thirty two is four mask bytes and is comfortably past the largest set described so far. */
#define MP_FIELDSET_MAX_FIELDS 32u

/* How wide a field is on the wire. There is no signed kind: a caller that has a signed quantity
 * biases it into an unsigned one, because the bias belongs to the quantity. */
typedef enum mp_field_width {
    MP_FIELD_U8  = 1,
    MP_FIELD_U16 = 2,
    MP_FIELD_U32 = 4
} mp_field_width_t;

typedef struct mp_field {
    const char       *name;   /* for a report and for a failing test, never for the wire */
    mp_field_width_t  width;
} mp_field_t;

typedef struct mp_fieldset {
    const char      *name;
    const mp_field_t *field;
    size_t            count;
} mp_fieldset_t;

/* Why an encode refused. A refusal is always the caller's error and never a wire condition, which
 * is why it is an enum and not a counter: a value that does not fit its field means the sampling
 * step clamped nothing, and silently truncating it would put a body somewhere it never was. */
typedef enum mp_fieldset_result {
    MP_FIELDSET_OK = 0,
    MP_FIELDSET_BAD_SET,        /* no table, no fields, or more fields than the mask can name */
    MP_FIELDSET_NO_ROOM,        /* the buffer cannot hold the record */
    MP_FIELDSET_VALUE_TOO_WIDE  /* a value does not fit the width its row declares */
} mp_fieldset_result_t;

/* How many mask bytes a set uses. Zero for a set with no fields, which is not a legal set. */
size_t mp_fieldset_mask_bytes(const mp_fieldset_t *set);

/* The largest a record of this set can ever be: the mask plus every field. Callers size buffers
 * with it, and the budget arithmetic for a class starts here. */
size_t mp_fieldset_max_bytes(const mp_fieldset_t *set);

/* Whether the table itself is usable. Checked once at install time so that a malformed table is a
 * line in the log rather than a refusal on every substep. */
bool mp_fieldset_valid(const mp_fieldset_t *set);

/* Encode `values` (one per field, in table order) against `baseline`. A NULL baseline writes every
 * field, which is what a full record is. `bytes` receives the length written. */
mp_fieldset_result_t mp_fieldset_encode(const mp_fieldset_t *set, const uint32_t *values,
                                        const uint32_t *baseline, uint8_t *out, size_t capacity,
                                        size_t *bytes);

/* Decode into `values`. `baseline` supplies every field the mask says did not change; a NULL
 * baseline leaves those fields at zero, which is only correct for a record encoded against NULL.
 * `bytes` receives how much of the buffer the record used, so a caller can read its own tail after
 * it. Returns false on a truncated or malformed record rather than reading past the end. */
bool mp_fieldset_decode(const mp_fieldset_t *set, const uint8_t *buffer, size_t available,
                        const uint32_t *baseline, uint32_t *values, size_t *bytes);

/* Which fields a record claims to carry, without decoding it. The report uses it to say what a
 * class actually sends, and a test uses it to pin that a still body sends the mask alone. */
bool mp_fieldset_changed(const mp_fieldset_t *set, const uint8_t *buffer, size_t available,
                         size_t field_index, bool *changed);

/* Whether a record carries every field of the set, which is what an encode against NULL writes
 * and the one form that reads the same against any baseline or none. False for a buffer shorter
 * than the mask. */
bool mp_fieldset_is_whole(const mp_fieldset_t *set, const uint8_t *buffer, size_t available);

#endif /* MULTIPLAYER_MP_FIELDSET_H */
