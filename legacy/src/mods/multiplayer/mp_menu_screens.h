/* mp_menu_screens.h: the multiplayer screens, each on the shipped overlay it borrows.
 *
 * Five screens, every one composed of the toolkit's own widget types placed where its overlay
 * paints a place for them:
 *
 *   HUB        on sopts.bmp    the options hub: four entries on its four slots, the title in its
 *                              readout, the red button back
 *   BEITRETEN  on presets.bmp  the server list in the list window, three actions on the slots,
 *                              the instruction on the band, the address on the edit bar
 *   HOSTEN     on dialog.bmp   the lamps in the black window, the values in the top-right list,
 *                              the apply entry on the readout, the slots on the slider
 *   EINGABE    on presets.bmp  one value typed on the edit bar, told what it is on the band
 *   SPIELER    on sload.bmp    the roster in the list window, the level in the frame, the chosen
 *                              player in the readout, team and ready on the two slots
 *
 * What comes out is the INTENTION in mp_settings: host or join, with what, and the saved list.
 * Nothing here connects directly: the lobby screen arms the transport as it opens, and a client
 * asks for its connection from there.
 */
#ifndef MULTIPLAYER_MP_MENU_SCREENS_H
#define MULTIPLAYER_MP_MENU_SCREENS_H

#include "mp_settings.h"

#include <stdbool.h>
#include <stdint.h>

/* Runs the hub and whatever the player opens from it. `settings` is read and written in place;
 * true when the player applied a choice (host or join), false when they backed out, in which
 * case `settings` was restored to what was handed in. */
bool mp_menu_screens_run(mp_settings_t *settings);

/* The player list, opened over the pause menu while a session is joined. */
void mp_menu_screens_run_players(void);

/* One sentence a player has to read before anything else, why the session ended, and the button
 * that closes it. The sentence is wrapped at its spaces into the list's column. */
void mp_menu_screens_run_notice(const char *text);

/* Counters for the report. */
uint32_t mp_menu_screens_opened(void);
uint32_t mp_menu_screens_announces_heard(void);

#endif /* MULTIPLAYER_MP_MENU_SCREENS_H */
