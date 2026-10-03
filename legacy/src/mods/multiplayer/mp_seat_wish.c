/* mp_seat_wish.c: the wish that waits until the one search has a seat for it. See mp_seat.h.
 *
 * Split from mp_seat.c along the seam that file's size note named: the wish asks the probe and
 * nothing else of the engine but where this player's own body stands, and the probe, the far
 * players and the seats that ended a life stay there. The joint is mp_seat_internal.h.
 *
 * Two things about the code are worth having in front of a maintainer, because neither is visible
 * in a type:
 *
 * A wish that is seated after lower slots foresees their seats on every look, not once. The anchor
 * is the host as he stands on this look, and a seat foreseen around where he stood a frame ago is
 * a seat nobody will be handed. The foresight asks the probe exactly as the lower slot's own
 * machine asks it, with that slot's ring start and the bodies that machine sees, and with nothing
 * of this player's own: no place of a death and no locked seat, because neither travels.
 *
 * The watch on a seat handed out is read when the pump hands the far players in, which is once a
 * frame in a session and never outside one, and it is timed on the world's own clock, so the two
 * seconds are the same at any frame rate and end with the level.
 */
#include "mp_seat.h"

#include "mp_cells.h"
#include "mp_seat_internal.h"
#include "mp_spawnpoints.h"

#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bodies a search of an ordered wish keeps clear of: the far players, this player's own body
 * as a lower slot's machine sees it, and a seat foreseen for every lower slot. */
#define ORDER_BODIES (MP_SEAT_FAR_BODIES + 1u + MP_SEAT_FAR_BODIES)

/* The distance that says no far player was known, or none measured yet. */
#define NO_DISTANCE (-1.0f)

/* The last seat an ordered wish handed out, read again MP_SEAT_NEIGHBOUR_SECONDS later. The counts
 * are the caller's own, which live as long as the process. */
typedef struct seat_watch {
    bool              armed;
    bool              counted;   /* this hand-over is already counted as within a unit */
    uint32_t          world;
    float             since;     /* world seconds at the hand-over */
    float             seat[3];
    mp_seat_counts_t *counts;
} seat_watch_t;

typedef struct wish_state {
    bool  named_known;   /* the host's scene named a seat for this machine's player */
    float named[3];
    float named_heading;

    seat_watch_t watch;
} wish_state_t;

static wish_state_t ws;

/* What a wish keeps away from: the place of the death it follows, when it knows one, and the seats
 * that ended a life of this player. Every body it is handed stands in the world, unless the search
 * says otherwise. */
static mp_seat_avoid_t avoid_of(const mp_seat_wish_t *wish)
{
    mp_seat_avoid_t avoid;

    avoid.death        = wish->died_known ? wish->died_at : NULL;
    avoid.locks        = true;
    avoid.in_the_world = MP_SEAT_FAR_BODIES;
    return avoid;
}

/* ==============================================================================================
 * Starting a wish.
 * ============================================================================================ */

static void start_wish(mp_seat_wish_t *wish, const char *who, mp_seat_kind_t kind, uint8_t slot)
{
    memset(wish, 0, sizeof *wish);
    wish->who   = who != NULL ? who : "a seat";
    wish->kind  = kind;
    wish->stage = MP_SEAT_STAGE_ANCHOR;
    wish->slot  = slot;
    mp_seat_rule_wait_start(&wish->wait);
}

void mp_seat_wish_beside_players(mp_seat_wish_t *wish, const char *who, const float *died_at,
                                 uint8_t slot)
{
    if (wish == NULL) {
        return;
    }
    start_wish(wish, who, MP_SEAT_KIND_BESIDE_PLAYERS, slot);
    if (died_at != NULL) {
        memcpy(wish->died_at, died_at, sizeof wish->died_at);
        wish->died_known = true;
    }
}

void mp_seat_wish_beside_body(mp_seat_wish_t *wish, const char *who, uint8_t slot)
{
    if (wish != NULL) {
        start_wish(wish, who, MP_SEAT_KIND_BESIDE_BODY, slot);
    }
}

void mp_seat_wish_at_point(mp_seat_wish_t *wish, const char *who, const float point[3],
                           float heading, uint8_t slot)
{
    if (wish == NULL || point == NULL) {
        return;
    }
    start_wish(wish, who, MP_SEAT_KIND_AT_POINT, slot);
    memcpy(wish->target, point, sizeof wish->target);
    wish->heading = heading;
}

