/* mp_world_anchor_rule.h: which body the host's world is woken and kept by.
 *
 * Layer 1, pure. The engine asks one question before it wakes an enemy and before it takes one
 * away for distance: which body is the living player. Its answer is the local player's body, or
 * nothing while he is dead, and on nothing the activation scan returns before its first
 * placement and the removal test in the entity loop is skipped. In single player that is right,
 * because a dead player's level has nobody left to play it. On a host it stops the level for
 * every client as long as the host lies dead, and the clients' own scans then wake enemies the
 * host never lists.
 *
 * The answer this gives: the engine's own whenever it names a body or the session does not
 * widen the range, a far player's body when the host is dead and one stands, and the engine's
 * nothing when nobody stands. Of the far players the one standing nearest to where this one died
 * is taken, which keeps the choice where the fight was; "nearest" is the seat's anchor order
 * (mp_seat_rule), the one rule the re-entry asks too.
 *
 * `widens` is the range gate's own answer, not a second reading of the role: the anchor may name
 * a far body exactly when the gate measures a far body, or the two would answer the same
 * question differently.
 */
#ifndef MULTIPLAYER_MP_WORLD_ANCHOR_RULE_H
#define MULTIPLAYER_MP_WORLD_ANCHOR_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One far player as the anchor sees it: the body object the engine can measure against, where
 * that player stands, and whether he stands at all in his own machine's words. A body of 0 is a
 * bank with nobody built in it. */
typedef struct mp_world_anchor_candidate {
    uint32_t body;
    float    position[3];
    bool     stands;
} mp_world_anchor_candidate_t;

/* How many candidates the rule reads, one for each far bank; any past this are not looked at. */
#define MP_WORLD_ANCHOR_CANDIDATES 3u

typedef enum mp_world_anchor_answer {
    MP_WORLD_ANCHOR_ENGINE = 0,   /* the engine's own answer stands, whatever it was */
    MP_WORLD_ANCHOR_FAR,          /* a far player's body stands in for the dead one */
    MP_WORLD_ANCHOR_NOBODY        /* the player is dead and no far player stands: nothing, as
                                   * the engine answers */
} mp_world_anchor_answer_t;

/* The rule. `local` is the engine's own answer, the body or 0. `died_at` is where this player's
 * body lies, NULL when it could not be read, and then the first standing candidate is taken.
 * `chosen` receives the candidate's index for MP_WORLD_ANCHOR_FAR and is left alone otherwise;
 * the body to hand the engine is that candidate's `body`. */
mp_world_anchor_answer_t mp_world_anchor_rule_pick(bool widens, uint32_t local,
                                                   const mp_world_anchor_candidate_t *candidates,
                                                   size_t count, const float *died_at,
                                                   size_t *chosen);

#endif /* MULTIPLAYER_MP_WORLD_ANCHOR_RULE_H */
