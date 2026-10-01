/* mp_enemy_shield_rule.c: a droideka's shield as sixteen bits. See the header. */
#include "mp_enemy_shield_rule.h"

#include <stddef.h>
#include <string.h>

/* The five colours command 2 paints, in operand order, as its three colour bytes. */
static const uint8_t COLOURS[MP_ENEMY_SHIELD_COLOURS][3] = {
    { 0xFFu, 0x00u, 0x00u },
    { 0x00u, 0x00u, 0xFFu },
    { 0xC8u, 0x00u, 0xFFu },
    { 0xFFu, 0xFFu, 0x00u },
    { 0x00u, 0xFFu, 0xFFu },
};

uint32_t mp_enemy_shield_pack(const mp_enemy_shield_t *shield)
{
    uint32_t field;

    if (shield == NULL || !shield->read) {
        return 0u;
    }
    field = MP_ENEMY_SHIELD_READ;
    if (shield->hangs) {
        field |= MP_ENEMY_SHIELD_HANGS;
        field |= shield->shown ? MP_ENEMY_SHIELD_SHOWN : 0u;
        if (shield->colour <= MP_ENEMY_SHIELD_COLOURS) {
            field |= (shield->colour << MP_ENEMY_SHIELD_COLOUR_SHIFT) & MP_ENEMY_SHIELD_COLOUR_MASK;
        }
        field |= (shield->radius << MP_ENEMY_SHIELD_RADIUS_SHIFT) & MP_ENEMY_SHIELD_RADIUS_MASK;
    }
    return field;
}

mp_enemy_shield_t mp_enemy_shield_unpack(uint32_t field)
{
    mp_enemy_shield_t shield;

    memset(&shield, 0, sizeof shield);
    if ((field & MP_ENEMY_SHIELD_READ) == 0u) {
        return shield;
    }
    shield.read  = true;
    shield.hangs = (field & MP_ENEMY_SHIELD_HANGS) != 0u;
    if (shield.hangs) {
        shield.shown  = (field & MP_ENEMY_SHIELD_SHOWN) != 0u;
        shield.colour = (field & MP_ENEMY_SHIELD_COLOUR_MASK) >> MP_ENEMY_SHIELD_COLOUR_SHIFT;
        if (shield.colour > MP_ENEMY_SHIELD_COLOURS) {
            shield.colour = 0u;   /* a code nobody paints is the default */
        }
        shield.radius = (field & MP_ENEMY_SHIELD_RADIUS_MASK) >> MP_ENEMY_SHIELD_RADIUS_SHIFT;
    }
    return shield;
}

uint32_t mp_enemy_shield_eighths(float radius)
{
    float steps;

    /* Not a number and not above nought are both no radius; the first is the only value that
     * compares unequal to itself. */
    if (radius != radius || radius <= 0.0f) {
        return 0u;
    }
    steps = radius * MP_ENEMY_SHIELD_RADIUS_STEPS + 0.5f;
    if (steps < 1.0f) {
        return 1u;
    }
    return steps >= 255.0f ? 255u : (uint32_t)steps;
}

float mp_enemy_shield_radius_of(uint32_t eighths)
{
    return (float)eighths / MP_ENEMY_SHIELD_RADIUS_STEPS;
}

uint32_t mp_enemy_shield_colour(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t i;

    for (i = 0; i < MP_ENEMY_SHIELD_COLOURS; ++i) {
        if (COLOURS[i][0] == red && COLOURS[i][1] == green && COLOURS[i][2] == blue) {
            return i + 1u;
        }
    }
    return 0u;
}

bool mp_enemy_shield_agrees(const mp_enemy_shield_t *want, const mp_enemy_shield_here_t *here)
{
    bool here_hangs;

    if (want == NULL || here == NULL || !want->read) {
        return true;
    }
    here_hangs = here->id >= 0 && here->in_use;
    if (want->hangs != here_hangs) {
        return false;
    }
    return !want->hangs ||
           (want->shown == here->shown && want->colour == here->colour &&
            (want->radius == 0u || want->radius == here->radius));
}

mp_enemy_shield_plan_t mp_enemy_shield_plan(const mp_enemy_shield_t *want,
                                            const mp_enemy_shield_here_t *here)
{
    mp_enemy_shield_plan_t plan;
    bool                   here_hangs;
    bool                   word_set;

    memset(&plan, 0, sizeof plan);
    if (want == NULL || here == NULL || !want->read || mp_enemy_shield_agrees(want, here)) {
        return plan;
    }
    here_hangs = here->id >= 0 && here->in_use;
    word_set   = here->id >= 0;
    if (!want->hangs) {
        plan.release = true;   /* only reached with one hanging here */
        return plan;
    }
    if (here_hangs && here->colour == want->colour) {
        plan.show   = want->shown != here->shown;
        plan.shown  = want->shown;
        plan.size   = want->radius != 0u && want->radius != here->radius;
        plan.radius = want->radius;
        return plan;
    }
    plan.recolour = here_hangs;
    plan.release  = word_set;
    plan.create   = true;
    plan.colour   = want->colour;
    if (!want->shown) {
        plan.show  = true;
        plan.shown = false;
    }
    plan.size   = want->radius != 0u;
    plan.radius = want->radius;
    return plan;
}
