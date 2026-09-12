/* diag_footsteps.h: where the wet footprints come from.
 *
 * The engine keeps one timestamp per body, thing+0x108, written by the footstep tick whenever the
 * polygon under the foot carries floor material 12, 13 or 14 (shallow water, swamp, the water
 * surface plane), and the print pass lays a wet footprint on any other material for eight seconds
 * after that stamp. Wet prints in the Mos Espa desert, where none of those materials should be
 * underfoot, are therefore one of two things: a polygon authored with a wet material nibble, or a
 * floor pointer that was not pointing at a polygon for a tick and read a random nibble.
 *
 * This observer reports the stamp as it happens, with the polygon, its surface word and the
 * position, and reports the first print of each wet spell with the material it was laid on and the
 * age of the stamp. Read only; the tick runs as it always did.
 */
#ifndef DIAG_FOOTSTEPS_H
#define DIAG_FOOTSTEPS_H

/* `level` of 0 installs nothing. Returns the number of observers that went live, 0 or 1. */
int diag_footsteps_install(int level);

#endif /* DIAG_FOOTSTEPS_H */
