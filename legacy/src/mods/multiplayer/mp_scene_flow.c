/* mp_scene_flow.c: the gathering's state machines, pure. See the header.
 *
 * SIZE NOTE: over 600 lines. Four machines that one gathering runs together, the host's scene, a
 * client's mirror, one player's seat and the seating, each small, and every one of them read by the
 * same two bindings. The next seam is the seating, mp_scene_seat_everyone and what it calls, which
 * shares nothing with the other three but the sitter type; its test is a file of its own already.
 */
#include "mp_scene_flow.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Substeps a second, the simulation's own rate, for turning a fade's seconds into a deadline. */
#define SUBSTEPS_A_SECOND 32.0f

/* What a deadline adds to the fade itself: a quarter of a second, for a tint the renderer sets its
 * end in only when it draws. */
#define FADE_SLACK_SUBSTEPS 8u

uint8_t mp_scene_what_of(mp_scene_kind_t kind)
{
    switch (kind) {
    case MP_SCENE_KIND_LOCK:
        return (uint8_t)(MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS);
    case MP_SCENE_KIND_HERO:
        return (uint8_t)(MP_SCENE_WHAT_HERO | MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS);
    case MP_SCENE_KIND_WARP:
        return (uint8_t)MP_SCENE_WHAT_WARP;
    case MP_SCENE_KINDS:
    default:
        return 0u;
    }
}

bool mp_scene_for_all_now(uint8_t phase, uint8_t what)
{
    return (phase == (uint8_t)MP_SCENE_PHASE_GATHERING ||
            phase == (uint8_t)MP_SCENE_PHASE_RUNNING) &&
           (what & (uint8_t)(MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_HERO)) != 0u;
}

/* ==============================================================================================
 * The host's scene.
 * ============================================================================================ */

const char *mp_scene_release_text(mp_scene_release_t released)
{
    switch (released) {
    case MP_SCENE_RELEASE_SEATED:
        return "everyone stood at their seat";
    case MP_SCENE_RELEASE_BOUND:
        return "the 1500 ms bound ran out while the host stood";
    case MP_SCENE_RELEASE_ALONE:
        return "every far player had left the session";
    case MP_SCENE_RELEASE_NOBODY:
        return "nobody was gathered, every player stayed where it stood";
    case MP_SCENE_RELEASE_SEATED_SOME:
        return "everyone with a seat stood at it";
    case MP_SCENE_RELEASE_NONE:
    default:
        return "no hold stood";
    }
}

const char *mp_scene_given_up_text(mp_scene_move_t host)
{
    switch (host) {
    case MP_SCENE_MOVE_DEAD:
        return "lay dead";
    case MP_SCENE_MOVE_MODE:
        return "could not be moved (a gun, the water, a push block, a jump or a ledge)";
    case MP_SCENE_MOVE_NO_BODY:
        return "had no body to be taken (a respawn or a load)";
    case MP_SCENE_MOVE_YES:
    default:
        return "stood again only as the wait ran out";
    }
}

/* The next number after `serial`, never nought, which is the number of no scene. */
static uint16_t next_serial(uint16_t serial)
{
    uint16_t next = (uint16_t)(serial + 1u);

    return next == 0u ? 1u : next;
}

static uint8_t next_warp(uint8_t serial)
{
    uint8_t next = (uint8_t)(serial + 1u);

    return next == 0u ? 1u : next;
}

