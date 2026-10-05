/* mp_warp_follow.h: a client's player follows the host when a script warps him.
 *
 * Layer 3. A script's warp is the host's alone, and it leaves the far players where they stood:
 * in a level that keeps sending its player to another place as another hero, they were left
 * behind at every one. So a client follows. The level's journal tells it that the host was sent
 * away and where to (mp_level_state_warp.h); this module waits until the host stands there and
 * then has the teleport to the host pressed, the one the developer menu's button presses, with
 * its door, its search for a free place beside him and its move under a short fade. The player
 * keeps its own hero.
 *
 * It owns the wait and nothing of the teleport. The teleport has one caller, the reader of the
 * two buttons (mp_player_help.c), which asks here once a frame whether a press is due, presses,
 * and says here what the door answered and how the teleport ended. A teleport that ends without
 * a landing is waited for again inside the same bound; one the player ends with the repair
 * button, or a session that ends, is not.
 *
 * A newer warp replaces an older one that is still waited for. One heard while the teleport of
 * the last is under way is taken up as that teleport ends.
 *
 * The decisions are mp_warp_follow_rule's.
 */
#ifndef MULTIPLAYER_MP_WARP_FOLLOW_H
#define MULTIPLAYER_MP_WARP_FOLLOW_H

#include "mp_player_help_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* One look, once a drawn frame of a session, before the teleport's own frame. `substeps` is the
 * frame pump's count, the clock of the wait. True when the teleport's door is to be pressed for
 * a warp on this frame; the caller presses and answers with the call below. */
bool mp_warp_follow_due(uint32_t substeps);

/* What the door answered to that press. */
void mp_warp_follow_pressed(mp_player_help_verdict_t verdict, uint32_t substeps);

/* The teleport pressed for a warp ended by itself: landed, or given up in its search or move. */
void mp_warp_follow_ended(mp_player_help_verdict_t ended, uint32_t substeps);

/* It was ended from outside, by the player's repair or with the session: the warp is not
 * followed any further. Also ends a wait with no teleport under way. `why` is the line's word
 * for who ended it. */
void mp_warp_follow_left(const char *why);

void mp_warp_follow_report(void);

#endif /* MULTIPLAYER_MP_WARP_FOLLOW_H */
