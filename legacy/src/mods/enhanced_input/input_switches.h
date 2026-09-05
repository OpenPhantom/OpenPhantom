#ifndef INPUT_SWITCHES_H
#define INPUT_SWITCHES_H

/* The two settings of this DLL that can change while the game is running, Strafe and FreeLook:
 * the setters the controls screen calls, the availability queries it asks before offering a box,
 * and the once-a-second re-read that picks up an edit made from anywhere else.
 *
 * Split out of enhanced_input.c along the seam that file's own size note had named: none of this
 * patches a byte, reads a player record or runs on a substep. What is left on the other side is
 * the two phase thunks, the sites they depend on and the order installation has to come up in.
 *
 * The setters themselves are declared in enhanced_input.h, because they are what the rest of the
 * module already calls them by. This header carries only the installation of the re-read.
 */

/* Registers the per-frame poll that re-reads Strafe and FreeLook from the ini.
 *
 * Call it AFTER the controls screen has been patched: that screen is the other writer of these two
 * keys, and this only makes an edit from somewhere else arrive sooner. Losing the hook costs a
 * restart, which is what these settings did before, so it warns rather than refusing anything. */
void input_switches_install(void);

#endif /* INPUT_SWITCHES_H */
