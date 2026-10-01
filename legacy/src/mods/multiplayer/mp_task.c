/* mp_task.c: one slot in the engine's scheduler, and what riding it measures.
 *
 * The return value is the contract and it is easy to get fatally wrong. The scheduler reads it as
 * the number of substeps to wait, so zero means next substep. A negative value does two things: it
 * takes the slot back, and it passes one field of the task record to the memory allocator as a
 * pointer. Out of `task_runAll` at 0x0047582A:
 *
 *     if (--t->countdown >= 1) continue;
 *     arg = t->fn(arg);
 *     if (arg < 0) {
 *         g_numTasks--;
 *         if (t->pWakeFlag) mem_free(*(void **)&t->wakeValue);
 *         t->fn = 0;
 *     } else {
 *         t->countdown = arg;
 *     }
 *
 * The record is 0x2C bytes, sixty four of them at [0x00868740]; the handler is at +0x14 and a non
 * zero one is the whole free list, the flag pointer at +0x1C, the freed value at +0x20 and the
 * countdown at +0x24, pre decremented every substep. The two fields the negative path reads sit
 * outside the five dwords a registration copies in from its staging area and are zero only
 * because the registration clears the whole record first, so a negative return from a record
 * that was never fully cleared would free whatever was left there.
 *
 * Zero. Always zero.
 */
#include "mp_task.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_task_state {
    uintptr_t frame_counter;
    uint32_t  ticks;
    uint32_t  frames_with_ticks;
    uint32_t  min_per_frame;
    uint32_t  max_per_frame;
    uint32_t  current_frame;
    uint32_t  in_this_frame;
    bool      have_frame;
} mp_task_state_t;

static mp_task_state_t task_census;

/* A layer above this one may ride the substep without this file knowing what it does. The task
 * stays the owner of the return contract; the client returns nothing and cannot change it. */
static void (*tick_client)(void);

void mp_task_set_frame_counter(uintptr_t cell)
{
    task_census.frame_counter = cell;
}

void mp_task_set_tick_client(void (*client)(void))
{
    tick_client = client;
}

uint32_t mp_task_ticks(void)             { return task_census.ticks; }
uint32_t mp_task_frames_with_ticks(void) { return task_census.frames_with_ticks; }
uint32_t mp_task_min_per_frame(void)     { return task_census.min_per_frame; }
uint32_t mp_task_max_per_frame(void)     { return task_census.max_per_frame; }

/* A frame's count is only complete once the frame has changed, so the closing happens on the first
 * tick of the NEXT frame. The last frame of a run is therefore never closed, which costs one
 * sample out of thousands and keeps the counting free of a second trigger. A frame with no tick
 * is not a zero: one field run saw its first tick on frame 832 of 4934, and counting the 831
 * before it as zeroes would put the minimum at zero and mean nothing. */
static void close_frame(void)
{
    if (task_census.in_this_frame == 0) {
        return;
    }
    ++task_census.frames_with_ticks;
    if (task_census.min_per_frame == 0 || task_census.in_this_frame < task_census.min_per_frame) {
        task_census.min_per_frame = task_census.in_this_frame;
    }
    if (task_census.in_this_frame > task_census.max_per_frame) {
        task_census.max_per_frame = task_census.in_this_frame;
    }
    task_census.in_this_frame = 0;
}

int32_t __cdecl mp_task_tick(int32_t arg)
{
    uint32_t frame = 0;

    (void)arg;
    ++task_census.ticks;

    if (task_census.frame_counter != 0 && memory_try_read_u32(task_census.frame_counter, &frame)) {
        if (!task_census.have_frame) {
            task_census.have_frame    = true;
            task_census.current_frame = frame;
        } else if (frame != task_census.current_frame) {
            close_frame();
            task_census.current_frame = frame;
        }
        ++task_census.in_this_frame;
    }

    if (tick_client != NULL) {
        tick_client();
    }

    return 0;
}

void mp_task_report(const char *why)
{
    if (task_census.ticks == 0) {
        log_warning("substep task at %s: never ticked, although it holds a slot", why);
        return;
    }

    if (task_census.frame_counter == 0) {
        log_info("substep task at %s: %u ticks; the frame counter is unknown, so nothing can be "
                 "said about how they are spread", why, (unsigned)task_census.ticks);
        return;
    }

    /* Per CLOCK TICK, not per presented frame. The engine's clock and the screen are not the same
     * rate, measured 30 against 119 in the field, and saying "per frame" here would invite the
     * reader to divide by the wrong number. The clock is the right reference for this one anyway:
     * both it and the substep ladder are on the simulation side. */
    log_info("substep task at %s: %u ticks over %u clock ticks that had any, %u to %u per clock "
             "tick", why, (unsigned)task_census.ticks, (unsigned)task_census.frames_with_ticks,
             (unsigned)task_census.min_per_frame, (unsigned)task_census.max_per_frame);
}
