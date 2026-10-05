/* mp_warp_follow_rule.h: when a client's player follows a warp of the host's, as decisions a test
 * can drive.
 *
 * Layer 1, pure. No address and no game in the process: mp_warp_follow.c reads the game and asks
 * here.
 *
 * A script's warp sends the host to another place of the level, and a level like the assault on
 * the palace does it again and again, each time as another hero. The far players are told of it
 * by the level's journal (mp_level_state_warp.h) and each comes beside him on its own machine,
 * through the same teleport the developer menu's button presses. What is decided here is only
 * when that teleport is pressed, and when the wait for it ends.
 *
 * The wait has two parts, and the second exists because of the first:
 *
 *   The host is not at the target when the entry arrives. The engine takes a warp as a fade of a
 *   second, and the journal's entry is written as the fade begins. A teleport pressed then would
 *   find the host where he still stands, beside this player, and answer that there is nothing to
 *   do. So the press waits until the host's pose has read near the target once, which is kept,
 *   because a host who lands and walks on is no less landed. A host whose pose never reads there
 *   is taken as landed after three seconds all the same: his pose may have passed the target
 *   between two looks, and the door then measures how far off he really is.
 *
 *   The door may be shut for a while for reasons that pass: this player is dead and its re-entry
 *   is bringing it back, it is being moved already, it mans a gun, the developer menu holds it,
 *   or the host has no standing body yet. Each of those is waited out, for twenty seconds from
 *   the entry in all. A door shut for a reason that does not pass ends the wait at once.
 */
#ifndef MULTIPLAYER_MP_WARP_FOLLOW_RULE_H
#define MULTIPLAYER_MP_WARP_FOLLOW_RULE_H

#include "mp_player_help_rule.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* The host counts as landed once his pose reads this near the warp's target: the reach of the
 * seat search's outer ring, the same distance the teleport's door calls beside him. */
#define MP_WARP_FOLLOW_LANDED MP_SEAT_RING_FAR

/* After this many substeps the host is taken as landed without that reading: three seconds,
 * three times the fade the engine takes a warp under. A field run on the assault level measured
 * 34 substeps from a warp's door to the landing. */
#define MP_WARP_FOLLOW_LANDING_SUBSTEPS 96u

/* The whole wait from the entry, in substeps: twenty seconds, the longest the seat search itself
 * looks for a place. A re-entry lands within one or two of them: in the same run a dead player
 * stood again 1054 to 1060 ms after the death. */
#define MP_WARP_FOLLOW_SUBSTEPS 640u

typedef enum mp_warp_follow_step {
    MP_WARP_FOLLOW_WAIT = 0,
    MP_WARP_FOLLOW_PRESS,     /* the teleport's door is pressed now */
    MP_WARP_FOLLOW_BESIDE,    /* this player stands beside the host already: nothing to do */
    MP_WARP_FOLLOW_GIVE_UP
} mp_warp_follow_step_t;

typedef struct mp_warp_follow_look {
    uint32_t                 waited;   /* substeps since the entry was heard */
    bool                     landed;   /* the host's pose has read near the target since */
    mp_player_help_verdict_t door;     /* what the teleport's door would answer now */
} mp_warp_follow_look_t;

/* Whether the host has landed, kept once it was so: `host_off` is how far his pose reads from
 * the warp's target on this look, and counts only with `host_known`. A distance that is not a
 * finite number is not near. */
bool mp_warp_follow_landed(bool landed_before, bool host_known, float host_off);

/* Whether a refusal of the door passes by itself and is waited out. */
bool mp_warp_follow_passes(uint8_t reason);

/* What one look comes to. In this order: a door shut for a reason that does not pass gives up;
 * so does the whole wait run out; a host not landed is waited for until he is taken as landed;
 * then the door decides, open to press, beside him already to end, and any refusal that passes
 * to wait. */
mp_warp_follow_step_t mp_warp_follow_step(const mp_warp_follow_look_t *look);

/* What the end of a teleport pressed for a warp comes to: true when the warp is followed again
 * from the wait, which is every end but a landing and a level that changed or ended under it. */
bool mp_warp_follow_tries_again(mp_player_help_verdict_t ended);

#endif /* MULTIPLAYER_MP_WARP_FOLLOW_RULE_H */
