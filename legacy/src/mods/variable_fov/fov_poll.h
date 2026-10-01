/* fov_poll.h: how often the frame poll may ask the settings file whether ExtraDegrees moved.
 *
 * Asking is one file attribute query, cheap once and not cheap when it is asked on every frame: at
 * 240 frames a second that was 240 queries a second, measured at 11.7 microseconds each, to learn
 * about a number a person drags by hand. The only writer that needs it answered quickly is
 * the developer menu's field of view row, which writes the key as the slider moves, so the look is
 * throttled to a period that keeps that preview live at about 33 steps a second.
 *
 * Kept apart from the poll itself so the decision can be tested without a settings file or a
 * clock: the caller hands in the tick count.
 */
#ifndef FOV_POLL_H
#define FOV_POLL_H

#include <stdbool.h>
#include <stdint.h>

/* The period between two looks, in milliseconds. A slider dragged in the developer menu previews
 * at about 33 steps a second rather than at the frame rate, which is still a slider rather than a
 * series of jumps. */
#define FOV_POLL_PERIOD_MS 30u

typedef struct fov_poll {
    uint32_t last_ms;    /* the tick count of the last look */
    bool     looked;     /* false until the first look, which is always due */
    uint32_t looks;      /* looks taken, for the log */
    uint32_t taken;      /* changes the caller applied, for the log; this file never counts them */
} fov_poll_t;

/* True when a look is due at `now_ms`, and then the look is recorded. The first call is always
 * due, a tick count of 0 included; after that a look is due once `period_ms` have passed since the
 * last one. The subtraction is unsigned, so the tick count's wrap after 49.7 days needs no case of
 * its own. */
bool fov_poll_due(fov_poll_t *poll, uint32_t now_ms, uint32_t period_ms);

#endif /* FOV_POLL_H */
