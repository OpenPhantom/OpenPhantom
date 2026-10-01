/* spawn_place.h: the entity spawner's placement mode, as a state and the rules it keeps.
 *
 * The mode is a state of the panel: the panel stays open, so the player stays held and the keys
 * stay the panel's, but it is not drawn, and the pointer is free to leave it and point into the
 * world. What is under the pointer is shown as the entity that would be placed there; a left click
 * places it and the mode stays on, so a row of guards is five clicks; the wheel turns it; a right
 * click removes the copy under the pointer. All of that is spawn_mode.c's, which reaches the
 * engine. This file holds only what can be decided without it:
 *
 *   whether the mode is on, and why it went off     the world changed, the panel shut, the player
 *                                                   left or the engine side stopped answering
 *   what a click, a turn and a reset ask for        asked from a window message and taken by the
 *                                                   next frame, because a window message may land
 *                                                   in the middle of a substep and every probe of
 *                                                   the world walks the engine's one polygon list
 *   the turn                                        15 degrees a notch, 1 with Shift, 90 with Ctrl
 *                                                   snapped to the nearest quarter, kept in
 *                                                   [0, 360); or facing the player until turned
 *   the click lock                                  a double click is one entity, not two on the
 *                                                   same spot
 *   the settle                                      in a single player game the mode holds the
 *                                                   world as the panel does, and lets it run for
 *                                                   two of its substeps after each copy placed
 *   the right click                                 who may remove the copy under the pointer
 *   the reach of the pointer                        a copy behind what the ray struck is not
 *                                                   under it
 *
 * One instance, the panel's, reached through spawn_place_state(); the functions take the instance
 * so a test can drive its own. Pure: no engine, no clock of its own. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_SPAWN_PLACE_H
#define DEV_OVERLAY_SPAWN_PLACE_H

#include <stdbool.h>
#include <stdint.h>

/* 15 degrees a notch is 24 notches to the full turn; 1 degree with Shift for the fine set; 90 with
 * Ctrl, to line up with a wall. */
#define SPAWN_PLACE_TURN_STEP    15.0f
#define SPAWN_PLACE_TURN_FINE     1.0f
#define SPAWN_PLACE_TURN_COARSE  90.0f

/* A second click this soon after the one that placed is the same click. */
#define SPAWN_PLACE_CLICK_LOCK_MS 150u

/* How long the world runs after a copy is placed in a single player game, where the mode holds it
 * as the panel does. The engine's spawn writes only the actor's position; the body's position stays
 * at zero from bapobj_init until the actor's substep hands the actor's position on to it, stamping
 * the previous position first. So after the first substep the pair the draw interpolates between
 * is the world's origin and the place, and a world held then would show the copy somewhere between
 * the two for as long as it is held; after the second both are the place, and the held picture
 * shows the copy where it stands. Two substeps, counted by the
 * panel's module node, which the engine sends one message at the end of each (0x0E). A world that
 * has not run two in a quarter of a second, eight substeps at 32 a second, is not running for
 * another reason, and is held again. */
#define SPAWN_PLACE_SETTLE_SUBSTEPS 2u
#define SPAWN_PLACE_SETTLE_MAX_MS   250u

/* How far past the point the ray struck a copy still counts as under the pointer. The engine's ray
 * is a sphere of 0.15 units, which strikes with its centre that far short of the surface; a
 * little over it takes in a copy standing on the struck floor and leaves out one behind a wall. */
#define SPAWN_PLACE_HOVER_SLACK 0.25f

typedef enum spawn_place_off {
    SPAWN_PLACE_STILL_ON = 0,
    SPAWN_PLACE_OFF_ASKED,        /* Escape, the key, or the panel row again */
    SPAWN_PLACE_OFF_PANEL,        /* the panel shut: the key that opened it, or a close */
    SPAWN_PLACE_OFF_WORLD,        /* the level ended, restarted or changed */
    SPAWN_PLACE_OFF_PLAYER,       /* no player */
    SPAWN_PLACE_OFF_DIED,         /* the player died: a ghost over a corpse is nothing to work */
    SPAWN_PLACE_OFF_UNAVAILABLE   /* the engine side can no longer place */
} spawn_place_off_t;

