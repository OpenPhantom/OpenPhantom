/* mp_wear_rule.c: the three rules for a far body that may wear a borrowed model. See the header. */
#include "mp_wear_rule.h"

#include "mp_starter_rule.h"

#include "common/model_wear_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool mp_wear_rule_same_name(const char *a, const char *b)
{
    size_t i;

    a = (a != NULL) ? a : "";
    b = (b != NULL) ? b : "";
    for (i = 0; lower(a[i]) == lower(b[i]); ++i) {
        if (a[i] == '\0') {
            return true;
        }
    }
    return false;
}

/* Finite by the bit pattern: an exponent of all ones is an infinity or a NaN and nothing else. */
static bool finite(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

static bool answered_worn(const mp_wear_answer_t *answer)
{
    return answer != NULL && answer->read && answer->state == MODEL_WEAR_STATE_WORN;
}

bool mp_wear_rule_rebuild(bool worn, const mp_wear_answer_t *answer, const char *wanted,
                          uint32_t stood_ms)
{
    if (answer == NULL || !answer->read) {
        return false;
    }
    if (answer->state == MODEL_WEAR_STATE_REFUSED && answer->reason == MODEL_WEAR_REASON_BROKEN) {
        return true;    /* half a model and half a hero; only a new body is a known state */
    }
    if (stood_ms < MP_WEAR_SETTLE_MS) {
        return false;
    }
    return worn && !mp_wear_rule_same_name(answer->echo, wanted);
}

bool mp_wear_rule_deathmatch_asks(bool ready_weapon, const mp_wear_answer_t *answer)
{
    return ready_weapon && !(answered_worn(answer) && !answer->weapon);
}

bool mp_wear_rule_rig_is_senders(bool worn, const mp_wear_answer_t *answer, const char *sender)
{
    if (sender == NULL || sender[0] == '\0') {
        return !worn;
    }
    return answered_worn(answer) && mp_wear_rule_same_name(answer->echo, sender);
}

bool mp_wear_rule_scale(float factor, float actor_scale, bool worn, const mp_wear_answer_t *answer,
                        float *out)
{
    float base;
    float product;

    if (out == NULL) {
        return false;
    }
    if (worn) {
        if (!answered_worn(answer)) {
            return false;
        }
        base = answer->scale;
    } else {
        base = actor_scale;
    }
    /* Written as the test for being inside, so a NaN factor reads as never asked. */
    product = (factor > 0.0f ? factor : 1.0f) * base;
    if (!(product > 0.0f) || !finite(product)) {
        return false;
    }
    *out = product;
    return true;
}

bool mp_wear_rule_blade_step(float size, float substep, uint32_t weapon, uint32_t requested,
                             mp_wear_blade_step_t *out)
{
    if (out == NULL || !finite(size) || !(substep >= 0.0f) || !finite(substep)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->size = size;
    if (weapon == MP_STARTER_SLOT_SABRE && out->size != 1.0f) {
        out->size = substep * MP_WEAR_BLADE_GROW_RATE + out->size;
        if (out->size > 1.0f) {
            out->size = 1.0f;
        }
        out->grew = true;
    }
    if (requested != MP_STARTER_SLOT_SABRE && out->size != 0.0f) {
        out->size = out->size - substep * MP_WEAR_BLADE_SHRINK_RATE;
        if (out->size < 0.0f) {
            out->size = 0.0f;
        }
        out->shrank = true;
    }
    out->power = out->size;
    out->range = out->size * MP_WEAR_BLADE_LIGHT_RANGE;
    return out->grew || out->shrank;
}
