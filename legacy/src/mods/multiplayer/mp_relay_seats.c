/* mp_relay_seats.c: a relay host's players, one endpoint each. See the header. */
#include "mp_relay_seats.h"

#include "mp_relay_inner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_relay_seats_init(mp_relay_seats_t *seats)
{
    memset(seats, 0, sizeof *seats);
}

/* Whether a notice on (leg, counter) came after one on (seen_leg, seen_counter). */
static bool after(uint32_t leg, uint64_t counter, uint32_t seen_leg, uint64_t seen_counter)
{
    return leg > seen_leg || (leg == seen_leg && counter > seen_counter);
}

static mp_relay_seat_t *seat_of(mp_relay_seats_t *seats, uint32_t endpoint)
{
    if (endpoint == 0u || endpoint > MP_RELAY_SEATS_MAX || !seats->seat[endpoint - 1u].used) {
        return NULL;
    }
    return &seats->seat[endpoint - 1u];
}

/* The seat on a pair: the open one when there is one, since a closed seat the session still holds
 * may share its pair with the player who came after it. */
static uint32_t find(const mp_relay_seats_t *seats, uint8_t slot, uint16_t gen)
{
    uint32_t closed = 0u;
    uint32_t i;

    for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
        const mp_relay_seat_t *s = &seats->seat[i];

        if (s->used && s->slot == slot && s->gen == gen) {
            if (s->open) {
                return i + 1u;
            }
            closed = closed != 0u ? closed : i + 1u;
        }
    }
    return closed;
}

static void end_seat(mp_relay_seat_t *s)
{
    s->open = false;
    if (!s->held) {
        memset(s, 0, sizeof *s);
    }
}

static void seat(mp_relay_seat_t *s, uint8_t slot, uint16_t gen,
                 const uint8_t r[MP_RELAY_SEAT_VALUE_BYTES], uint32_t leg, uint64_t counter)
{
    s->used         = true;
    s->open         = true;
    s->slot         = slot;
    s->gen          = gen;
    s->open_leg     = leg;
    s->open_counter = counter;
    memcpy(s->r, r, sizeof s->r);
}

/* A slot holds one player: a new one on it means whoever sat there before is gone, whether or not
 * their MemberClosed has arrived yet. */
static void vacate_slot(mp_relay_seats_t *seats, uint8_t slot, uint32_t keep)
{
    uint32_t i;

    for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
        mp_relay_seat_t *s = &seats->seat[i];

        if (i + 1u != keep && s->used && s->open && s->slot == slot) {
            end_seat(s);
        }
    }
}

uint32_t mp_relay_seats_open(mp_relay_seats_t *seats, uint8_t slot, uint16_t gen, uint8_t flags,
                             const uint8_t r[MP_RELAY_SEAT_VALUE_BYTES], uint32_t leg,
                             uint64_t counter)
{
    uint32_t endpoint;
    uint32_t i;

    if ((flags & (uint8_t)MP_RELAY_MEMBER_OPEN_CONTINUES) != 0u) {
        for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
            mp_relay_seat_t *s = &seats->seat[i];

            if (!s->used || memcmp(s->r, r, sizeof s->r) != 0) {
                continue;
            }
            if (s->open && s->slot == slot && s->gen == gen &&
                !after(leg, counter, s->open_leg, s->open_counter)) {
                ++seats->stale;
                return 0u;
            }
            seat(s, slot, gen, r, leg, counter);
            vacate_slot(seats, slot, i + 1u);
            ++seats->continued;
            return i + 1u;
        }
    }
    endpoint = find(seats, slot, gen);
    if (endpoint != 0u) {
        mp_relay_seat_t *s = &seats->seat[endpoint - 1u];

        if (memcmp(s->r, r, sizeof s->r) == 0) {
            if (!after(leg, counter, s->open_leg, s->open_counter)) {
                ++seats->stale;
                return 0u;
            }
            seat(s, slot, gen, r, leg, counter);
            vacate_slot(seats, slot, endpoint);
            return endpoint;
        }
        if (!after(leg, counter, s->open_leg, s->open_counter)) {
            ++seats->stale;   /* the player before, delivered late */
            return 0u;
        }
        end_seat(s);   /* the same pair for another seat: whoever sat on it is gone */
    }
    for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
        if (!seats->seat[i].used) {
            seat(&seats->seat[i], slot, gen, r, leg, counter);
            vacate_slot(seats, slot, i + 1u);
            ++seats->opened;
            return i + 1u;
        }
    }
    ++seats->full;
    return 0u;
}

uint32_t mp_relay_seats_close(mp_relay_seats_t *seats, uint8_t slot, uint16_t gen, uint32_t leg,
                              uint64_t counter)
{
    uint32_t         endpoint = find(seats, slot, gen);
    mp_relay_seat_t *s        = seat_of(seats, endpoint);

    if (s == NULL || !s->open || !after(leg, counter, s->open_leg, s->open_counter)) {
        ++seats->stale;
        return 0u;
    }
    end_seat(s);
    ++seats->closed;
    return endpoint;
}

uint32_t mp_relay_seats_from(mp_relay_seats_t *seats, uint8_t slot, uint16_t gen)
{
    uint32_t endpoint = find(seats, slot, gen);

    if (endpoint == 0u || !seats->seat[endpoint - 1u].open) {
        ++seats->unknown_data;
        return 0u;
    }
    return endpoint;
}

bool mp_relay_seats_to(mp_relay_seats_t *seats, uint32_t endpoint, uint8_t *slot, uint16_t *gen)
{
    const mp_relay_seat_t *s = seat_of(seats, endpoint);

    if (s == NULL || !s->open) {
        ++seats->refused_sends;
        return false;
    }
    *slot = s->slot;
    *gen  = s->gen;
    return true;
}

void mp_relay_seats_hold(mp_relay_seats_t *seats, uint32_t endpoint, bool held)
{
    mp_relay_seat_t *s = seat_of(seats, endpoint);

    if (s == NULL) {
        return;
    }
    s->held = held;
    if (!held && !s->open) {
        memset(s, 0, sizeof *s);
    }
}

uint64_t mp_relay_seats_address(const mp_relay_seats_t *seats, uint32_t endpoint)
{
    const mp_relay_seat_t *s;
    uint64_t               address = 0u;
    size_t                 i;

    if (endpoint == 0u || endpoint > MP_RELAY_SEATS_MAX || !seats->seat[endpoint - 1u].used) {
        return 0u;
    }
    s = &seats->seat[endpoint - 1u];
    for (i = 0; i < 8u; ++i) {
        address |= (uint64_t)s->r[i] << (8u * i);
    }
    return address != 0u ? address : 1u;
}

void mp_relay_seats_close_all(mp_relay_seats_t *seats)
{
    uint32_t i;

    for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
        if (seats->seat[i].used && seats->seat[i].open) {
            end_seat(&seats->seat[i]);
            ++seats->closed;
        }
    }
}

uint32_t mp_relay_seats_open_count(const mp_relay_seats_t *seats)
{
    uint32_t count = 0u;
    uint32_t i;

    for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
        count += seats->seat[i].used && seats->seat[i].open ? 1u : 0u;
    }
    return count;
}
