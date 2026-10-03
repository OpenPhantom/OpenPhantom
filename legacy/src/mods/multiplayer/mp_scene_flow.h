/* mp_scene_flow.h: a scene of the host's, as the state machines that run it.
 *
 * Layer 1, pure. The binding that reads the engine and calls it is mp_scene_host.c; what is here
 * is every decision it makes, so that a test can walk each path with no game in the process.
 *
 * The rule: a scene belongs to the host alone. A client is in no scene and goes on playing through
 * it. When a far player sets a scene off, a lock or a scene with the hero as an actor, the host is
 * brought to the place that player's body was tested at, and the scene waits until he stands
 * there; then it runs as it does with nobody else in the world. Nobody else is waited for.
 *
 * Two machines:
 *
 *   The HOST's scene: none, gathering, running, over. A scene a far player set off begins held:
 *   the actor whose script opened its door and the hero's grab wait while the host is brought,
 *   and the hold falls on the first look that finds him standing at his place. It never falls
 *   with the host dead or away from his place, and it waits while the place is still being read.
 *   That waiting ends at MP_SCENE_WAIT_CAP_SUBSTEPS from the beginning, unless the engine's
 *   respawn is bringing the host: a hero scene is given up, nothing is held any more, and the
 *   engine plays it here as it would alone; a lock is dropped as no scene of the host's. With no
 *   place to be brought to, the host of a hero scene is released where he stands and a lock is
 *   dropped at once. With every far player gone the hold falls at once, unless the host is on his
 *   way to a place already, which he then finishes. A scene the host set off himself begins
 *   running, with nothing held. A warp holds nothing either and is over when the host has
 *   landed. A second scene while one is held or runs, or while one given up may still be played
 *   here, is counted and begins nothing: the engine has one lock, one camera and one hero. A
 *   warp is the exception, because the engine moves the host whatever this does. So is a hero
 *   its own actor spawns in the substep of its lock, which makes that scene a hero's.
 *
 *   One player's SEAT: wait until the body may be moved, fade out, hand the seat to the placement,
 *   wait until the body stands there, fade in. The host is brought to his place this way; every
 *   way out of it takes a held fade back. A seat of a gathering is not given up while its scene
 *   wants it: a try that did not take is tried again, twice, and a body that stays in a mode the
 *   teleport may not move is brought by the engine's own respawn with its own hero, once.
 *
 * Where the place is, and whether a body stands at it, is mp_scene_room.
 *
 * References: Source's scripted sequence moves its actor onto the mark rather than walking it
 * there, and starts only once the actor stands there alive; a body in a state the plain move
 * cannot take is moved by the engine's own respawn, the state change Quake 3's TeleportPlayer and
 * Unreal's SetMovementMode make for a teleport out of any movement.
 */
#ifndef MULTIPLAYER_MP_SCENE_FLOW_H
#define MULTIPLAYER_MP_SCENE_FLOW_H

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The longest a scene waits for its host, from its beginning until it is seen running: the hold
 * while the host lies dead or is on his way, and a hero scene's grab while the host may not be
 * moved. Twenty seconds, the seat search's own bound for a wait. At it the scene is given up
 * rather than played on with a corpse or without its grab. */
#define MP_SCENE_WAIT_CAP_SUBSTEPS 640u

/* After a hold, how long the engine's grab of the hero or the lock may fail to show while the host
 * could be taken, before the scene is taken for one that never ran: two seconds of substeps in
 * which the host may be moved. A host that may not be moved is waited for, up to the cap. */
#define MP_SCENE_GRAB_GRACE_SUBSTEPS 64u

/* How long a warp's landing may take: the engine's own fade out of a second, the respawn, and its
 * fade in, with room. */
#define MP_SCENE_WARP_CAP_SUBSTEPS 192u

/* How near its seat a body has to stand to count as arrived, in world units. */
#define MP_SCENE_ARRIVED_DISTANCE 1.0f

/* How many times a gathering's seat tries again after a try that did not take: a fade that ended
 * with the body in a mode it may not be moved from, or a placement the body did not take. */
#define MP_SCENE_SEAT_RETRIES 2u

