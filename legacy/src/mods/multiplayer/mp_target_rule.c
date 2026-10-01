/* mp_target_rule.c: which calls the target resolver's hull leaves alone. See the header. */
#include "mp_target_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_target_left_to_the_engine(const uintptr_t *returns, size_t count, uintptr_t caller)
{
    size_t index;

    if (returns == NULL || caller == 0u) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (returns[index] != 0u && returns[index] == caller) {
            return true;
        }
    }
    return false;
}

bool mp_target_answer_is_kept(bool left_to_the_engine)
{
    return !left_to_the_engine;
}

/* The pick over the bodies `alive_only` lets in: the attacker outright, otherwise the nearest that
 * beats the engine's answer, the lower index among equals. */
static int pick(const mp_target_far_t *far, size_t count, float engine_distance, bool alive_only,
                bool *by_aggro)
{
    int    best = -1;
    float  best_distance = engine_distance;
    size_t i;

    *by_aggro = false;
    for (i = 0; i < count; ++i) {
        if (!far[i].readable || (alive_only && !far[i].stands)) {
            continue;
        }
        if (far[i].attacker) {
            *by_aggro = true;
            return (int)i;
        }
        if (far[i].distance < best_distance) {
            best          = (int)i;
            best_distance = far[i].distance;
        }
    }
    return best;
}

int mp_target_rule_far_pick(const mp_target_far_t *far, size_t count, float engine_distance,
                            bool *by_aggro, bool *passed_dead)
{
    bool ignored = false;
    int  answer;
    int  without_the_test;

    *by_aggro    = false;
    *passed_dead = false;
    if (far == NULL) {
        return -1;
    }
    answer           = pick(far, count, engine_distance, true, by_aggro);
    without_the_test = pick(far, count, engine_distance, false, &ignored);
    *passed_dead     = without_the_test >= 0 && !far[without_the_test].stands;
    return answer;
}
