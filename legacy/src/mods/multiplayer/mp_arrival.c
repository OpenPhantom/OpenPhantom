/* mp_arrival.c: the client's offset onto the host, after his saved game or at a fresh co-operative
 * level. See the header.
 *
 * Three things about the code are worth having in front of a maintainer, because none of them is
 * visible in a type:
 *
 * The search and the seating are in two different modules on purpose. The point beside the host
 * is searched by the seat module, which owns the three world probes, the reading of the four
 * situations that are a reason to wait and the stages a search goes through when it finds nothing.
 * The body is moved by the start module, which owns the teleport. This file is the decision
 * between them and holds neither.
 *
 * The point is probed on the frame it is used. Where the host stands moves, and so does what is
 * around him. A point computed when the level began would already be somewhere else by the time
 * the host had finished loading his own.
 *
 * The arm is cleared on every way out, including the ones that did nothing. A run that turns out
 * not to want an offset stands down once rather than asking the same question every frame for the
 * rest of the level, and it says so once in the counters rather than once in the log.
 */
#include "mp_arrival.h"

#include "mp_bank.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_lobby_late.h"
#include "mp_lobby.h"
#include "mp_seat.h"
#include "mp_seat_order.h"
#include "mp_start.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The log's name for this caller of the seat search. */
static const char ARRIVAL_WHO[] = "the offset";

typedef struct arrival_state {
    bool     armed;
    bool     counted;   /* this arm has already been counted as one that wants an offset */
    bool     from_save; /* what this arm wanted it for, so the hand-over says which */
    bool     late;      /* this level was entered by joining a session that already ran */
    uint32_t frames;    /* drawn frames since the arm, for the wait's own line */

    /* The deadline's clock: this machine's substeps since the first tick after the arm, so a
     * load on this side, which runs none, costs nothing against it. */
    bool     clock_started;
    uint32_t armed_at;

    /* The seat, searched from the first frame the host stands and carried across frames, so its
     * clock ends a search that finds nothing. */
    bool             seeking;
    mp_seat_wish_t   wish;
    mp_seat_counts_t seat;

    /* Where the host's history stood as this level began: a pose sampled before it is not taken. */
    bool     marked;
    uint32_t mark_tick;
    uint32_t mark_starts;

    /* Each look at the host's pose while an offset was wanted, by what the pose was. */
    uint32_t looks_world_before;   /* another world's */
    uint32_t looks_before_mark;    /* this world's, sampled before the level began */
    uint32_t looks_this_world;     /* this world's, sampled after */
    uint32_t looks_new_history;    /* of a history begun after the level began */

    /* Every path out of this module increments exactly one of the first group, and every frame
     * that ends in a wait increments exactly one of the second. */
    uint32_t levels;           /* level begins this module was told about */
    uint32_t wanted;           /* of those, the ones that turned out to want an offset */
    uint32_t stood_down;       /* of those, the ones that did not */
    uint32_t placed;           /* a point beside the host was handed to the placement */
    uint32_t refused;          /* the placement would not take it */
    uint32_t dropped;          /* the deadline passed */

    uint32_t waits_for_setup;  /* a client that had not read the host's setup note yet */
    uint32_t waits_for_host;   /* no pose, or a host not standing in a level of his own */
    uint32_t waits_for_seat;   /* the host is there and no free point beside him answered */

    uint32_t for_save;         /* of the wanted ones, those after the host's savegame */
    uint32_t for_fresh;        /* and those at a fresh co-operative level */

    /* Who the points were searched beside, per hand-over, and the frames the host lay dead in
     * this world with nobody standing to arrive beside instead. */
    uint32_t anchors_host;
    uint32_t anchors_other;
    uint32_t anchors_none;
} arrival_state_t;

static arrival_state_t ar;

/* ==============================================================================================
 * The pure decisions.
 * ============================================================================================ */

mp_arrival_want_t mp_arrival_wanted_for(bool is_client, bool setup_known, bool from_save,
                                        bool coop)
{
    if (!is_client) {
        return MP_ARRIVAL_WANT_NO;
    }
    if (!setup_known) {
        return MP_ARRIVAL_WANT_UNKNOWN;
    }
    return (from_save || coop) ? MP_ARRIVAL_WANT_YES : MP_ARRIVAL_WANT_NO;
}

