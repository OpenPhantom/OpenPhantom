/* mp_taken.c: the codec for "these pickups are gone". See the header for why it says that rather
 * than "this one just went".
 */
#include "mp_taken.h"

#include <string.h>

bool mp_taken_add(mp_taken_t *taken, uint8_t index)
{
    size_t at;
    size_t i;

    if (taken == NULL) {
        return false;
    }
    /* Where it belongs, and whether it is already there. One walk answers both, because the list
     * is sorted: the first entry not below the new one is either it or its place.
     *
     * It runs before the full test, because an index already in the list needs no room. The other
     * order made a full list refuse an index it was already carrying, which contradicts the
     * header's promise that saying so twice is not a failure. */
    for (at = 0; at < taken->count; ++at) {
        if (taken->index[at] == index) {
            return true;   /* already named; saying so twice is not a failure */
        }
        if (taken->index[at] > index) {
            break;
        }
    }
    if (taken->count >= MP_TAKEN_MAX) {
        return false;
    }
    for (i = taken->count; i > at; --i) {
        taken->index[i] = taken->index[i - 1u];
    }
    taken->index[at] = index;
    ++taken->count;
    return true;
}

size_t mp_taken_encode(const mp_taken_t *taken, uint8_t *buffer, size_t capacity)
{
    size_t bytes;
    size_t i;

    if (taken == NULL || buffer == NULL || taken->count > MP_TAKEN_MAX) {
        return 0u;
    }
    bytes = MP_TAKEN_BYTES_FOR(taken->count);
    if (capacity < bytes) {
        return 0u;
    }
    buffer[0] = (uint8_t)MP_TAKEN_TAG;
    buffer[1] = (uint8_t)(taken->level & 0xFFu);
    buffer[2] = (uint8_t)((taken->level >> 8) & 0xFFu);
    buffer[3] = taken->count;
    for (i = 0; i < taken->count; ++i) {
        buffer[MP_TAKEN_HEADER_BYTES + i] = taken->index[i];
    }
    return bytes;
}

bool mp_taken_is(const uint8_t *buffer, size_t bytes)
{
    if (buffer == NULL || bytes < MP_TAKEN_HEADER_BYTES) {
        return false;
    }
    return buffer[0] == (uint8_t)MP_TAKEN_TAG && buffer[3] <= MP_TAKEN_MAX &&
           bytes == MP_TAKEN_BYTES_FOR(buffer[3]);
}

bool mp_taken_decode(const uint8_t *buffer, size_t bytes, mp_taken_t *out)
{
    size_t i;

    if (out == NULL || !mp_taken_is(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->level = (uint16_t)((uint16_t)buffer[1] | ((uint16_t)buffer[2] << 8));
    out->count = buffer[3];
    for (i = 0; i < out->count; ++i) {
        out->index[i] = buffer[MP_TAKEN_HEADER_BYTES + i];
    }
    return true;
}

bool mp_taken_equal(const mp_taken_t *a, const mp_taken_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    if (a->level != b->level || a->count != b->count) {
        return false;
    }
    return memcmp(a->index, b->index, a->count) == 0;
}
