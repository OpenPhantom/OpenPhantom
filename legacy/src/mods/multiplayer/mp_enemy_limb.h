/* mp_enemy_limb.h: a limb an enemy lost, on the machine that only watches.
 *
 * Layer 2. The host's engine takes a limb off an actor; a client's actor is a parked replica that
 * never runs the damage path, so its body stays whole.
 *
 * The limb travels as a world event (mp_world_event.h), one event per limb, and a client performs
 * it the way the engine does: through the engine's own cut on the replica, which hides the node on
 * the body and throws the piece that flies for five seconds. The number and the decisions are
 * mp_enemy_limb_rule.h: the node is first put back to what it was on the host before the cut, so a
 * piece cut after the host's mask arrived does not fly invisible, and a piece is not thrown while
 * the engine's task table is nearly full, where the node is only hidden. A replica built after the
 * window arrives with the node hidden by the mask, and no piece.
 */
#ifndef MULTIPLAYER_MP_ENEMY_LIMB_H
#define MULTIPLAYER_MP_ENEMY_LIMB_H

#include <stdbool.h>

/* The hull on both sides, and the player of the kind on a client. */
bool mp_enemy_limb_install(void);

void mp_enemy_limb_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_LIMB_H */
