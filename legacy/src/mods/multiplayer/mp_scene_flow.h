/* mp_scene_flow.h: a scene for everybody, as the state machines that run it.
 *
 * Layer 1, pure. The binding that reads the engine and calls it is in mp_scene_host.c for the host
 * and mp_scene_client.c for a client; what is here is every decision they make, so that a test can
 * walk each path with no game in the process.
 *
 * The rule: when any player, a far client as well, sets off a scene, a scene with the hero as an
 * actor or a warp, it starts for everybody, and EVERY player is brought to the place first. The
 * host is the actor. A client in the host's scene is locked, gets the bars and keeps its own
 * camera.
 *
 * Four machines:
 *
 *   The HOST's scene: none, gathering, running, over. A lock or a hero scene holds its actor and
 *   the hero's grab while the players are gathered, and runs once everybody is seated or after
 *   MP_SCENE_HOLD_SUBSTEPS of the host standing; it never runs for everybody with the host dead,
 *   and a hero scene whose grab cannot come because the host may not be moved waits for him. All
 *   of that waiting ends at MP_SCENE_WAIT_CAP_SUBSTEPS from the beginning: then the scene is given
 *   up for everybody, the far players are let go and the engine plays it here as it would alone.
 *   With nobody else in the session nothing is held. A warp gathers the clients around the target
 *   the engine sends the host to and is over when the host has landed. A second scene while one
 *   gathers or runs, or while one given up may still be played here, is counted and not gathered:
 *   the engine has one lock, one camera and one hero. A warp is the exception, because the engine
 *   moves the host whatever this does, and the others follow him.
 *
 *   A client's MIRROR of the host's scene: free or held. Held while the newest note of this world
 *   says gathering or running a lock or a hero scene, and while the host is heard; the lock is held
 *   at the script's level on every substep of it, raised only once the player may be moved, and let
 *   go through exactly one exit.
 *
 *   One player's SEAT: wait until the body may be moved, fade out, hand the seat to the placement,
 *   wait until the body stands there, fade in. The host seats itself this way and a client does;
 *   every way out of it takes a held fade back.
 *
 *   The SEATING: every player to be seated, in turn, through the one seat search the re-entry and
 *   the arrival use, with the seats already handed out standing in as bodies, so that no two
 *   players are handed one seat and nobody is seated on the anchor. A player no seat around the
 *   place answered for is then searched beside every seat handed out, in the order they were
 *   handed out: each of those is a point with a floor, free and reachable on foot from the place,
 *   so a ring around it reaches one step further along what can be walked. Navigation meshes find
 *   standing room near a point the same way, over the reachable area rather than a fixed ring
 *   (Detour's findPolysAroundCircle, Unreal's GetRandomReachablePointInRadius).
 *
 * References: Synergy, the co-operative mod of Half-Life 2, teleports the players at a scripted
 * point rather than stopping the script, and leaves them where the scene put them; Unreal's level
 * sequence is played by the server and replicated, the players locked in cinematic mode; a player
 * left behind is brought along under a black screen, as Destiny's "joining allies" does.
 */
#ifndef MULTIPLAYER_MP_SCENE_FLOW_H
#define MULTIPLAYER_MP_SCENE_FLOW_H

#include "mp_scene_note.h"
#include "mp_scene_rule.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long a gathering may hold its scene, in substeps of the host STANDING: a second and a half
 * at thirty two a second. A dead host's time is not counted, because a hero scene cannot be played
 * by a corpse and the host's re-entry takes the gathering seat. */
#define MP_SCENE_HOLD_SUBSTEPS 48u

/* The longest a scene for everybody waits for its host, from its beginning until it is seen
 * running: the hold while the host lies dead, and a hero scene's grab while the host may not be
 * moved. Twenty seconds, the seat search's own bound for a wait. At it the scene is given up for
 * everybody rather than played on with a corpse or without its grab. */
#define MP_SCENE_WAIT_CAP_SUBSTEPS 640u

/* After a hold, how long the engine's grab of the hero or the lock may fail to show while the host
 * could be taken, before the scene is taken for one that never ran: two seconds of substeps in
 * which the host may be moved. A host that may not be moved is waited for, up to the cap. */