bool mp_arrival_anchor_is_ready(bool resolved, uint8_t far_slot, uint8_t my_slot,
                                bool alive, bool dead)
{
    if (!resolved) {
        return false;
    }
    if (far_slot != (uint8_t)MP_BRIDGE_HOST_SLOT) {
        return false;
    }
    /* A machine whose own slot is the host's slot is the host, whatever else it thinks it is.
     * Asked here as well as through the role, because these two answers come from different
     * places and a session in which they disagree is one to do nothing in. */
    if (far_slot == my_slot) {
        return false;
    }
    return alive && !dead;
}

bool mp_arrival_pose_is_after(bool marked, uint32_t mark_tick, uint32_t mark_starts,
                              uint32_t state_tick, uint32_t starts)
{
    if (!marked || starts != mark_starts) {
        return true;
    }
    return (int32_t)(state_tick - mark_tick) > 0;
}

mp_arrival_anchor_t mp_arrival_pick_anchor(bool host_ready, bool host_dead, const float *host_at,
                                           const mp_seat_body_t *others, size_t count,
                                           size_t *chosen)
{
    size_t nearest = 0u;

    if (host_ready) {
        return MP_ARRIVAL_ANCHOR_HOST;
    }
    if (!host_dead || host_at == NULL || others == NULL) {
        return MP_ARRIVAL_ANCHOR_NONE;
    }
    if (mp_seat_rule_anchor_order(others, count, host_at, &nearest, 1u) == 0u) {
        return MP_ARRIVAL_ANCHOR_NONE;
    }
    if (chosen != NULL) {
        *chosen = nearest;
    }
    return MP_ARRIVAL_ANCHOR_OTHER;
}

mp_arrival_step_t mp_arrival_step(bool armed, mp_arrival_want_t want, bool anchor_ready,
                                  uint32_t substeps_waited)
{
    if (!armed) {
        return MP_ARRIVAL_STEP_IDLE;
    }
    if (want == MP_ARRIVAL_WANT_NO) {
        return MP_ARRIVAL_STEP_STAND_DOWN;
    }
    if (want == MP_ARRIVAL_WANT_YES && anchor_ready) {
        return MP_ARRIVAL_STEP_RUN;
    }
    if (substeps_waited >= MP_ARRIVAL_DEADLINE_SUBSTEPS) {
        return MP_ARRIVAL_STEP_DROP;
    }
    return MP_ARRIVAL_STEP_WAIT;
}

/* ==============================================================================================
 * Arming.
 * ============================================================================================ */

void mp_arrival_note_level_begin(void)
{
    /* Before anything else: where the host's history stands as this level begins, so that no pose
     * sampled before it, the level before's last above all, is handed a seat search as the host. */
    ar.marked = mp_bridge_drain_host_history_mark(&ar.mark_tick, &ar.mark_starts);
    ++ar.levels;
    ar.armed         = true;
    ar.counted       = false;
    ar.late          = mp_bridge_lobby_late_start();
    ar.seeking       = false;
    ar.frames        = 0u;
    ar.clock_started = false;
    /* And anything the last level handed over and never got seated. A point beside the host is
     * three floats in ONE world; carried into the next one it is a teleport into whatever happens
     * to be at those coordinates there. */
    mp_start_cancel_pose();
}

/* ==============================================================================================
 * Carrying it out.
 * ============================================================================================ */

/* The point handed to the placement, and the line that says which rule it came from. The line for
 * a savegame is the one the field runs have always carried, word for word.
 *
 * The body goes through the placement's teleport and not through the respawn, although the same
 * candidate points stand in front of both doors. The respawn entry returns in silence unless the
 * player module's own state is 1, and what it then does is record a wish and drop the module into
 * state 4, which the player tick turns into a fade out, a despawn, a spawn and a fade back in:
 * right for a corpse, wrong for a body that is alive and was just spawned, whose screen would go
 * black and whose loadout would be written again. The teleport writes the position, the heading
 * and the rider position, and zeroes the ground contact block first so a body moved off a
 * platform is not pulled back onto it on the next substep; it is the door every level warp and
 * every cutscene placement in the shipped game goes through. */
