/* rest_rule.h: whether this DLL may move a speaker's body, asked of the actor behind it.
 *
 * The engine changes a character's clip only while its script runs, which is reaction state 1,
 * and a script runs there only while the character lives or its own death branch plays. What
 * this DLL does to a body after a line, the rest, the cut with the voice, the gesture repeat, the
 * stand-in and the hold, is a move the script would have made, so it asks the script's question
 * first: is the actor alive and in its script. Asking the clip's name instead stood corpses back
 * up. A droid thrown and then killed dies on brnockdi, whose name says nothing of a death, and the
 * rest put it on its stand half a second after it lay down, in single player as in a session.
 *
 * Three answers, because the second is not the third. An actor that is alive but out of its
 * script, hit, thrown or getting up, has an engine arm holding its body; that body is passed over
 * for now and watched on. One that is dead, or in the dying, fading, shattering or corpse states,
 * is let go. A body with no character behind it, the player's for one, has only the clip's name
 * to go by, as before, and a clip named for a death is left alone whoever holds it.
 *
 * Header only: pure, and small enough that its test is the only translation unit it needs.
 */
#ifndef DIALOGUE_ANIM_FIX_REST_RULE_H
#define DIALOGUE_ANIM_FIX_REST_RULE_H

#include <stdbool.h>
#include <stdint.h>

#define REST_STATE_SCRIPT        1    /* the reaction state in which the script runs */
#define REST_STATE_DEATH_FIRST   11   /* dying, fading, shattering and the corpse, 11 to 14 */
#define REST_STATE_DEATH_LAST    14

typedef enum rest_verdict {
    REST_MAY = 0,     /* alive and in its script, or no actor behind the body: it may be moved */
    REST_NOT_NOW,     /* alive, with an engine arm holding the body: passed over, watched on */
    REST_NEVER        /* dead, in a death state, or on a clip named for a death: let go */
} rest_verdict_t;

/* `has_actor` is whether a character record stands behind the body, `health` and `state` its two
 * cells when it does. `death_named` is the clip on the body having "die" or "death" in its name,
 * the models' own names for most deaths; a caller with no clip to judge passes false. */
static inline rest_verdict_t rest_verdict(bool has_actor, int32_t health, int32_t state,
                                          bool death_named)
{
    if (death_named) {
        return REST_NEVER;
    }
    if (!has_actor) {
        return REST_MAY;
    }
    if (health <= 0 || (state >= REST_STATE_DEATH_FIRST && state <= REST_STATE_DEATH_LAST)) {
        return REST_NEVER;
    }
    return state == REST_STATE_SCRIPT ? REST_MAY : REST_NOT_NOW;
}

#endif /* DIALOGUE_ANIM_FIX_REST_RULE_H */
