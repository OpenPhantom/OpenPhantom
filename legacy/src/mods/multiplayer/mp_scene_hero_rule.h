/* mp_scene_hero_rule.h: whether the hero a scene drives still gets anywhere, as arithmetic.
 *
 * Layer 1, pure: it is handed numbers and answers with numbers, and knows no engine cell. The
 * binding that reads the actor and writes it is mp_scene_hero_watch.
 *
 * In a scene the engine drives the host's own body through a scripted actor, and that actor's walk
 * opcode has no deadline: it answers "not there yet" until the body stands in the box around its
 * goal, and the script waits on it for good. A walk that cannot arrive, at a step up the walk
 * refuses, on a slope, against a body in its way, holds the scene, its camera and every player's
 * lock until the level ends. This decides when such a walk is stuck, and the answer is to put the
 * body onto the goal so that the next walk arrives.
 *
 * A walking wish is the walk opcode run in this tick, with a speed that is not nought and an even
 * move mode. Nought only turns, a negative speed walks backwards, and an odd mode walks through
 * bodies and geometry and cannot stick.
 *
 * The goal is read off the actor's stored target, which holds a detour point instead while the walk
 * goes round an obstacle, and still holds it in the tick the walk arrives there with the detour
 * already cleared. So a reading is the real goal only when no detour was flagged in this walking
 * tick nor in the one before, and its target is not the detour point seen last. The clock starts on
 * a real goal, runs on while detours come and go, and starts again only when a later real goal
 * stands more than a quarter unit from the one it started on. A new waypoint means the goal was
 * reached: the goal is dropped until the next real reading, so a body is never put back onto a goal
 * it has passed.
 *
 * A walk whose way is blocked from the first tick of its waypoint goes round from the start, and a
 * chain of detours may never show its own goal. With no goal known there is nothing to measure and
 * nowhere to put the body, and no goal is guessed: such walking substeps are counted, and every 96
 * of them in a row are named, so a hero that hangs this way is seen in the log.
 *
 * Progress is the best distance in the plane to the goal; 96 walking substeps in which it does not
 * fall by a tenth of a unit are a stuck walk.
 */
#ifndef MULTIPLAYER_MP_SCENE_HERO_RULE_H
#define MULTIPLAYER_MP_SCENE_HERO_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_SCENE_HERO_STILL_SUBSTEPS 96u     /* three seconds at 32 substeps a second */
#define MP_SCENE_HERO_PROGRESS       0.1f    /* units the best distance has to fall by */
#define MP_SCENE_HERO_GOAL_MOVED     0.25f   /* a goal this far from the clock's is a new one */
#define MP_SCENE_HERO_WALK_MEMORY    32u     /* substeps a walking wish is remembered for */
#define MP_SCENE_HERO_SAME_POINT     0.01f   /* a detour point read again, within float noise */

/* One look at the actor after the enemy tick of a substep. */
typedef struct mp_scene_hero_reading {
    uintptr_t actor;            /* which actor, nought for none */
    bool      move_requested;   /* the walk opcode ran in this tick */
    float     move_speed;
    int32_t   move_mode;
    bool      detour;           /* the walk goes round an obstacle: the target is a detour point */
    int32_t   waypoint;
    float     target[3];        /* the stored target */
    float     position[3];
} mp_scene_hero_reading_t;

typedef struct mp_scene_hero_clock {
    uintptr_t actor;            /* the actor the clock is about, nought for none */
    bool      walked;           /* a walking wish was seen since the clock was reset */
    uint32_t  last_walk;        /* and the substep of the last one */
    int32_t   waypoint;
    bool      detour_before;    /* the last walking tick flagged a detour */
    bool      detour_seen;      /* detour_point holds one */
    float     detour_point[3];
    bool      detour_ran;       /* a detour was flagged on this waypoint */
    bool      goal_known;
    float     goal[3];          /* the latest real goal */
    float     goal_start[3];    /* the real goal the clock started on */
    float     distance;         /* to the goal in the plane, at the last walking tick */
    float     best;             /* the least of those since the clock started */
    float     mark;             /* the best at the last progress */
    uint32_t  still;            /* walking substeps since the last progress */
    uint32_t  longest_still;
    uint32_t  blind;            /* walking substeps in a row with no real goal known */
    uint32_t  longest_blind;
} mp_scene_hero_clock_t;

typedef enum mp_scene_hero_verdict {
    MP_SCENE_HERO_IDLE = 0,     /* no actor, or no walking wish in this tick */
    MP_SCENE_HERO_WALKING,      /* a walk, getting somewhere or not yet stuck */
    MP_SCENE_HERO_PUT,          /* stuck: put the actor onto the goal handed back */
    MP_SCENE_HERO_BLIND,        /* a walk with no real goal read yet: nothing to measure */
    MP_SCENE_HERO_LOST          /* the 96th such substep in a row: say so, put nothing */
} mp_scene_hero_verdict_t;

/* Whether the reading is a walking wish: the walk ran, the speed is not nought, the mode even. */
bool mp_scene_hero_rule_wants_to_walk(const mp_scene_hero_reading_t *reading);

void mp_scene_hero_rule_reset(mp_scene_hero_clock_t *clock);

/* One substep's reading. A NULL reading or an actor of nought resets the clock. On PUT `goal`
 * holds the real goal, never a detour point, and the clock counts again from nothing. */
mp_scene_hero_verdict_t mp_scene_hero_rule_step(mp_scene_hero_clock_t *clock, uint32_t now,
                                                const mp_scene_hero_reading_t *reading,
                                                float goal[3]);

/* Whether a walking wish was seen within the last MP_SCENE_HERO_WALK_MEMORY substeps. */
bool mp_scene_hero_rule_walked_lately(const mp_scene_hero_clock_t *clock, uint32_t now);

#endif /* MULTIPLAYER_MP_SCENE_HERO_RULE_H */
