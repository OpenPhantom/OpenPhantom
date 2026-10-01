/* mp_seat.h: the one search for a place to put a body, and the wish that waits until it has one.
 *
 * ================================== One search, every seat ====================================
 *
 * A dead player coming back, a client arriving beside its host and a scene gathering the players
 * all need the same thing: a point a body can stand on, near somebody, that nobody else stands on.
 * The engine has three probes that answer better than any estimate, and this module is the only
 * place that asks them for a seat: the walkable line from the anchor to the candidate, the ground
 * probe that says where the floor under a point is, and the head clearance probe. The rules that
 * turn their answers into a verdict are in mp_seat_rule, with no engine behind them.
 *
 * The search and the seating are separate on purpose. A dead player goes back through the engine's
 * own re-entry, which needs the health written first, fades the screen and has to be watched all
 * the way round; a living player goes through the teleport, which writes the position and returns.
 * The teleport at 0x00451266 writes the position, the heading and the rider position and clears
 * the 0x88 byte ground contact block; neither door can stand in for the other. Two engine doors
 * with two preconditions, and the module that owns each door keeps it. What they share is here.
 *
 * ================================= The four cases that wait ===================================
 *
 * A reading of the ground under the anchor is ordinary only when the anchor stands on its floor.
 * Falling, the probe finds nothing. Swimming or over a drop, it finds the bottom several units
 * down. On a lift it reads an ordinary distance, but the walkable probe clears the mover flag
 * before each of its own probes and judges every candidate against whatever is under the lift
 * shaft. All four end by themselves, so a search around such an anchor waits rather than guesses,
 * and the clock that ends an empty search stops while it does.
 *
 * ================================== The wish, in three stages =================================
 *
 * A search that finds nothing used to be tried again next frame for good, and one host lay as a
 * corpse for minutes because every candidate around his anchor stood on a slope or under a crawl
 * space. A wish now goes through three stages, each with its own clock (mp_seat_rule):
 *
 *   beside the anchor, both rings, the standing players read afresh on every look;
 *   on the authored point nearest the anchor, or the level start, both rings around it;
 *   that point itself, unprobed, which always ends the wish.
 *
 * A wish on a point the caller named has only the first stage and the last.
 *
 * =============================== Two clients arriving together ================================
 *
 * The ring of each slot starts in its own direction, but that is only where it starts: a slot that
 * loses two of its own directions goes on into the first of the next slot's, and a candidate is
 * taken only by a body, never by a seat another machine is about to hand out. So a wish beside the
 * host can be told the client slots of the session below its own, and it then foresees, on every
 * look, the seat each of them takes around the same anchor, lowest first, with the bodies that
 * slot's own machine sees, and keeps those seats clear as the scene's seating keeps the seats it
 * has handed out. Every client computes the same answer from the same roster without a message,
 * as every node of a lockstep game checks a spawn spot in the same player order (Doom's
 * G_CheckSpot). It holds while every machine reads the host at the same place; where it does not,
 * the next form is the host handing the seats out, as Quake 3's ClientSpawn and Unreal's
 * ChoosePlayerStart do on the server.
 */
#ifndef MULTIPLAYER_MP_SEAT_H
#define MULTIPLAYER_MP_SEAT_H

#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How far the floor under a point may be from the point itself before the reading counts as
 * swimming or a drop, in world units either side. Standing reads a fraction of one. */
#define MP_SEAT_FLOOR_BAND 2.0f

/* How many far players the search is told about, one for each far bank. */
#define MP_SEAT_FAR_BODIES 3u

/* A lower slot of the session whose body this machine shows in no far bank yet. */
#define MP_SEAT_ORDER_NO_BODY 0xFFu