/* After how many substeps in a mode the teleport may not move a gathering's seat takes the hard
 * way, counted over every try: a second for the host, whose scene waits for him, and two for a
 * seat nothing waits for. */
#define MP_SCENE_HARD_HOST_SUBSTEPS   32u
#define MP_SCENE_HARD_CLIENT_SUBSTEPS 64u

/* How long the hard way may take, from the respawn asked for to the body standing on its seat:
 * the engine's own fade out of a second, the respawn, and its fade in, with room. */
#define MP_SCENE_RESPAWN_SUBSTEPS 192u

/* The fade a player is moved under, in seconds. */
#define MP_SCENE_FADE_SECONDS 0.25f

/* From the hand-over to the body standing on its seat, in substeps: the placement takes a frame
 * and the body is written by the next player tick. */
#define MP_SCENE_PLACE_SUBSTEPS 32u

/* How long a seat that sets itself a bound waits for its body to be movable, in substeps: ten
 * seconds (wait_max of the seat's flow). A player dead, swimming or at a gun would otherwise be
 * moved minutes later. */
#define MP_SCENE_WARP_WAIT_SUBSTEPS 320u

/* No world slot is known for the player a scene's script meant. */
#define MP_SCENE_TRIGGER_UNKNOWN 0xFFu

/* Where a scene of the host's stands. */
typedef enum mp_scene_phase {
    MP_SCENE_PHASE_NONE = 0,
    MP_SCENE_PHASE_GATHERING,   /* held while the host is brought to its place; a warp until the
                                 * host has landed */
    MP_SCENE_PHASE_RUNNING,
    MP_SCENE_PHASE_OVER         /* given up, and the engine may still play it here */
} mp_scene_phase_t;

/* What set a scene off. */
typedef enum mp_scene_kind {
    MP_SCENE_KIND_LOCK = 0,
    MP_SCENE_KIND_HERO,
    MP_SCENE_KIND_WARP,
    MP_SCENE_KINDS
} mp_scene_kind_t;

/* What a scene of the host's knows of itself, for a line spoken while it stands: its place and
 * its number. Read only while the scene stands, and only on the host; a client is in no scene.
 * `gathered` says the scene is this machine's player's own, which on the host it always is. */
typedef struct mp_scene_known {
    bool      anchor_known;
    float     anchor[3];
    bool      gathered;
    uint16_t  serial;         /* the scene's number, as the host counts them */
} mp_scene_known_t;

/* ==============================================================================================
 * The host's scene.
 * ============================================================================================ */

typedef enum mp_scene_release {
    MP_SCENE_RELEASE_NONE = 0,
    MP_SCENE_RELEASE_AT_THE_PLACE,   /* the host stood at his place */
    MP_SCENE_RELEASE_ALONE,          /* every far player left the session, and the host was not
                                      * on his way to a place */
    MP_SCENE_RELEASE_NOBODY          /* a hero scene, and the host has no place: neither the place
                                      * of the player the script meant nor one beside the scene's
                                      * actor could be stood on */
} mp_scene_release_t;

/* Why a hold fell, as the host's line says it after "because". */
const char *mp_scene_release_text(mp_scene_release_t released);

/* Why a lock a far player set off was dropped as no scene of the host's. A lock scene is played
 * where its trigger stands, a lift above all, and a host who cannot be brought there has no part
 * in it: the binding lets him go and the lock's script plays on for the player it meant. A hero
 * scene is never dropped, because the engine takes the host for it wherever he stands. */
typedef enum mp_scene_drop {
    MP_SCENE_DROP_NONE = 0,
    MP_SCENE_DROP_NO_PLACE,     /* the host has no place: a mover, nothing free, none to read */
    MP_SCENE_DROP_AT_THE_CAP    /* the wait for the host ran out */
} mp_scene_drop_t;

/* Why a lock was dropped, as the host's line says it after "because". */
const char *mp_scene_drop_text(mp_scene_drop_t dropped);

/* What the host was when a scene was given up, as the host's line says it after "the host". */
const char *mp_scene_given_up_text(mp_scene_move_t host);

/* Why a body could not be moved, as a line says it in brackets. */
const char *mp_scene_move_text(mp_scene_move_t move);

