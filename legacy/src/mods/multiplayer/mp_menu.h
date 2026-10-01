/* mp_menu.h: the multiplayer entry in the game's own front end.
 *
 * The player chooses in the main menu, and the choice is carried out on the way into the lobby:
 * the transport goes up there, and a client connects from the lobby with this side's statement of
 * what it plays with, which stands from that arming because the shot module's init has run by the
 * time a menu is shown.
 *
 * The screen is the engine's own. Its widget toolkit has a text edit and a list box, both shipped,
 * both authored elsewhere in the retail data, and a mod may build a screen of its own out of them
 * without a single new asset: the bitmap table may be absent and the font table is the one every
 * boot screen already shares. What this file adds is one line to the title screen and one screen
 * behind it.
 *
 * The line on the title screen cannot be selectable, and the reason is not taste. The title
 * screen's own loop indexes its four background videos with `focusId - 1` and checks only for -1;
 * a widget with id 10 reaches one past the end of that array, which is the saved frame pointer,
 * and the call that follows writes through it unconditionally. So the line is STATIC, which the
 * engine skips for both focus and hit-testing, and this file does the hit-testing itself.
 */
#ifndef MULTIPLAYER_MP_MENU_H
#define MULTIPLAYER_MP_MENU_H

#include "mp_exit.h"
#include "mp_settings.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolves the toolkit, reads the saved settings and appends the line to the title screen. False
 * when a part of that did not stand, in which case nothing was appended and every entry point
 * below turns back; the saved settings are still readable, so a build whose front end cannot be
 * touched still plays what the ini says. */
bool mp_menu_install(void);

/* What the player chose, saved across runs. Never NULL, and a build where nothing resolved answers
 * the defaults. */
const mp_settings_t *mp_menu_settings(void);

/* What to do when the player applies a choice. It is a callback rather than a call into the
 * bridge because this file has no business knowing that a bridge exists; what it knows is
 * that somebody wants to be told. Set before install, or not at all. */
typedef void (*mp_menu_applied_fn)(const mp_settings_t *settings);
void mp_menu_set_applied_client(mp_menu_applied_fn client);

/* What to do when a LOBBY needs the transport standing: a host binds its port, a client connects.
 * Separate from the applied client above because of WHEN it happens, a lobby arms on the way in,
 * where the other one fires on the way out, and because this one has to answer whether it
 * worked, so the screen can say so instead of showing an empty player list forever. */
typedef bool (*mp_menu_arm_fn)(const mp_settings_t *settings);

/* Which side the process armed, as an mp_settings_role_t, 0 while none or while a role from
 * the ini or the environment holds. The lobby band reads it to say WHY an arming was refused.
 * It is the root's own cell read through this door, not a second judgement: the root hands
 * the reader in with the arming callback, and a build with no root reads 0. */
typedef uint32_t (*mp_menu_role_fn)(void);
void mp_menu_set_arm_client(mp_menu_arm_fn client, mp_menu_role_fn armed_role);
uint32_t mp_menu_armed_role(void);

/* Puts the transport up for these settings. False when there is no client, or it refused. */
bool mp_menu_arm(const mp_settings_t *settings);

/* The session's two clients the screens need. The exit is the one way out of a session; the title
 * client runs while the title screen is on show and answers a sentence the player is owed, or
 * NULL. Both are the feature's, handed in, so the screens hold no session of their own. */
typedef void (*mp_menu_exit_fn)(mp_exit_why_t why);
typedef const char *(*mp_menu_title_fn)(void);
void mp_menu_set_session_clients(mp_menu_exit_fn exit_client, mp_menu_title_fn title_client);

/* Leaves the session by the one exit. */
void mp_menu_exit(mp_exit_why_t why);

void mp_menu_report(void);

#endif /* MULTIPLAYER_MP_MENU_H */