/* What the frame tells the state, all of it read by the glue. */
typedef struct spawn_place_facts {
    bool     panel_open;
    bool     available;     /* a kind is chosen, the spawner stands, the camera reads */
    bool     player;        /* a player stands */
    bool     player_dead;   /* the engine's dead flag on the player's record is up */
    uint32_t world;         /* the world epoch, which moves with every level begun or ended */
    /* Why not, in the caller's own words, or NULL while `available` is true. Carried through so
     * that the panel's row and the mode's log line are one sentence decided once. A literal of the
     * caller's, kept as a pointer and never copied. */
    const char *unavailable;
} spawn_place_facts_t;

/* What one frame came to. */
typedef struct spawn_place_step {
    bool              entered;
    spawn_place_off_t left;       /* SPAWN_PLACE_STILL_ON unless the mode went off this frame */
    bool              refused;    /* asked on, and could not be: `available` or the panel said no */
} spawn_place_step_t;

typedef struct spawn_place {
    bool     on;
    bool     asked_on;
    bool     asked_off;
    uint32_t world;              /* the world the mode was entered in */
    bool     fixed;              /* turned by the wheel; otherwise it faces the player */
    float    facing;             /* degrees, while fixed */
    bool     click_asked;
    bool     remove_asked;
    uint32_t last_click_ms;
    bool     clicked_once;
    bool     available;          /* as the last frame found it, for the panel row */
    const char *unavailable;     /* and why not, for the row that says so; NULL when available */
    bool     settling;           /* the world runs for a copy just placed */
    bool     settle_counted;     /* the substeps can be counted; otherwise only the time ends it */
    uint32_t settle_step;        /* the substep count when it began */
    uint32_t settle_ms;          /* and the clock */
} spawn_place_t;

/* What one frame of the settle came to. */
typedef enum spawn_settle {
    SPAWN_SETTLE_IDLE = 0,
    SPAWN_SETTLE_RUNNING,
    SPAWN_SETTLE_DONE,           /* the substeps ran, this frame */
    SPAWN_SETTLE_TIMED_OUT       /* the time ran out first, this frame */
} spawn_settle_t;

/* Who may remove the copy under the pointer, in the order it is decided. */
typedef enum spawn_remove {
    SPAWN_REMOVE_DELETE = 0,     /* a single player game, or the host of a session */
    SPAWN_REMOVE_NOTHING,        /* no copy under the pointer */
    SPAWN_REMOVE_CLIENT,         /* a client cannot yet ask its host for one copy */
    SPAWN_REMOVE_RIDDEN          /* the player sits on it, and its delete frees what he rides */
} spawn_remove_t;

/* The panel's instance. */
spawn_place_t *spawn_place_state(void);

/* From the panel row or the key: turn the mode on or off. Taken by the next frame. */
void spawn_place_ask(spawn_place_t *place, bool on);

/* Once a frame, first: honours what was asked and turns the mode off when a fact says so. */
spawn_place_step_t spawn_place_frame(spawn_place_t *place, const spawn_place_facts_t *facts);

/* From a window message. A click places, a right click removes, both taken by the next frame. */
void spawn_place_ask_click(spawn_place_t *place);
void spawn_place_ask_remove(spawn_place_t *place);

/* Whether the click asked for may place now: one was asked, and it is not within the lock of the
 * last one that placed. Takes the ask either way. `now_ms` is any millisecond clock that wraps. */
bool spawn_place_take_click(spawn_place_t *place, uint32_t now_ms);

/* The right click asked for, taken. */
bool spawn_place_take_remove(spawn_place_t *place);

/* After a copy is placed: the world runs from here until SPAWN_PLACE_SETTLE_SUBSTEPS substeps have
 * passed, counted from `step` when `counted`, or SPAWN_PLACE_SETTLE_MAX_MS have. A second placement
 * inside it starts it again. `now_ms` is any millisecond clock that wraps. */