void mp_seat_wish_follow(mp_seat_wish_t *wish, const float anchor[3], float heading)
{
    if (wish == NULL || anchor == NULL || wish->stage != MP_SEAT_STAGE_ANCHOR ||
        wish->kind != MP_SEAT_KIND_BESIDE_BODY) {
        return;
    }
    memcpy(wish->target, anchor, sizeof wish->target);
    wish->heading = heading;
}

void mp_seat_wish_seat_after(mp_seat_wish_t *wish, bool known, const uint8_t *slots,
                             const uint8_t *bodies, size_t count)
{
    size_t i;

    if (wish == NULL || wish->kind != MP_SEAT_KIND_BESIDE_BODY) {
        return;
    }
    wish->ordered     = true;
    wish->order_known = known;
    wish->order_count = 0u;
    for (i = 0; known && slots != NULL && i < count && i < MP_SEAT_FAR_BODIES; ++i) {
        wish->order_slot[i] = slots[i];
        wish->order_body[i] = bodies != NULL ? bodies[i] : (uint8_t)MP_SEAT_ORDER_NO_BODY;
        ++wish->order_count;
    }
}

/* ==============================================================================================
 * The lower slots, foreseen.
 * ============================================================================================ */

/* The bodies lower slot `index` has in its own search: every body this search has so far but that
 * slot's own, which its machine does not see, and this player's body, which it does. The seats
 * foreseen for the slots below it are among the first, as seats handed out are in the scene's
 * seating. Answers how many. */
static size_t bodies_a_lower_slot_sees(const mp_seat_wish_t *wish, size_t index,
                                       const mp_seat_body_t *bodies, size_t count,
                                       mp_seat_body_t view[ORDER_BODIES])
{
    size_t seen = 0u;
    size_t i;

    for (i = 0; i < count && seen + 1u < ORDER_BODIES; ++i) {
        view[seen] = bodies[i];
        if (i < MP_SEAT_FAR_BODIES && i == (size_t)wish->order_body[index]) {
            view[seen].known = false;
        }
        ++seen;
    }
    memset(&view[seen], 0, sizeof view[seen]);
    view[seen].known  = mp_cells_hero_position(view[seen].position);
    view[seen].stands = view[seen].known;
    return seen + 1u;
}

/* The bodies this player's own search keeps clear of: the far players, then the seat each lower
 * slot is foreseen to take around the same anchor, lowest first. A lower slot that finds nothing is
 * counted and takes no seat; an anchor that moves, or probes that are missing, end the foresight,
 * because this player's own search is about to meet the same answer and say it. Answers how many.
 */
static size_t foresee_the_lower_slots(const mp_seat_wish_t *wish, mp_seat_counts_t *counts,
                                      mp_seat_body_t bodies[ORDER_BODIES])
{
    size_t count = MP_SEAT_FAR_BODIES;
    size_t i;

    memcpy(bodies, mp_seat_far_bodies(), MP_SEAT_FAR_BODIES * sizeof bodies[0]);
    for (i = 0; i < wish->order_count && count < ORDER_BODIES; ++i) {
        mp_seat_body_t    view[ORDER_BODIES];
        mp_seat_counts_t  scratch;
        mp_seat_outcome_t outcome;
        size_t            seen;
        float             seat[3];

        seen = bodies_a_lower_slot_sees(wish, i, bodies, count, view);
        memset(&scratch, 0, sizeof scratch);
        outcome = mp_seat_probe_avoiding(wish->target, true, wish->order_slot[i], view, seen, NULL,
                                         &scratch, seat);
        if (outcome == MP_SEAT_NONE_FREE) {
            ++counts->order_none;
            continue;
        }
        if (outcome != MP_SEAT_FOUND) {
            break;
        }
        memset(&bodies[count], 0, sizeof bodies[count]);
        bodies[count].known  = true;
        bodies[count].stands = true;
        memcpy(bodies[count].position, seat, sizeof bodies[count].position);
        ++count;
    }
    return count;
}

/* ==============================================================================================
 * The watch on a seat handed out.
 * ============================================================================================ */

/* How far the nearest far player stands from `seat`, standing or not, or NO_DISTANCE with none
 * known. */
