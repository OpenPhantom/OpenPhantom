/* mp_module.c: the module census, which only counts, and the verdict it reaches on the order.
 *
 * What would be silent if it were wrong: a healthy save reported as an ordering fault at every
 * level end, which is what a census that took the save for a backward walk reports; a message
 * box inside a save, which the engine opens for a full disk and the developer panel for a refused
 * save, reported as two faults; and, the other way round, a real fault that no longer shows.
 */
#include "unittest.h"

#include "mp_module.h"

#include <stdint.h>

#define MSG_SHUTDOWN   0x04
#define MSG_LEVEL_END  0x06
#define MSG_SAVE       0x0A
#define MSG_FRAME      0x0C
#define MSG_DRAW       0x15

/* A walk as the registry sends it: the appended node first for a backward message, the relinked
 * one first for a forward message. */
static void backward(int msg)
{
    (void)mp_module_tail_proc(msg, 0, 0.0f);
    (void)mp_module_head_proc(msg, 0, 0.0f);
}

static void forward(int msg)
{
    (void)mp_module_head_proc(msg, 0, 0.0f);
    (void)mp_module_tail_proc(msg, 0, 0.0f);
}

int main(void)
{
    uint32_t faults;

    ut_section("walks in their own order are not faults");
    backward(MSG_FRAME);
    backward(MSG_FRAME);
    forward(MSG_SHUTDOWN);
    forward(MSG_LEVEL_END);
    forward(MSG_SAVE);
    forward(MSG_SAVE);
    ut_checkf(mp_module_faults() == 0u,
              "frames walk backward, the quit, the level end and the save walk forward (%u)",
              (unsigned)mp_module_faults());
    ut_check(mp_module_row(MSG_SAVE)->out_of_order == 0u,
             "the save walks head to tail, as module_broadcastSave does");

    ut_section("a walk inside a walk of the same message");
    (void)mp_module_tail_proc(MSG_DRAW, 0, 0.0f);      /* the frame that commits the save */
    (void)mp_module_tail_proc(MSG_DRAW, 0, 0.0f);      /* a frame the message box pumps */
    (void)mp_module_head_proc(MSG_DRAW, 0, 0.0f);
    (void)mp_module_head_proc(MSG_DRAW, 0, 0.0f);
    ut_checkf(mp_module_faults() == 0u && mp_module_row(MSG_DRAW)->nested == 1u,
              "is nested, counted, and no fault (%u faults, %u nested)",
              (unsigned)mp_module_faults(), (unsigned)mp_module_row(MSG_DRAW)->nested);

    ut_section("the faults still show");
    faults = mp_module_faults();
    forward(MSG_FRAME);
    ut_check(mp_module_faults() > faults,
             "a backward message entered at the head first is a fault");
    faults = mp_module_faults();
    (void)mp_module_tail_proc(MSG_FRAME, 0, 0.0f);
    ut_check(mp_module_faults() == faults + 1u,
             "and a walk that never reached the other end is one while it stays open");
    (void)mp_module_head_proc(MSG_FRAME, 0, 0.0f);
    ut_check(mp_module_faults() == faults, "until it does");
    faults = mp_module_faults();
    (void)mp_module_tail_proc(MSG_FRAME, 0, 0.0f);
    (void)mp_module_tail_proc(MSG_FRAME, 0, 0.0f);
    (void)mp_module_head_proc(MSG_FRAME, 0, 0.0f);
    ut_check(mp_module_faults() == faults + 1u,
             "a second node carrying the appended procedure leaves a walk open every time");

    ut_section("the report runs");
    mp_module_report("the test");
    ut_check(true, "reporting is not a fault");

    return ut_summary("mp_module");
}