void spawn_place_settle_begin(spawn_place_t *place, bool counted, uint32_t step, uint32_t now_ms);

/* Once a frame: whether the settle still runs, and on the frame it ends, how. `ran` receives the
 * substeps counted since it began, 0 when they cannot be counted. */
spawn_settle_t spawn_place_settle_tick(spawn_place_t *place, bool counted, uint32_t step,
                                       uint32_t now_ms, uint32_t *ran);

/* The right click's decision: nothing under the pointer, then a client, then a ridden copy. */
spawn_remove_t spawn_place_remove_verdict(bool hovered, bool client, bool ridden);

/* How far along the ray a copy may be entered and still be under the pointer, from how far the
 * ray struck (SPAWN_SPOT_REACH when it struck nothing). */
float spawn_place_hover_limit(float strike);

/* Whether the cap is reached: the panel's own in a single player game, the host's in a session,
 * where a client that does not know it (0) leaves the answer to the host. */
bool spawn_place_cap_reached(bool session, uint32_t session_cap, uint32_t alive,
                             uint32_t single_cap);

/* The wheel: `notches` away from the player is positive. The first turn starts from where the
 * entity faces now, `current`, and from then on the facing stays where it is put. */
void spawn_place_turn(spawn_place_t *place, int32_t notches, bool fine, bool coarse,
                      float current);

/* Back to facing the player. */
void spawn_place_face_player(spawn_place_t *place);

/* Where the entity faces: the fixed facing, or `toward_player`. */
float spawn_place_facing(const spawn_place_t *place, float toward_player);

/* The pure arithmetic under the three calls above, exported for the test. */
float spawn_place_turned(float facing, int32_t notches, bool fine, bool coarse);
float spawn_place_wrap(float degrees);

/* The yaw, in the engine's degrees, of something standing at `from` that looks at `to`: the
 * engine's forward from a yaw is (-sin, cos). */
float spawn_place_yaw_toward(const float from[2], const float to[2]);

/* ==============================================================================================
 * Where the pointer puts the entity, and whether it may stand there.
 *
 * The ray under the pointer is cast into the world; where it strikes, the floor is looked for just
 * above the strike, so a floor struck from above and a wall struck from the side both give the
 * floor in front of what was struck; the entity stands on that floor. Then the questions that
 * refuse: a floor at all and within reach, no moving platform (no probe of the engine sees one
 * move), room over it, no copy on it, and the cap. The ray from the eye needs no line test of its
 * own: it is the line, and whatever it could not pass is what it struck.
 *
 * Only a floor makes a place. A ray that strikes nothing, or a probe that cannot be asked, is a
 * refusal and never a guess: an entity is set only where a floor holds.
 *
 * A body is not a point. The floor in front of a wall is the floor at its foot, and a body stood
 * there with its centre on it is half in the wall, which is how the spawner's first copies stood
 * (the field's "spawns in the wall"). So four short rays go out from the place at a body's
 * height, ahead along the pointer's heading, back and to both sides, each as long as the entity's
 * collision radius and a margin; what they strike pushes the place away by as much as it is
 * short, the floor is found again there, and the rays are cast once more. One that still strikes
 * is a place between two walls closer than the body is wide, and it is refused.
 * ============================================================================================ */

typedef enum spawn_spot_verdict {
    SPAWN_SPOT_OK = 0,
    SPAWN_SPOT_NOTHING_HIT,    /* the ray struck nothing within reach */
    SPAWN_SPOT_NO_FLOOR,       /* no floor under the strike, or too far under it, or no probe */
    SPAWN_SPOT_ON_MOVER,       /* the floor is a moving platform's */
    SPAWN_SPOT_WALLED,         /* no place off the walls there that the body fits */
    SPAWN_SPOT_NO_HEADROOM,
    SPAWN_SPOT_CROWDED,        /* a copy or the player stands there */
    SPAWN_SPOT_CAP             /* as many as may be alive are */
} spawn_spot_verdict_t;

