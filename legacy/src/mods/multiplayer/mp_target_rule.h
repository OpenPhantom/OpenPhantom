/* mp_target_rule.h: which calls the target resolver's hull leaves alone, which far player answers
 * the others, and for which actors the host stays the player, as arithmetic.
 *
 * Layer 1, pure. The hull and the engine reads are in mp_target.c; what is here is the decision,
 * so a test can drive it with no game in the process.
 */
#ifndef MULTIPLAYER_MP_TARGET_RULE_H
#define MULTIPLAYER_MP_TARGET_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether a call is left to the engine's own answer, by the address it returns to.
 *
 * The extension answers a far player for every kind 0 and kind 1 call, because almost every such
 * call is a script asking where "the player" is to walk, aim or trigger. One caller asks a
 * different question. The conversation menu's facing test takes the position through the resolver
 * and the heading from this machine's own player, and both halves have to be the same player: a
 * menu is a conversation of the player at this machine. Answered with a far player, a far client
 * passes the test and the conversation opens on the host's screen; with the host dead, the heading
 * is read off no body at all.
 *
 * `returns` holds the resolved return addresses of such callers. An empty list, or a nought entry,
 * leaves nobody to the engine, which is the behaviour before this rule existed. */
bool mp_target_left_to_the_engine(const uintptr_t *returns, size_t count, uintptr_t caller);

/* Whether a resolution becomes the actor's last answer, which the scene watch reads to name the
 * player a script meant. A call left to the engine does not: the facing test answers with this
 * machine's player whatever the NPC fights, and kept, it would overwrite a far player's fresh
 * answer for the same actor, so a scene begun within that answer's age would be put down to the
 * host. */
bool mp_target_answer_is_kept(bool left_to_the_engine);

/* One far body as the hull sees it when a script asks for the player. */
typedef struct mp_target_far {
    bool  readable;   /* a body stands for that bank and its position read */
    bool  stands;     /* its player stands, in his own machine's words */
    bool  attacker;   /* it is the body the actor remembers as having hurt it */
    float distance;   /* squared, from the actor */
} mp_target_far_t;

/* Which far body answers a script's question for the player, by its index, or -1 for the engine's
 * own answer. The one that hurt the actor wins outright, otherwise the nearest one that beats
 * `engine_distance`, the squared distance of the engine's own answer.
 *
 * A far player who does not stand is nobody's answer, which is what the engine's own
 * player_getActorIfAlive says about the local player: a corpse is no target, and a script such as
 * the fan's that kills whoever it finds finds nobody. `passed_dead` says whether the answer
 * without that test would have been a far player lying dead. */
int mp_target_rule_far_pick(const mp_target_far_t *far, size_t count, float engine_distance,
                            bool *by_aggro, bool *passed_dead);

/* Why the engine's own answer for the player, the host, is kept for an actor, and no far player
 * is weighed against it. */
typedef enum mp_target_claim {
    MP_TARGET_CLAIM_NONE = 0,   /* it is not: the far players are weighed as for any actor */
    MP_TARGET_CLAIM_SCENE,      /* an actor of the host's scene: the one whose script opened its
                                 * door, its hero, or the actor that drives the host's body */
    MP_TARGET_CLAIM_TAKER,      /* it took the camera, the lock or the bars on this host by its own
                                 * script and has not given back */
    MP_TARGET_CLAIM_AFTER,      /* the actor of a scene of the host's that has just ended */
    MP_TARGET_CLAIMS
} mp_target_claim_t;

/* What is known when a script asks where the player is. */
typedef struct mp_target_claim_evidence {
    bool engine_answered;   /* the engine's own answer names a target: the host stands */
    bool joined;            /* somebody else is in the session */
    bool of_the_scene;
    bool taker;
    bool after_the_scene;
} mp_target_claim_evidence_t;

/* Whether the engine's answer stands, and why.
 *
 * A scene is the host's alone, and its script measures "the player" from tick to tick: how far he
 * stands, whether he looks, whether he has walked on. Answered with whoever stands nearest, the
 * scene would follow a far player who walks up to its actor. So for the scene's own actors the
 * host is the player. The same holds for an actor that has taken something of the host's and has
 * yet to give it back, because the test that gives back is a test of the player: a far player
 * standing nearer would hold the host's camera for as long as he stood there. And for the actor
 * of a scene just ended, so that the next part of a conversation in several parts still means the
 * host.
 *
 * Nothing else is kept. Every other actor goes on answering every player, the far players' own
 * fights and triggers included, while a scene of the host's stands.
 *
 * With no answer of the engine's the host lies dead, and the rule that follows is asked as ever:
 * keeping "no target" would make every script's test for a player far away come true at once.
 * With nobody joined there is no far player to weigh, and nothing is kept either. */
mp_target_claim_t mp_target_rule_claim(const mp_target_claim_evidence_t *evidence);

#endif /* MULTIPLAYER_MP_TARGET_RULE_H */