static void hand_over(const float seat[3], float heading)
{
    if (!mp_start_place_at(seat, heading)) {
        ++ar.refused;
        log_warning("a free point beside the host was found at %.2f %.2f %.2f and the placement "
                    "would not take it, so this player begins at the level's own start point",
                    (double)seat[0], (double)seat[1], (double)seat[2]);
        return;
    }
    /* Handed over rather than done: the placement waits for the two engine gates and says in its
     * own line whether the body actually took the point. */
    ++ar.placed;
    if (ar.wish.stage != MP_SEAT_STAGE_ANCHOR) {
        log_info("no free point beside the host answered, so the point the fallback chose at %.2f "
                 "%.2f %.2f facing %.2f was handed to the placement instead",
                 (double)seat[0], (double)seat[1], (double)seat[2], (double)heading);
    } else if (ar.from_save) {
        log_info("the host loaded a savegame, so a free point beside him at %.2f %.2f %.2f facing "
                 "%.2f was handed to the placement instead of the level's own start point",
                 (double)seat[0], (double)seat[1], (double)seat[2], (double)heading);
    } else {
        log_info("the level began fresh, so a free point beside the host at %.2f %.2f %.2f facing "
                 "%.2f was handed to the placement instead of the level's own start point, which "
                 "every player of a fresh level shares",
                 (double)seat[0], (double)seat[1], (double)seat[2], (double)heading);
    }
}

/* One look for the point beside the anchor, and the body moved onto it. False means nothing
 * answered this frame and the offset stays armed; the seat's stages are what end a search that
 * never finds one. The heading is the anchor's own: two players who begin together look the same
 * way. The anchor is the host, or while he lies dead the standing far player nearest him. */
static bool place_beside(const mp_bridge_far_pose_t *anchor, mp_arrival_anchor_t kind,
                         uint32_t substeps)
{
    float seat[3];
    float heading = 0.0f;

    if (!ar.seeking) {
        mp_seat_order_wish_beside_host(&ar.wish, ARRIVAL_WHO, mp_bridge_drain_my_slot(), ar.late);
        ar.seeking = true;
    }
    mp_seat_wish_follow(&ar.wish, anchor->position, anchor->heading);
    if (!mp_seat_wish_step(&ar.wish, substeps, &ar.seat, seat, &heading)) {
        return false;
    }
    if (kind == MP_ARRIVAL_ANCHOR_OTHER) {
        ++ar.anchors_other;
        log_info("the offset's anchor: the far player of slot %u at %.2f %.2f %.2f, the one "
                 "standing nearest the host, who lies dead", (unsigned)anchor->slot,
                 (double)anchor->position[0], (double)anchor->position[1],
                 (double)anchor->position[2]);
    } else {
        ++ar.anchors_host;
        log_info("the offset's anchor: the host at %.2f %.2f %.2f from the sample of tick %u, "
                 "this world's first after tick %u", (double)anchor->position[0],
                 (double)anchor->position[1], (double)anchor->position[2],
                 (unsigned)anchor->state_tick, (unsigned)(ar.marked ? ar.mark_tick : 0u));
    }
    hand_over(seat, heading);
    return true;
}

/* The far players who are neither the host nor this player, as poses of this world, for the
 * choice made while the host lies dead. Answers how many; `poses` holds each one's pose. */
static size_t read_the_others(uint8_t my_slot, mp_seat_body_t *bodies,
                              mp_bridge_far_pose_t *poses)
{
    size_t count = 0u;
    size_t bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX && count < MP_SEAT_FAR_BODIES; ++bank) {
        mp_bridge_far_pose_t pose;

        if (!mp_bridge_far_pose(bank, MP_FAR_READER_ARRIVAL, &pose) ||
            pose.slot == (uint8_t)MP_BRIDGE_HOST_SLOT || pose.slot == my_slot) {
            continue;
        }
        poses[count] = pose;
        memset(&bodies[count], 0, sizeof bodies[count]);
        bodies[count].known   = true;
        bodies[count].stands  = mp_bridge_far_pose_stands(&pose);
        bodies[count].heading = pose.heading;
        memcpy(bodies[count].position, pose.position, sizeof bodies[count].position);
        ++count;
    }
    return count;
}

