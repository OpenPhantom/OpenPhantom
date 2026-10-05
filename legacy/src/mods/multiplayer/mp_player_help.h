/* mp_player_help.h: the two buttons of the developer menu's Multiplayer heading, heard and
 * answered.
 *
 * Layer 3. The developer overlay draws "Repair lock" and "Teleport to host" and may not call this
 * feature, so a press is a record it files (common/player_help_note), and what came of the press
 * is a second record this module files back. Both buttons act for the player who pressed and for
 * nobody else, and neither puts anything on the wire.
 *
 * This module is the reader and the answer:
 *
 *   It looks at the ask once a drawn frame, from the bridge's frame pump. That pump turns back
 *   in its first line while no transport stands, so with no session nothing here runs.
 *
 *   As a transport goes up it takes the serial on file as its mark, so a press made in single
 *   player or in the session before is never acted on.
 *
 *   On the first frame after that it says in the answer that it listens and what it can do,
 *   which is what the overlay offers each button by.
 *
 *   As the transport comes down it says that nobody listens, ends a teleport that was under way
 *   with its fade, and ends an answer that still stood open, which would otherwise grey its
 *   button into the next session.
 *
 * What a press does is mp_repair_lock for the first button and mp_teleport_host for the second;
 * a repair ends a teleport under way. The decisions are mp_player_help_rule's.
 *
 * The teleport has this one caller. A warp of the host's that a client follows (mp_warp_follow)
 * is the same teleport, so it is pressed from here as well, on the frame that module says it is
 * due; it answers no press, and its end goes back to that module and not into the record.
 */
#ifndef MULTIPLAYER_MP_PLAYER_HELP_H
#define MULTIPLAYER_MP_PLAYER_HELP_H

#include <stdint.h>

/* A transport went up: the mark is taken. Nothing is filed yet; the first frame of the pump
 * says that somebody listens, because only then is it known that the pump runs. */
void mp_player_help_arm(void);

/* One drawn frame of a session, from the bridge's frame pump, between two substeps and before
 * the respawn and the placement are ticked. `substeps` is the pump's count. */
void mp_player_help_frame(uint32_t substeps);

/* The transport comes down, at the one exit of a session: a teleport under way is left, an
 * answer still open is ended, and the answer says that nobody listens. */
void mp_player_help_withdraw(void);

void mp_player_help_report(void);

#endif /* MULTIPLAYER_MP_PLAYER_HELP_H */