#define MP_SCENE_GRAB_GRACE_SUBSTEPS 64u

/* How long the host repeats "over", two seconds, so a client that missed the change hears it. */
#define MP_SCENE_OVER_SUBSTEPS 64u

/* How long a warp's landing may take: the engine's own fade out of a second, the respawn, and its
 * fade in, with room. */
#define MP_SCENE_WARP_CAP_SUBSTEPS 192u

/* How long a client holds a scene with no word from the host at all, in milliseconds. */
#define MP_SCENE_SILENCE_MS 2000u

/* How near its seat a body has to stand to count as arrived, in world units. */
#define MP_SCENE_ARRIVED_DISTANCE 1.0f

/* The fade a gathering moves a player under, and the one a warp does, in seconds. The warp's is
 * the engine's own respawn fade. */
#define MP_SCENE_FADE_SECONDS      0.25f
#define MP_SCENE_WARP_FADE_SECONDS 1.0f

/* From the hand-over to the body standing on its seat, in substeps: the placement takes a frame
 * and the body is written by the next player tick. */
#define MP_SCENE_PLACE_SUBSTEPS 32u

/* A client this near a warp's target when it hears of the warp is left where it is: it came back
 * there by its re-entry, or never left. */
#define MP_SCENE_WARP_NEAR 8.0f

/* How long a client's seat for a warp waits for its body to be movable, in substeps: ten seconds.
 * A warp is no scene the client is held in, so nothing else ends the wait while the host's note
 * keeps the warp's number; a player dead, swimming or at a gun would otherwise be moved minutes
 * later, long after the others. */
#define MP_SCENE_WARP_WAIT_SUBSTEPS 320u

/* What set a scene off. */
typedef enum mp_scene_kind {
    MP_SCENE_KIND_LOCK = 0,
    MP_SCENE_KIND_HERO,
    MP_SCENE_KIND_WARP,
    MP_SCENE_KINDS
} mp_scene_kind_t;

/* The note's `what` for a kind. A lock and a hero scene lock a client and give it the bars; a
 * warp only moves it. */
uint8_t mp_scene_what_of(mp_scene_kind_t kind);

/* Whether a scene runs for everybody now: gathering or running, and a lock or a hero scene. The
 * one answer for both roles, from the host's own state or a client's mirror of the note. */
bool mp_scene_for_all_now(uint8_t phase, uint8_t what);

/* What a scene for all knows of itself, for a line spoken while it runs: the place its players are
 * gathered around, known wherever a note of it arrived, and that alone, because a line is judged
 * the same way on every machine. The actor whose script began it is known on the host alone, no
 * note names it, so it is not told here. Read only while the scene runs for all.
 *
 * `gathered` is whether it gathered this machine's player: the player its script meant, who stands
 * at the place already, or one it seated there. A player it left where it stood, dead, with no seat
 * or in a mode that cannot be moved, or one still on the way, is not. */
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
    MP_SCENE_RELEASE_SEATED,   /* everybody stood at their seat, the host standing */
    MP_SCENE_RELEASE_BOUND,    /* the hold's second and a half ran out while the host stood */
    MP_SCENE_RELEASE_ALONE,    /* every far player left the session: nobody is waited for */
    MP_SCENE_RELEASE_NOBODY,   /* nobody was gathered: the place could not be read or sat on */
    MP_SCENE_RELEASE_SEATED_SOME   /* everybody with a seat stood at it, and a far player had
                                    * none: "everybody seated" would leave that player out */
} mp_scene_release_t;

/* Why a hold fell, as the host's line says it after "because". */
const char *mp_scene_release_text(mp_scene_release_t released);

/* What the host was when a scene was given up, as the host's line says it after "the host". */
const char *mp_scene_given_up_text(mp_scene_move_t host);

