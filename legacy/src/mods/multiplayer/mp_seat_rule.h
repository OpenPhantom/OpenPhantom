/* mp_seat_rule.h: where a body may be seated beside another, as rules with no engine behind them.
 *
 * Every seat this feature hands out goes through one search: a dead player on his way back, a
 * client arriving beside its host, and whatever later gathers the players in one place. The
 * engine answers the three questions about a point, whether the line to it is walkable, where the
 * floor under it is and whether there is room over it. Everything else is decided here, where a
 * test can pin it with no game in the process:
 *
 *   which direction a player's ring starts in, so that two players seated at once are not handed
 *   the same point;
 *   what a candidate's answers amount to, one verdict each, so that a search that finds nothing
 *   can say why;
 *   which standing player a search tries first;
 *   how long a search may find nothing before it falls back, counted only while waiting can help;
 *   which authored point a co-operative fallback takes.
 *
 * The references for the shape are Quake 3, whose SelectSpawnPoint chooses the point at the moment
 * of the respawn and refuses one where SpotWouldTelefrag says a body stands, and Unreal, whose
 * ChoosePlayerStart runs on every restart. Both choose when the player comes back, not when he
 * died, and both refuse a point that is occupied.
 */
#ifndef MULTIPLAYER_MP_SEAT_RULE_H
#define MULTIPLAYER_MP_SEAT_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many directions a ring tries. The rings are coarse on purpose: the engine's own walkable
 * probe is coarser still, since it probes at cell centres one world unit apart, so a finer ring
 * would ask a question the probe cannot answer more precisely. */
#define MP_SEAT_RING_STEPS 8u

/* The two rings, in world units. The player's standing height is 2.8 units, byte-proven in the
 * head clearance probe, which tests its window against the position's z plus 2.8, so a body is
 * well under a unit wide and two units puts two bodies clear of each other rather than in the
 * cylinder overlap the engine's push apart at 0x004131EB would resolve. The per body cylinder
 * radius is available at run time, at +0xB8 of the object, seeded from +0xEC of the actor asset;
 * it is deliberately not read, because it would add an offset to the shared cell header for a
 * value well under a unit on every shipped hero, and the search already refuses a candidate that
 * does not stand on floor. The second ring is for a
 * body standing in a pocket whose walls are closer than two units: the field run with four players
 * held a host as a corpse for minutes because all eight near candidates stood on a slope or under
 * a crawl space. */
#define MP_SEAT_RING_NEAR 2.0f
#define MP_SEAT_RING_FAR  4.0f

/* How close a seat may come to a player's body before it counts as taken, in world units. Half the
 * near ring: a candidate that close to a body is a body standing where the candidate is. */
#define MP_SEAT_BODY_CLEARANCE 1.0f

/* How long a stage of a search may find nothing on a good anchor before it falls back: three
 * seconds of simulation at thirty two substeps a second. Counted in substeps, because a drawn frame
 * is anything from four to forty milliseconds and the same wait would otherwise be a different
 * wait on every machine. */
#define MP_SEAT_GIVE_UP_SUBSTEPS 96u

/* And how long a stage may wait at all, whatever the anchor did: twenty seconds, long enough for a
 * lift to arrive and a swimmer to climb out. Without this bound an anchor that rides a mover for
 * good would hold a corpse for good. */
#define MP_SEAT_WAIT_CAP_SUBSTEPS 640u

/* No point answered. Index 0 is an answer, so it cannot double as the failure. */
#define MP_SEAT_NO_POINT ((size_t)-1)

/* Two flags of the floor face under a seat, from the surface word the ground probe's face carries.
 * The first is the floor that hurts whoever stands on it, three health every fifth of a second;
 * the second is water, which the player swims in. Neither is a place to put somebody back: the
 * first kills a player who stands up slowly, and Unreal refuses a start in a water volume. */
#define MP_SEAT_SURFACE_HURTS 0x0400u
#define MP_SEAT_SURFACE_WATER 0x0040u

/* How close a seat may come to where the player it is for just died, in world units: the second
 * ring. Whatever killed him there may still be there, a fan, a field, a trap a script runs, and
 * each of those reaches a few units at most. Quake 3 chooses its spawn point away from the place
 * of the death for the same reason. */
#define MP_SEAT_DEATH_CLEARANCE 4.0f

/* A seat whose life ended this soon after the body stood on it, in seconds of the world's own
 * clock, is refused to the same player for the rest of the level, and so is everything within the
 * near ring of it. World time rather than substeps, because the sixty frames cheat halves the
 * substep. UT2004 marks the start a player last used the same way, so a search that is
 * deterministic does not hand out the same bad seat twice. */
#define MP_SEAT_KILL_WINDOW_SECONDS 5.0f
#define MP_SEAT_LOCK_RADIUS         2.0f

