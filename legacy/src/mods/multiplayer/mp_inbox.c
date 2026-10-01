/* mp_inbox.c: a byte ring of whole messages.
 *
 * The ring never holds part of a message. A put checks the room for the length and the data
 * together before it writes a byte, and a take checks the buffer before it moves the head, so every
 * state between two calls is a run of whole messages starting at head.
 */
#include "mp_inbox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_inbox_init(mp_inbox_t *inbox)
{
    inbox->head       = 0u;
    inbox->used       = 0u;
    inbox->count      = 0u;
    inbox->most_count = 0u;
    inbox->most_bytes = 0u;
    inbox->blocked    = false;
    inbox->stalls     = 0u;
}

/* Copies across the end of the ring in at most two pieces. */
static void copy_in(mp_inbox_t *inbox, size_t at, const uint8_t *from, size_t bytes)
{
    size_t first = MP_INBOX_BYTES - at;

    if (first > bytes) {
        first = bytes;
    }
    memcpy(inbox->ring + at, from, first);
    memcpy(inbox->ring, from + first, bytes - first);
}

static void copy_out(const mp_inbox_t *inbox, size_t at, uint8_t *to, size_t bytes)
{
    size_t first = MP_INBOX_BYTES - at;

    if (first > bytes) {
        first = bytes;
    }
    memcpy(to, inbox->ring + at, first);
    memcpy(to + first, inbox->ring, bytes - first);
}

bool mp_inbox_fits(const mp_inbox_t *inbox, size_t bytes)
{
    return bytes <= 0xFFFFu && MP_INBOX_LENGTH_BYTES + bytes <= MP_INBOX_BYTES - inbox->used;
}

bool mp_inbox_put(mp_inbox_t *inbox, const void *data, size_t bytes)
{
    uint8_t length[MP_INBOX_LENGTH_BYTES];
    size_t  tail;

    if (!mp_inbox_fits(inbox, bytes) || (data == NULL && bytes > 0u)) {
        return false;
    }
    tail      = (inbox->head + inbox->used) % MP_INBOX_BYTES;
    length[0] = (uint8_t)(bytes & 0xFFu);
    length[1] = (uint8_t)((bytes >> 8) & 0xFFu);
    copy_in(inbox, tail, length, sizeof length);
    if (bytes > 0u) {
        copy_in(inbox, (tail + MP_INBOX_LENGTH_BYTES) % MP_INBOX_BYTES, (const uint8_t *)data,
                bytes);
    }
    inbox->used += MP_INBOX_LENGTH_BYTES + bytes;
    ++inbox->count;
    if (inbox->most_count < inbox->count) {
        inbox->most_count = inbox->count;
    }
    if (inbox->most_bytes < inbox->used) {
        inbox->most_bytes = inbox->used;
    }
    return true;
}

bool mp_inbox_take(mp_inbox_t *inbox, void *buffer, size_t capacity, size_t *bytes)
{
    uint8_t length[MP_INBOX_LENGTH_BYTES];
    size_t  size;

    if (inbox->count == 0u || bytes == NULL) {
        return false;
    }
    copy_out(inbox, inbox->head, length, sizeof length);
    size = (size_t)length[0] | ((size_t)length[1] << 8);
    if (capacity < size || (buffer == NULL && size > 0u)) {
        return false;   /* stays whole at the head */
    }
    if (size > 0u) {
        copy_out(inbox, (inbox->head + MP_INBOX_LENGTH_BYTES) % MP_INBOX_BYTES, (uint8_t *)buffer,
                 size);
    }
    *bytes       = size;
    inbox->head  = (inbox->head + MP_INBOX_LENGTH_BYTES + size) % MP_INBOX_BYTES;
    inbox->used -= MP_INBOX_LENGTH_BYTES + size;
    --inbox->count;
    return true;
}

void mp_inbox_note_move(mp_inbox_t *inbox, bool left_one_behind)
{
    if (left_one_behind && !inbox->blocked) {
        ++inbox->stalls;
    }
    inbox->blocked = left_one_behind;
}

size_t mp_inbox_most_count(const mp_inbox_t *inbox)  { return inbox->most_count; }
size_t mp_inbox_most_bytes(const mp_inbox_t *inbox)  { return inbox->most_bytes; }
uint32_t mp_inbox_stalls(const mp_inbox_t *inbox)    { return inbox->stalls; }