mp_scene_begin_t mp_scene_host_begin(mp_scene_host_flow_t *flow, mp_scene_kind_t kind,
                                     uint32_t now)
{
    bool             inside;
    mp_scene_begin_t answer = MP_SCENE_BEGIN_NEW;

    if (flow == NULL || kind >= MP_SCENE_KINDS) {
        return MP_SCENE_BEGIN_SECOND;
    }
    /* A scene given up may still be played by the engine here: a door of it is not a new one. */
    inside = flow->phase == MP_SCENE_PHASE_GATHERING || flow->phase == MP_SCENE_PHASE_RUNNING ||
             flow->given_up;
    if (inside && kind != MP_SCENE_KIND_WARP) {
        return MP_SCENE_BEGIN_SECOND;
    }
    if (inside) {
        answer = MP_SCENE_BEGIN_WARP_OVER;
    }
    flow->serial        = next_serial(flow->serial);
    flow->kind          = kind;
    flow->phase         = MP_SCENE_PHASE_GATHERING;
    flow->holds         = kind != MP_SCENE_KIND_WARP;
    flow->began         = now;
    flow->last          = now;
    flow->phase_since   = now;
    flow->hold_standing = 0u;
    flow->hold_dead     = 0u;
    flow->seen_running  = false;
    flow->released      = MP_SCENE_RELEASE_NONE;
    flow->grace         = 0u;
    flow->given_up      = false;
    if (kind == MP_SCENE_KIND_WARP) {
        flow->warp_serial = next_warp(flow->warp_serial);
    }
    return answer;
}

static void enter(mp_scene_host_flow_t *flow, mp_scene_phase_t phase, uint32_t now)
{
    flow->phase       = phase;
    flow->phase_since = now;
}

/* The wait has run out: over for everybody. The actor and the grab are free, the note says over so
 * every client is let go, and the engine plays the scene here as it would alone, with the host
 * dead or wherever he stands. Never a release into running: that would play the scene for
 * everybody with a corpse, or lock the clients into a scene whose grab cannot come. */
static void give_the_scene_up(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look)
{
    flow->holds        = false;
    flow->given_up     = true;
    flow->given_up_for = look->host_stands ? look->may_move : MP_SCENE_MOVE_DEAD;
    flow->seen_running = look->running;
    flow->grace        = 0u;
    enter(flow, MP_SCENE_PHASE_OVER, look->now);
}

static bool out_of_time(const mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look)
{
    return look->now - flow->began >= MP_SCENE_WAIT_CAP_SUBSTEPS;
}

/* A lock or a hero scene: the hold's clocks, then whether it falls. The substeps since the last
 * look belong to what this look found, the host standing or dead. A hold falls into running only
 * with the host standing: a dead host is waited for, his re-entry bringing him to the gathering. */
static void step_the_hold(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look,
                          uint32_t passed)
{
    if (look->host_stands) {
        flow->hold_standing += passed;
    } else {
        flow->hold_dead += passed;
    }
    if (look->alone) {
        /* With nobody else in the session there is nobody to wait for and nobody to play the
         * scene for: it runs as it would with no session at all. */
        flow->released = MP_SCENE_RELEASE_ALONE;
    } else if (look->host_stands && look->nobody_gathered) {
        /* Nobody was handed a seat, so "everybody seated" would be true of nobody. */
        flow->released = MP_SCENE_RELEASE_NOBODY;
    } else if (look->host_stands && look->everyone_seated) {
        /* A far player no seat answered for is at no seat, so "everybody" would leave him out. */
        flow->released = look->unseated == 0u ? MP_SCENE_RELEASE_SEATED
                                              : MP_SCENE_RELEASE_SEATED_SOME;
    } else if (look->host_stands && flow->hold_standing >= MP_SCENE_HOLD_SUBSTEPS) {
        flow->released = MP_SCENE_RELEASE_BOUND;
    } else {
        if (out_of_time(flow, look)) {
            give_the_scene_up(flow, look);
        }
        return;
    }
    flow->holds        = false;
    flow->seen_running = look->running;
    flow->grace        = 0u;
    enter(flow, MP_SCENE_PHASE_RUNNING, look->now);
}

/* Whether the engine has played the scene here, or plainly will not: seen running and stopped, or
 * never seen for the grace while the host could have been taken. A grab waits for nothing else,
 * so the grace counts only the substeps in which the host may be moved; a host at a gun, in the
 * water, behind a push block or dead is waited for instead. */
static bool the_engine_is_done(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look,
                               uint32_t passed)
{
    flow->seen_running = flow->seen_running || look->running;
    if (flow->seen_running) {
        return !look->running;
    }
    if (look->may_move != MP_SCENE_MOVE_YES) {
        return false;
    }
    flow->grace += passed;
    return flow->grace >= MP_SCENE_GRAB_GRACE_SUBSTEPS;
}

