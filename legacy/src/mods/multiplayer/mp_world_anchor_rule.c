/* mp_world_anchor_rule.c: which body the host's world is woken and kept by. See the header. */
#include "mp_world_anchor_rule.h"

#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

mp_world_anchor_answer_t mp_world_anchor_rule_pick(bool widens, uint32_t local,
                                                   const mp_world_anchor_candidate_t *candidates,
                                                   size_t count, const float *died_at,
                                                   size_t *chosen)
{
    mp_seat_body_t players[MP_WORLD_ANCHOR_CANDIDATES];
    size_t         order[1];
    size_t         index;

    if (!widens || local != 0u) {
        return MP_WORLD_ANCHOR_ENGINE;
    }
    if (candidates == NULL) {
        count = 0u;
    }
    if (count > MP_WORLD_ANCHOR_CANDIDATES) {
        count = MP_WORLD_ANCHOR_CANDIDATES;
    }
    /* Who stands nearest to the death is the seat's rule, the one the re-entry asks as well, so
     * the player the world is woken by and the player a dead one comes back beside are chosen the
     * same way: nearest first, the lower bank on a tie. A bank with no body built in it is nobody
     * the engine could measure against. */
    for (index = 0u; index < count; ++index) {
        players[index].known   = candidates[index].body != 0u;
        players[index].stands  = candidates[index].stands;
        players[index].heading = 0.0f;
        memcpy(players[index].position, candidates[index].position,
               sizeof players[index].position);
    }
    if (mp_seat_rule_anchor_order(players, count, died_at, order, 1u) == 0u) {
        return MP_WORLD_ANCHOR_NOBODY;
    }
    if (chosen != NULL) {
        *chosen = order[0];
    }
    return MP_WORLD_ANCHOR_FAR;
}
