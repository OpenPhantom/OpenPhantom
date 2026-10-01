/* mp_level_state_fog_rule.h: the director's fog as one side's engine holds it, followed from the
 * commands it plays, and the commands that bring another engine to it.
 *
 * Layer 1, pure. The engine keeps its fog in a handful of words (the start a ramp works from, the
 * ramp's target, its length and what is left of it, whether the level's own fog comes back at the
 * end) and in the device's colour and band. It saves exactly those in its savegame and puts them
 * back in the same order, start, end, colour. This follows them the way the engine's own four fog
 * functions write them, from the script's values alone: never read out of the cells, because on
 * a machine where another fix scales the level's band the cells hold that machine's band and not
 * the script's.
 *
 * What the engine does with each command, and so what this model does:
 *
 *   6  a start below zero is ignored; otherwise the start, and the one the next ramp works from.
 *   7  an end below zero is ignored; otherwise the end.
 *   11 a length below one or a target below zero is ignored; otherwise pale green, the level's fog
 *      bit on, and a ramp from the current start to the target.
 *   12 the same ramp with the colour left as it is, and the level's own fog put back when it runs
 *      out.
 *
 * A ramp counts down by the substep's time. When it runs out the start it worked towards becomes
 * the current one, and a ramp of command 12 puts the level's own fog back, which throws away every
 * start, end and colour the script set before.
 *
 * A command that leaves the model as it was took no effect. Those are the ones that do not travel:
 * a script that says the same start every substep, as the end of the gas room's does two hundred
 * times, changes nothing after the first.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_FOG_RULE_H
#define MULTIPLAYER_MP_LEVEL_STATE_FOG_RULE_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The four commands by number, and the colour the director's green ramp passes. */
#define MP_LEVEL_FOG_START       6
#define MP_LEVEL_FOG_END         7
#define MP_LEVEL_FOG_RAMP_GREEN  11
#define MP_LEVEL_FOG_RAMP_BACK   12
#define MP_LEVEL_FOG_GREEN_RED   0xAAu
#define MP_LEVEL_FOG_GREEN_GREEN 0xFFu
#define MP_LEVEL_FOG_GREEN_BLUE  0xAAu

/* The engine counts a ramp down by the time step of its substep, 1/32 s, or 1/64 s under the frame
 * rate cheat. The first is what a side assumes that could not read its own; the second bounds what
 * a reading may be, the longest step the engine ever hands a frame. */
#define MP_LEVEL_FOG_SUBSTEP_SECONDS (1.0f / 32.0f)
#define MP_LEVEL_FOG_SUBSTEP_CEILING 0.1f

/* The most commands a heal plays: a start, an end, a green ramp and the ramp back. */
#define MP_LEVEL_FOG_HEAL_MAX 4u

typedef struct mp_level_fog_model {
    mp_level_fog_state_t state;   /* what travels; its `left` is `seconds_left` rounded up */
    float                seconds_left;
} mp_level_fog_model_t;

typedef enum mp_level_fog_effect {
    MP_LEVEL_FOG_SAME = 0,   /* the command left the fog as it was */
    MP_LEVEL_FOG_CHANGED,
    MP_LEVEL_FOG_IGNORED,    /* the engine refuses its arguments and does nothing */
    MP_LEVEL_FOG_NOT_FOG     /* not one of the four */
} mp_level_fog_effect_t;

/* One director command as a side replays it: the float argument as its bits. */
typedef struct mp_level_fog_command {
    int32_t  command;
    int32_t  a1;
    uint32_t a2;
} mp_level_fog_command_t;

void mp_level_fog_model_init(mp_level_fog_model_t *model);

/* One command the engine is about to play, as the model sees it. */
mp_level_fog_effect_t mp_level_fog_apply(mp_level_fog_model_t *model, int32_t command,
                                         int32_t a1, uint32_t a2);

/* One substep. True when what travels changed: a whole second of the ramp passed, or it ran out. */
bool mp_level_fog_tick(mp_level_fog_model_t *model, float seconds);

/* The length of one substep from what this side read of it: the reading when it is a positive
 * number no longer than the ceiling, the engine's own 1/32 s otherwise. */
float mp_level_fog_substep_seconds(float read);

/* The start the engine draws right now: the ramp's point between the start it works from and its
 * target, or the start itself when no ramp runs. `left` is taken as the whole seconds that travel.
 * NaN-free for any finite state; a length of 0 is a ramp that has run out. */
float mp_level_fog_start_now(const mp_level_fog_state_t *state);

/* The director commands that bring an engine to `state` from wherever its fog stands, in the
 * engine's own order: start, end, then the ramp that gives the colour. A ramp that still runs is
 * replayed with what is left of it in whole seconds, so it ends at most a second after the other
 * side's. A state the level's own fog was put back into is reached by a one second ramp back.
 * Returns how many were written. */
size_t mp_level_fog_heal_plan(const mp_level_fog_state_t *state, mp_level_fog_command_t *out,
                              size_t capacity);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_FOG_RULE_H */