bool mp_scene_host_step(mp_scene_host_flow_t *flow, const mp_scene_host_look_t *look)
{
    mp_scene_phase_t before;
    uint32_t         passed;

    if (flow == NULL || look == NULL) {
        return false;
    }
    before     = flow->phase;
    passed     = look->now - flow->last;
    flow->last = look->now;
    switch (flow->phase) {
    case MP_SCENE_PHASE_GATHERING:
        if (flow->kind != MP_SCENE_KIND_WARP) {
            step_the_hold(flow, look, passed);
        } else if (look->warp_landed || look->now - flow->began >= MP_SCENE_WARP_CAP_SUBSTEPS) {
            enter(flow, MP_SCENE_PHASE_OVER, look->now);
        }
        break;
    case MP_SCENE_PHASE_RUNNING:
        /* The engine's grab comes a substep after the hold falls, so a scene counts as having
         * run only once it has been seen running. */
        if (the_engine_is_done(flow, look, passed)) {
            enter(flow, MP_SCENE_PHASE_OVER, look->now);
        } else if (!flow->seen_running && out_of_time(flow, look)) {
            give_the_scene_up(flow, look);
        }
        break;
    case MP_SCENE_PHASE_OVER:
        if (flow->given_up && the_engine_is_done(flow, look, passed)) {
            flow->given_up = false;
        }
        if (!flow->given_up && look->now - flow->phase_since >= MP_SCENE_OVER_SUBSTEPS) {
            enter(flow, MP_SCENE_PHASE_NONE, look->now);
        }
        break;
    case MP_SCENE_PHASE_NONE:
    default:
        break;
    }
    return flow->phase != before;
}

bool mp_scene_host_leave(mp_scene_host_flow_t *flow)
{
    bool held;

    if (flow == NULL) {
        return false;
    }
    held           = flow->holds;
    flow->holds    = false;
    flow->phase    = MP_SCENE_PHASE_NONE;
    flow->released = MP_SCENE_RELEASE_NONE;
    flow->given_up = false;
    flow->grace    = 0u;
    return held;
}

/* ==============================================================================================
 * A client's mirror.
 * ============================================================================================ */

/* Whether the note the mirror holds asks for the lock: a lock or a hero scene, gathering or
 * running. The same question the public one asks, for the same note. */
static bool wants_the_lock(const mp_scene_mirror_t *mirror)
{
    return mirror->known && mp_scene_for_all_now(mirror->phase, mirror->what);
}

mp_scene_take_t mp_scene_mirror_take(mp_scene_mirror_t *mirror, const mp_scene_note_t *note,
                                     uint8_t generation_here, uint32_t now_ms)
{
    if (mirror == NULL || note == NULL) {
        return MP_SCENE_TAKE_FOREIGN;
    }
    if (note->generation != generation_here) {
        return MP_SCENE_TAKE_FOREIGN;
    }
    if (mirror->known && mirror->generation == note->generation &&
        mp_scene_serial_after(mirror->serial, note->serial)) {
        return MP_SCENE_TAKE_OLDER;
    }
    if (mirror->known && mirror->generation == note->generation &&
        mirror->serial == note->serial && mirror->phase == note->phase &&
        mirror->what == note->what) {
        memcpy(mirror->anchor, note->anchor, sizeof mirror->anchor);
        return MP_SCENE_TAKE_REPEAT;
    }
    mirror->known      = true;
    mirror->generation = note->generation;
    mirror->serial     = note->serial;
    mirror->phase      = note->phase;
    mirror->what       = note->what;
    mirror->heard_ms   = now_ms;
    memcpy(mirror->anchor, note->anchor, sizeof mirror->anchor);
    return MP_SCENE_TAKE_NEW;
}

/* The one release: what the mirror raised, it lets go of once. */
static uint32_t let_go(mp_scene_mirror_t *mirror, mp_scene_let_go_t reason,
                       mp_scene_let_go_t *why)
{
    if (!mirror->locked) {
        return 0u;
    }
    mirror->locked = false;
    if (why != NULL) {
        *why = reason;
    }
    return MP_SCENE_MIRROR_LET_GO;
}