/* The anchor of this frame: the host when he is ready, and when he lies dead in this world the
 * standing far player nearest him. Counts a frame the host lay dead and nobody stood. */
static mp_arrival_anchor_t choose_anchor(bool host_ready, const mp_bridge_far_pose_t *host,
                                         bool resolved, mp_bridge_far_pose_t *anchor)
{
    mp_seat_body_t       bodies[MP_SEAT_FAR_BODIES];
    mp_bridge_far_pose_t poses[MP_SEAT_FAR_BODIES];
    size_t               count  = 0u;
    size_t               chosen = 0u;
    bool                 dead   = resolved && host->slot == (uint8_t)MP_BRIDGE_HOST_SLOT &&
                                  host->dead;
    mp_arrival_anchor_t  kind;

    if (dead) {
        count = read_the_others(mp_bridge_drain_my_slot(), bodies, poses);
    }
    kind = mp_arrival_pick_anchor(host_ready, dead, host->position, bodies, count, &chosen);
    if (kind == MP_ARRIVAL_ANCHOR_OTHER) {
        *anchor = poses[chosen];
    } else {
        *anchor = *host;
        ar.anchors_none += dead ? 1u : 0u;
    }
    return kind;
}

/* One look at the host's pose while an offset is wanted, counted by what the pose was. True when
 * it is one to arrive beside: of this world, and sampled after this level began here. */
static bool look_at_the_host(bool resolved, const mp_bridge_far_pose_t *host)
{
    uint32_t newest = 0u;
    uint32_t starts = 0u;

    if (!resolved) {
        ar.looks_world_before += mp_bridge_drain_host_elsewhere() ? 1u : 0u;
        return false;
    }
    (void)mp_bridge_drain_host_history_mark(&newest, &starts);
    if (!mp_arrival_pose_is_after(ar.marked, ar.mark_tick, ar.mark_starts, host->state_tick,
                                  starts)) {
        ++ar.looks_before_mark;
        return false;
    }
    if (ar.marked && starts != ar.mark_starts) {
        ++ar.looks_new_history;
    } else {
        ++ar.looks_this_world;
    }
    return true;
}

/* Counted the first time the answer is settled rather than on the frame it is acted on: a search
 * that has to be retried would otherwise count the same level begin once per frame. */
static void count_the_want(mp_arrival_want_t want, bool from_save)
{
    if (want != MP_ARRIVAL_WANT_YES || ar.counted) {
        return;
    }
    ar.counted   = true;
    ar.from_save = from_save;
    ++ar.wanted;
    if (from_save) {
        ++ar.for_save;
    } else {
        ++ar.for_fresh;
    }
}

