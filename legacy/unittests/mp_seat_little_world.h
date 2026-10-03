/* unittests/mp_seat_little_world.h: the engine under the seat search, as the seat tests play it.
 *
 * The seat search asks three probes of the engine, reads the game mode and the world record, and a
 * wish reads this player's own position and the level's spawn points. Here the probes answer from a
 * flat floor at one height, with the points a test makes unwalkable, crawl spaces, one hole, one
 * mover and a count of how often the walkable line was asked; the cells are fields of this file.
 * mp_seat_order and mp_seat_loop link it, and each test decides what stands where.
 */
#ifndef UNITTESTS_MP_SEAT_LITTLE_WORLD_H
#define UNITTESTS_MP_SEAT_LITTLE_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORLD_RECORD_BYTES 0x60u
#define POINTS             16u

typedef struct little_world {
    float    floor_z;
    float    unwalkable[POINTS][3];   /* a walkable line that ends here is stopped */
    size_t   unwalkable_count;
    float    crawl[POINTS][3];        /* a crawl space over each of these */
    size_t   crawl_count;
    bool     crawl_everywhere;        /* and over every point but `open` */
    float    open[3];
    float    hole[3];                 /* no floor at all under this point */
    bool     hole_on;
    float    mover[3];                /* the floor under this point belongs to a mover */
    bool     mover_on;
    unsigned walkable_asked;          /* how often the walkable line was asked */
} little_world_t;

typedef struct little_engine {
    uint32_t game_mode;
    uint32_t world;
    uint8_t  world_record[WORLD_RECORD_BYTES];
    bool     own_known;
    float    own[3];
    uint32_t substep;
} little_engine_t;

extern little_world_t  wld;
extern little_engine_t eng;

/* A level on a flat floor at `floor_z`, with nothing in the way yet, its clock at a second. */
void little_world_open(float floor_z);

/* The world's own clock, in seconds since the level began. */
void set_the_clock(float seconds);

/* The point of `direction` on the ring of `radius` around `anchor`. */
void ring_point(const float anchor[3], size_t direction, float radius, float out[3]);

/* The walkable line to the near ring's point of `direction` is stopped; and to any point. */
void unwalkable(const float anchor[3], size_t direction);
void unwalkable_at(const float point[3]);

/* A crawl space over the near ring's point of `direction`. */
void crawl_over(const float anchor[3], size_t direction);

float apart(const float a[3], const float b[3]);

#endif /* UNITTESTS_MP_SEAT_LITTLE_WORLD_H */
