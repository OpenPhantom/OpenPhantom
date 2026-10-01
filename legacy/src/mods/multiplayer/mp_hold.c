/* mp_hold.c: a byte ring of whole entries, some of them marked dead. See the header.
 *
 * The ring never holds part of an entry, and the entry at its head is never a dead one: every call
 * that marks or takes an entry releases the dead ones in front of it before it returns. So "the
 * oldest message held" is always the entry at the head, and an empty hold is always a ring of
 * nought bytes.
 */
#include "mp_hold.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FLAG_DEAD 0x01u

/* Where the fields sit inside an entry's header. */
#define AT_LENGTH 0u
#define AT_KIND   2u
#define AT_FLAGS  3u
#define AT_HELD   4u
#define AT_KEY    8u

void mp_hold_init(mp_hold_t *hold)
{
    hold->head       = 0u;
    hold->used       = 0u;
    hold->live       = 0u;
    hold->held       = 0u;
    hold->held_bytes = 0u;
    hold->delivered  = 0u;
    hold->superseded = 0u;
    hold->most_live  = 0u;
    hold->most_bytes = 0u;
    hold->longest_ms = 0u;
}

/* Copies across the end of the ring in at most two pieces. */
static void copy_in(mp_hold_t *hold, size_t at, const uint8_t *from, size_t bytes)
{
    size_t first = MP_HOLD_BYTES - at;

    if (first > bytes) {
        first = bytes;
    }
    memcpy(hold->ring + at, from, first);
    memcpy(hold->ring, from + first, bytes - first);
}

static void copy_out(const mp_hold_t *hold, size_t at, uint8_t *to, size_t bytes)
{
    size_t first = MP_HOLD_BYTES - at;

    if (first > bytes) {
        first = bytes;
    }
    memcpy(to, hold->ring + at, first);
    memcpy(to + first, hold->ring, bytes - first);
}

