/* steer_log.h: one line per substep, so the steering stops being argued about.
 *
 * Three questions have now cost a round of guessing each: why the upper-body twist is not visible,
 * why sideways movement stops while the fire button is held, and which of the two control schemes
 * actually owns a given substep. Every one of them is answered by the same handful of numbers, and
 * none of them is visible from outside the DLL.
 *
 * So this prints them. It is OFF by default, it stops after a fixed number of lines, and it prints
 * only substeps where the player is actually doing something, a log that scrolls while nothing
 * happens buries the frames that matter.
 */
#ifndef STEER_LOG_H
#define STEER_LOG_H

#include <stdbool.h>
#include <stdint.h>

/* Which branch of the phase-2 thunk took the substep. The whole point is to distinguish "the
 * feature ran and did nothing visible" from "the feature never ran". */
typedef enum steer_branch {
    STEER_BRANCH_DECLINED,      /* the record or the mode gate refused the substep */
    STEER_BRANCH_FREE_LOOK,     /* free look took it; no lean yet, turn cell zeroed    */
    STEER_BRANCH_MOUSE_LOOK,    /* mouse look took it */
    STEER_BRANCH_PASSIVE        /* neither: MouseLook=0, the original alone */
} steer_branch_t;

void steer_log_configure(int max_lines);

/* Called once per substep from the phase-2 thunk, after everything else has run, so the values are
 * the ones that were really written. `record` may be NULL. */
void steer_log_substep(const uint8_t *record, steer_branch_t branch, float substep_seconds,
                       float yaw_degrees, float travel_degrees, int mode_index);

#endif /* STEER_LOG_H */