static float nearest_far_player(const float seat[3])
{
    const mp_seat_body_t *far = mp_seat_far_bodies();
    float                 nearest = NO_DISTANCE;
    size_t                i;

    for (i = 0; i < MP_SEAT_FAR_BODIES; ++i) {
        float dx;
        float dy;
        float dz;
        float distance;

        if (!far[i].known) {
            continue;
        }
        dx       = far[i].position[0] - seat[0];
        dy       = far[i].position[1] - seat[1];
        dz       = far[i].position[2] - seat[2];
        distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (nearest < 0.0f || distance < nearest) {
            nearest = distance;
        }
    }
    return nearest;
}

static bool within_a_unit(float distance)
{
    return distance >= 0.0f && distance < MP_SEAT_BODY_CLEARANCE;
}

/* An ordered wish handed `seat` out: where the far players stand now, and a watch that reads them
 * again later. A hand-over is counted within a unit once, whichever reading finds it. */
static void watch_the_seat(mp_seat_counts_t *counts, const float seat[3])
{
    seat_watch_t *watch = &ws.watch;

    counts->handed_nearest = nearest_far_player(seat);
    counts->later_nearest  = NO_DISTANCE;
    watch->counted         = within_a_unit(counts->handed_nearest);
    if (watch->counted) {
        ++counts->handed_close;
    }
    memcpy(watch->seat, seat, sizeof watch->seat);
    watch->counts = counts;
    watch->armed  = mp_seat_world_now(&watch->world, &watch->since);
}

void mp_seat_wish_far_bodies_noted(void)
{
    seat_watch_t *watch = &ws.watch;
    uint32_t      world = 0u;
    float         now   = 0.0f;

    if (!watch->armed) {
        return;
    }
    if (!mp_seat_world_now(&world, &now) || world != watch->world || now < watch->since) {
        watch->armed = false;   /* another level, or none: its seat is no longer there to read */
        return;
    }
    if (now - watch->since < MP_SEAT_NEIGHBOUR_SECONDS) {
        return;
    }
    watch->armed                 = false;
    watch->counts->later_nearest = nearest_far_player(watch->seat);
    if (!watch->counted && within_a_unit(watch->counts->later_nearest)) {
        ++watch->counts->handed_close;
    }
}

void mp_seat_wish_world_ended(void)
{
    ws.watch.armed = false;
}

/* The first look of an ordered wish tells its caller's counts who it is and how many slots it is
 * seated after; a watch still standing for an earlier wish ends there. */
static void note_the_order(mp_seat_wish_t *wish, mp_seat_counts_t *counts)
{
    if (!wish->ordered || wish->order_noted) {
        return;
    }
    wish->order_noted      = true;
    counts->order_who      = wish->who;
    counts->order_lower    = wish->order_count;
    counts->handed_nearest = NO_DISTANCE;
    counts->later_nearest  = NO_DISTANCE;
    if (!wish->order_known) {
        ++counts->order_no_roster;
    }
    ws.watch.armed = false;
}

/* ==============================================================================================
 * The search, stage by stage.
 * ============================================================================================ */

/* How much reason to wait an outcome gives, so that several anchors on one look can be summed up
 * as the most hopeful of them: no level at all stops the clock, an anchor that is moving may stop
 * moving, and a missing probe or an empty ring is nothing to wait for. */
static int reason_to_wait(mp_seat_outcome_t outcome)
{
    switch (outcome) {
    case MP_SEAT_NO_LEVEL:      return 3;
    case MP_SEAT_ANCHOR_MOVING: return 2;
    case MP_SEAT_NO_PROBES:     return 1;
    case MP_SEAT_NONE_FREE:
    case MP_SEAT_FOUND:
    default:                    return 0;
    }
}

void mp_seat_name_anchor(const float *seat, float heading)
{
    ws.named_known = seat != NULL;
    if (seat != NULL) {
        memcpy(ws.named, seat, sizeof ws.named);
        ws.named_heading = heading;
    }
}

/* Beside the players: the seat a scene named first, then every standing far player, the one nearest
 * the death first, read as they stand on this look. The first free seat wins and faces the way its
 * anchor faces. Nobody standing is answered before either, so a named seat never stands in for the
 * wipe. */