/* What one search around one anchor or point came to. */
typedef enum mp_seat_outcome {
    MP_SEAT_FOUND,
    MP_SEAT_NONE_FREE,       /* the anchor stood on its floor and no candidate was free */
    MP_SEAT_ANCHOR_MOVING,   /* falling, on a mover, over water or a drop: it ends by itself */
    MP_SEAT_NO_LEVEL,        /* no level is running, so there is no world to probe */
    MP_SEAT_NO_PROBES,       /* a probe this needs did not resolve, or nothing was installed */
    MP_SEAT_NOBODY_STANDING  /* beside the players, and none stands: the caller's rule set
                              * decides that, and the clock waits for it */
} mp_seat_outcome_t;

/* Who a wish is seated beside. */
typedef enum mp_seat_kind {
    MP_SEAT_KIND_BESIDE_PLAYERS,   /* the standing far players, nearest to the death first */
    MP_SEAT_KIND_BESIDE_BODY,      /* one body the caller moves the wish to on every look */
    MP_SEAT_KIND_AT_POINT          /* a point to stand on: the point itself first */
} mp_seat_kind_t;

/* One wish for a seat. The caller owns it and starts it with one of the three calls below. */
typedef struct mp_seat_wish {
    const char     *who;          /* the log's name for the caller, "the re-entry" */
    mp_seat_kind_t  kind;
    mp_seat_stage_t stage;
    uint8_t         slot;         /* whose seat: the ring starts in this slot's direction */
    bool            died_known;
    float           died_at[3];   /* beside the players: the one nearest this is tried first */
    float           target[3];    /* the body or the point the current stage searches around */
    float           heading;
    bool            said_no_point;
    mp_seat_wait_t  wait;

    /* The client slots this wish's player is seated after, lowest first, and the far body that
     * shows each here. Only a wish that was told them has `ordered` set. */
    bool            ordered;
    bool            order_known;   /* the session's roster was there to read them from */
    bool            order_noted;   /* counted in the caller's counts on its first look */
    uint8_t         order_count;
    uint8_t         order_slot[MP_SEAT_FAR_BODIES];
    uint8_t         order_body[MP_SEAT_FAR_BODIES];   /* bank less one, or MP_SEAT_ORDER_NO_BODY */
} mp_seat_wish_t;

/* Resolves the three probes, the world pointer and the game mode cell. Safe to call again. A probe
 * that did not resolve is named in the log and disables the searches that need it. */
void mp_seat_install(void);
bool mp_seat_probes_resolved(void);

/* Whether a level is running: the game mode cell reads 2. The one reading of it for the seat and
 * for the re-entry's gates, so the two cannot disagree about the same frame. */
bool mp_seat_level_running(void);

/* The world the level runs in and the world's own clock, in seconds since that level began, which
 * runs at the same pace whatever the length of a substep. False with no world to read. */
bool mp_seat_world_now(uint32_t *world, float *seconds);

/* What the ground probe's reading under a point means. `floor_distance` is the signed height of
 * the floor over the point; the engine's own "no floor" value is a float maximum. */
mp_seat_floor_t mp_seat_floor_state(float floor_distance, bool on_mover);

/* The far players as the interpolator last resolved them, handed in once a frame: where each far
 * bank's player is and whether its own machine calls it standing. Index is the bank less one. A
 * seat an arriving wish handed out is read against them again two seconds of world time later. */
void mp_seat_note_body(size_t index, const float position[3], float heading, bool stands);
void mp_seat_note_no_body(size_t index);

/* One search around one anchor or one point, for any caller that seats: `beside` makes `target` a
 * body to step away from, never a candidate itself; otherwise the point itself is tried first.
 * Two rings of eight, starting in the direction of `slot`, the slot of the player being seated,
 * and every ring candidate reachable on foot from `target`, beside a body and around a point alike.
 * A candidate on a floor that hurts or in water is refused, and one closer than
 * MP_SEAT_BODY_CLEARANCE to any of `bodies` is taken. Every refused candidate is counted in
 * `counts` by its reason. */
