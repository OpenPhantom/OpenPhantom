/* unittests/mp_seat_order_no_session.c: the session an arrival's order reads, with nothing in it.
 *
 * mp_seat_order reads the session's roster, for the client slots below this player's, and asks
 * which far bank shows each of them. Both answers are the bridge's, and linking the bridge would
 * bring a session that does not exist. The tests that link mp_arrival.c for something else,
 * mp_arrival and mp_seat_world, get this: no roster has arrived and no bank shows anybody, which is
 * a client before its first roster. The arrival's wish then has no order and searches as it did
 * before there was one. unittests/mp_seat_order.c answers both itself, from what each case sets.
 *
 * The arrival asks three more things of the bridge: the far players' poses, for a host who lies
 * dead, and whether this level was entered by joining a running session. Here nobody else stands,
 * and nobody came late.
 */
#include "mp_bridge_far.h"
#include "mp_bridge_lobby_late.h"
#include "mp_bridge_roster.h"
#include "mp_roster.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_bridge_roster_current(mp_roster_t *out)
{
    (void)out;
    return false;
}

size_t mp_bridge_far_bank_of_slot(uint8_t slot)
{
    (void)slot;
    return 0u;
}

bool mp_bridge_far_occupied(size_t bank)
{
    (void)bank;
    return false;
}

bool mp_bridge_far_pose(size_t bank, mp_bridge_far_reader_t reader, mp_bridge_far_pose_t *out)
{
    (void)bank;
    (void)reader;
    (void)out;
    return false;
}

bool mp_bridge_far_pose_stands(const mp_bridge_far_pose_t *pose)
{
    (void)pose;
    return false;
}

bool mp_bridge_lobby_late_start(void)
{
    return false;
}
