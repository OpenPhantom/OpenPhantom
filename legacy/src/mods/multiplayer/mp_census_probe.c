/* mp_census_probe.c: the census read again both ways, once a second. See the header. */
#include "mp_census_probe.h"

#include "mp_enemy_bind.h"
#include "mp_enemy_body.h"
#include "mp_enemy_pose.h"
#include "mp_enemy_sync.h"

#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PROBE_PERIOD_MS 1000u

/* The actor pool's own size: no census can hold more live actors than this. */
#define PROBE_ACTORS 128u

/* One actor as the reads that change nothing see it. Cleared whole before each read, so two rows
 * that were read the same compare the same, padding included. */
typedef struct probe_row {
    mp_enemy_record_t     record;
    mp_enemy_pose_track_t track;
    mp_enemy_body_state_t body;
    bool                  actor_read;
    bool                  track_read;
    bool                  body_read;
} probe_row_t;

typedef struct census_probe {
    bool        armed;
    uint64_t    last_ms;
    uint32_t    probes;
    uint32_t    compared;
    uint32_t    differed;
    uint64_t    asking_ticks;
    uint64_t    trying_ticks;
    uintptr_t   actor[PROBE_ACTORS];
    probe_row_t asked[PROBE_ACTORS];
    probe_row_t tried[PROBE_ACTORS];
} census_probe_t;

static census_probe_t probe;

void mp_census_probe_arm(bool armed)
{
    probe.armed = armed;
}

static void read_row(uintptr_t actor, probe_row_t *row)
{
    uint32_t body = 0;

    memset(row, 0, sizeof *row);
    row->actor_read = mp_enemy_bind_peek(actor, &row->record, &body);
    if (row->actor_read) {
        row->track_read = mp_enemy_pose_base_track(body, &row->track);
        row->body_read  = mp_enemy_body_peek(body, &row->body);
    }
}

/* Every actor of the census in one pass, timed as one, in the mode asked for. */
static uint64_t read_all(bool asking, probe_row_t *rows, uint32_t count)
{
    LARGE_INTEGER before;
    LARGE_INTEGER after;
    uint32_t      i;

    memory_watch_asking_mode(asking);
    QueryPerformanceCounter(&before);
    for (i = 0; i < count; ++i) {
        read_row(probe.actor[i], &rows[i]);
    }
    QueryPerformanceCounter(&after);
    memory_watch_asking_mode(false);
    return (uint64_t)(after.QuadPart - before.QuadPart);
}

/* The census's actors, in its key order. */
static uint32_t collect(void)
{
    uint32_t key;
    uint32_t count = 0;

    for (key = 0; key < MP_ENEMY_SYNC_KEYS && count < PROBE_ACTORS; ++key) {
        uintptr_t actor = mp_enemy_sync_actor_for(key);

        if (actor != 0u) {
            probe.actor[count++] = actor;
        }
    }
    return count;
}

/* The two passes take turns going first, so that the one that finds the cache cold is not always
 * the same one. */
static void probe_once(void)
{
    uint32_t count = collect();
    uint32_t i;

    if (count == 0u) {
        return;
    }
    if ((probe.probes & 1u) == 0u) {
        probe.asking_ticks += read_all(true, probe.asked, count);
        probe.trying_ticks += read_all(false, probe.tried, count);
    } else {
        probe.trying_ticks += read_all(false, probe.tried, count);
        probe.asking_ticks += read_all(true, probe.asked, count);
    }
    for (i = 0; i < count; ++i) {
        if (memcmp(&probe.asked[i], &probe.tried[i], sizeof probe.asked[i]) != 0) {
            ++probe.differed;
        }
    }
    probe.compared += count;
    ++probe.probes;
}

void mp_census_probe_run(void)
{
    uint64_t now;

    /* Only where a census is being described: on a client, or on a host whose last peer has gone,
     * the table is a stale one, the probe compares nothing that is sent, and the longer frame it
     * costs once a second would only add to the hitches the run is judged by. */
    if (!probe.armed || !mp_enemy_sync_describing()) {
        return;
    }
    now = GetTickCount64();
    if (probe.last_ms != 0u && now - probe.last_ms < PROBE_PERIOD_MS) {
        return;
    }
    probe.last_ms = now;
    probe_once();
}

void mp_census_probe_totals(mp_census_probe_totals_t *out)
{
    if (out == NULL) {
        return;
    }
    out->compared     = probe.compared;
    out->differed     = probe.differed;
    out->asking_ticks = probe.asking_ticks;
    out->trying_ticks = probe.trying_ticks;
}
