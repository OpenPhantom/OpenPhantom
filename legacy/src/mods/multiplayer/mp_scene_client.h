/* mp_scene_client.h: a client in the host's scene: locked, the bars on, its own camera, and seated
 * where the host said before the scene runs.
 *
 * Layer 3. The client's half of mp_scene_host.h. The host decides everything; a client reads the
 * note, 0xA4, and does what it says, and it never asks its own cells whether a scene runs, because
 * the scene is the host's.
 *
 *   THE MIRROR. While the newest note of this world says a lock or a hero scene gathers or runs,
 *   and the host is heard, the client's lock stands at the script's level on every substep, raised
 *   through the engine's own entry past the gates that refuse this machine's scripts, and the bars
 *   are drawn once. It is raised only once the player may be moved, so nobody is locked in the air.
 *   The camera stays the client's own: nothing here touches it.
 *
 *   ONE EXIT for the lock: the note says over, the host has not been heard for two seconds, or the
 *   level or the session ends. Each releases the lock once, through the engine's own release, and
 *   takes the bars down.
 *
 *   THE RELEASE HULL. While the mirror holds, a release of the lock that a script of THIS machine
 *   asks for is refused, known by the two addresses those releases return to. Refusing a lower is
 *   safe under the pair rule the scene gates are held to, because the lower checks for itself: it
 *   unwinds only a lock at or under its own level, so a refused release leaves a lock nobody else
 *   would have kept. The raise this machine's scripts would pair it with is refused already, by the
 *   scene gates, for the same reason the release is: a scene on a client is the host's.
 *
 *   THE SEAT. A note that hands this player a seat is followed once: the screen goes dark for a
 *   quarter of a second, the placement takes the seat, the screen comes back; a warp does the same
 *   under the engine's own fade of a second, with the player's own hero and nothing else changed.
 *   A seat handed out for a scene that runs already is not taken any more; a warp is followed once
 *   by its number, and not at all by a player who stands near its target already.
 */
#ifndef MULTIPLAYER_MP_SCENE_CLIENT_H
#define MULTIPLAYER_MP_SCENE_CLIENT_H

#include "mp_scene_flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The release hull and the two addresses it knows scripts by. Idempotent; installed on both roles,
 * where it lets every call through unless a client's mirror holds. */
bool mp_scene_client_install(void);

/* A note off the reliable channel, from the drain. True when it was this note, taken or refused. */
bool mp_scene_client_take(bool as_client, const uint8_t *note, size_t bytes);

/* One substep of the mirror and the seat, from the bridge's post-tick half on a client. */
void mp_scene_client_tick(uint32_t substep);

/* Whether the host's scene runs for everybody as this client's newest note says. With `known`,
 * the place that scene gathers around as the note says it; a client never knows its actor. */
bool mp_scene_client_for_all(mp_scene_known_t *known);

/* The one exit: the lock released once, the bars down, a held fade given back. */
void mp_scene_client_leave(void);

void mp_scene_client_report(void);

#endif /* MULTIPLAYER_MP_SCENE_CLIENT_H */
