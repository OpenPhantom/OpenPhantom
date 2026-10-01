/* mp_start_phase.h: where a start the lobby asked for stands in the title menu, as one value.
 *
 * Layer 1, no engine. mp_start.c drives the title menu one navigation code a frame; what it does
 * on a frame is decided here, out of the phase, the kind and whether the title's focus held.
 *
 * The kind and a step used to be two fields, and "pending" was the one or the other. A map's
 * start cleared the kind at the accept and left the step standing, and a guard that tested the
 * kind first never put the step down: the start stayed pending for good, and the key that opens
 * the multiplayer menu did nothing again. One phase now answers "pending" alone, and the kind
 * stays beside it for what the widget and the load screen need to know.
 */
#ifndef MULTIPLAYER_MP_START_PHASE_H
#define MULTIPLAYER_MP_START_PHASE_H

#include "mp_start.h"

#include <stdbool.h>
#include <stdint.h>

/* How many times the drive may run its focus-then-accept cycle before it gives up. It takes one
 * round to work; three is one spare plus the round that reports the failure. Without a bound a
 * savegame whose load screen never came up pressed an accept into the open menu for ever. */
#define MP_START_DRIVE_ROUNDS 3u

/* How many frames the drive may find the title's focus on some other widget before it gives up.
 * The title menu moves its focus to whatever selectable widget the mouse pointer moves over (its
 * loop restores the last focus every frame, and the hit test moves it on a mouse move), so a
 * hand on the mouse takes the focus back the frame after the drive set it; the accept then
 * activates whatever the pointer is on. One field run pressed three accepts into the wrong
 * widget in nine frames and reported that the load screen never came up. Three seconds is long
 * enough for a hand to move away and short enough not to read as a hang. */
#define MP_START_FOCUS_MISSES_MAX 300u

typedef enum mp_start_phase {
    MP_START_PHASE_IDLE = 0,     /* nothing asked, or what was asked is done */
    MP_START_PHASE_ASKED,        /* asked; the focus goes on the next frame */
    MP_START_PHASE_FOCUS_SET,    /* the focus is set; the accept goes on a frame it holds */
    MP_START_PHASE_ACCEPT_SENT   /* the accept went; a savegame waits for the load screen */
} mp_start_phase_t;

/* What the drive does with the frame. */
typedef enum mp_start_action {
    MP_START_ACT_PASS = 0,   /* hand the frame's code on untouched */
    MP_START_ACT_FOCUS,      /* set the focus on the start widget and swallow the code */
    MP_START_ACT_ACCEPT,     /* press accept on it */
    MP_START_ACT_GIVE_UP     /* the request is gone; hand the code on */
} mp_start_action_t;

typedef struct mp_start_machine {
    mp_start_kind_t  kind;
    mp_start_phase_t phase;
    uint32_t         rounds;         /* cycles whose accept did not reach the load screen */
    uint32_t         focus_misses;   /* frames the focus was found elsewhere, this request */
} mp_start_machine_t;

/* A request of `kind`, from the first phase, with both counts at nought. */
void mp_start_machine_ask(mp_start_machine_t *machine, mp_start_kind_t kind);

/* Whether the frame about to be driven reads the title's focus: only the one that would accept. */
bool mp_start_machine_wants_focus(const mp_start_machine_t *machine);

/* One frame. `focus_holds` is whether the focus is on the start widget now, and is read only on
 * the frame that would accept. A give up has cancelled the request by the time it is answered. */
mp_start_action_t mp_start_machine_drive(mp_start_machine_t *machine, bool focus_holds);

/* The load screen took the savegame: the kind goes, and with it a request that had not reached
 * its first focus; a later phase the next frame puts down. */
void mp_start_machine_consume(mp_start_machine_t *machine);

/* Nothing asked, from any phase. The focus misses stay for the report of the request they
 * belonged to. */
void mp_start_machine_cancel(mp_start_machine_t *machine);

bool mp_start_machine_pending(const mp_start_machine_t *machine);

#endif /* MULTIPLAYER_MP_START_PHASE_H */
