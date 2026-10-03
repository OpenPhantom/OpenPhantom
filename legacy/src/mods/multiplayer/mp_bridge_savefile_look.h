/* mp_bridge_savefile_look.h: the look at the host's savegame before a client restores it, and
 * what the transfer hands that look.
 *
 * Layer 3, the same as the transfer it belongs to. The question itself, mp_bridge_savefile_look,
 * is declared with the rest of the transfer in mp_bridge_savefile.h, because that is where its
 * callers look for it. What is here is the three things the look needs from the transfer's own
 * state and the look's line in the report.
 *
 * It is a file of its own because the transfer's file is long, and this half touches neither the
 * wire nor the host's end: it reads the bytes the client put together, or the disk, and may ask
 * for the file to be written again.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_SAVEFILE_LOOK_H
#define MULTIPLAYER_MP_BRIDGE_SAVEFILE_LOOK_H

#include "mp_savefile.h"

#include <stdint.h>

/* The client's assembly: whether it is whole, which file it is, and its bytes. Never NULL. */
const mp_savefile_assembly_t *mp_bridge_savefile_assembly(void);

/* The clock the transfer was last ticked with. */
uint32_t mp_bridge_savefile_now_ms(void);

/* Forgets that the file was written, so mp_bridge_savefile_ready answers no and the next tick
 * writes the file again from a whole assembly, or asks the host for its slices again. */
void mp_bridge_savefile_forget_the_write(void);

void mp_bridge_savefile_look_report(void);

#endif /* MULTIPLAYER_MP_BRIDGE_SAVEFILE_LOOK_H */