/* How many such seats are remembered; the oldest gives way. */
#define MP_SEAT_LOCKS 4u

/* What the ground probe's reading under one point means. Only the first is a floor a body can be
 * put on; the other three are the situations in which the search waits rather than guesses. */
typedef enum mp_seat_floor {
    MP_SEAT_FLOOR_OK,
    MP_SEAT_FLOOR_NONE,     /* falling, or over a hole: the probe found nothing */
    MP_SEAT_FLOOR_MOVER,    /* on a lift, which the walkable probe cannot see */
    MP_SEAT_FLOOR_FAR       /* swimming, or over a drop: the floor is not under the feet */
} mp_seat_floor_t;

/* One candidate's verdict. Every refused candidate is refused for exactly one of these, the first
 * in the order the search asks: the walkable line, the floor, what the floor is, the place of the
 * death and the seats that ended a life, the bodies, the ceiling. */
typedef enum mp_seat_verdict {
    MP_SEAT_FREE = 0,
    MP_SEAT_NOT_WALKABLE,
    MP_SEAT_NO_FLOOR,
    MP_SEAT_A_DROP,
    MP_SEAT_ON_MOVER,
    MP_SEAT_TAKEN,
    MP_SEAT_LOW_CEILING,
    MP_SEAT_HURTS,          /* a floor that hurts */
    MP_SEAT_WATER,
    MP_SEAT_NEAR_DEATH,     /* closer than MP_SEAT_DEATH_CLEARANCE to where he died */
    MP_SEAT_ENDED_A_LIFE,   /* near a seat that ended a life of his in this level */
    MP_SEAT_VERDICTS
} mp_seat_verdict_t;

/* A player's body as the search sees it: where it is, which way it faces, and whether its own
 * machine calls it standing. `known` false is an empty entry. */
typedef struct mp_seat_body {
    bool  known;
    bool  stands;
    float position[3];
    float heading;
} mp_seat_body_t;

/* What one look of a search came to, for the clock that decides when it falls back. */
typedef enum mp_seat_look {
    MP_SEAT_LOOK_NONE,      /* nothing was searched: no level, the engine's gates shut, or nobody
                             * standing beside whom to search, which the caller's rules answer */
    MP_SEAT_LOOK_EMPTY,     /* searched and empty, on a good anchor */
    MP_SEAT_LOOK_MOVING     /* an anchor tried was falling, riding a mover or over water */
} mp_seat_look_t;

/* The clock of one stage. Zero it with mp_seat_rule_wait_start. */
typedef struct mp_seat_wait {
    bool     started;
    uint32_t last;    /* the substep count at the last look */
    uint32_t good;    /* substeps that passed between empty looks on a good anchor */
    uint32_t total;   /* substeps that passed between any two looks of this stage */
} mp_seat_wait_t;

/* The three stages a wish goes through, each with its own clock. */
typedef enum mp_seat_stage {
    MP_SEAT_STAGE_ANCHOR,       /* beside the anchor, or on the point the caller named */
    MP_SEAT_STAGE_FALLBACK,     /* on the authored point the fallback chose */
    MP_SEAT_STAGE_AS_AUTHORED   /* that point itself, unprobed: the search always ends */
} mp_seat_stage_t;

/* What a caller's searches came to, kept per caller so that a line about the re-entry never counts
 * an arrival. The first five are the counters the re-entry's report line has always had.
 *
 * The last group is a wish's that is seated after the lower slots of its session, which is the
 * arrival's. `order_who` is that caller's name from its first such look on, and the report prints
 * the order's two lines only when it is set. The two distances are the latest hand-over's, -1 for
 * none: no far player known, or not measured yet. */
typedef struct mp_seat_counts {
    uint32_t searches;
    uint32_t found_nothing;
    uint32_t anchor_falling;
    uint32_t anchor_on_mover;
    uint32_t anchor_far_floor;
    uint32_t refused[MP_SEAT_VERDICTS];   /* per candidate, by reason; the free slot stays 0 */
    uint32_t anchors_tried;
    uint32_t live_reads;       /* anchors read afresh from a far player's pose on that look */
    uint32_t nobody_standing;  /* searches beside the players with none of them standing */
    uint32_t to_point;         /* fallbacks to the spawn point nearest the anchor */
    uint32_t to_start;         /* fallbacks to the level start */
    uint32_t as_authored;      /* bodies put on the fallback point unprobed */
    uint32_t longest_wait;     /* substeps on a good anchor, the most any stage ran */
    uint32_t on_named;         /* seats found around the anchor a scene's gathering named */

    const char *order_who;       /* the caller whose wishes are seated after lower slots */
    uint32_t    order_lower;     /* lower slots the latest such wish is seated after */
    uint32_t    order_held;      /* candidates refused only for a seat a lower slot takes */
    uint32_t    order_none;      /* foresights that found no seat for a lower slot */
    uint32_t    order_no_roster; /* such wishes begun with the session's roster unread */
    float       handed_nearest;  /* the nearest far player to the seat as it was handed over */
    float       later_nearest;   /* and MP_SEAT_NEIGHBOUR_SECONDS of world time later */
    uint32_t    handed_close;    /* hand-overs with a far player within a unit of the seat */
} mp_seat_counts_t;

