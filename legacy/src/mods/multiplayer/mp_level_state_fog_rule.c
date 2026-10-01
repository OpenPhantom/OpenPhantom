/* mp_level_state_fog_rule.c: the director's fog followed from its commands. See the header. */
#include "mp_level_state_fog_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static float float_of(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint32_t bits_of(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* What is left of a ramp in the unit the director replays, rounded up so a replayed ramp never
 * ends before the other side's. */
static uint16_t whole_seconds(float seconds)
{
    uint32_t whole;

    if (!(seconds > 0.0f)) {
        return 0u;
    }
    if (seconds >= (float)UINT16_MAX) {
        return (uint16_t)UINT16_MAX;
    }
    whole = (uint32_t)seconds;
    if ((float)whole < seconds) {
        ++whole;
    }
    return (uint16_t)whole;
}

void mp_level_fog_model_init(mp_level_fog_model_t *model)
{
    if (model != NULL) {
        memset(model, 0, sizeof *model);
    }
}

/* The engine tests every value against zero with `fcomp` and skips on "below", and an unordered
 * compare sets that flag too, so a NaN is refused like a negative. */
static bool engine_takes(float value)
{
    return value >= 0.0f;
}

static mp_level_fog_effect_t apply_edge(mp_level_fog_state_t *s, uint8_t flag, uint32_t *word,
                                        uint32_t a2)
{
    if (!engine_takes(float_of(a2))) {
        return MP_LEVEL_FOG_IGNORED;
    }
    if ((s->flags & flag) != 0u && *word == a2) {
        return MP_LEVEL_FOG_SAME;
    }
    *word = a2;
    s->flags = (uint8_t)((s->flags | flag) & ~MP_LEVEL_FOG_RESTORED);
    return MP_LEVEL_FOG_CHANGED;
}

static mp_level_fog_effect_t apply_ramp(mp_level_fog_model_t *model, bool keep_world, int32_t a1,
                                        uint32_t a2)
{
    mp_level_fog_state_t *s = &model->state;

    if (a1 <= 0 || !engine_takes(float_of(a2))) {
        return MP_LEVEL_FOG_IGNORED;
    }
    if (!keep_world) {
        s->colour[0] = (uint8_t)MP_LEVEL_FOG_GREEN_RED;
        s->colour[1] = (uint8_t)MP_LEVEL_FOG_GREEN_GREEN;
        s->colour[2] = (uint8_t)MP_LEVEL_FOG_GREEN_BLUE;
        s->flags |= (uint8_t)MP_LEVEL_FOG_COLOUR;
        s->flags = (uint8_t)(s->flags & ~MP_LEVEL_FOG_KEEP_WORLD);
    } else {
        s->flags |= (uint8_t)MP_LEVEL_FOG_KEEP_WORLD;
    }
    s->target            = a2;
    s->span              = bits_of((float)a1);
    model->seconds_left  = (float)a1;
    s->left              = whole_seconds(model->seconds_left);
    s->flags = (uint8_t)((s->flags | MP_LEVEL_FOG_RAMP) & ~MP_LEVEL_FOG_RESTORED);
    return MP_LEVEL_FOG_CHANGED;
}

mp_level_fog_effect_t mp_level_fog_apply(mp_level_fog_model_t *model, int32_t command,
                                         int32_t a1, uint32_t a2)
{
    if (model == NULL) {
        return MP_LEVEL_FOG_NOT_FOG;
    }
    switch (command) {
    case MP_LEVEL_FOG_START:
        return apply_edge(&model->state, (uint8_t)MP_LEVEL_FOG_HAS_START, &model->state.cur, a2);
    case MP_LEVEL_FOG_END:
        return apply_edge(&model->state, (uint8_t)MP_LEVEL_FOG_HAS_END, &model->state.end, a2);
    case MP_LEVEL_FOG_RAMP_GREEN:
        return apply_ramp(model, false, a1, a2);
    case MP_LEVEL_FOG_RAMP_BACK:
        return apply_ramp(model, true, a1, a2);
    default:
        return MP_LEVEL_FOG_NOT_FOG;
    }
}

