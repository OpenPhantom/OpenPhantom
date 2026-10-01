/* mp_hold.c: the hold alone, order across the end of its ring, dead entries, and its limits. */
#include "unittest.h"

#include "mp_hold.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_hold_t s_hold;

static bool put_numbered(mp_hold_t *hold, uint8_t kind, uint32_t number, size_t bytes,
                         uint32_t now_ms)
{
    uint8_t         data[MP_CHANNEL_MESSAGE_BYTES];
    mp_hold_entry_t entry;

    memset(data, (int)(number & 0xFFu), sizeof data);
    memcpy(data, &number, sizeof number < bytes ? sizeof number : bytes);
    entry.kind    = kind;
    entry.key     = number * 7u;
    entry.held_ms = 0u;
    entry.bytes   = bytes;
    return mp_hold_put(hold, &entry, data, now_ms);
}

static bool take_numbered(mp_hold_t *hold, uint32_t *number, size_t *bytes, uint32_t now_ms)
{
    uint8_t         data[MP_CHANNEL_MESSAGE_BYTES];
    mp_hold_entry_t entry;

    if (!mp_hold_oldest(hold, &entry, data, sizeof data)) {
        return false;
    }
    *number = 0u;
    memcpy(number, data, sizeof *number < entry.bytes ? sizeof *number : entry.bytes);
    *bytes = entry.bytes;
    mp_hold_pop(hold, now_ms);
    return true;
}

static void check_order_across_the_end(void)
{
    uint32_t put = 0;
    uint32_t got = 0;
    uint32_t number = 0;
    size_t   bytes = 0;
    bool     in_order = true;
    bool     whole = true;
    int      round;

    ut_section("messages come out whole and in order across the end of the ring");
    mp_hold_init(&s_hold);
    for (round = 0; round < 400; ++round) {
        size_t size = 4u + (size_t)((round * 131) % 1100);

        while (put_numbered(&s_hold, 0u, put, size, (uint32_t)round)) {
            ++put;
            size = 4u + (size_t)((put * 57u) % 900u);
        }
        while (mp_hold_count(&s_hold) > 3u && take_numbered(&s_hold, &number, &bytes, 1000u)) {
            if (number != got) {
                in_order = false;
            }
            whole = whole && bytes >= 4u;
            ++got;
        }
    }
    while (take_numbered(&s_hold, &number, &bytes, 1000u)) {
        if (number != got) {
            in_order = false;
        }
        ++got;
    }
    ut_checkf(put > 4000u && got == put, "%u messages of mixed length went in and all came out",
              (unsigned)put);
    ut_check(in_order && whole, "in the order they went in, none torn");
    ut_check(mp_hold_empty(&s_hold) && s_hold.used == 0u && s_hold.head == 0u,
             "and an emptied hold is a ring of nought bytes starting at nought");
    ut_checkf(s_hold.delivered == put && s_hold.held == put,
              "the counters say %u held and %u handed on", (unsigned)s_hold.held,
              (unsigned)s_hold.delivered);
}

static void check_limits(void)
{
    uint32_t count = 0;

    ut_section("the hold refuses whole what it cannot take, and nothing past a channel message");
    mp_hold_init(&s_hold);
    ut_check(!put_numbered(&s_hold, 0u, 1u, MP_CHANNEL_MESSAGE_BYTES + 1u, 0u),
             "a message larger than any channel carries is refused");
    while (put_numbered(&s_hold, 0u, count, MP_CHANNEL_MESSAGE_BYTES, 0u)) {
        ++count;
    }
    ut_checkf(count == MP_HOLD_BYTES / (MP_HOLD_HEADER_BYTES + MP_CHANNEL_MESSAGE_BYTES),
              "%u of the largest messages fill it", (unsigned)count);
    ut_check(!mp_hold_fits(&s_hold, MP_CHANNEL_MESSAGE_BYTES) &&
             mp_hold_count(&s_hold) == count,
             "the next is refused and nothing held was touched");
    ut_check(put_numbered(&s_hold, 0u, 99u, 0u, 0u) ||
             MP_HOLD_BYTES - s_hold.used < MP_HOLD_HEADER_BYTES,
             "an empty message costs only its header");
}

static void check_ages(void)
{
    uint32_t number = 0;
    size_t   bytes = 0;

    ut_section("the age of the oldest message, and the longest wait handed on");
    mp_hold_init(&s_hold);
    ut_check(mp_hold_oldest_age_ms(&s_hold, 100u) == 0u, "an empty hold has no age");
    (void)put_numbered(&s_hold, 0u, 1u, 20u, 20u);
    (void)put_numbered(&s_hold, 0u, 2u, 20u, 50u);
    ut_checkf(mp_hold_oldest_age_ms(&s_hold, 100u) == 80u,
              "the oldest was held at 20 and is 80 ms old at 100 (%u)",
              (unsigned)mp_hold_oldest_age_ms(&s_hold, 100u));
    ut_check(take_numbered(&s_hold, &number, &bytes, 100u) && number == 1u &&
             mp_hold_oldest_age_ms(&s_hold, 100u) == 50u,
             "taking it leaves the next, 50 ms old");
    ut_checkf(s_hold.longest_ms == 80u, "the longest wait handed on was 80 ms (%u)",
              (unsigned)s_hold.longest_ms);
}

static void check_dead_entries(void)
{
    uint32_t number = 0;
    size_t   bytes = 0;
    uint32_t key = 0;

    ut_section("a newer copy of a state marks the older one dead, and dead ones never come out");
    mp_hold_init(&s_hold);
    (void)put_numbered(&s_hold, 0x8Fu, 1u, 200u, 10u);   /* a roster */
    (void)put_numbered(&s_hold, 0u, 2u, 20u, 20u);       /* an event */
    (void)put_numbered(&s_hold, 0x92u, 3u, 100u, 30u);   /* a setup */
    ut_check(mp_hold_newest_key(&s_hold, 0x8Fu, &key) && key == 7u,
             "the roster held is found by its kind, with its key");
    ut_check(mp_hold_supersede(&s_hold, 0x8Fu), "a newer roster marks the held one dead");
    ut_check(!mp_hold_newest_key(&s_hold, 0x8Fu, &key) && !mp_hold_supersede(&s_hold, 0x8Fu),
             "and no roster is held any more");
    ut_check(!mp_hold_supersede(&s_hold, 0u), "an event is never marked");
    ut_checkf(mp_hold_count(&s_hold) == 2u && s_hold.superseded == 1u,
              "two live entries remain (%u), one superseded", (unsigned)mp_hold_count(&s_hold));
    ut_checkf(mp_hold_oldest_age_ms(&s_hold, 100u) == 80u,
              "the oldest live one is the event held at 20, 80 ms old at 100 (%u)",
              (unsigned)mp_hold_oldest_age_ms(&s_hold, 100u));
    ut_check(take_numbered(&s_hold, &number, &bytes, 100u) && number == 2u,
             "the event comes out first, the dead roster in front of it never does");
    (void)mp_hold_supersede(&s_hold, 0x92u);
    ut_check(mp_hold_empty(&s_hold) && s_hold.used == 0u,
             "marking the last live entry dead empties the ring");
}

int main(void)
{
    check_order_across_the_end();
    check_limits();
    check_ages();
    check_dead_entries();
    return ut_summary("mp_hold");
}