/* How long after a hand-over the far players around the seat are read again, in seconds of the
 * world's own clock: long enough for another client arriving at the same moment to have been
 * placed and its body passed on by the host, short enough that nobody has walked far yet. */
#define MP_SEAT_NEIGHBOUR_SECONDS 2.0f

/* The client slots an arriving player is seated after: the slots of `roster_slots` below
 * `my_slot`, the host's slot left out, each once and lowest first. Writes at most `lower_max` of
 * them into `lower` and answers how many. Every client computes the same list from the same roster,
 * which is what lets each of them foresee the seats of the ones before it. */
size_t mp_seat_rule_lower_slots(const uint8_t *roster_slots, size_t count, uint8_t host_slot,
                                uint8_t my_slot, uint8_t *lower, size_t lower_max);

/* Where a player's ring starts, from his world slot: two directions apart per slot, so four players
 * start a quarter turn from each other. Every ring used to start in the same direction, and four
 * players seated at once were handed four times the same point. */
size_t mp_seat_rule_ring_start(uint8_t slot);

/* The offset of step `step` of a ring starting at direction `start`, on the horizontal plane. One
 * turn divided into MP_SEAT_RING_STEPS, direction 0 along positive x; the index wraps. */
void mp_seat_rule_ring_offset(size_t start, size_t step, float radius, float offset[2]);

/* What a floor reading makes of a candidate: free, or refused for that reading. */
mp_seat_verdict_t mp_seat_rule_floor_verdict(mp_seat_floor_t floor);

/* What the floor face under a candidate is: free, a floor that hurts, or water. `surface` is the
 * face's surface word, nought for no face. */
mp_seat_verdict_t mp_seat_rule_surface_verdict(uint16_t surface);

/* Whether `seat` lies closer than `radius` to `point`, in three dimensions. A seat on a ring of
 * exactly that radius around the point is not closer: the ring is meant to stay open, and the
 * arithmetic of its directions puts a candidate a few millionths of a unit either side. */
bool mp_seat_rule_within(const float seat[3], const float point[3], float radius);

/* Whether the life a seat began ended soon enough to lock the seat: before the body was ever seen
 * standing on it, or less than MP_SEAT_KILL_WINDOW_SECONDS of world time after. A clock that runs
 * backwards is a new level, and no lock. */
bool mp_seat_rule_ended_a_life(bool landed, float landed_at, float ended_at);

/* Whether `seat` lies closer than `clearance` to any known body. Unknown entries are nobody. */
bool mp_seat_rule_near_a_body(const float seat[3], const mp_seat_body_t *bodies, size_t count,
                              float clearance);

/* The order in which a search tries the standing bodies as anchors: nearest to `died_at` first,
 * index order among equals and when `died_at` is NULL. Writes at most `order_max` indices into
 * `order` and answers how many; a body that is not standing is no anchor. */
size_t mp_seat_rule_anchor_order(const mp_seat_body_t *bodies, size_t count, const float *died_at,
                                 size_t *order, size_t order_max);

/* The clock of one stage. Every tick of a waiting wish is a look, searched or not; `now` is the
 * substep count, and only the substeps between two looks are counted, so time with the gates shut
 * is not. True when the stage has run out: MP_SEAT_GIVE_UP_SUBSTEPS of empty looks on a good
 * anchor, or MP_SEAT_WAIT_CAP_SUBSTEPS in all. A moving anchor stops the first clock and not the
 * second. */
void mp_seat_rule_wait_start(mp_seat_wait_t *wait);
bool mp_seat_rule_wait_look(mp_seat_wait_t *wait, uint32_t now, mp_seat_look_t look);

/* The stage after `stage` has run out. Beside an anchor the next is the fallback; on a point the
 * caller named, and after the fallback, it is that point unprobed, which is also the last. */
mp_seat_stage_t mp_seat_rule_next_stage(mp_seat_stage_t stage, bool beside);

/* The co-operative fallback: of `count` authored points, the free one nearest to `anchor`. Index 0
 * is the level start by construction, and it is the answer when no point is free. MP_SEAT_NO_POINT
 * for an empty table. `level_start` says whether the answer is index 0. */
size_t mp_seat_rule_nearest_free(const float (*points)[3], const bool *unlocked, size_t count,
                                 const float anchor[3], bool *level_start);

#endif /* MULTIPLAYER_MP_SEAT_RULE_H */
