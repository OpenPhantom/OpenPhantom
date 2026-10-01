/* mp_start_restore.h: what the shipped LOAD arm does after a restore, and the hull left out.
 *
 * The retail load screen restores, then calls swmenu_setCloseKeepsMode(1), then answers 0. With
 * the flag up, swmenu_close leaves the input mode alone and broadcasts message 0x18,
 * RESTORE_DONE, which is the only broadcast of that message in the image: it parks the restored
 * actors' interpolation, reapplies the fog and the music, and stands the camera up from the
 * player. The lobby's hull restored and answered 0 without the call, so none of that ran after a
 * savegame chosen in the lobby. Byte proven against the reconstruction of swmenu_close and the
 * retail arm at 00440D18.
 *
 * The setter is thirteen bytes whose only identity is its operand, the flag's own address, so it
 * cannot stand in the signature table, whose test refuses an absolute address in a pattern. It is
 * found at a fixed distance behind swmenu_close instead, which is resolved already, and proven
 * there with the operand masked. Layer 2, beside mp_start. */
#ifndef MP_START_RESTORE_H
#define MP_START_RESTORE_H

#include <stdbool.h>

/* Resolves the setter behind swmenu_close. False, with one warning, when it is not there. */
bool mp_start_restore_install(void);

/* Asks the menu to keep the input mode on the close that empties its stack, which is what makes
 * that close say RESTORE_DONE. An open in between clears the request again; in the sequence this
 * serves, the next close is the title screen's. Does nothing when the setter did not resolve. */
void mp_start_restore_keep_mode(void);

#endif
