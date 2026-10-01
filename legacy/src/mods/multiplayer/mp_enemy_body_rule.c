/* mp_enemy_body_rule.c: how an enemy's body ends, as one word, and when a client writes it. See
 * the header. */
#include "mp_enemy_body_rule.h"

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The body's own flag word: bit 0 is what the renderer draws by, bit 1 the ground shadow. */
#define FLAG_DRAWN  0x01u
#define FLAG_SHADOW 0x02u

/* One frame, for the head that went back. Below it the two reads are the same position. */
#define STARTED_BACK_FRAMES 1.0f

uint32_t mp_enemy_body_pack(const mp_enemy_body_state_t *state)
{
    uint32_t field = 0;

    if (state == NULL || !state->has) {
        return 0u;
    }
    field |= MP_ENEMY_BODY_HAS;
    field |= state->drawn ? MP_ENEMY_BODY_IS_DRAWN : 0u;
    field |= state->solid ? MP_ENEMY_BODY_IS_SOLID : 0u;
    field |= state->shadow ? MP_ENEMY_BODY_HAS_SHADOW : 0u;
    field |= (uint32_t)state->alpha << MP_ENEMY_BODY_ALPHA_SHIFT;
    field |= (uint32_t)state->dissolve << MP_ENEMY_BODY_DISSOLVE_SHIFT;
    field |= ((uint32_t)state->starts & MP_ENEMY_BODY_STARTS_MASK) << MP_ENEMY_BODY_STARTS_SHIFT;
    return field;
}

void mp_enemy_body_unpack(uint32_t field, mp_enemy_body_state_t *out)
{
    if (out == NULL) {
        return;
    }
    out->has      = (field & MP_ENEMY_BODY_HAS) != 0u;
    out->drawn    = (field & MP_ENEMY_BODY_IS_DRAWN) != 0u;
    out->solid    = (field & MP_ENEMY_BODY_IS_SOLID) != 0u;
    out->shadow   = (field & MP_ENEMY_BODY_HAS_SHADOW) != 0u;
    out->alpha    = (uint8_t)((field >> MP_ENEMY_BODY_ALPHA_SHIFT) & 0xFFu);
    out->dissolve = (uint8_t)((field >> MP_ENEMY_BODY_DISSOLVE_SHIFT) & 0xFFu);
    out->starts   = (uint8_t)((field >> MP_ENEMY_BODY_STARTS_SHIFT) & MP_ENEMY_BODY_STARTS_MASK);
}

uint8_t mp_enemy_body_quantise(float level, uint8_t otherwise)
{
    if (level != level) {
        return otherwise;
    }
    if (level <= 0.0f) {
        return 0u;
    }
    if (level >= 1.0f) {
        return 255u;
    }
    return (uint8_t)(level * 255.0f + 0.5f);
}

float mp_enemy_body_level(uint8_t quantised)
{
    return (float)quantised / 255.0f;
}

uint32_t mp_enemy_body_differ(const mp_enemy_body_state_t *a, const mp_enemy_body_state_t *b)
{
    uint32_t parts = 0;

    parts |= (a->drawn != b->drawn) ? MP_ENEMY_BODY_PART_DRAWN : 0u;
    parts |= (a->solid != b->solid) ? MP_ENEMY_BODY_PART_SOLID : 0u;
    parts |= (a->shadow != b->shadow) ? MP_ENEMY_BODY_PART_SHADOW : 0u;
    parts |= (a->alpha != b->alpha) ? MP_ENEMY_BODY_PART_ALPHA : 0u;
    parts |= (a->dissolve != b->dissolve) ? MP_ENEMY_BODY_PART_DISSOLVE : 0u;
    return parts;
}

mp_enemy_body_verdict_t mp_enemy_body_decide(const mp_enemy_body_state_t *host,
                                             const mp_enemy_body_state_t *acted,
                                             const mp_enemy_body_state_t *local)
{
    mp_enemy_body_verdict_t verdict = { 0u, 0u, 0u };
    const uint32_t          every   = (1u << MP_ENEMY_BODY_PARTS) - 1u;
    uint32_t                changed;
    uint32_t                apart;

    if (host == NULL || local == NULL || !host->has) {
        return verdict;   /* a host that read no body says nothing about this one */
    }
    changed = (acted == NULL || !acted->has) ? every : mp_enemy_body_differ(host, acted);
    apart   = mp_enemy_body_differ(host, local);

    verdict.write   = changed & apart;
    verdict.already = changed & ~apart;
    verdict.left    = ~changed & apart & every;
    return verdict;
}

int32_t mp_enemy_body_class_for(bool solid, int32_t shooter_class)
{
    return solid ? shooter_class : 0;
}

uint32_t mp_enemy_body_flags_with(uint32_t flags, const mp_enemy_body_state_t *state,
                                  uint32_t parts)
{
    if (state == NULL) {
        return flags;
    }
    if ((parts & MP_ENEMY_BODY_PART_DRAWN) != 0u) {
        flags = state->drawn ? (flags | FLAG_DRAWN) : (flags & ~FLAG_DRAWN);
    }
    if ((parts & MP_ENEMY_BODY_PART_SHADOW) != 0u) {
        flags = state->shadow ? (flags | FLAG_SHADOW) : (flags & ~FLAG_SHADOW);
    }
    return flags;
}

bool mp_enemy_body_started(int32_t slot_before, uint32_t keyframe_before, float head_before,
                           int32_t slot, uint32_t keyframe, float head, bool loops)
{
    if (slot != slot_before || keyframe != keyframe_before) {
        return true;
    }
    return !loops && head + STARTED_BACK_FRAMES < head_before;
}