/* How far the ray looks, in units. The engine's ray walks at most sixty four grid cells, one unit
 * each, and a line crosses at most about 1.41 cells a unit, so forty units stay inside the walk. */
#define SPAWN_SPOT_REACH       40.0f
/* Where a ray that strikes nothing puts the entity, to be seen and refused. */
#define SPAWN_SPOT_MISS        6.0f
/* How far above the strike the floor is looked for, and how far below that a floor may lie and
 * still be the floor of what was struck. The engine's floor probe takes a floor up to a unit above
 * the point it is asked at, so half a unit catches a step struck on its edge. */
#define SPAWN_SPOT_LIFT        0.5f
#define SPAWN_SPOT_FLOOR_REACH 3.0f
/* Keeping a body off the walls. The rays go out a unit over the floor, above a step the floor
 * probe would climb, and the engine's ray is a sphere of 0.15 units that strikes with its centre
 * that far short of the surface, so a ray is cast that much shorter than the room the body needs.
 * The second cast is a hair shorter again, so a place pushed exactly clear is not struck by the
 * rounding. */
#define SPAWN_SPOT_SIDE_HEIGHT 1.0f
#define SPAWN_SPOT_RAY_RADIUS  0.15f
#define SPAWN_SPOT_WALL_MARGIN 0.05f
#define SPAWN_SPOT_RECAST_SLACK 0.02f
/* Another body on the place: nearer than this across, and within this much of its height. */
#define SPAWN_SPOT_CROWD_UNITS  0.6f
#define SPAWN_SPOT_CROWD_HEIGHT 2.0f

/* The floor probe's three answers, floor_probe.h's. */
typedef enum spawn_spot_floor {
    SPAWN_SPOT_FLOOR_UNAVAILABLE = 0,
    SPAWN_SPOT_FLOOR_NONE,
    SPAWN_SPOT_FLOOR_FOUND
} spawn_spot_floor_t;

typedef struct spawn_spot_probes {
    /* The ray: true and the distance from `from` when it strikes. NULL never strikes. */
    bool (*hit)(void *user, const float from[3], const float to[3], float *distance);
    /* The floor under `at`: the signed offset to add to stand on it, and whether it moves. */
    spawn_spot_floor_t (*floor)(void *user, const float at[3], float *offset, bool *on_mover);
    /* Room over `at`; NULL is not asked. */
    bool (*headroom)(void *user, const float at[3]);
    /* A copy on `at`; NULL is not asked. */
    bool (*crowded)(void *user, const float at[3]);
    /* The short rays that keep a body off the walls, answered as `hit` is; NULL is not asked. */
    bool (*side)(void *user, const float from[3], const float to[3], float *distance);
} spawn_spot_probes_t;

/* What the look found besides the verdict. */
typedef struct spawn_spot_found {
    float at[3];      /* where the entity would stand, at its feet */
    float strike;     /* how far along the ray it struck, SPAWN_SPOT_REACH when nothing */
    float moved;      /* how far the place was pushed off a wall, 0 when it was not */
} spawn_spot_found_t;

/* The place under the ray from `origin` along `direction` (unit length) for a body of collision
 * radius `radius`, and whether the entity may stand there. `found->at` is always written, so a
 * refused place is still shown where it is: at the floor found, at the strike when there is no
 * floor, in front of the eye when nothing is struck. A radius of 0 or less keeps nothing off the
 * walls. */
spawn_spot_verdict_t spawn_place_spot(const spawn_spot_probes_t *probes, void *user,
                                      const float origin[3], const float direction[3],
                                      float radius, bool cap_reached, spawn_spot_found_t *found);

/* Whether another body standing at `other` takes the place `at`. */
bool spawn_place_crowds(const float at[3], const float other[3]);

/* The verdict in the few words the line under the pointer has room for. */
const char *spawn_place_spot_word(spawn_spot_verdict_t verdict);

#endif /* DEV_OVERLAY_SPAWN_PLACE_H */