static mp_seat_outcome_t search_beside_players(mp_seat_wish_t *wish, mp_seat_counts_t *counts,
                                               float seat[3], float *heading)
{
    const mp_seat_body_t *far = mp_seat_far_bodies();
    size_t                order[MP_SEAT_FAR_BODIES];
    size_t                anchors;
    size_t                i;
    mp_seat_outcome_t     overall = MP_SEAT_NONE_FREE;
    mp_seat_avoid_t       avoid = avoid_of(wish);

    anchors = mp_seat_rule_anchor_order(far, MP_SEAT_FAR_BODIES,
                                        wish->died_known ? wish->died_at : NULL, order,
                                        MP_SEAT_FAR_BODIES);
    if (anchors == 0u) {
        /* Not this search's to answer. Nobody standing is the wipe, and the caller's rule set
         * decides it and takes the wish back; the clock does not run meanwhile, so no fallback
         * can put a dead player alone on an authored point. */
        ++counts->nobody_standing;
        return MP_SEAT_NOBODY_STANDING;
    }
    if (ws.named_known &&
        mp_seat_probe_avoiding(ws.named, false, wish->slot, far, MP_SEAT_FAR_BODIES, &avoid,
                               counts, seat) == MP_SEAT_FOUND) {
        ++counts->on_named;
        *heading = ws.named_heading;
        return MP_SEAT_FOUND;
    }
    for (i = 0; i < anchors; ++i) {
        const mp_seat_body_t *anchor = &far[order[i]];
        mp_seat_outcome_t     outcome;

        ++counts->live_reads;
        outcome = mp_seat_probe_avoiding(anchor->position, true, wish->slot, far,
                                         MP_SEAT_FAR_BODIES, &avoid, counts, seat);
        if (outcome == MP_SEAT_FOUND) {
            *heading = anchor->heading;
            return MP_SEAT_FOUND;
        }
        if (reason_to_wait(outcome) > reason_to_wait(overall)) {
            overall = outcome;
        }
    }
    return overall;
}

/* One look at the stage the wish is in. Beside a body the lower slots' seats are foreseen first;
 * a wish that was told none keeps clear of the far players alone, as before. */
static mp_seat_outcome_t search_once(mp_seat_wish_t *wish, mp_seat_counts_t *counts,
                                     float seat[3], float *heading)
{
    mp_seat_outcome_t outcome;
    mp_seat_avoid_t   avoid = avoid_of(wish);

    if (wish->stage == MP_SEAT_STAGE_ANCHOR && wish->kind == MP_SEAT_KIND_BESIDE_PLAYERS) {
        return search_beside_players(wish, counts, seat, heading);
    }
    if (wish->stage == MP_SEAT_STAGE_ANCHOR && wish->kind == MP_SEAT_KIND_BESIDE_BODY) {
        mp_seat_body_t bodies[ORDER_BODIES];
        size_t         count = foresee_the_lower_slots(wish, counts, bodies);

        ++counts->live_reads;
        outcome = mp_seat_probe_avoiding(wish->target, true, wish->slot, bodies, count, &avoid,
                                         counts, seat);
    } else {
        outcome = mp_seat_probe_avoiding(wish->target, false, wish->slot, mp_seat_far_bodies(),
                                         MP_SEAT_FAR_BODIES, &avoid, counts, seat);
    }
    if (outcome == MP_SEAT_FOUND) {
        *heading = wish->heading;
    }
    return outcome;
}

/* What an outcome means for the clock. A missing probe counts as an empty look, so a build whose
 * probes did not resolve still reaches the point itself rather than holding a corpse for good.
 * Nobody standing does not count: it is the caller's rule set that answers it. */
static mp_seat_look_t look_of(mp_seat_outcome_t outcome)
{
    switch (outcome) {
    case MP_SEAT_NO_LEVEL:
    case MP_SEAT_NOBODY_STANDING: return MP_SEAT_LOOK_NONE;
    case MP_SEAT_ANCHOR_MOVING: return MP_SEAT_LOOK_MOVING;
    case MP_SEAT_FOUND:
    case MP_SEAT_NONE_FREE:
    case MP_SEAT_NO_PROBES:
    default:                    return MP_SEAT_LOOK_EMPTY;
    }
}

/* The authored point a beside wish falls back to: the free one nearest its first anchor, or the
 * level start. False when the level offers none, and the wish then goes on looking where it is. */
