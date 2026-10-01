/* mp_module.c: what the two module nodes do with every message they get.
 *
 * They count. That sounds thin for a file of its own, and the reason it is not is the ordering
 * column: whether the node at the head of the registry is entered in the position the relink was
 * supposed to put it in.
 *
 * ================================ Two directions, not one =====================================
 *
 * The first version of this census got that wrong and reported it as an engine fault, which is
 * worth keeping in the file. Most broadcasts walk the list BACKWARDS
 * from the tail, so the appended node hears them first and the relinked node last. But messages 4
 * and 6, the quit and the level end, walk FORWARD, and for those two the relinked node is entered
 * FIRST and that is correct. A guard that expects one direction for everything reports two faults
 * per session on a healthy build, and a real fault on a per frame message would then be one line
 * among three that all say the same thing.
 *
 * ============================== Frames are not clock ticks ====================================
 *
 * The other thing the first version got wrong. It counted arrivals against the engine's own clock
 * cell and called the column "per frame". Measured in the field that clock runs at a fixed 30 Hz
 * while the screen ran at 119, so a message sent once per drawn frame showed a maximum of four and
 * looked like a simulation step message. The per present column is now counted against the frame
 * hook, which is one call per presented frame, and the clock is kept only for placing a message in
 * time.
 */
#include "mp_module.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODULE_MSG_INSTALL   0x01
#define MODULE_MSG_SHUTDOWN  0x04
#define MODULE_MSG_LEVEL_END 0x06
#define MODULE_MSG_SAVE      0x0A
/* The install reads the answer to message 1 and sets the node's started bit when it is 0 or 2,
 * clears it when it is 1; messages 2, 3, 4, 8 and 9 skip a node whose bit is clear. Every other
 * answer is 2, the value the engine's own task manager procedure returns for "not handled". */
#define MODULE_ANSWER_OK     0u    /* sets the node's started bit */
#define MODULE_ANSWER_PASS   2u    /* the house value for "not handled" */

/* The two messages the registry sends from the head of the list towards the tail. Everything else
 * goes the other way. Read from the registry's own branch on the message number: the timed
 * broadcast at 0x0046F4A9 (messages 0x0C, 0x0D, 0x0E, 0x11 and 0x15) walks tail to head by the
 * previous pointer, the plain broadcast at 0x0046F3C3 walks the same way for everything except 4
 * and 6, for which it walks head to tail by the next pointer, and a targeted send goes through
 * the lookup at 0x0046F529, head to tail. */
static bool walks_forward(unsigned message)
{
    /* And the save: module_broadcastSave, 0x0046F180, walks head to tail as well. */
    return message == MODULE_MSG_SHUTDOWN || message == MODULE_MSG_LEVEL_END ||
           message == MODULE_MSG_SAVE;
}

typedef struct mp_module_state {
    uintptr_t        frame_counter;
    uint32_t         presents;
    mp_message_row_t rows[MP_MESSAGE_SLOTS];
    /* Walks of this message begun at the node that is entered FIRST and not yet ended at the
     * other one. Which node that is depends on the walk direction. */
    uint32_t         open[MP_MESSAGE_SLOTS];
    bool             seen[MP_MESSAGE_SLOTS];
    uint32_t         present_of[MP_MESSAGE_SLOTS];
    uint32_t         in_this_present[MP_MESSAGE_SLOTS];
    uint32_t         total_arrivals;
    uint32_t         out_of_range;
} mp_module_state_t;

static mp_module_state_t module_census;

void mp_module_set_frame_counter(uintptr_t cell)
{
    module_census.frame_counter = cell;
}

void mp_module_note_present(void)
{
    ++module_census.presents;
}

const mp_message_row_t *mp_module_row(unsigned message)
{
    if (message >= MP_MESSAGE_SLOTS) {
        return NULL;
    }
    return &module_census.rows[message];
}

uint32_t mp_module_total_arrivals(void) { return module_census.total_arrivals; }
uint32_t mp_module_out_of_range(void)   { return module_census.out_of_range; }

uint32_t mp_module_faults(void)
{
    uint32_t faults = 0;
    unsigned message;

    for (message = 0; message < MP_MESSAGE_SLOTS; ++message) {
        faults += module_census.rows[message].out_of_order + module_census.open[message];
    }
    return faults;
}

static uint32_t clock_now(void)
{
    uint32_t value = 0;

    if (module_census.frame_counter == 0 ||
        !memory_try_read_u32(module_census.frame_counter, &value)) {
        return 0;
    }
    return value;
}

/* The node the walk should reach FIRST for this message. It records the arrival and opens a
 * walk. A walk already open is a NESTED one, not a fault: a message box pumps frames, and a box
 * opened inside the 0x15 that commits a save broadcasts 0x15 again, once a frame, inside the
 * outer one; the engine's own box for a full disk does it, and so does a refused save. The inner
 * walk ends before the outer one does. A walk that never reaches the other end stays open, and
 * the report counts it. Message 1 is sent to each node as it is linked, the appended one first,
 * so it opens and closes like a backward walk. */