void mp_arrival_tick(uint32_t substeps)
{
    mp_lobby_setup_t     setup;
    mp_bridge_far_pose_t host;
    mp_bridge_far_pose_t anchor;
    mp_arrival_want_t    want;
    mp_arrival_anchor_t  kind = MP_ARRIVAL_ANCHOR_NONE;
    bool                 setup_known;
    bool                 resolved;
    bool                 from_save;
    bool                 host_ready;

    if (!ar.armed) {
        return;
    }
    if (!ar.clock_started) {
        ar.clock_started = true;
        ar.armed_at      = substeps;
    }

    memset(&setup, 0, sizeof setup);
    memset(&host, 0, sizeof host);
    setup_known = mp_bridge_lobby_setup(&setup);
    resolved    = mp_bridge_drain_far_pose(&host);
    from_save   = (setup.flags & MP_LOBBY_F_FROM_SAVE) != 0u;

    want = mp_arrival_wanted_for(mp_bridge_drain_is_client(), setup_known, from_save,
                                 setup.mode == (uint8_t)MP_LOBBY_MODE_COOP);
    count_the_want(want, from_save);
    if (want == MP_ARRIVAL_WANT_YES) {
        resolved = look_at_the_host(resolved, &host);
    }
    host_ready = mp_arrival_anchor_is_ready(resolved, host.slot, mp_bridge_drain_my_slot(),
                                            host.alive, host.dead);
    anchor = host;
    if (want == MP_ARRIVAL_WANT_YES) {
        kind = choose_anchor(host_ready, &host, resolved, &anchor);
    }

    switch (mp_arrival_step(true, want, kind != MP_ARRIVAL_ANCHOR_NONE,
                            substeps - ar.armed_at)) {
    case MP_ARRIVAL_STEP_RUN:
        if (place_beside(&anchor, kind, substeps)) {
            ar.armed = false;
        } else {
            ++ar.waits_for_seat;
            ++ar.frames;
        }
        break;
    case MP_ARRIVAL_STEP_STAND_DOWN:
        ar.armed = false;
        ++ar.stood_down;
        break;
    case MP_ARRIVAL_STEP_DROP:
        ar.armed = false;
        ++ar.dropped;
        log_warning("an offset was armed %u substep(s) of simulation ago and never became "
                    "possible, so this player stays where the level put him; of the %u frame(s) "
                    "drawn since, %u had no setup note from the host and %u had no host standing "
                    "in a level to come to", (unsigned)(substeps - ar.armed_at),
                    (unsigned)ar.frames, (unsigned)ar.waits_for_setup,
                    (unsigned)ar.waits_for_host);
        break;
    case MP_ARRIVAL_STEP_WAIT:
        if (want == MP_ARRIVAL_WANT_UNKNOWN) {
            ++ar.waits_for_setup;
        } else {
            ++ar.waits_for_host;
        }
        /* A host who is not there is not a look for the seat, so its clock does not run. */
        if (ar.seeking) {
            mp_seat_wish_pass(&ar.wish, substeps);
        }
        ++ar.frames;
        break;
    case MP_ARRIVAL_STEP_IDLE:
    default:
        break;
    }
}

void mp_arrival_report(void)
{
    if (ar.levels == 0u) {
        /* Said rather than skipped. A module that is never told a level began is exactly the shape
         * of a feature that ships built, tested and never run, and this line is the one that would
         * otherwise be missing from the log. */
        log_warning("  the offset: no level begin ever reached this module, so no client was ever "
                    "put beside a host who had loaded a savegame");
        return;
    }
    log_info("  the offset: %u level begin(s), %u wanted one, %u stood down, %u handed to the "
             "placement, %u refused there, %u dropped at the deadline",
             (unsigned)ar.levels, (unsigned)ar.wanted, (unsigned)ar.stood_down,
             (unsigned)ar.placed, (unsigned)ar.refused, (unsigned)ar.dropped);
    log_info("  the offset waited: %u frame(s) with no setup note from the host, %u with no host "
             "standing in a level, %u with the host there and no free point beside him%s",
             (unsigned)ar.waits_for_setup, (unsigned)ar.waits_for_host,
             (unsigned)ar.waits_for_seat, ar.armed ? "; one is still armed" : "");
    log_info("  the offset's reason: %u for a savegame, %u for a fresh co-op level",
             (unsigned)ar.for_save, (unsigned)ar.for_fresh);
    mp_seat_report("the offset's seat:", "the offset's fallback:", &ar.seat);
    log_info("  the offset's anchors: host %u, another standing player %u, none %u (frames "
             "the host lay dead in this world with nobody standing)", (unsigned)ar.anchors_host,
             (unsigned)ar.anchors_other, (unsigned)ar.anchors_none);
    log_info("  the offset's anchor: %u look(s) at a pose of the host from the world before, %u "
             "at one of this world from before the level began (tick %u or older), %u look(s) at "
             "a pose of this world, %u look(s) at a pose of a history begun after the level began",
             (unsigned)ar.looks_world_before, (unsigned)ar.looks_before_mark,
             (unsigned)(ar.marked ? ar.mark_tick : 0u), (unsigned)ar.looks_this_world,
             (unsigned)ar.looks_new_history);
}
