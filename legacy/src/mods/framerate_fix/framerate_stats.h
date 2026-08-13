/* framerate_stats.h: the measurements that prove this DLL's own work, and nothing else.
 *
 * These are here rather than in the diagnostics DLL because they read exactly the quantities the
 * frame-rate fix acts on, frame time, substep count, interpolation alpha, and resolving those
 * a second time somewhere else would mean two owners for one set of sites.
 *
 * Both are OFF by default and log nothing when off.
 */
#ifndef FRAMERATE_STATS_H
#define FRAMERATE_STATS_H

#include <stdbool.h>
#include <stdint.h>

/* `frame_sample_interval` > 0 logs a frame-time/substep summary every N frames.
 * `player_frames` > 0 dumps the player's draw interpolation for that many frames. */
void framerate_stats_install(int frame_sample_interval, int player_frames);

/* Called once per frame with the frame delta the engine itself used. */
void framerate_stats_sample(float frame_delta);

/* What a finished window is allowed to claim about the simulation.
 *
 * The substep counter is shared: a menu frame ticks it once per rendered frame, so a window taken
 * outside gameplay reports a substep rate that is really the frame rate. The simulation clock is
 * advanced only by the substep loop, and comparing the two is what separates the cases. */
typedef enum window_verdict {
    WINDOW_VERDICT_UNAVAILABLE,  /* the simulation clock did not resolve, nothing can be checked */
    WINDOW_VERDICT_CLEAN,        /* every tick in the window came from the substep loop */
    WINDOW_VERDICT_IDLE,         /* the simulation did not advance: a menu, a load or a pause */
    WINDOW_VERDICT_MIXED,        /* the tick delta is not a whole number of substeps */
    WINDOW_VERDICT_RESET         /* the clock went backwards, so a level loaded inside the window */
} window_verdict_t;

/* Declared here only because the unit test drives it; nothing else outside the module calls it.
 * `simulation_advance` is in seconds and `tick_delta` is the shared counter's movement, both
 * measured across one window. */
window_verdict_t framerate_stats_classify_window(bool clock_available,
                                                 float simulation_advance,
                                                 uint32_t tick_delta);

#endif /* FRAMERATE_STATS_H */