bool mp_level_fog_tick(mp_level_fog_model_t *model, float seconds)
{
    mp_level_fog_state_t *s;
    uint16_t              left;

    if (model == NULL || !(model->seconds_left > 0.0f)) {
        return false;
    }
    s = &model->state;
    model->seconds_left -= seconds;
    if (model->seconds_left > 0.0f) {
        left = whole_seconds(model->seconds_left);
        if (left == s->left) {
            return false;
        }
        s->left = left;
        return true;
    }
    /* Run out: the target becomes the start, and a ramp back puts the level's own fog in place of
     * everything the script had set. */
    model->seconds_left = 0.0f;
    s->left             = 0u;
    s->cur              = s->target;
    s->flags |= (uint8_t)MP_LEVEL_FOG_HAS_START;
    if ((s->flags & MP_LEVEL_FOG_KEEP_WORLD) != 0u) {
        s->flags = (uint8_t)(s->flags & ~(MP_LEVEL_FOG_HAS_START | MP_LEVEL_FOG_HAS_END |
                                          MP_LEVEL_FOG_COLOUR | MP_LEVEL_FOG_KEEP_WORLD));
        s->flags |= (uint8_t)MP_LEVEL_FOG_RESTORED;
    }
    return true;
}

/* Written as a NOT, so that a NaN takes the engine's own step rather than a ramp that never
 * ends. */
float mp_level_fog_substep_seconds(float read)
{
    if (!(read > 0.0f && read <= MP_LEVEL_FOG_SUBSTEP_CEILING)) {
        return MP_LEVEL_FOG_SUBSTEP_SECONDS;
    }
    return read;
}

float mp_level_fog_start_now(const mp_level_fog_state_t *state)
{
    float target;
    float cur;
    float span;
    float left;

    if (state == NULL) {
        return 0.0f;
    }
    cur = float_of(state->cur);
    if (state->left == 0u || (state->flags & MP_LEVEL_FOG_RAMP) == 0u) {
        return cur;
    }
    target = float_of(state->target);
    span   = float_of(state->span);
    left   = (float)state->left;
    if (!(span > 0.0f)) {
        return target;
    }
    if (left > span) {
        left = span;
    }
    /* The engine's own step: the target, less what is still to go of the way from the start. */
    return target - (target - cur) * left / span;
}

static void put(mp_level_fog_command_t *out, size_t capacity, size_t *n, int32_t command,
                int32_t a1, uint32_t a2)
{
    if (*n < capacity) {
        out[*n].command = command;
        out[*n].a1      = a1;
        out[*n].a2      = a2;
        ++*n;
    }
}

size_t mp_level_fog_heal_plan(const mp_level_fog_state_t *state, mp_level_fog_command_t *out,
                              size_t capacity)
{
    size_t n = 0;

    if (state == NULL || out == NULL || state->flags == 0u) {
        return 0u;
    }
    if ((state->flags & MP_LEVEL_FOG_RESTORED) != 0u) {
        put(out, capacity, &n, MP_LEVEL_FOG_RAMP_BACK, 1, state->cur);
        return n;
    }
    if ((state->flags & MP_LEVEL_FOG_HAS_START) != 0u) {
        put(out, capacity, &n, MP_LEVEL_FOG_START, 0, bits_of(mp_level_fog_start_now(state)));
    }
    if ((state->flags & MP_LEVEL_FOG_HAS_END) != 0u) {
        put(out, capacity, &n, MP_LEVEL_FOG_END, 0, state->end);
    }
    if (state->left != 0u) {
        /* A ramp back that runs on green was preceded by a green ramp: the colour first, then the
         * ramp that is still running, whose words the second call writes over the first's. */
        if ((state->flags & MP_LEVEL_FOG_KEEP_WORLD) != 0u) {
            if ((state->flags & MP_LEVEL_FOG_COLOUR) != 0u) {
                put(out, capacity, &n, MP_LEVEL_FOG_RAMP_GREEN, 1, state->target);
            }
            put(out, capacity, &n, MP_LEVEL_FOG_RAMP_BACK, (int32_t)state->left, state->target);
        } else {
            put(out, capacity, &n, MP_LEVEL_FOG_RAMP_GREEN, (int32_t)state->left, state->target);
        }
    } else if ((state->flags & MP_LEVEL_FOG_COLOUR) != 0u) {
        /* The green stands with no ramp running: a one second ramp from the start to itself gives
         * the colour and the fog bit and moves nothing. */
        put(out, capacity, &n, MP_LEVEL_FOG_RAMP_GREEN, 1, state->cur);
    }
    return n;
}
