/* spawn_mode.h: the entity spawner's placement mode, joined to the engine.
 *
 * spawn_place.c holds the state and the rules, spawn_ghost.c the preview's render handle,
 * spawn_marks.c the marks, world_pick.c the camera arithmetic and world_camera.c the camera's
 * numbers. This is where they meet the running game: once a frame, at the end of the scene, it
 * reads the camera and the pointer, casts the ray, asks the world probes where the entity would
 * stand, finds the copy under the pointer, and places or removes what the last clicks asked for.
 * The window messages only ask (spawn_mode_key, spawn_mode_button); every probe of the world runs
 * from the frame, which is a place in the engine's own flow, where a window message is not.
 *
 * What a field log shows, all under "npc spawner:":
 *
 *   the placement mode is on: <entity>, ...          entered
 *   the placement mode is off: <why>; it placed ...  left, with what it came to
 *   the placement mode could not start: <why>        asked and refused
 *   the click places <entity> at x y z facing f ...  placed (or asked of the host), with how far
 *                                                    the ray struck and the push off a wall
 *   the click places nothing: <why>                  a click on a refused place
 *   the world ran <n> substep(s) in <t> ms ...       the settle after a copy placed, single player
 *   the right click removes <name> (<key>)           removed
 *   the right click removes nothing: <why>           nothing under the pointer, a client, ridden
 *   the placement mode waits / goes on               the free camera took the mouse, and let go
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_SPAWN_MODE_H
#define DEV_OVERLAY_SPAWN_MODE_H

#include <stdbool.h>
#include <stdint.h>

/* The camera cells and the two keys, at install. The ghost's draw path is armed the first time the
 * mode comes on, resolving before it hooks. */
void spawn_mode_install(void);

/* Once a frame at the end of the scene, before the owner of the pointer is read. */
void spawn_mode_frame(void);

/* The marks, when the mode owns the picture. */
void spawn_mode_draw(void);

/* The one line at the top of the picture: what is being placed while the mode runs, why it stands
 * still when the free camera has the pointer, and what it did for a moment after it ends. Called
 * whoever owns the input, because the last of those three is drawn once the panel has the picture
 * back; it draws nothing when there is nothing to say. */
void spawn_mode_banner(void);

/* From the panel's message hook. A key: Escape or the mode's key leaves the mode, the face key
 * turns the entity back to the player, and the mode's key enters it from the panel. A button: the
 * left places, the right removes, the middle turns back to the player. True when the message was
 * the mode's; outside the mode only the key that enters it is. */
bool spawn_mode_key(int32_t virtual_key);
bool spawn_mode_button(int32_t message);

/* With the panel shut: whether this key opens it straight into the mode. */
bool spawn_mode_opens_on(int32_t virtual_key);

/* Asks the mode on or off; the next frame takes it. */
void spawn_mode_ask(bool on);

#endif /* DEV_OVERLAY_SPAWN_MODE_H */
