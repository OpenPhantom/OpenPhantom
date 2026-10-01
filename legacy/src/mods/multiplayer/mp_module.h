/* mp_module.h: the two module procedures, and the census they take.
 *
 * The foothold owns two nodes in the engine's module registry, one at each end of the list, and
 * every broadcast reaches both. What they do with that is measure, because the measurement is the
 * thing the layers above cannot get any other way:
 *
 *   which messages arrive at all, and how often;
 *   how many times each arrives within one frame, which is what separates a per frame message from
 *     a per substep one without anybody having to assert which is which;
 *   and whether the node at the head really is entered LAST, which is the whole reason one of the
 *     two was moved there and is not provable from a count.
 */
#ifndef MULTIPLAYER_MP_MODULE_H
#define MULTIPLAYER_MP_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Message numbers seen in this engine run from 1 to 0x19. Anything outside the table is counted
 * separately rather than dropped: a number nobody has seen before is a finding. */
#define MP_MESSAGE_SLOTS 0x20u

typedef struct mp_message_row {
    uint32_t tail_arrivals;     /* at the node the registry appended */
    uint32_t head_arrivals;     /* at the node that was moved to the front */
    uint32_t max_per_present;   /* the most arrivals at the tail inside one presented frame */
    uint32_t out_of_order;      /* entered in the wrong order for this message's walk direction */
    uint32_t nested;            /* walks begun while one of the same message was still open */
    uint32_t first_frame;       /* in engine clock ticks, which are NOT presented frames */
    uint32_t last_frame;
} mp_message_row_t;

/* The two procedures, in the shape module_install calls them: cdecl, three dword arguments, and a
 * return value that only matters for the install message. */
uint32_t __cdecl mp_module_tail_proc(int msg, int arg, float dt);
uint32_t __cdecl mp_module_head_proc(int msg, int arg, float dt);

/* The engine's own clock cell, which the census stamps arrivals with so that a message can be
 * placed in time. It is NOT the presented frame: measured in the field it runs at a fixed 30 Hz
 * while the screen ran at 119, so a maximum counted against it would say four where the truth is
 * one. Without the cell the span column reads 0..0 and the report says so. The cell is the tick
 * counter at 0x00868710, and the ratio between it and the presented frames is itself a reading:
 * 1469 ticks over 4879 frames in one level, 3.3 to one, with the substep ladder at 32 a second
 * from the inside. Why that clock runs at 30 Hz while the broadcasts follow the screen is not
 * read; the frame rate fix was loaded in the measured run and is the obvious suspect, no more. */
void mp_module_set_frame_counter(uintptr_t cell);

/* Called once per presented frame, by whoever owns the frame hook. This is what the per present
 * maximum counts against, and it is the column that separates a message sent once per drawn frame
 * from one sent per simulation step. */
void mp_module_note_present(void);

const mp_message_row_t *mp_module_row(unsigned message);
uint32_t                mp_module_total_arrivals(void);
uint32_t                mp_module_out_of_range(void);

/* The ordering faults so far: last nodes entered with no walk open, and walks still open. */
uint32_t                mp_module_faults(void);

/* One line per message that has been seen, plus a verdict on the ordering. `why` names the moment,
 * because the same census at a level end and at a quit are different measurements. */
void mp_module_report(const char *why);

#endif /* MULTIPLAYER_MP_MODULE_H */