typedef struct mp_scene_host_flow {
    mp_scene_phase_t   phase;
    mp_scene_kind_t    kind;
    uint16_t           serial;          /* 0 before the first scene; never started over */
    bool               holds;           /* the scene's actor and the hero's grab are held */
    uint32_t           began;           /* substep counts */
    uint32_t           last;
    uint32_t           phase_since;
    uint32_t           hold_standing;   /* held substeps with the host standing */
    uint32_t           hold_dead;       /* and with it dead */
    bool               seen_running;
    mp_scene_release_t released;
    uint32_t           grace;           /* substeps with no grab while the host could be taken */
    /* The wait ran out. Until the engine has played the scene here, or plainly will not, a door
     * of it is no scene of its own. */
    bool               given_up;
    mp_scene_move_t    given_up_for;    /* the host then: dead, or where it could not be moved */
    /* A held lock scene ended as no scene of the host's, by the step that set this. The binding
     * reads it in that substep, before the one exit takes it away. */
    mp_scene_drop_t    dropped;
} mp_scene_host_flow_t;

typedef enum mp_scene_begin {
    MP_SCENE_BEGIN_NEW = 0,     /* a scene of its own */
    MP_SCENE_BEGIN_SECOND,      /* inside a scene that is held or runs: counted, nothing begun */
    MP_SCENE_BEGIN_WARP_OVER    /* a warp inside a scene: it takes over */
} mp_scene_begin_t;

/* A door. `holds` is the caller's choice for a lock or a hero scene: with it the scene begins
 * gathering, its actor and the hero's grab held until the host stands at his place; without it
 * the scene begins running, as one the host set off himself does. A warp never holds and gathers
 * until the host has landed. */
mp_scene_begin_t mp_scene_host_begin(mp_scene_host_flow_t *flow, mp_scene_kind_t kind,
                                     uint32_t now, bool holds);

/* A scene the engine runs with no door heard, taken over as the host's (mp_scene_doorless): a new
 * number, and running at once, seen running and with nothing held, because it runs already. Every
 * field a beginning sets is set. Only from none and never a warp; a door heard while it runs is a
 * second scene, counted. True when it was taken. */
bool mp_scene_host_adopt(mp_scene_host_flow_t *flow, mp_scene_kind_t kind, uint32_t now);

/* What the host's binding reads once a substep. */
typedef struct mp_scene_host_look {
    uint32_t now;
    bool     host_stands;       /* the engine's live player test */
    bool     running;           /* mp_scene_running on the host's own cells */
    bool     warp_landed;       /* the host's respawn has come back to a running module */
    bool     alone;             /* no far player is in the session any more */
    mp_scene_move_t may_move;   /* mp_scene_may_move for the host: the engine grabs only a YES */
    bool     has_place;         /* the host has a place he is brought to or stands at */
    bool     no_place;          /* the place was looked for and there is none: the host stays */
    bool     place_pending;     /* the place of the player the script meant is read again */
    bool     away;              /* the host has a place and does not stand at it, with his seat
                                 * done and the module running */
    bool     own_respawning;    /* the engine's respawn brings the host to his place: his seat
                                 * is in it, and the module has left its running state */
} mp_scene_host_look_t;

/* One substep. True when the phase changed. */
bool mp_scene_host_step(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look);

/* A hero's door opened by the scene's own actor in the substep its lock began the scene: one
 * script raises the lock and spawns the hero right behind it. The scene is a hero scene from
 * there, so its place is looked for as a hero's and it is never dropped. True when it became
 * one; false for any other scene, and for a hero's door a substep later, which is a second
 * scene. */
bool mp_scene_host_takes_the_hero(mp_scene_host_flow_t *flow, uint32_t now);

/* The one exit, for every way out of a scene's world: a level ending or changing, the session
 * ending. Back to none with nothing held. True when a hold stood, which is what the caller has to
 * give back. */
bool mp_scene_host_leave(mp_scene_host_flow_t *flow);

/* Whether a scene of the host's stands: a lock or a hero scene held or running, or one given up
 * that the engine may still play here. A warp is no scene, and neither is none. While this holds
 * the far bodies are passable on the host, and a door of a far player's run begins no second
 * scene. */