mp_seat_outcome_t mp_seat_probe(const float target[3], bool beside, uint8_t slot,
                                const mp_seat_body_t *bodies, size_t body_count,
                                mp_seat_counts_t *counts, float seat[3]);

/* Starting a wish. `who` must outlive it. `died_at` may be NULL, and then the players are tried in
 * bank order. */
void mp_seat_wish_beside_players(mp_seat_wish_t *wish, const char *who, const float *died_at,
                                 uint8_t slot);
void mp_seat_wish_beside_body(mp_seat_wish_t *wish, const char *who, uint8_t slot);
void mp_seat_wish_at_point(mp_seat_wish_t *wish, const char *who, const float point[3],
                           float heading, uint8_t slot);

/* Where the body a beside-body wish stands next to is now, and which way it faces. Taken only in
 * the first stage; the fallback's point is the wish's own. */
void mp_seat_wish_follow(mp_seat_wish_t *wish, const float anchor[3], float heading);

/* A beside-body wish is seated after the client slots `slots`, lowest first, `bodies` naming the
 * far body that shows each (a bank less one, or MP_SEAT_ORDER_NO_BODY). `known` false says the
 * session's slots could not be read; the wish then searches as one with no order and the report
 * counts it. Every look of the first stage foresees their seats before its own; no other stage
 * and no other kind of wish does. At most MP_SEAT_FAR_BODIES slots are kept. */
void mp_seat_wish_seat_after(mp_seat_wish_t *wish, bool known, const uint8_t *slots,
                             const uint8_t *bodies, size_t count);

/* One look, with the engine's gates open. True with a seat and the heading to face; false means
 * wait, and the wish may have moved to its next stage. `now` is the substep count. */
bool mp_seat_wish_step(mp_seat_wish_t *wish, uint32_t now, mp_seat_counts_t *counts,
                       float seat[3], float *heading);

/* A tick on which the wish was not searched, so its clock does not count the time behind it. */
void mp_seat_wish_pass(mp_seat_wish_t *wish, uint32_t now);

/* A seat named as the first anchor of every search beside the players, before the players
 * themselves: the point first, then its rings. A host's scene gathering names the seat it handed
 * the host while its hold stands, so a host that dies while its scene waits for it comes back
 * where the scene is played rather than beside whichever far player stood nearest its death. It is
 * one more anchor in the one search, not a search of its own. NULL takes it away. */
void mp_seat_name_anchor(const float *seat, float heading);

/* ================================= A seat that ended a life ===================================
 *
 * The search is deterministic: the same anchor, the same ring start and the same geometry give the
 * same seat. A seat on which a life ended again at once would be handed out again, so the re-entry
 * tells this module three things, and a seat whose life ended before its body was seen standing,
 * or within MP_SEAT_KILL_WINDOW_SECONDS of world time after, is refused to every later search of
 * the level, near ring and all. A wish that knows where its player died keeps its seats out of
 * MP_SEAT_DEATH_CLEARANCE of that place as well. Neither applies to a seat the scene gathering
 * probes for itself. */
void mp_seat_note_seated(const float seat[3]);   /* the engine was asked to put the body here */
void mp_seat_note_landed(void);                  /* and a living body stood up on it */
void mp_seat_note_life_ended(void);              /* the life that began there has ended */

/* The level ended: its seats that ended a life and the seat last handed to the engine go with it.
 * Called from the one exit every way out of a level or a session takes. */
void mp_seat_world_ended(void);

/* The caller's two report lines, each opened by the caller's own label, and between them, for a
 * caller whose wishes are seated after lower slots, the order's two lines under its own name. */
void mp_seat_report(const char *seat_label, const char *fallback_label,
                    const mp_seat_counts_t *counts);

/* The first of them alone, for a caller whose searches have no fallback: a scene's seats. */
void mp_seat_report_searches(const char *seat_label, const mp_seat_counts_t *counts);

#endif /* MULTIPLAYER_MP_SEAT_H */
