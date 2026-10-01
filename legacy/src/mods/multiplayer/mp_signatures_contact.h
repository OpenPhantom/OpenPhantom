/* mp_signatures_contact.h: the engine's own delivery of a contact to a task node.
 *
 * Every contact the engine delivers goes through one function: it takes the receiver's handler
 * node, refuses a node whose contact slot is empty, makes that node the task being run while its
 * handler runs, and puts the previous one back. A hit this machine is told about by the host goes
 * the same way, so that a corpse, whose slot the engine emptied, cannot be hurt again and the
 * death entry empties the slot of the player rather than of whatever task was running.
 *
 * A table of its own because the main table's file is at its size limit, the way the pause and the
 * world probes are arranged. The function is only called, never hulled, so the site declares no
 * prologue and claims no write range.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_CONTACT_H
#define MULTIPLAYER_MP_SIGNATURES_CONTACT_H

#include <stddef.h>
#include <stdint.h>

typedef enum mp_contact_site {
    MP_CONTACT_SITE_TASK_RUN,       /* 0x00475953  task_run: the delivery, with its empty slot
                                     * gate */
    MP_CONTACT_SITE_COUNT
} mp_contact_site_t;

/* Resolves the table once, with a line, and answers the same thing afterwards: how many of its
 * sites resolved. Safe to call from anywhere that wants an address, which keeps the table out of
 * every caller's installation order. */
size_t mp_signatures_contact_resolve(void);

/* 0 when the site did not resolve. */
uintptr_t mp_signatures_contact_address(mp_contact_site_t site);

#endif /* MULTIPLAYER_MP_SIGNATURES_CONTACT_H */
