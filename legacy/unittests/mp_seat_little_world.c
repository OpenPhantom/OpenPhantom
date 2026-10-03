/* unittests/mp_seat_little_world.c: the engine under the seat search, as the seat tests play it.
 * See the header. */
#include "mp_seat_little_world.h"

#include "mp_cells.h"
#include "mp_placements.h"
#include "mp_seat_rule.h"
#include "mp_signatures.h"
#include "mp_signatures_world.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WORLD_CLOCK_SECONDS 0x54u   /* the world's own clock inside that record, in seconds */
#define NEAR_A_POINT        0.1f

/* The ground contact block's word that says the floor belongs to a mover. */
#define GROUND_ON_MOVER_WORD 6u

little_world_t  wld;
little_engine_t eng;

static bool at(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];

    return dx * dx + dy * dy < NEAR_A_POINT * NEAR_A_POINT;
}

/* `points` is `count` points of three floats each. */
static bool in_list(const float position[3], const float *points, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (at(position, &points[3u * i])) {
            return true;
        }
    }
    return false;
}

static void __cdecl fake_probe_floor(const float position[3], void *ground)
{
    float   distance = wld.floor_z - position[2];
    int32_t mover    = 1;

    if (wld.hole_on && at(position, wld.hole)) {
        distance = MP_PROBE_NO_FLOOR;
    }
    memcpy(ground, &distance, sizeof distance);
    if (wld.mover_on && at(position, wld.mover)) {
        memcpy((uint8_t *)ground + GROUND_ON_MOVER_WORD * sizeof(int32_t), &mover, sizeof mover);
    }
}

static float __cdecl fake_head_clearance(const float position[3], uint16_t mask)
{
    (void)mask;
    if (wld.crawl_everywhere && !at(position, wld.open)) {
        return 1.0f;
    }
    return in_list(position, &wld.crawl[0][0], wld.crawl_count) ? 1.0f : 0.0f;
}

static float __cdecl fake_walkable_distance(uintptr_t world, const float from[3],
                                            const float to[3])
{
    (void)world;
    (void)from;
    ++wld.walkable_asked;
    return in_list(to, &wld.unwalkable[0][0], wld.unwalkable_count) ? 1.0f : 0.0f;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_GAME_MODE: return (uintptr_t)&eng.game_mode;
    case MP_CELL_LEVEL:     return (uintptr_t)&eng.world;
    default:                return 0u;
    }
}

bool mp_cells_hero_position(float out[3])
{
    if (!eng.own_known) {
        return false;
    }
    memcpy(out, eng.own, sizeof eng.own);
    return true;
}

size_t mp_signatures_world_resolve(void)
{
    return (size_t)MP_WORLD_SITE_COUNT;
}

uintptr_t mp_signatures_world_address(mp_world_site_t site)
{
    switch (site) {
    case MP_WORLD_SITE_PROBE_FLOOR:       return (uintptr_t)&fake_probe_floor;
    case MP_WORLD_SITE_HEAD_CLEARANCE:    return (uintptr_t)&fake_head_clearance;
    case MP_WORLD_SITE_WALKABLE_DISTANCE: return (uintptr_t)&fake_walkable_distance;
    default:                              return 0u;
    }
}

bool mp_placements_table(uintptr_t *world, uint32_t *count, uint32_t *table)
{
    (void)world;
    (void)count;
    (void)table;
    return false;
}

void set_the_clock(float seconds)
{
    memcpy(eng.world_record + WORLD_CLOCK_SECONDS, &seconds, sizeof seconds);
}

void little_world_open(float floor_z)
{
    memset(&wld, 0, sizeof wld);
    wld.floor_z   = floor_z;
    eng.game_mode = 2u;
    eng.world     = (uint32_t)(uintptr_t)eng.world_record;
    set_the_clock(1.0f);
}

void ring_point(const float anchor[3], size_t direction, float radius, float out[3])
{
    float offset[2];

    mp_seat_rule_ring_offset(direction, 0u, radius, offset);
    out[0] = anchor[0] + offset[0];
    out[1] = anchor[1] + offset[1];
    out[2] = anchor[2];
}

void unwalkable_at(const float point[3])
{
    if (wld.unwalkable_count < POINTS) {
        memcpy(wld.unwalkable[wld.unwalkable_count++], point, 3u * sizeof(float));
    }
}

void unwalkable(const float anchor[3], size_t direction)
{
    float point[3];

    ring_point(anchor, direction, MP_SEAT_RING_NEAR, point);
    unwalkable_at(point);
}

void crawl_over(const float anchor[3], size_t direction)
{
    if (wld.crawl_count < POINTS) {
        ring_point(anchor, direction, MP_SEAT_RING_NEAR, wld.crawl[wld.crawl_count++]);
    }
}

float apart(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}