typedef struct mp_scene_host_flow {
    mp_scene_phase_t   phase;
    mp_scene_kind_t    kind;
    uint16_t           serial;          /* 0 before the first scene; never started over */
    uint8_t            warp_serial;     /* 0 before the first warp; never started over */
    bool               holds;           /* the scene's actor and the hero's grab are held */
    uint32_t           began;           /* substep counts */
    uint32_t           last;
    uint32_t           phase_since;
    uint32_t           hold_standing;   /* held substeps with the host standing */
    uint32_t           hold_dead;       /* and with it dead */
    bool               seen_running;
    mp_scene_release_t released;
    uint32_t           grace;           /* substeps with no grab while the host could be taken */
    /* The wait ran out: over for everybody. Until the engine has played the scene here, or plainly
     * will not, a door of it is no scene of its own. */
    bool               given_up;
    mp_scene_move_t    given_up_for;    /* the host then: dead, or where it could not be moved */
} mp_scene_host_flow_t;

typedef enum mp_scene_begin {
    MP_SCENE_BEGIN_NEW = 0,     /* a scene of its own, gathered */
    MP_SCENE_BEGIN_SECOND,      /* inside a scene that gathers or runs: counted, not gathered */
    MP_SCENE_BEGIN_WARP_OVER    /* a warp inside a scene: it takes over */
} mp_scene_begin_t;

mp_scene_begin_t mp_scene_host_begin(mp_scene_host_flow_t *flow, mp_scene_kind_t kind,
                                     uint32_t now);

/* What the host's binding reads once a substep. */
typedef struct mp_scene_host_look {
    uint32_t now;
    bool     host_stands;       /* the engine's live player test */
    bool     everyone_seated;   /* the host and every far player handed a seat stand on it */
    bool     running;           /* mp_scene_running on the host's own cells */
    bool     warp_landed;       /* the host's respawn has come back to a running module */
    bool     alone;             /* no far player is in the session any more */
    mp_scene_move_t may_move;   /* mp_scene_may_move for the host: the engine grabs only a YES */
    bool     nobody_gathered;   /* the seating found no place: nobody was handed a seat */
    uint32_t unseated;          /* far players to be seated that no seat answered for */
} mp_scene_host_look_t;

/* One substep. True when the phase changed. */
bool mp_scene_host_step(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look);

/* The one exit, for every way out of a scene's world: a level ending or changing, the session
 * ending. Back to none with nothing held. True when a hold stood, which is what the caller has to
 * give back. */
bool mp_scene_host_leave(mp_scene_host_flow_t *flow);

/* ==============================================================================================
 * A client's mirror of the host's scene.
 * ============================================================================================ */

typedef struct mp_scene_mirror {
    bool     known;        /* a note of this world has been taken since the last exit */
    uint16_t serial;
    uint8_t  phase;
    uint8_t  what;
    uint8_t  generation;
    bool     locked;       /* the mirror raised the lock and owes it one release */
    uint32_t heard_ms;     /* the last time the host was heard */
    uint32_t held_since_ms;
    float    anchor[3];    /* where the scene gathers, as its newest note says */
} mp_scene_mirror_t;

typedef enum mp_scene_take {
    MP_SCENE_TAKE_NEW = 0,    /* a new scene, or a new phase of the one held */
    MP_SCENE_TAKE_REPEAT,     /* nothing a client acts on has changed */
    MP_SCENE_TAKE_FOREIGN,    /* a note of another world */
    MP_SCENE_TAKE_OLDER       /* a scene older than the one held */
} mp_scene_take_t;

/* The place the scene gathers around is taken from a repeat as well: a host whose first note left
 * before the place was read sends it in the next one, which says nothing else new. */
mp_scene_take_t mp_scene_mirror_take(mp_scene_mirror_t *mirror, const mp_scene_note_t *note,
                                     uint8_t generation_here, uint32_t now_ms);

/* What a substep of the mirror does, as bits. */
#define MP_SCENE_MIRROR_RAISE  0x1u   /* the lock at the script's level, this substep */
#define MP_SCENE_MIRROR_BARS   0x2u   /* the bars on, once, with the first raise */
#define MP_SCENE_MIRROR_LET_GO 0x4u   /* the one release and the bars off */

typedef enum mp_scene_let_go {
    MP_SCENE_LET_GO_NONE = 0,
    MP_SCENE_LET_GO_OVER,     /* the host said the scene is over, or none runs */
    MP_SCENE_LET_GO_SILENT,   /* nothing from the host for MP_SCENE_SILENCE_MS */
    MP_SCENE_LET_GO_EXIT      /* the level or the session this mirror belonged to ended */
} mp_scene_let_go_t;