uint32_t mp_scene_mirror_step(mp_scene_mirror_t *mirror, const mp_scene_mirror_look_t *look,
                              mp_scene_let_go_t *why)
{
    if (why != NULL) {
        *why = MP_SCENE_LET_GO_NONE;
    }
    if (mirror == NULL || look == NULL) {
        return 0u;
    }
    if (look->host_heard) {
        mirror->heard_ms = look->now_ms;
    }
    if (wants_the_lock(mirror) && look->now_ms - mirror->heard_ms >= MP_SCENE_SILENCE_MS) {
        /* A host this long silent is gone or stopped; the note it last sent is forgotten, so the
         * next one it sends is taken as new and holds the player again. */
        mirror->phase = MP_SCENE_PHASE_NONE;
        return let_go(mirror, MP_SCENE_LET_GO_SILENT, why);
    }
    if (!wants_the_lock(mirror)) {
        return let_go(mirror, MP_SCENE_LET_GO_OVER, why);
    }
    if (mirror->locked) {
        return MP_SCENE_MIRROR_RAISE;
    }
    if (!look->may_lock) {
        return 0u;
    }
    mirror->locked        = true;
    mirror->held_since_ms = look->now_ms;
    return MP_SCENE_MIRROR_RAISE | MP_SCENE_MIRROR_BARS;
}

bool mp_scene_note_gathers(const mp_scene_note_t *note, uint16_t gathered_serial, bool seat_given)
{
    return note != NULL && seat_given && (note->what & MP_SCENE_WHAT_WARP) == 0u &&
           mp_scene_for_all_now(note->phase, note->what) && note->serial != gathered_serial;
}

bool mp_scene_seat_wanted(const mp_scene_mirror_t *mirror, uint16_t seat_serial, bool warp)
{
    return mirror != NULL && mirror->known && mirror->serial == seat_serial &&
           (warp || mp_scene_for_all_now(mirror->phase, mirror->what));
}

uint32_t mp_scene_mirror_leave(mp_scene_mirror_t *mirror)
{
    uint32_t act;

    if (mirror == NULL) {
        return 0u;
    }
    act = let_go(mirror, MP_SCENE_LET_GO_EXIT, NULL);
    memset(mirror, 0, sizeof *mirror);
    return act;
}

/* ==============================================================================================
 * One player's seat.
 * ============================================================================================ */

void mp_scene_seat_start(mp_scene_seat_flow_t *flow, uint32_t now, float fade_seconds)
{
    if (flow == NULL) {
        return;
    }
    memset(flow, 0, sizeof *flow);
    flow->stage        = MP_SCENE_SEAT_WAITING;
    flow->since        = now;
    flow->fade_seconds = fade_seconds;
    flow->refused      = MP_SCENE_MOVE_YES;
}

uint32_t mp_scene_seat_fade_deadline(float fade_seconds)
{
    float substeps = isfinite(fade_seconds) && fade_seconds > 0.0f
                         ? ceilf(fade_seconds * SUBSTEPS_A_SECOND)
                         : 0.0f;

    return (uint32_t)substeps + FADE_SLACK_SUBSTEPS;
}

static mp_scene_seat_act_t give_up(mp_scene_seat_flow_t *flow)
{
    bool held = flow->fade_held;

    flow->stage     = MP_SCENE_SEAT_GIVEN_UP;
    flow->fade_held = false;
    return held ? MP_SCENE_SEAT_ACT_FADE_IN : MP_SCENE_SEAT_ACT_NONE;
}

