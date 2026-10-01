/* mp_world_values.h: the values on this machine that decide the shared world, read and said.
 *
 * Layer 2. Nothing here changes a value. It reads the cells and the game data and prints them, so
 * two machines' logs can be laid side by side before anything is taken over from a host.
 */
#ifndef MULTIPLAYER_MP_WORLD_VALUES_H
#define MULTIPLAYER_MP_WORLD_VALUES_H

#include <stdbool.h>
#include <stdint.h>

/* The substep the session counts in, in seconds: the 1/32 immediate the rate choice at 0x00475737
 * writes into the frame delta cell. The 60fps cheat writes 1/64 instead unless framerate_fix pins
 * it. */
#define MP_WORLD_VALUES_SUBSTEP_SECONDS 0.03125f

/* Whether a substep of `seconds` is the session's own length. Exact, because the engine writes one
 * of two float immediates and never computes it. */
bool mp_world_values_substep_is_standard(float seconds);

/* The damage table as this machine holds it now, and whether shot_init has run: `seen` is true
 * when any row holds its actor template. False when the table did not resolve or read. */
bool mp_world_values_damage_table(uint32_t *hash, bool *seen);

/* The world settings and the game data, said at the top of every level begin (message 5), before
 * the arena and the spawn points read them. */
void mp_world_values_note_level_begin(void);

/* The game data before any level, from the frame begin: said at the first frame, and again once
 * the shot table shows that shot_init has run, then no more. */
void mp_world_values_note_frame_begin(void);

/* One substep: the frame delta cell measured against the session's length. */
void mp_world_values_note_substep(void);

void mp_world_values_report(void);

#endif /* MULTIPLAYER_MP_WORLD_VALUES_H */
