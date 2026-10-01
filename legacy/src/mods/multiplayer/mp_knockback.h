/* mp_knockback.h: what the contacts with code 0x22 did on the host, counted.
 *
 * Layer 2. It reads the contact code, the wave's side and the actor, and changes nothing: the
 * hull on the enemy contact handler asks it before and after the engine's own call, and the hit
 * relay tells it which reports the trust rule refused for a wave's code. What the numbers mean is
 * decided in mp_knockback_rule.h; the line is printed in the hit relay's report.
 */
#ifndef MULTIPLAYER_MP_KNOCKBACK_H
#define MULTIPLAYER_MP_KNOCKBACK_H

#include "mp_knockback_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What one contact looked like before the handler ran. Filled by the call before, read by the
 * call after, and nothing else keeps it. */
typedef struct mp_knockback_watch {
    bool                 counted;        /* a wave's first touch on an actor */
    bool                 unread;         /* and the actor did not read before the call */
    uintptr_t            actor;
    uint8_t              bank;           /* 0 this machine's own wave, 1 and up a far player's */
    int32_t              state_before;
    mp_knockback_touch_t touch;
} mp_knockback_watch_t;

/* Before the host's handler runs: `victim` is the actor the contact names or 0, `sender` the
 * object that sent it, `code_cell` the cell the contact code is in. A contact with any other code
 * leaves the watch empty and costs one read. */
void mp_knockback_before(mp_knockback_watch_t *watch, uintptr_t victim, uint32_t sender,
                         uintptr_t code_cell);

/* After it: holds the state the handler left against what the watch expected, and counts. */
void mp_knockback_after(const mp_knockback_watch_t *watch);

/* The host refused a report with a wave's code from far player `bank` on `key`. */
void mp_knockback_note_refused(uint32_t key, size_t bank);

/* The two lines of the hit relay's report. */
void mp_knockback_report(void);

#endif /* MULTIPLAYER_MP_KNOCKBACK_H */
