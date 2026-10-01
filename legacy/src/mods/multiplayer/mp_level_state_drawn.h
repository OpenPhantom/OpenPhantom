/* mp_level_state_drawn.h: the fog as this side's device draws it, read after a fog command.
 *
 * Layer 2, a measurement and nothing else. The fog travels as the script's values and each side
 * plays them through its own director, so whether a client's screen shows what the host's does was
 * never seen by any line: the level's state said what was replayed, not what was drawn. This reads
 * the cells the device's fog is programmed from, right after a command and a second later, and
 * writes one line for each of the first sixteen fog commands a side plays in a session.
 *
 * WHICH CELLS depends on the regime. With the device's table fog the band is the pair the fog
 * functions write; with the engine's own per vertex ramp (what the view distance fix switches to)
 * the band is the level's, read every frame out of the world, and a script's start and end are not
 * seen at all. So the line names the regime, reads the band from the cells that regime draws from,
 * and reads the colour and the level's fog bit in both. Two machines may differ in the band (the
 * view distance fix scales it by each machine's own view) and must agree in colour and fog bit.
 *
 * Every cell is found through the director's own arms, the ramp's function and the one site that
 * sets the vertex flag, and proved as the engine shipped it; a side where any of that fails writes
 * that it could not read rather than numbers.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_DRAWN_H
#define MULTIPLAYER_MP_LEVEL_STATE_DRAWN_H

#include <stdbool.h>
#include <stdint.h>

/* Whose command it was, for the line. */
#define MP_LEVEL_DRAWN_OWN      "this side's own"
#define MP_LEVEL_DRAWN_REPLAYED "a replayed"
#define MP_LEVEL_DRAWN_HEALING  "a healing"
#define MP_LEVEL_DRAWN_VIEWER   "the viewer's"

/* A fog command this side plays or lets its engine play. `sequence` is its journal number, 0 for
 * one that has none. */
void mp_level_state_drawn_note(const char *whose, int32_t command, uint16_t sequence);

/* Once a substep of a session: the readings that are due, and the lines that are whole. */
void mp_level_state_drawn_tick(void);

void mp_level_state_drawn_report(bool host);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_DRAWN_H */