typedef struct mp_scene_mirror_look {
    uint32_t now_ms;
    bool     host_heard;   /* any word of the host's arrived lately, not only this note */
    bool     may_lock;     /* mp_scene_may_move answered yes for this player */
} mp_scene_mirror_look_t;

uint32_t mp_scene_mirror_step(mp_scene_mirror_t *mirror, const mp_scene_mirror_look_t *look,
                              mp_scene_let_go_t *why);

/* The one exit. LET_GO when the lock is the mirror's to release; everything forgotten. */
uint32_t mp_scene_mirror_leave(mp_scene_mirror_t *mirror);

/* Whether a note of the host's gathers this client: a scene for everybody, gathering or running,
 * that has not gathered it yet (`gathered_serial` is the scene its last seat was for) and hands it
 * a seat. Any such note does, not only the first: the channel keeps only the newest copy of the
 * note, so a gathering note still on its way can be replaced by the running one, and a note has to
 * stand on its own. A warp is followed by its own number (mp_scene_warp_wanted). */
bool mp_scene_note_gathers(const mp_scene_note_t *note, uint16_t gathered_serial, bool seat_given);

/* Whether a client's seat is still wanted by the note the mirror holds: the same scene, and for a
 * gathering's seat that scene still running for everybody. A warp's seat lives as long as its
 * number, bounded by its own wait (MP_SCENE_WARP_WAIT_SUBSTEPS). */
bool mp_scene_seat_wanted(const mp_scene_mirror_t *mirror, uint16_t seat_serial, bool warp);

/* ==============================================================================================
 * One player's seat.
 * ============================================================================================ */

typedef enum mp_scene_seat_stage {
    MP_SCENE_SEAT_IDLE = 0,
    MP_SCENE_SEAT_WAITING,    /* a seat, and a body that may not be moved yet */
    MP_SCENE_SEAT_FADING,     /* the screen goes dark */
    MP_SCENE_SEAT_PLACED,     /* handed to the placement; waiting for the body to stand there */
    MP_SCENE_SEAT_DONE,
    MP_SCENE_SEAT_GIVEN_UP
} mp_scene_seat_stage_t;

typedef enum mp_scene_seat_act {
    MP_SCENE_SEAT_ACT_NONE = 0,
    MP_SCENE_SEAT_ACT_FADE_OUT,
    MP_SCENE_SEAT_ACT_PLACE,
    MP_SCENE_SEAT_ACT_FADE_IN
} mp_scene_seat_act_t;

typedef struct mp_scene_seat_flow {
    mp_scene_seat_stage_t stage;
    uint32_t              since;
    float                 fade_seconds;
    bool                  fade_held;       /* a fade out stands that no fade in has answered */
    bool                  fade_on_clock;   /* the fade ended on this flow's own deadline */
    mp_scene_move_t       refused;         /* why the body could not be moved, the last time */
    bool                  at_seat;         /* DONE with the body on its seat */
    uint32_t              wait_max;        /* substeps the body is waited for, 0 for no bound */
    bool                  waited_out;      /* GIVEN_UP because that wait ran out */
} mp_scene_seat_flow_t;

void mp_scene_seat_start(mp_scene_seat_flow_t *flow, uint32_t now, float fade_seconds);

typedef struct mp_scene_seat_look {
    uint32_t        now;
    bool            live;        /* the scene the seat belongs to still gathers */
    mp_scene_move_t may_move;
    bool            fade_done;   /* the engine's own tint says the fade is over */
    bool            at_seat;     /* the body stands within MP_SCENE_ARRIVED_DISTANCE of the seat */
} mp_scene_seat_look_t;

/* How long a fade may take before the flow goes on without the engine's word, in substeps. */
uint32_t mp_scene_seat_fade_deadline(float fade_seconds);

mp_scene_seat_act_t mp_scene_seat_step(mp_scene_seat_flow_t *flow,
                                       const mp_scene_seat_look_t *look);

/* The exit for any way out: FADE_IN when a fade out still stands, and the flow is idle after. */
mp_scene_seat_act_t mp_scene_seat_leave(mp_scene_seat_flow_t *flow);

