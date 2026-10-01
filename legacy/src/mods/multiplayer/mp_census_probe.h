/* mp_census_probe.h: once a second, the enemy census read again both ways, asking and trying.
 *
 * The census reads every enemy the host holds once a substep, and those reads moved from asking
 * the system about every address first to catching the rare fault instead. What that bought is a
 * number no earlier run can give, because the earlier runs had no stopwatch on the census. So,
 * while the substep is being measured, this reads the actors of the last census once a second
 * more, twice over: once with the reading code asking first, exactly as it did before the swap,
 * and once trying, as it does now. It prints the time of each and how many actors came out
 * different, which must be none: before, after and the proof they are the same, in one run and on
 * the engine's own addresses.
 *
 * Only reads that change nothing are run: the actor's own fields, the puppet's clip, playheads and
 * turned nodes, the base track, and the body's five values. The start count, the shield, the
 * hidden nodes, the death watch and the world events are left out, because each of them counts or
 * remembers something, and a second read would change what the census reports. None of those
 * asked the system before the swap, except the start count through the base track, which is read
 * here on its own. The walk is not repeated either; its two asking reads an actor are the part of
 * the old cost this does not see.
 *
 * It runs from the frame pump, between substeps, so that its time lands in no stage of the
 * stopwatch. Its own cost is one longer frame a second on the host while it is armed.
 */
#ifndef MULTIPLAYER_MP_CENSUS_PROBE_H
#define MULTIPLAYER_MP_CENSUS_PROBE_H

#include <stdbool.h>
#include <stdint.h>

/* Armed with the stopwatch. Unarmed, a call below is one comparison. */
void mp_census_probe_arm(bool armed);

/* Once a frame; probes once a second, over whatever actors the last census found. */
void mp_census_probe_run(void);

/* Since the process began: actors compared, those that came out different, and the performance
 * counter ticks each form spent over all of them. The report prints them. */
typedef struct mp_census_probe_totals {
    uint32_t compared;
    uint32_t differed;
    uint64_t asking_ticks;
    uint64_t trying_ticks;
} mp_census_probe_totals_t;

void mp_census_probe_totals(mp_census_probe_totals_t *out);

#endif /* MULTIPLAYER_MP_CENSUS_PROBE_H */