static void put_u32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
    at[2] = (uint8_t)((value >> 16) & 0xFFu);
    at[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t get_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

/* The header of the entry at `at`, read whole. */
static void read_header(const mp_hold_t *hold, size_t at, uint8_t header[MP_HOLD_HEADER_BYTES])
{
    copy_out(hold, at, header, MP_HOLD_HEADER_BYTES);
}

static size_t entry_length(const uint8_t header[MP_HOLD_HEADER_BYTES])
{
    return (size_t)header[AT_LENGTH] | ((size_t)header[AT_LENGTH + 1u] << 8);
}

/* Releases dead entries in front of the head, so the head is live or the ring is empty. */
static void release_dead_head(mp_hold_t *hold)
{
    uint8_t header[MP_HOLD_HEADER_BYTES];

    while (hold->used != 0u) {
        size_t size;

        read_header(hold, hold->head, header);
        if ((header[AT_FLAGS] & FLAG_DEAD) == 0u) {
            return;
        }
        size        = MP_HOLD_HEADER_BYTES + entry_length(header);
        hold->head  = (hold->head + size) % MP_HOLD_BYTES;
        hold->used -= size;
    }
    hold->head = 0u;   /* an empty ring starts over at nought, which keeps a zeroed hold valid */
}

bool mp_hold_fits(const mp_hold_t *hold, size_t bytes)
{
    return bytes <= MP_CHANNEL_MESSAGE_BYTES &&
           MP_HOLD_HEADER_BYTES + bytes <= MP_HOLD_BYTES - hold->used;
}

bool mp_hold_put(mp_hold_t *hold, const mp_hold_entry_t *entry, const void *data,
                 uint32_t now_ms)
{
    uint8_t header[MP_HOLD_HEADER_BYTES];
    size_t  tail;

    if (entry == NULL || !mp_hold_fits(hold, entry->bytes) ||
        (data == NULL && entry->bytes > 0u)) {
        return false;
    }
    tail = (hold->head + hold->used) % MP_HOLD_BYTES;
    header[AT_LENGTH]      = (uint8_t)(entry->bytes & 0xFFu);
    header[AT_LENGTH + 1u] = (uint8_t)((entry->bytes >> 8) & 0xFFu);
    header[AT_KIND]        = entry->kind;
    header[AT_FLAGS]       = 0u;
    put_u32(header + AT_HELD, now_ms);
    put_u32(header + AT_KEY, entry->key);
    copy_in(hold, tail, header, sizeof header);
    if (entry->bytes > 0u) {
        copy_in(hold, (tail + MP_HOLD_HEADER_BYTES) % MP_HOLD_BYTES, (const uint8_t *)data,
                entry->bytes);
    }
    hold->used       += MP_HOLD_HEADER_BYTES + entry->bytes;
    ++hold->live;
    ++hold->held;
    hold->held_bytes += (uint32_t)entry->bytes;
    if (hold->most_live < hold->live) {
        hold->most_live = hold->live;
    }
    if (hold->most_bytes < hold->used) {
        hold->most_bytes = hold->used;
    }
    return true;
}

bool mp_hold_oldest(const mp_hold_t *hold, mp_hold_entry_t *entry, void *buffer, size_t capacity)
{
    uint8_t header[MP_HOLD_HEADER_BYTES];
    size_t  size;

    if (hold->live == 0u || entry == NULL) {
        return false;
    }
    read_header(hold, hold->head, header);
    size = entry_length(header);
    if (capacity < size || (buffer == NULL && size > 0u)) {
        return false;   /* stays whole at the head */
    }
    if (size > 0u) {
        copy_out(hold, (hold->head + MP_HOLD_HEADER_BYTES) % MP_HOLD_BYTES, (uint8_t *)buffer,
                 size);
    }
    entry->kind    = header[AT_KIND];
    entry->key     = get_u32(header + AT_KEY);
    entry->held_ms = get_u32(header + AT_HELD);
    entry->bytes   = size;
    return true;
}

void mp_hold_pop(mp_hold_t *hold, uint32_t now_ms)
{
    uint8_t  header[MP_HOLD_HEADER_BYTES];
    size_t   size;
    uint32_t waited;

    if (hold->live == 0u) {
        return;
    }
    read_header(hold, hold->head, header);
    size   = entry_length(header);
    waited = now_ms - get_u32(header + AT_HELD);
    if (waited < 0x80000000u && hold->longest_ms < waited) {
        hold->longest_ms = waited;
    }
    hold->head        = (hold->head + MP_HOLD_HEADER_BYTES + size) % MP_HOLD_BYTES;
    hold->used       -= MP_HOLD_HEADER_BYTES + size;
    --hold->live;
    ++hold->delivered;
    release_dead_head(hold);
}

bool mp_hold_supersede(mp_hold_t *hold, uint8_t kind)
{
    uint8_t header[MP_HOLD_HEADER_BYTES];
    size_t  at = hold->head;
    size_t  walked = 0u;
    bool    marked = false;

    if (kind == 0u) {
        return false;
    }
    while (walked < hold->used) {
        size_t size;

        read_header(hold, at, header);
        size = MP_HOLD_HEADER_BYTES + entry_length(header);
        if ((header[AT_FLAGS] & FLAG_DEAD) == 0u && header[AT_KIND] == kind) {
            header[AT_FLAGS] |= (uint8_t)FLAG_DEAD;
            copy_in(hold, at, header, sizeof header);
            --hold->live;
            ++hold->superseded;
            marked = true;
        }
        at      = (at + size) % MP_HOLD_BYTES;
        walked += size;
    }
    release_dead_head(hold);
    return marked;
}

bool mp_hold_newest_key(const mp_hold_t *hold, uint8_t kind, uint32_t *key)
{
    uint8_t header[MP_HOLD_HEADER_BYTES];
    size_t  at = hold->head;
    size_t  walked = 0u;
    bool    found = false;

    if (kind == 0u || key == NULL) {
        return false;
    }
    while (walked < hold->used) {
        size_t size;

        read_header(hold, at, header);
        size = MP_HOLD_HEADER_BYTES + entry_length(header);
        if ((header[AT_FLAGS] & FLAG_DEAD) == 0u && header[AT_KIND] == kind) {
            *key  = get_u32(header + AT_KEY);
            found = true;
        }
        at      = (at + size) % MP_HOLD_BYTES;
        walked += size;
    }
    return found;
}

bool mp_hold_empty(const mp_hold_t *hold)
{
    return hold->live == 0u;
}

size_t mp_hold_count(const mp_hold_t *hold)
{
    return hold->live;
}


uint32_t mp_hold_oldest_age_ms(const mp_hold_t *hold, uint32_t now_ms)
{
    uint8_t  header[MP_HOLD_HEADER_BYTES];
    uint32_t age;

    if (hold->live == 0u) {
        return 0u;
    }
    read_header(hold, hold->head, header);
    age = now_ms - get_u32(header + AT_HELD);
    return age < 0x80000000u ? age : 0u;
}