mp_scene_seat_act_t mp_scene_seat_step(mp_scene_seat_flow_t *flow,
                                       const mp_scene_seat_look_t *look)
{
    if (flow == NULL || look == NULL) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    switch (flow->stage) {
    case MP_SCENE_SEAT_WAITING:
        if (!look->live) {
            return give_up(flow);
        }
        if (look->may_move != MP_SCENE_MOVE_YES) {
            flow->refused = look->may_move;
            if (flow->wait_max != 0u && look->now - flow->since >= flow->wait_max) {
                flow->waited_out = true;
                return give_up(flow);
            }
            return MP_SCENE_SEAT_ACT_NONE;
        }
        flow->stage     = MP_SCENE_SEAT_FADING;
        flow->since     = look->now;
        flow->fade_held = true;
        return MP_SCENE_SEAT_ACT_FADE_OUT;
    case MP_SCENE_SEAT_FADING:
        if (!look->live) {
            return give_up(flow);
        }
        if (!look->fade_done &&
            look->now - flow->since < mp_scene_seat_fade_deadline(flow->fade_seconds)) {
            return MP_SCENE_SEAT_ACT_NONE;
        }
        flow->fade_on_clock = !look->fade_done;
        if (look->may_move != MP_SCENE_MOVE_YES) {
            flow->refused = look->may_move;
            return give_up(flow);
        }
        flow->stage = MP_SCENE_SEAT_PLACED;
        flow->since = look->now;
        return MP_SCENE_SEAT_ACT_PLACE;
    case MP_SCENE_SEAT_PLACED:
        /* Handed over: the scene ending now does not take the seat back, the body is on its way. */
        if (look->at_seat) {
            flow->stage     = MP_SCENE_SEAT_DONE;
            flow->at_seat   = true;
            flow->fade_held = false;
            return MP_SCENE_SEAT_ACT_FADE_IN;
        }
        if (look->now - flow->since >= MP_SCENE_PLACE_SUBSTEPS) {
            return give_up(flow);
        }
        return MP_SCENE_SEAT_ACT_NONE;
    case MP_SCENE_SEAT_IDLE:
    case MP_SCENE_SEAT_DONE:
    case MP_SCENE_SEAT_GIVEN_UP:
    default:
        return MP_SCENE_SEAT_ACT_NONE;
    }
}

mp_scene_seat_act_t mp_scene_seat_leave(mp_scene_seat_flow_t *flow)
{
    bool held;

    if (flow == NULL) {
        return MP_SCENE_SEAT_ACT_NONE;
    }
    held            = flow->fade_held;
    flow->fade_held = false;
    flow->stage     = MP_SCENE_SEAT_IDLE;
    return held ? MP_SCENE_SEAT_ACT_FADE_IN : MP_SCENE_SEAT_ACT_NONE;
}

mp_scene_warp_step_t mp_scene_warp_wanted(uint8_t handled, uint8_t warp_serial, bool seat_given,
                                          float distance_to_seat)
{
    if (warp_serial == 0u || warp_serial == handled) {
        return MP_SCENE_WARP_KNOWN;
    }
    if (!seat_given) {
        return MP_SCENE_WARP_NO_SEAT;
    }
    if (isfinite(distance_to_seat) && distance_to_seat < MP_SCENE_WARP_NEAR) {
        return MP_SCENE_WARP_NEAR_ALREADY;
    }
    return MP_SCENE_WARP_MOVE;
}

/* ==============================================================================================
 * The seating.
 * ============================================================================================ */

/* What the seating has handed out so far: the bodies every search keeps clear of, a seat among
 * them, and the sitters in the order they were seated. */
typedef struct seating {
    mp_seat_body_t around[MP_SCENE_BODIES_MAX];
    size_t         taken;
    size_t         order[MP_SCENE_SITTERS];
    size_t         handed;
} seating_t;

/* Sitter `index` has its seat: a seat handed out is a body the next search has to keep clear of,
 * and one the second pass may search beside. */
static void hand_out(seating_t *seating, const mp_scene_sitter_t *sitters, size_t index)
{
    const mp_scene_sitter_t *sitter = &sitters[index];

    if (seating->taken < MP_SCENE_BODIES_MAX) {
        mp_seat_body_t *body = &seating->around[seating->taken++];

        body->known  = true;
        body->stands = true;
        memcpy(body->position, sitter->seat, sizeof body->position);
        body->heading = 0.0f;
    }
    if (seating->handed < MP_SCENE_SITTERS) {
        seating->order[seating->handed++] = index;
    }
}