/* A client hearing of a warp: move to the seat, or stay because the warp is known already, the
 * note has no seat for this player, or the player is near the target anyway. */
typedef enum mp_scene_warp_step {
    MP_SCENE_WARP_MOVE = 0,
    MP_SCENE_WARP_KNOWN,
    MP_SCENE_WARP_NO_SEAT,
    MP_SCENE_WARP_NEAR_ALREADY
} mp_scene_warp_step_t;

mp_scene_warp_step_t mp_scene_warp_wanted(uint8_t handled, uint8_t warp_serial, bool seat_given,
                                          float distance_to_seat);

/* ==============================================================================================
 * The seating.
 * ============================================================================================ */

/* One player's place in the seating. */
typedef struct mp_scene_sitter {
    bool    wanted;         /* this player is to be seated */
    uint8_t slot;           /* its world slot, which is the direction its ring starts in */
    bool    seated;         /* a seat answered */
    float   seat[3];
    bool    chained;        /* the seat was found beside a seat handed out first */
    uint8_t beside_slot;    /* and that seat was this slot's */
    uint8_t tried_beside;   /* seats handed out it was searched beside, found or not */
} mp_scene_sitter_t;

/* What one search came to, as the seating reads it. */
typedef enum mp_scene_probe {
    MP_SCENE_PROBE_FOUND = 0,
    MP_SCENE_PROBE_NONE,     /* nothing free around the anchor */
    MP_SCENE_PROBE_ANCHOR    /* the anchor stands on a mover, over water or a drop, or falls; a
                              * seat searched beside in the second pass is then no place to search
                              * from, and nothing more */
} mp_scene_probe_t;

/* The one seat search, handed in: the binding passes mp_seat_probe, a test a stand-in of the same
 * shape. `bodies` are the players' bodies and the seats handed out so far. */
typedef mp_scene_probe_t (*mp_scene_probe_fn_t)(void *context, const float anchor[3],
                                                uint8_t slot, const mp_seat_body_t *bodies,
                                                size_t body_count, float seat[3]);

/* The host and the far players a session holds, and the bodies a seat must keep clear of: the
 * bodies handed in and every seat handed out. The largest is a warp's, the host, three far bodies,
 * the target and three seats; the host's binding, which knows the banks, asserts it. A seat that
 * finds the list full is still handed out, and only the next search does not keep clear of it. */
#define MP_SCENE_SITTERS    4u
#define MP_SCENE_BODIES_MAX 8u

typedef enum mp_scene_seating {
    MP_SCENE_SEATING_DONE = 0,          /* every wanted player searched; some may have no seat */
    MP_SCENE_SEATING_ANCHOR_REFUSED     /* nobody is gathered: the anchor stands nowhere to sit */
} mp_scene_seating_t;

/* Two passes. The first searches every wanted player around the anchor, in the order given, and
 * an anchor it refuses gathers nobody. The second searches every wanted player still without a
 * seat beside each seat handed out so far, in the order they were handed out, those of the second
 * pass included; the first found wins and is a seat like any other. The first pass is what the
 * seating always did, so a scene that seats everybody in it seats them exactly as before. */
mp_scene_seating_t mp_scene_seat_everyone(const float anchor[3], const mp_seat_body_t *bodies,
                                          size_t body_count, mp_scene_sitter_t *sitters,
                                          size_t sitter_count, mp_scene_probe_fn_t probe,
                                          void *context);

/* What one seating came to, per player it was to seat: a search is a player to be seated, and one
 * that found nothing is a player with no seat after both passes. Of the players no seat around the
 * place answered for, how many were seated beside a seat handed out first. A seating whose anchor
 * was refused searched nobody around the place, so `around_none` and `beside_a_seat` stay 0. */
typedef struct mp_scene_seating_tally {
    uint32_t wanted;
    uint32_t unseated;
    uint32_t around_none;
    uint32_t beside_a_seat;
} mp_scene_seating_tally_t;

mp_scene_seating_tally_t mp_scene_seating_tally(const mp_scene_sitter_t *sitters,
                                                size_t sitter_count, mp_scene_seating_t seating);

#endif /* MULTIPLAYER_MP_SCENE_FLOW_H */