static bool choose_fallback(mp_seat_wish_t *wish, mp_seat_counts_t *counts)
{
    const mp_seat_body_t *far = mp_seat_far_bodies();
    size_t                order[MP_SEAT_FAR_BODIES];
    const float          *died_at = wish->died_known ? wish->died_at : NULL;
    const float          *anchor  = NULL;
    float                 point[3];
    float                 heading = 0.0f;
    bool                  level_start = false;

    if (wish->kind == MP_SEAT_KIND_BESIDE_BODY) {
        anchor = wish->target;
    } else if (mp_seat_rule_anchor_order(far, MP_SEAT_FAR_BODIES, died_at, order, 1u) != 0u) {
        anchor = far[order[0]].position;
    } else {
        anchor = died_at;
    }
    if (!mp_spawnpoints_take_nearest(anchor, died_at, mp_spawnpoints_now(), point, &heading,
                                     &level_start)) {
        if (!wish->said_no_point) {
            wish->said_no_point = true;
            log_warning("%s found no seat beside a standing player and the level offers no "
                        "authored point to fall back to, so it goes on looking beside the players",
                        wish->who);
        }
        return false;
    }
    memcpy(wish->target, point, sizeof wish->target);
    wish->heading = heading;
    if (level_start) {
        ++counts->to_start;
    } else {
        ++counts->to_point;
    }
    log_info("%s found no seat beside a standing player in %u substep(s), %u of them on a good "
             "anchor, so it takes %s at %.2f %.2f %.2f", wish->who, (unsigned)wish->wait.total,
             (unsigned)wish->wait.good,
             level_start ? "the level's own start" : "the spawn point nearest to the anchor",
             (double)point[0], (double)point[1], (double)point[2]);
    return true;
}

/* A stage has run out: on to the next one, which starts its own clock. */
static void advance(mp_seat_wish_t *wish, mp_seat_counts_t *counts)
{
    mp_seat_stage_t next = mp_seat_rule_next_stage(wish->stage,
                                                   wish->kind != MP_SEAT_KIND_AT_POINT);

    if (wish->wait.good > counts->longest_wait) {
        counts->longest_wait = wish->wait.good;
    }
    if (next == MP_SEAT_STAGE_FALLBACK && !choose_fallback(wish, counts)) {
        mp_seat_rule_wait_start(&wish->wait);
        return;
    }
    if (next == MP_SEAT_STAGE_AS_AUTHORED) {
        log_warning("%s found no seat around %.2f %.2f %.2f either in %u substep(s), so the body "
                    "is put on that point as the level authored it", wish->who,
                    (double)wish->target[0], (double)wish->target[1], (double)wish->target[2],
                    (unsigned)wish->wait.total);
    }
    wish->stage = next;
    mp_seat_rule_wait_start(&wish->wait);
}

bool mp_seat_wish_step(mp_seat_wish_t *wish, uint32_t now, mp_seat_counts_t *counts,
                       float seat[3], float *heading)
{
    mp_seat_outcome_t outcome;

    if (wish == NULL || counts == NULL || seat == NULL || heading == NULL) {
        return false;
    }
    note_the_order(wish, counts);
    /* The last stage always answers: the point the fallback chose, as the level put it there. */
    if (wish->stage == MP_SEAT_STAGE_AS_AUTHORED) {
        memcpy(seat, wish->target, sizeof wish->target);
        *heading = wish->heading;
        ++counts->as_authored;
        if (wish->ordered) {
            watch_the_seat(counts, seat);
        }
        return true;
    }
    ++counts->searches;
    outcome = search_once(wish, counts, seat, heading);
    if (outcome == MP_SEAT_FOUND) {
        if (wish->wait.good > counts->longest_wait) {
            counts->longest_wait = wish->wait.good;
        }
        if (wish->ordered) {
            watch_the_seat(counts, seat);
        }
        return true;
    }
    ++counts->found_nothing;
    if (mp_seat_rule_wait_look(&wish->wait, now, look_of(outcome))) {
        advance(wish, counts);
    }
    return false;
}

void mp_seat_wish_pass(mp_seat_wish_t *wish, uint32_t now)
{
    if (wish != NULL) {
        (void)mp_seat_rule_wait_look(&wish->wait, now, MP_SEAT_LOOK_NONE);
    }
}
