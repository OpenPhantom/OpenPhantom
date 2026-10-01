/* mp_world_anchor.h: who wakes and keeps the enemies of a session's world. Only the host, and the
 * host even while its own player lies dead.
 *
 * Layer 3. It reads the far players' poses and the session's setup note, and it binds two places
 * in the engine for it.
 *
 * The level belongs to the host. The engine wakes an enemy in its activation scan and takes one
 * away for distance in the entity loop, and both measure against the one body the engine calls the
 * living player. Two things went wrong with that in a session:
 *
 *   - While the host's player is dead the engine has no living player, so the scan returns before
 *     its first placement and the distance removal is skipped. Nothing woke or went for anybody,
 *     for as long as the host lay dead. The two calls that ask for the living player are pointed
 *     at the anchor here, which answers what the engine answers, except on a host whose player is
 *     dead: then the body of the standing far player nearest to where he died, and nothing when
 *     nobody stands (mp_world_anchor_rule). The range gate widens the same two questions to every
 *     far player from there. Rewriting the two call operands leaves every other reader of the
 *     live player test untouched, where a hull on the test itself would have answered them all:
 *     the music, the pause menu, the dialogue and the statistics among them.
 *
 *   - A client's own scan woke enemies around its own player, with its own AI, which the host
 *     never listed and so the client never parked: they fought that one client, died there and
 *     released their next wave there, on each client for itself. On a client of a started session
 *     the scan is held here and every enemy comes from the host's enemy block, which builds what
 *     the host lists. The question asked is mp_session_now_client_of_a_started_session, the one
 *     the scene and movie gates ask, so the scan runs again the moment the session ends.
 *
 * Installed from the session's arming and never from the DLL's own start: single player reaches
 * neither bind, and a bind left behind by a session asks its predicate at every call.
 */
#ifndef MULTIPLAYER_MP_WORLD_ANCHOR_H
#define MULTIPLAYER_MP_WORLD_ANCHOR_H

#include <stdbool.h>

/* Both binds, each on its own and each at most once. False when neither stands; each one that
 * cannot stand says why in the log. */
bool mp_world_anchor_install(void);

/* Whether this machine's own player stands, by the engine's own live player test: the function the
 * two anchored calls named before they were moved, called as it is. The one answer to "does the
 * host stand" for everything a session measures by it, the hold of a scene's gathering among them.
 * `known` false when the test did not resolve, or a bank window is open and the player record is a
 * far body's. */
bool mp_world_anchor_player_stands(bool *known);

/* The two report lines: the host's anchor while its player was dead, and the scan a client held. */
void mp_world_anchor_report(void);

#endif /* MULTIPLAYER_MP_WORLD_ANCHOR_H */
