/* mp_task.h: the substep task, and the ladder measured from inside it.
 *
 * The scheduler reads a task's return value as the number of substeps to wait before the next call.
 * Zero means next substep, and a negative value removes the task AND frees one of its record fields
 * as a pointer. So zero is the only value this may ever return, and that is a contract rather than
 * a preference.
 *
 * What the task measures is how many times it is entered inside one value of the frame counter,
 * which is the simulation ladder seen from the one place that actually rides it.
 *
 * The record's second entry point at +0x18 is left null on purpose. `task_run` at 0x00475953
 * calls that slot and refuses the whole call when it is null, and it is how a contact is
 * delivered: every object carries a task pointer and the contact path runs it. Null keeps this
 * foothold off that path. Nothing has checked that the task is never entered that way, and the
 * rate itself, ticks per frame, is still the design's claim rather than this instrument's reading.
 */
#ifndef MULTIPLAYER_MP_TASK_H
#define MULTIPLAYER_MP_TASK_H

#include <stdint.h>

/* The shape the scheduler calls: cdecl, one argument, and the return value is the wait. */
int32_t __cdecl mp_task_tick(int32_t arg);

/* Without the frame counter the task can still count ticks, but it cannot say how they are spread
 * over frames, and the report says which of the two it is reporting. */
void mp_task_set_frame_counter(uintptr_t cell);

/* One client from a layer above, entered once per tick after the census. The task neither knows
 * nor cares what it does; the client cannot touch the return contract. NULL turns it off. */
void mp_task_set_tick_client(void (*client)(void));

uint32_t mp_task_ticks(void);

/* Frames in which the task ran at all, and the fewest and most ticks seen in one of them. A frame
 * with no tick at all is counted separately: outside a level there are no substeps, and those
 * frames are the majority of a session. */
uint32_t mp_task_frames_with_ticks(void);
uint32_t mp_task_min_per_frame(void);
uint32_t mp_task_max_per_frame(void);

void mp_task_report(const char *why);

#endif /* MULTIPLAYER_MP_TASK_H */
