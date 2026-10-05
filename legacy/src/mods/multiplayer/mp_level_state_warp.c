/* mp_level_state_warp.c: a warp of the host's in the level's journal. See the header. */
#include "mp_level_state_warp.h"

#include "mp_enemy_wire.h"
#include "mp_level_state_internal.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The lines a client writes one by one. A level warps its player a handful of times. */
#define LINES_WRITTEN 32u

typedef struct warp_state {
    /* the host */
    uint32_t said;      /* warps the journal took */
    uint32_t unsaid;    /* warps it did not take, or whose target no entry carries */
    /* a client */
    uint32_t heard;     /* entries played in this process */
    bool     known;     /* one of them in this level */
    int32_t  hero;
    float    at[3];
    uint32_t lines;
} warp_state_t;

static warp_state_t warp;

bool mp_level_state_warp_encode(int32_t hero, const float at[3], uint8_t *a, uint16_t *b,
                                uint32_t *c)
{
    uint32_t wire[3];
    size_t   k;

    if (at == NULL || a == NULL || b == NULL || c == NULL) {
        return false;
    }
    for (k = 0u; k < 3u; ++k) {
        if (!mp_enemy_wire_put_position(at[k], &wire[k])) {
            return false;
        }
    }
    *a = (uint8_t)MP_LEVEL_WARP_HERO_UNKNOWN;
    if (hero >= 0 && hero < (int32_t)MP_LEVEL_WARP_HERO_UNKNOWN) {
        *a = (uint8_t)hero;
    }
    *b = (uint16_t)wire[0];
    *c = (wire[1] & 0xFFFFu) | ((wire[2] & 0xFFFFu) << 16);
    return true;
}

void mp_level_state_warp_decode(const mp_level_journal_entry_t *entry, int32_t *hero,
                                float at[3])
{
    if (entry == NULL) {
        return;
    }
    if (hero != NULL) {
        *hero = entry->a == (uint8_t)MP_LEVEL_WARP_HERO_UNKNOWN ? -1 : (int32_t)entry->a;
    }
    if (at != NULL) {
        at[0] = mp_enemy_wire_get_position(entry->b);
        at[1] = mp_enemy_wire_get_position(entry->c & 0xFFFFu);
        at[2] = mp_enemy_wire_get_position(entry->c >> 16);
    }
}

uint16_t mp_level_state_warp_say(int32_t hero, const float at[3])
{
    uint8_t  a = 0u;
    uint16_t b = 0u;
    uint32_t c = 0u;
    uint16_t number;

    if (!mp_level_state_warp_encode(hero, at, &a, &b, &c)) {
        ++warp.unsaid;
        return 0u;
    }
    number = mp_level_state_journal_note((uint8_t)MP_LEVEL_JOURNAL_WARP, a, b, c);
    if (number == 0u) {
        ++warp.unsaid;
        return 0u;
    }
    ++warp.said;
    return number;
}

void mp_level_state_warp_play(const mp_level_journal_entry_t *entry)
{
    if (entry == NULL || entry->kind != (uint8_t)MP_LEVEL_JOURNAL_WARP) {
        return;
    }
    mp_level_state_warp_decode(entry, &warp.hero, warp.at);
    warp.known = true;
    ++warp.heard;
    if (warp.lines < LINES_WRITTEN) {
        ++warp.lines;
        log_info("a script warped the host to %.2f %.2f %.2f as hero %d, heard here as number %u "
                 "of the level's journal", (double)warp.at[0], (double)warp.at[1],
                 (double)warp.at[2], (int)warp.hero, (unsigned)entry->sequence);
    }
}

bool mp_level_state_warp_heard(uint32_t *count, float at[3], int32_t *hero)
{
    if (!warp.known) {
        return false;
    }
    if (count != NULL) {
        *count = warp.heard;
    }
    if (at != NULL) {
        memcpy(at, warp.at, sizeof warp.at);
    }
    if (hero != NULL) {
        *hero = warp.hero;
    }
    return true;
}

void mp_level_state_warp_reset(void)
{
    warp.known = false;
}

void mp_level_state_warp_report(bool host)
{
    if (host) {
        log_info("  the host's warps in the level's journal (host): %u said to the far players, "
                 "%u not said, the journal took no entry or the target lies outside what an "
                 "entry carries", (unsigned)warp.said, (unsigned)warp.unsaid);
        return;
    }
    if (warp.known) {
        log_info("  the host's warps in the level's journal (client): %u heard; the newest of "
                 "this level sent him to %.2f %.2f %.2f as hero %d", (unsigned)warp.heard,
                 (double)warp.at[0], (double)warp.at[1], (double)warp.at[2], (int)warp.hero);
        return;
    }
    log_info("  the host's warps in the level's journal (client): %u heard, none in this level",
             (unsigned)warp.heard);
}
