/* mp_death.c: the death message, encoded and decoded. See the header for why it is not in the
 * file that notices a death.
 */
#include "mp_death.h"

bool mp_death_is(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes == MP_EVENT_DEATH_BYTES && note[0] == (uint8_t)MP_EVENT_DEATH;
}

bool mp_death_encode(const mp_death_note_t *note, uint8_t *bytes, size_t capacity)
{
    if (note == NULL || bytes == NULL || capacity < MP_EVENT_DEATH_BYTES) {
        return false;
    }
    if (note->reason > MP_DEATH_REASON_MAX) {
        return false;
    }
    if (note->killer_slot == note->victim_slot) {
        return false;   /* a slot cannot kill itself; that case is the suicide reason */
    }
    bytes[0] = (uint8_t)MP_EVENT_DEATH;
    bytes[1] = note->victim_slot;
    bytes[2] = note->killer_slot;
    bytes[3] = note->reason;
    return true;
}

bool mp_death_decode(const uint8_t *note, size_t bytes, mp_death_note_t *out)
{
    if (out == NULL || !mp_death_is(note, bytes)) {
        return false;
    }
    if (note[3] > MP_DEATH_REASON_MAX) {
        return false;
    }
    if (note[2] == note[1]) {
        return false;
    }
    out->victim_slot = note[1];
    out->killer_slot = note[2];
    out->reason      = note[3];
    return true;
}