/* The second pass: every wanted player still without a seat, in turn, beside every seat handed out
 * so far in the order they were handed out, those this pass hands out included. Each of those is a
 * point with a floor, free, and reachable on foot from the place, because the search asked for the
 * walkable line to it; a ring around it reaches one step further along what can be walked. The
 * anchor answer here says only that this seat is no place to search from, never that nobody is
 * gathered: the first pass has settled that. */
static void seat_beside_the_seats(seating_t *seating, mp_scene_sitter_t *sitters,
                                  size_t sitter_count, mp_scene_probe_fn_t probe, void *context)
{
    size_t unseated_at;

    for (unseated_at = 0u; unseated_at < sitter_count; ++unseated_at) {
        mp_scene_sitter_t *sitter = &sitters[unseated_at];
        size_t             beside_at;

        if (!sitter->wanted || sitter->seated) {
            continue;
        }
        for (beside_at = 0u; beside_at < seating->handed && !sitter->seated; ++beside_at) {
            const mp_scene_sitter_t *first = &sitters[seating->order[beside_at]];

            ++sitter->tried_beside;
            if (probe(context, first->seat, sitter->slot, seating->around, seating->taken,
                      sitter->seat) != MP_SCENE_PROBE_FOUND) {
                continue;
            }
            sitter->seated      = true;
            sitter->chained     = true;
            sitter->beside_slot = first->slot;
            hand_out(seating, sitters, unseated_at);
        }
    }
}

mp_scene_seating_t mp_scene_seat_everyone(const float anchor[3], const mp_seat_body_t *bodies,
                                          size_t body_count, mp_scene_sitter_t *sitters,
                                          size_t sitter_count, mp_scene_probe_fn_t probe,
                                          void *context)
{
    seating_t seating;
    size_t    i;

    if (anchor == NULL || sitters == NULL || probe == NULL) {
        return MP_SCENE_SEATING_ANCHOR_REFUSED;
    }
    memset(&seating, 0, sizeof seating);
    for (i = 0u; bodies != NULL && i < body_count && seating.taken < MP_SCENE_BODIES_MAX; ++i) {
        seating.around[seating.taken++] = bodies[i];
    }
    for (i = 0u; i < sitter_count; ++i) {
        sitters[i].seated       = false;
        sitters[i].chained      = false;
        sitters[i].beside_slot  = 0u;
        sitters[i].tried_beside = 0u;
    }
    for (i = 0u; i < sitter_count; ++i) {
        mp_scene_sitter_t *sitter = &sitters[i];
        mp_scene_probe_t   found;

        if (!sitter->wanted) {
            continue;
        }
        found = probe(context, anchor, sitter->slot, seating.around, seating.taken, sitter->seat);
        if (found == MP_SCENE_PROBE_ANCHOR) {
            size_t undone;

            for (undone = 0u; undone < sitter_count; ++undone) {
                sitters[undone].seated = false;
            }
            return MP_SCENE_SEATING_ANCHOR_REFUSED;
        }
        if (found != MP_SCENE_PROBE_FOUND) {
            continue;
        }
        sitter->seated = true;
        hand_out(&seating, sitters, i);
    }
    seat_beside_the_seats(&seating, sitters, sitter_count, probe, context);
    return MP_SCENE_SEATING_DONE;
}

mp_scene_seating_tally_t mp_scene_seating_tally(const mp_scene_sitter_t *sitters,
                                                size_t sitter_count, mp_scene_seating_t seating)
{
    mp_scene_seating_tally_t tally;
    size_t                   i;

    memset(&tally, 0, sizeof tally);
    for (i = 0u; sitters != NULL && i < sitter_count; ++i) {
        if (!sitters[i].wanted) {
            continue;
        }
        ++tally.wanted;
        tally.unseated += sitters[i].seated ? 0u : 1u;
        if (seating == MP_SCENE_SEATING_DONE && (sitters[i].chained || !sitters[i].seated)) {
            ++tally.around_none;
            tally.beside_a_seat += sitters[i].chained ? 1u : 0u;
        }
    }
    return tally;
}