static void note_first(unsigned message)
{
    mp_message_row_t *row     = &module_census.rows[message];
    uint32_t          present = module_census.presents;

    /* A separate flag rather than a test on the arrival counts. The counts are incremented by the
     * caller before this runs, so a "have I seen this before" written as `count == 0` is never true
     * and the first clock value is never recorded. That is not hypothetical: the column read 0 for
     * every message in the first run that used it, and the message whose only arrival was at clock
     * 116 reported the span 0..116, which is what gave it away. */
    if (!module_census.seen[message]) {
        module_census.seen[message] = true;
        row->first_frame = clock_now();
    }
    row->last_frame = clock_now();

    if (module_census.open[message] != 0u) {
        ++row->nested;
    }
    ++module_census.open[message];

    if (module_census.present_of[message] != present || row->max_per_present == 0) {
        module_census.present_of[message]      = present;
        module_census.in_this_present[message] = 1;
    } else {
        ++module_census.in_this_present[message];
    }
    if (module_census.in_this_present[message] > row->max_per_present) {
        row->max_per_present = module_census.in_this_present[message];
    }
}

/* The node the walk should reach LAST. Finding the flag clear means the walk arrived here before
 * it arrived at the other end, which is the failure the relink exists to prevent. */
static void note_last(unsigned message)
{
    if (module_census.open[message] == 0u) {
        ++module_census.rows[message].out_of_order;
    } else {
        --module_census.open[message];
    }
}

static uint32_t enter(int msg, bool is_head)
{
    unsigned message = (unsigned)msg;

    ++module_census.total_arrivals;

    if (message >= MP_MESSAGE_SLOTS) {
        ++module_census.out_of_range;
        return (msg == MODULE_MSG_INSTALL) ? MODULE_ANSWER_OK : MODULE_ANSWER_PASS;
    }

    if (is_head) {
        ++module_census.rows[message].head_arrivals;
    } else {
        ++module_census.rows[message].tail_arrivals;
    }

    /* The head node is entered first exactly when the message walks forward. */
    if (is_head == walks_forward(message)) {
        note_first(message);
    } else {
        note_last(message);
    }

    return (msg == MODULE_MSG_INSTALL) ? MODULE_ANSWER_OK : MODULE_ANSWER_PASS;
}

/* The procedure's shape is the install's call site, at 0x0046EE22:
 *
 *     0046EE22  6A 00 6A 00 6A 01     push 0 / push 0 / push 1
 *     0046EE2B  8B 4D F8 FF 51 10     call [node+0x10]
 *     0046EE31  83 C4 0C              add esp, 0xC
 *
 * Three dword arguments pushed right to left, so the first is the message; the caller cleans the
 * stack, so cdecl; the handler lives at +0x10 of the node. The third argument is a float in the
 * timed broadcast and a zero dword here, which is the bit pattern of 0.0f either way. */
uint32_t __cdecl mp_module_tail_proc(int msg, int arg, float dt)
{
    (void)arg;
    (void)dt;
    return enter(msg, false);
}

uint32_t __cdecl mp_module_head_proc(int msg, int arg, float dt)
{
    (void)arg;
    (void)dt;
    return enter(msg, true);
}

/* What a healthy run reads like, one level loaded and left: 4879 presented frames, message 0x0C
 * arriving 4879 times with a maximum of one per present, which is once per drawn frame exactly,
 * and 0x0E reaching four in a single frame, which is the simulation catching up; 0x0D and 0x11 at
 * 4826, so 53 drawn frames did not carry them, and 0x15 at 4880, one more than there were frames.
 * Twelve messages at a level end, thirteen at the quit, and 8 and 9 (the AI suspend and resume)
 * appear only in a run that suspends, so a list of twelve is not the message space. With a third
 * node carrying the tail procedure installed on purpose, the tail column reads exactly twice the
 * head column on every backward message and every one of them is flagged, 14304 faults in one
 * level: the guard has been watched failing, which is the only kind worth its line. */
void mp_module_report(const char *why)
{
    unsigned message;
    uint32_t faults        = mp_module_faults();
    uint32_t messages_seen = 0;

    log_info("module census at %s: %u arrivals over both nodes, %u presented frames", why,
             (unsigned)module_census.total_arrivals, (unsigned)module_census.presents);

    if (module_census.frame_counter == 0) {
        log_warning("  the engine clock is unknown, so the span column below reads 0..0");
    }

    for (message = 0; message < MP_MESSAGE_SLOTS; ++message) {
        const mp_message_row_t *row = &module_census.rows[message];

        if (row->tail_arrivals == 0 && row->head_arrivals == 0) {
            continue;
        }
        ++messages_seen;

        log_info("  msg %02X  %-8s tail %6u  head %6u  max/present %2u  clock %u..%u%s%s",
                 message, walks_forward(message) ? "forward" : "backward",
                 (unsigned)row->tail_arrivals, (unsigned)row->head_arrivals,
                 (unsigned)row->max_per_present,
                 (unsigned)row->first_frame, (unsigned)row->last_frame,
                 (row->nested != 0) ? "  nested" : "",
                 (row->out_of_order != 0 || module_census.open[message] != 0)
                     ? "  OUT OF ORDER" : "");
    }

    log_info("  %u distinct messages, %u out of range", (unsigned)messages_seen,
             (unsigned)module_census.out_of_range);

    /* The engine clock against the presented frame. They are not the same thing and the difference
     * is worth a line of its own: it decides how every other number here is read. */
    if (module_census.frame_counter != 0 && module_census.presents != 0) {
        uint32_t ticks = clock_now();

        log_info("  the engine clock advanced %u times over %u presented frames",
                 (unsigned)ticks, (unsigned)module_census.presents);
    }

    if (faults == 0) {
        log_info("  every message was entered at the two nodes in its own walk's order");
    } else {
        log_error("  %u ordering faults: a broadcast reached one of our nodes out of turn",
                  (unsigned)faults);
    }
}