bool mp_scene_host_stands_now(const mp_scene_host_flow_t *flow);

/* Whether the scene may still be played here: released into running, or given up and not seen
 * done by the engine. What the end of a level asks, because a scene whose script never reaches its
 * end runs until then. */
bool mp_scene_host_still_running(const mp_scene_host_flow_t *flow);

/* ==============================================================================================
 * One player's seat.
 * ============================================================================================ */

typedef enum mp_scene_seat_stage {
    MP_SCENE_SEAT_IDLE = 0,
    MP_SCENE_SEAT_WAITING,    /* a seat, and a body that may not be moved yet */
    MP_SCENE_SEAT_FADING,     /* the screen goes dark */
    MP_SCENE_SEAT_PLACED,     /* handed to the placement; waiting for the body to stand there */
    MP_SCENE_SEAT_DONE,
    MP_SCENE_SEAT_GIVEN_UP,
    MP_SCENE_SEAT_RESPAWNING  /* the engine's respawn takes the body to the seat, under its own
                               * fade */
} mp_scene_seat_stage_t;

typedef enum mp_scene_seat_act {
    MP_SCENE_SEAT_ACT_NONE = 0,
    MP_SCENE_SEAT_ACT_FADE_OUT,
    MP_SCENE_SEAT_ACT_PLACE,
    MP_SCENE_SEAT_ACT_FADE_IN,
    MP_SCENE_SEAT_ACT_RESPAWN   /* the engine's respawn with the player's own hero, onto the seat */
} mp_scene_seat_act_t;

/* Why a seat was given up. */
typedef enum mp_scene_seat_end {
    MP_SCENE_SEAT_END_NONE = 0,
    MP_SCENE_SEAT_END_SCENE,       /* what it belongs to wants it no longer */
    MP_SCENE_SEAT_END_DEADLINE,    /* a plain seat: a fade ended unmovable, or a placement not
                                    * taken */
    MP_SCENE_SEAT_END_WAITED_OUT,  /* the body never became movable within the seat's own bound */
    MP_SCENE_SEAT_END_TRIES        /* a gathering's: out of tries, and the hard way closed */
} mp_scene_seat_end_t;

typedef struct mp_scene_seat_flow {
    mp_scene_seat_stage_t stage;
    uint32_t              since;
    uint32_t              last;            /* the substep of the last look */
    float                 fade_seconds;
    bool                  fade_held;       /* a fade out stands that no fade in has answered */
    bool                  fade_on_clock;   /* the fade ended on this flow's own deadline */
    uint32_t              fade_since;      /* when the fade out that stands began */
    mp_scene_move_t       refused;         /* why the body could not be moved, the last time */
    bool                  at_seat;         /* DONE with the body on its seat */
    uint32_t              wait_max;        /* substeps the body is waited for, 0 for no bound */
    bool                  waited_out;      /* GIVEN_UP because that wait ran out */
    mp_scene_seat_end_t   ended;           /* why GIVEN_UP, or IDLE out of the hard way */

    /* A gathering's seat, rather than a plain one: tried again rather than given up while its
     * scene wants it, and taken the hard way after `hard_after` substeps in a mode the teleport
     * may not move, 0 for no hard way. */
    bool                  gathering;
    uint32_t              hard_after;
    uint32_t              retries;         /* tries that did not take, tried again */
    bool                  tries_spent;     /* the last one did not take either */
    uint32_t              mode_wait;       /* substeps in a mode the teleport may not move */
    bool                  hard_spent;      /* the hard way was taken, which it is once */
    bool                  hard_wanted;     /* the hard way was due at the last look and not taken */
    bool                  respawn_left;    /* the module has been seen away from running */
    bool                  respawn_failed;  /* the hard way ran out of time and the seat waits */
    bool                  by_respawn;      /* DONE by the engine's respawn */
} mp_scene_seat_flow_t;

/* A plain seat: waits, fades, places and fades back, and gives up when a try does not take. */
void mp_scene_seat_start(mp_scene_seat_flow_t *flow, uint32_t now, float fade_seconds);

