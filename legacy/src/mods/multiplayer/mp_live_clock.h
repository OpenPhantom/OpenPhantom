/* mp_live_clock.h: the time in which the engine could answer.
 *
 * Layer 1. No engine, no address, no socket: two counts and two flags in, one count out.
 *
 * A wait that asks "has the engine answered by now" may only count the time in which the engine
 * was able to. Two things take that ability away while the frame pump goes on drawing and the
 * wall clock runs through all of it. The player is parked: a menu, a scene or a tool has put the
 * player module into the state the engine calls suspended, in which its task runs no phase and no
 * fade. Or the level's simulation is held as a whole and no substep runs. A deadline measured on
 * frames and the wall clock has given up a landing behind an open developer menu, and the level
 * was ended over the corpse that was left. This clock is the wall clock with those stretches left
 * out.
 *
 * It is the wall clock everywhere else, and on purpose. Outside a running level nothing simulates
 * by nature: a wish that outlives its level has to run out there, as it always did, and not wait
 * for the next level to be carried out in.
 *
 * More frames are drawn than substeps run, so a frame without a substep says nothing. The world
 * counts as held once no substep has run for MP_LIVE_CLOCK_GAP_MS of wall time. A parked player
 * needs no such gap: the state is read, not inferred.
 */
#ifndef MULTIPLAYER_MP_LIVE_CLOCK_H
#define MULTIPLAYER_MP_LIVE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* How long a running level may go without a substep before it counts as held. The simulation
 * steps some thirty times a second, so a quarter of a second is seven steps that did not come,
 * and a hold is over on the first look that sees one again. */
#define MP_LIVE_CLOCK_GAP_MS 250u

typedef struct mp_live_clock {
    bool     known;      /* a first look has been taken */
    uint32_t steps;      /* the simulation's count at the last look */
    uint32_t wall_ms;    /* and the wall clock at it */
    uint32_t still_ms;   /* the wall time since that count last moved, never read past the gap */
    bool     holding;    /* the last look found the engine unable to answer */
    uint32_t ms;         /* what this clock reads */
    uint32_t held_ms;    /* the wall time it left out */
    uint32_t holds;      /* and in how many stretches */
} mp_live_clock_t;

/* One look, once per drawn frame. `steps` is any count that moves exactly when this machine
 * simulates, `level_runs` whether a level is being played at all, `parked` whether the player
 * module reads its suspended state, `wall_ms` the wall clock. Answers what the clock reads after
 * the look; the first look reads nought. A NULL clock answers nought. */
uint32_t mp_live_clock_look(mp_live_clock_t *clock, uint32_t steps, bool level_runs, bool parked,
                            uint32_t wall_ms);

#endif /* MULTIPLAYER_MP_LIVE_CLOCK_H */
