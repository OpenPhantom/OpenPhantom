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

#endif /* FRAMERATE_STATS_H */