/* A gathering's seat: the same, tried again rather than given up while its scene wants it, and
 * taken the hard way after `hard_after` substeps in a mode the teleport may not move. */
void mp_scene_seat_start_gathering(mp_scene_seat_flow_t *flow, uint32_t now, float fade_seconds,
                                   uint32_t hard_after);

typedef struct mp_scene_seat_look {
    uint32_t        now;
    bool            live;        /* what the seat belongs to still wants it */
    mp_scene_move_t may_move;
    bool            fade_done;   /* the engine's own tint says the fade is over */
    bool            at_seat;     /* the body stands at the seat */
    bool            hard_ready;  /* mp_scene_bind_hard_way_open: the hard way may take the body */
    bool            module_running;   /* the player module reads its running state */
    bool            keep;        /* a seat to be kept once reached: the host's, while its hold
                                  * stands. A body that leaves it is brought back */
} mp_scene_seat_look_t;

/* How long a fade may take before the flow goes on without the engine's word, in substeps. */
uint32_t mp_scene_seat_fade_deadline(float fade_seconds);

mp_scene_seat_act_t mp_scene_seat_step(mp_scene_seat_flow_t *flow,
                                       const mp_scene_seat_look_t *look);

/* The exit for any way out: FADE_IN when a fade out still stands, and the flow is idle after. */
mp_scene_seat_act_t mp_scene_seat_leave(mp_scene_seat_flow_t *flow);

/* Whether the engine's respawn is under way for this seat: asked, and the module has left its
 * running state since. A respawn the engine declined in silence never is. */
bool mp_scene_seat_respawn_under_way(const mp_scene_seat_flow_t *flow);

/* ==============================================================================================
 * The host's input while he is brought to the place of a scene.
 * ============================================================================================ */

/* What the hold of the host's input looks at, at the head of a substep and again after his seat
 * has stepped. */
typedef struct mp_scene_input_look {
    bool                  hosting;      /* the scene module is installed and this machine hosts */
    mp_scene_phase_t      phase;
    bool                  holds;        /* the scene's hold stands */
    mp_scene_move_t       may_move;     /* mp_scene_may_move for the host */
    bool                  has_seat;     /* the host has a place to be brought to */
    mp_scene_seat_stage_t stage;        /* and his seat's stage */
    bool                  fade_held;    /* his seat holds the screen dark */
    bool                  held;         /* the input is held now */
    bool                  seen_running;
    uint32_t              since_phase;  /* substeps since the phase began */
} mp_scene_input_look_t;

/* Whether the host's input is held, and when it is not, why. */
typedef enum mp_scene_input {
    MP_SCENE_INPUT_HELD = 0,
    MP_SCENE_INPUT_NONE,          /* nothing to hold: the host has no place to be brought to */
    MP_SCENE_INPUT_RUNS,          /* the grab was seen, the scene runs */
    MP_SCENE_INPUT_NO_GRAB,       /* the grab did not show within its grace */
    MP_SCENE_INPUT_OVER,          /* the scene is over, given up, or a warp that holds nobody */
    MP_SCENE_INPUT_DEAD,
    MP_SCENE_INPUT_TRY,           /* a try did not take, and the screen is back */
    MP_SCENE_INPUT_NOT_HOSTING,
    MP_SCENE_INPUTS
} mp_scene_input_t;

/* While the hold stands the input is held for a host in the fade, placed, at his place, or
 * waiting in the dark for his body to come down: from each of those only his own jump would take
 * him away again. After the hold falls it is held on, if it was held, until the grab is seen, at
 * most MP_SCENE_GRAB_GRACE_SUBSTEPS, because the player's task may run before the enemies' in the
 * substep the grab comes in. Never for a dead host, who needs no input to come back, and never for
 * a host with no place, who is not moved at all. The caller writes the hold only when this
 * changes. */
mp_scene_input_t mp_scene_input_hold(const mp_scene_input_look_t *look);

/* Why the hold let go, as the host's line says it after "is let go: ". */
const char *mp_scene_input_text(mp_scene_input_t why);

#endif /* MULTIPLAYER_MP_SCENE_FLOW_H */
