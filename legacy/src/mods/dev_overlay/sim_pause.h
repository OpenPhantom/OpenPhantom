/* sim_pause.h: stop the simulation while the panel is open, using the engine's own pause flag.
 *
 * WHY THIS EXISTS SEPARATELY FROM input_freeze.c. That file answers the two functions the game
 * reads input through with "nothing pressed", which stops the player taking orders. It does not
 * stop the world: NPCs keep walking, movers keep moving and timers keep running behind the panel,
 * which is exactly what a player notices when they open the overlay mid fight.
 *
 * WHAT THE ENGINE ALREADY DOES. sys_frame gates its own simulation step on a flag:
 *
 *     0043EA13  83 3D <g_disabled> 00   cmp  dword ptr [DAT_00881344],0
 *     0043EA1A  75 13                   jnz  past the step
 *     0043EA1C  83 3D <g_paused>   00   cmp  dword ptr [g_paused],0
 *     0043EA23  75 0A                   jnz  past the step
 *     0043EA25  6A 00                   push 0
 *     0043EA27  E8 <rel32>              call sys_runSubsteps
 *
 * Non-zero in that second flag skips the substep loop entirely, which is the whole simulation. The
 * engine's own pause menu sets it, and render_frameEnd still runs below the gate, so the picture
 * keeps being drawn and the panel keeps being visible. Nothing has to be hooked: this is a flag the
 * frame function reads for itself every frame, and writing it is all a pause needs.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO. gameplay_open_pause_menu also broadcasts task command 8 on
 * the way in and 9 on the way out, which is how it pauses audio as well. This does not, for a
 * specific reason: the pause broadcast only marks a task paused when its handler returns 0, and
 * iMUSE's handler returns 2, so the mark is never set and the matching resume never fires ImResume.
 * Borrowing that pair would risk leaving music stopped with nothing to start it again. Music and
 * sound therefore keep playing behind the panel, which is a smaller wrong than silence that does
 * not come back.
 *
 * The previous value is remembered and restored rather than cleared to zero, so opening the panel
 * while the game is already paused for its own reasons cannot un-pause it on the way out.
 *
 * WHY THERE ARE HOLDERS RATHER THAN A SINGLE FLAG. Two features in this DLL want the simulation
 * stopped and they can be on at the same time: the panel while it is open, and the free camera
 * for as long as it is flying. The free camera used to write the cell itself, which is how the
 * two of them broke each other. Turn the free camera on, open the panel so this remembers a 1,
 * turn the free camera off inside the panel so the cell goes to 0, then close the panel: the 1 is
 * put back and the game is frozen with nothing holding it and nothing on screen to say why. The
 * mirror of that sequence cancels the free camera pause while it is still flying.
 *
 * So the cell has exactly one writer now, and callers say who they are. The value underneath is
 * captured when the FIRST holder takes it and put back when the LAST one lets go, which keeps the
 * original promise above and makes it hold for any number of holders rather than only one.
 */
#ifndef DEV_OVERLAY_SIM_PAUSE_H
#define DEV_OVERLAY_SIM_PAUSE_H

#include <stdbool.h>

/* Resolves the flag. False, with a log line, when the site does not match or the address it yields
 * is outside the image, in which case set() does nothing and the panel behaves as it did before. */
bool sim_pause_install(void);

/* Whether the flag was resolved, for the caller that has to be honest in its log line. */
bool sim_pause_is_available(void);

/* Who is asking. One bit each, because they can overlap. */
typedef enum sim_pause_holder {
    SIM_PAUSE_PANEL       = 1u << 0,   /* the dev panel, while it is open      */
    SIM_PAUSE_FREE_CAMERA = 1u << 1    /* the free camera, while it is flying  */
} sim_pause_holder_t;

/* Takes or releases one holder. Idempotent per holder: setting the state that holder is already
 * in does nothing, so a caller may drive it from its own open flag every frame without the
 * remembered value being overwritten by the value this itself wrote. The simulation is paused
 * while any holder has it, and the value from before the first one is restored when the last
 * lets go. */
void sim_pause_hold(sim_pause_holder_t who, bool held);

#endif /* DEV_OVERLAY_SIM_PAUSE_H */
