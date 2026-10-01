/* mp_scene_client.c: a client in the host's scene. See the header.
 *
 * The note is read in the drain, and only kept there; everything it asks for happens in the
 * substep after, from the bridge's post-tick half, where the lock, the fade and the placement are
 * asked everywhere else too. Several notes in one substep leave the newest, which is right for a
 * note that describes a state.
 */
#include "mp_scene_client.h"

#include "mp_bridge_drain.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_scene_bind.h"
#include "mp_scene_flow.h"
#include "mp_scene_note.h"
#include "mp_session_now.h"
#include "mp_signatures_scene.h"
#include "mp_start.h"
#include "mp_wallclock.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <intrin.h>

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Dialog_LeaveInputLock (level), cdecl, answering whether it released anything. */
typedef int32_t(__cdecl *release_fn_t)(int32_t level);

typedef struct client_counts {
    uint32_t held;
    uint32_t longest_ms;
    uint32_t quiet;
    uint32_t still_held;
    uint32_t gathered;
    uint32_t late_gathered;      /* gathered from a note that said running, the gathering's lost */
    uint32_t seated;
    uint32_t given_up;
    uint32_t refused_dead;
    uint32_t refused_mode;
    uint32_t ran_first;
    uint32_t warped;
    uint32_t late;
    uint32_t near_already;
    uint32_t warp_waited_out;    /* a warp's seat given up, its body not movable for its bound */
    uint32_t fades_on_clock;
    uint32_t locks_refused;      /* a script's lock refused while the mirror held */
    uint32_t releases_refused;   /* a script's release refused while the mirror held */
    uint32_t taken;
    uint32_t foreign;
    uint32_t older;
    uint32_t repeated;
    uint32_t torn;
    uint32_t at_host;
    uint32_t lock_substeps;
    uint32_t not_raised;
} client_counts_t;

typedef struct client_state {
    bool         installed;
    bool         release_bound;
    detour_t     release_hull;
    release_fn_t release;
    uintptr_t    script_release[2];

    mp_scene_mirror_t mirror;
    bool              note_pending;
    mp_scene_note_t   note;
    uint32_t          locks_at_raise;

    bool                 seat_active;
    mp_scene_seat_flow_t seat;
    uint16_t             seat_serial;
    bool                 seat_warp;
    uint8_t              seat_hero;
    float                target[3];
    float                heading;
    uint32_t             seat_began_ms;
    uint16_t             note_age_ms;
    uint8_t              warp_handled;

    client_counts_t n;
} client_state_t;

static client_state_t cs;

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* ==============================================================================================
 * The release hull.
 * ============================================================================================ */

/* While the mirror holds the lock, a release one of this machine's own scripts asks for is refused
 * with the engine's own answer for "nothing released"; the mirror's own release is the one that
 * counts. Every other call, and every call while nothing is mirrored, goes through.
 *
 * engine: int Dialog_LeaveInputLock(int level) */
static int32_t __cdecl hook_release(int32_t level)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();

    if (cs.mirror.locked && caller != 0u &&
        (caller == cs.script_release[0] || caller == cs.script_release[1])) {
        ++cs.n.releases_refused;
        return 0;
    }
    return cs.release(level);
}

/* Whether the five bytes in front of `return_address` call `callee`. */
static bool calls(uintptr_t return_address, uintptr_t callee)
{
    uintptr_t target = 0u;

    return return_address > 5u && callee != 0u &&
           patch_read_call_target(return_address - 5u, &target) && target == callee;
}

static bool hull_the_release(const uint8_t *bytes, const uint8_t *mask, size_t size,
                             size_t prologue)
{
    uintptr_t           address = signature_find_detour_target(bytes, mask, size, prologue);
    mp_cutscene_doors_t doors   = { 0u, 0u, 0u };

    if (address == 0u || !detour_install(&cs.release_hull, address, (const void *)&hook_release,
                                         prologue)) {
        return false;
    }
    cs.release = (release_fn_t)cs.release_hull.original;
    mp_cutscene_doors(&doors);
    if (doors.dolly_take_return != 0u &&
        calls(doors.dolly_take_return + SCENE_DOLLY_RELEASE_PAST_THE_TAKE, address)) {
        cs.script_release[0] = doors.dolly_take_return + SCENE_DOLLY_RELEASE_PAST_THE_TAKE;
    }
    if (doors.lock_take_return != 0u &&
        calls(doors.lock_take_return + SCENE_LOCK_RELEASE_PAST_THE_TAKE, address)) {
        cs.script_release[1] = doors.lock_take_return + SCENE_LOCK_RELEASE_PAST_THE_TAKE;
    }
    return true;
}

bool mp_scene_client_install(void)
{
    if (cs.installed) {
        return cs.release_bound;
    }
    cs.installed     = true;
    cs.release_bound = hull_the_release(SIG_SCENE_LOCK_LEAVE, MSK_SCENE_LOCK_LEAVE,
                                        sizeof SIG_SCENE_LOCK_LEAVE, SCENE_LOCK_LEAVE_PROLOGUE);
    if (!cs.release_bound || !mp_scene_bind_ready()) {
        log_warning("the mirror of the host's scene is not bound: %s, so a client is not locked "
                    "in a scene of the host's",
                    !cs.release_bound ? "the lock's release did not resolve or refused its hull"
                                      : "the gathering's own sites did not resolve");
        return false;
    }
    log_info("the host's scene is mirrored here: the lock's release is hulled at %08X, so no "
             "script of this machine lets a player go whom the host's scene holds (its releases "
             "return to %08X and %08X)", (unsigned)cs.release_hull.target,
             (unsigned)cs.script_release[0], (unsigned)cs.script_release[1]);
    return true;
}

static bool ready(void)
{
    return cs.release_bound && mp_scene_bind_ready();
}

/* ==============================================================================================
 * The note.
 * ============================================================================================ */

bool mp_scene_client_take(bool as_client, const uint8_t *note, size_t bytes)
{
    mp_scene_note_t decoded;

    if (!mp_scene_note_is_note(note, bytes)) {
        return false;
    }
    if (!mp_scene_note_decode(note, bytes, &decoded)) {
        ++cs.n.torn;
        return true;
    }
    if (!as_client) {
        ++cs.n.at_host;   /* a scene has one writer, and it is the side this arrived at */
        return true;
    }
    cs.note         = decoded;
    cs.note_pending = true;
    return true;
}

static void start_the_seat(const mp_scene_seat_t *seat, uint32_t substep, bool warp)
{
    memcpy(cs.target, seat->position, sizeof cs.target);
    cs.heading       = seat->heading;
    cs.seat_serial   = cs.note.serial;
    cs.seat_warp     = warp;
    cs.seat_began_ms = mp_wallclock_ms();
    cs.note_age_ms   = cs.note.age_ms;
    cs.seat_active   = true;
    mp_scene_seat_start(&cs.seat, substep,
                        (seat->flags & MP_SCENE_SEAT_F_WARP_FADE) != 0u ? MP_SCENE_WARP_FADE_SECONDS
                                                                        : MP_SCENE_FADE_SECONDS);
    /* A warp holds nobody, so only its own bound ends the wait for a body that cannot be moved;
     * a gathering's seat ends with its scene. */
    cs.seat.wait_max = warp ? MP_SCENE_WARP_WAIT_SUBSTEPS : 0u;
}

/* A note may hand this player a seat: a scene's once, from any of its notes while it runs for
 * everybody, a warp's once by its number. */
static void seat_from_the_note(uint32_t substep)
{
    const mp_scene_seat_t *seat = mp_scene_note_seat_of(&cs.note, mp_bridge_drain_my_slot());
    float                  here[3];
    float                  heading = 0.0f;
    float                  far_off = FLT_MAX;

    if ((cs.note.what & MP_SCENE_WHAT_WARP) != 0u) {
        if (seat != NULL && mp_scene_bind_own_pose(here, &heading)) {
            far_off = distance(here, seat->position);
        }
        switch (mp_scene_warp_wanted(cs.warp_handled, cs.note.warp_serial, seat != NULL,
                                     far_off)) {
        case MP_SCENE_WARP_MOVE:
            cs.n.late += cs.note.phase == (uint8_t)MP_SCENE_PHASE_OVER ? 1u : 0u;
            start_the_seat(seat, substep, true);
            cs.warp_handled = cs.note.warp_serial;
            break;
        case MP_SCENE_WARP_NEAR_ALREADY:
            ++cs.n.near_already;
            cs.warp_handled = cs.note.warp_serial;
            break;
        case MP_SCENE_WARP_NO_SEAT:
            cs.warp_handled = cs.note.warp_serial;
            break;
        case MP_SCENE_WARP_KNOWN:
        default:
            break;
        }
        return;
    }
    if (mp_scene_note_gathers(&cs.note, cs.seat_serial, seat != NULL)) {
        ++cs.n.gathered;
        cs.n.late_gathered += cs.note.phase != (uint8_t)MP_SCENE_PHASE_GATHERING ? 1u : 0u;
        start_the_seat(seat, substep, false);
    }
}

static void take_the_note(uint32_t now_ms, uint32_t substep)
{
    uint8_t generation = 0u;

    (void)mp_session_now_client_of_a_started_session(NULL, &generation);
    switch (mp_scene_mirror_take(&cs.mirror, &cs.note, generation, now_ms)) {
    case MP_SCENE_TAKE_NEW:
        ++cs.n.taken;
        seat_from_the_note(substep);
        break;
    case MP_SCENE_TAKE_REPEAT:
        ++cs.n.repeated;
        seat_from_the_note(substep);   /* every note of a scene stands on its own */
        break;
    case MP_SCENE_TAKE_OLDER:
        ++cs.n.older;
        break;
    case MP_SCENE_TAKE_FOREIGN:
    default:
        ++cs.n.foreign;
        break;
    }
}

/* ==============================================================================================
 * The mirror and the seat, once a substep.
 * ============================================================================================ */

static void let_go(mp_scene_let_go_t why, uint32_t now_ms)
{
    mp_cutscene_counts_t cut;
    uint32_t             held_ms = now_ms - cs.mirror.held_since_ms;

    (void)cs.release(MP_SCENE_LOCK_LEVEL);
    mp_cutscene_engine_bars(false);
    memset(&cut, 0, sizeof cut);
    mp_cutscene_counts(&cut);
    cs.n.locks_refused += cut.locks_refused_for_the_host - cs.locks_at_raise;
    if (held_ms > cs.n.longest_ms) {
        cs.n.longest_ms = held_ms;
    }
    if (why == MP_SCENE_LET_GO_SILENT) {
        ++cs.n.quiet;
        log_warning("the host has not been heard of for %u ms during its scene %u, so this player "
                    "is let go rather than left standing", (unsigned)MP_SCENE_SILENCE_MS,
                    (unsigned)cs.mirror.serial);
    } else if (why == MP_SCENE_LET_GO_OVER) {
        log_info("a scene of the host has ended: scene %u after %u ms, and this player is free "
                 "again", (unsigned)cs.mirror.serial, (unsigned)held_ms);
    }
}

static void step_the_mirror(uint32_t now_ms)
{
    mp_scene_mirror_look_t look;
    mp_scene_let_go_t      why = MP_SCENE_LET_GO_NONE;
    uint32_t               act;
    mp_cutscene_counts_t   cut;

    look.now_ms     = now_ms;
    look.host_heard = mp_bridge_drain_host_moving();
    look.may_lock   = mp_scene_bind_may_move() == MP_SCENE_MOVE_YES;
    act             = mp_scene_mirror_step(&cs.mirror, &look, &why);
    if ((act & MP_SCENE_MIRROR_BARS) != 0u) {
        memset(&cut, 0, sizeof cut);
        mp_cutscene_counts(&cut);
        cs.locks_at_raise = cut.locks_refused_for_the_host;
        mp_cutscene_engine_bars(true);
        ++cs.n.held;
        log_info("a scene of the host holds this player: scene %u, the lock is 5, the bars are "
                 "on, the camera of this side stays its own", (unsigned)cs.mirror.serial);
    }
    if ((act & MP_SCENE_MIRROR_RAISE) != 0u) {
        (void)mp_cutscene_engine_lock(MP_SCENE_LOCK_LEVEL);
        ++cs.n.lock_substeps;
    } else if (mp_scene_client_for_all(NULL) && !cs.mirror.locked) {
        ++cs.n.not_raised;
    }
    if ((act & MP_SCENE_MIRROR_LET_GO) != 0u) {
        let_go(why, now_ms);
    }
}

static void count_the_end(mp_scene_seat_stage_t before)
{
    if (cs.seat.stage == MP_SCENE_SEAT_DONE) {
        ++cs.n.seated;
        cs.n.warped += cs.seat_warp ? 1u : 0u;
    } else if (before == MP_SCENE_SEAT_PLACED) {
        ++cs.n.given_up;
    } else if (cs.seat.refused == MP_SCENE_MOVE_DEAD) {
        ++cs.n.refused_dead;
    } else if (cs.seat.refused == MP_SCENE_MOVE_MODE || cs.seat.refused == MP_SCENE_MOVE_NO_BODY) {
        ++cs.n.refused_mode;
    } else {
        ++cs.n.ran_first;
    }
}

static void say_the_seat(void)
{
    uint32_t hero = 0u;
    uint32_t took = mp_wallclock_ms() - cs.seat_began_ms;

    if (cs.seat_warp) {
        (void)memory_read_u32(mp_cells_address(MP_CELL_HERO_BLOCK) + MP_HERO_BLOCK_HERO_INDEX,
                              &hero);
        log_info("this player was sent on by a warp of the host: warp %u, seated at %.2f %.2f "
                 "%.2f, hero %u kept, under a fade of %.2f s", (unsigned)cs.warp_handled,
                 (double)cs.target[0], (double)cs.target[1], (double)cs.target[2], (unsigned)hero,
                 (double)cs.seat.fade_seconds);
        return;
    }
    log_info("this player was gathered for a scene: scene %u, seated at %.2f %.2f %.2f facing "
             "%.2f, the note arrived %u ms into the scene, seated after %u ms",
             (unsigned)cs.seat_serial,
             (double)cs.target[0], (double)cs.target[1], (double)cs.target[2],
             (double)cs.heading, (unsigned)cs.note_age_ms, (unsigned)took);
}

static void step_the_seat(uint32_t substep)
{
    mp_scene_seat_look_t  look;
    mp_scene_seat_stage_t before;
    mp_scene_seat_act_t   act;
    float                 here[3];
    float                 heading = 0.0f;

    if (!cs.seat_active) {
        return;
    }
    look.now       = substep;
    look.live      = mp_scene_seat_wanted(&cs.mirror, cs.seat_serial, cs.seat_warp);
    look.may_move  = mp_scene_bind_may_move();
    look.fade_done = mp_scene_bind_fade_done();
    look.at_seat   = mp_scene_bind_own_pose(here, &heading) &&
                   distance(here, cs.target) < MP_SCENE_ARRIVED_DISTANCE;
    before = cs.seat.stage;
    act    = mp_scene_seat_step(&cs.seat, &look);
    switch (act) {
    case MP_SCENE_SEAT_ACT_FADE_OUT:
        mp_scene_bind_fade_out(cs.seat.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_PLACE:
        cs.n.fades_on_clock += cs.seat.fade_on_clock ? 1u : 0u;
        (void)mp_start_place_for_scene(cs.target, cs.heading);
        break;
    case MP_SCENE_SEAT_ACT_FADE_IN:
        mp_scene_bind_fade_in(cs.seat.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_NONE:
    default:
        break;
    }
    if (cs.seat.stage != before &&
        (cs.seat.stage == MP_SCENE_SEAT_DONE || cs.seat.stage == MP_SCENE_SEAT_GIVEN_UP)) {
        count_the_end(before);
        if (cs.seat.stage == MP_SCENE_SEAT_DONE) {
            say_the_seat();
        } else if (cs.seat.waited_out) {
            ++cs.n.warp_waited_out;
            log_info("a warp of the host was given up here: warp %u, after %u substep(s) in which "
                     "this player %s, so it stays where it is", (unsigned)cs.warp_handled,
                     (unsigned)cs.seat.wait_max, mp_scene_given_up_text(cs.seat.refused));
        }
        cs.seat_active = false;
    }
}

void mp_scene_client_tick(uint32_t substep)
{
    uint32_t now_ms;

    if (!cs.installed || !ready() || !mp_bridge_drain_is_client()) {
        return;
    }
    now_ms = mp_wallclock_ms();
    if (cs.note_pending) {
        cs.note_pending = false;
        take_the_note(now_ms, substep);
    }
    step_the_mirror(now_ms);
    step_the_seat(substep);
}

/* Whether the scene the mirror holds gathered this player: the note of that scene names this
 * player as the one its script meant, or this player's seat for it was reached. */
static bool gathered_here(void)
{
    if (!cs.mirror.known) {
        return false;
    }
    if (cs.note.serial == cs.mirror.serial && cs.note.trigger_slot == mp_bridge_drain_my_slot()) {
        return true;
    }
    return !cs.seat_warp && cs.seat_serial == cs.mirror.serial &&
           cs.seat.stage == MP_SCENE_SEAT_DONE;
}

/* The note carries no flag for a place the host could not read; it then carries nought, which is
 * taken as the place like any other. */
bool mp_scene_client_for_all(mp_scene_known_t *known)
{
    if (known != NULL) {
        memset(known, 0, sizeof *known);
        known->anchor_known = cs.mirror.known;
        memcpy(known->anchor, cs.mirror.anchor, sizeof known->anchor);
        known->gathered = gathered_here();
        known->serial   = cs.mirror.serial;
    }
    return cs.mirror.known && mp_scene_for_all_now(cs.mirror.phase, cs.mirror.what);
}

void mp_scene_client_leave(void)
{
    if (!cs.installed) {
        return;
    }
    if ((mp_scene_mirror_leave(&cs.mirror) & MP_SCENE_MIRROR_LET_GO) != 0u) {
        let_go(MP_SCENE_LET_GO_EXIT, mp_wallclock_ms());
    }
    if (cs.seat_active && mp_scene_seat_leave(&cs.seat) == MP_SCENE_SEAT_ACT_FADE_IN) {
        mp_scene_bind_fade_in(cs.seat.fade_seconds);
    }
    cs.seat_active  = false;
    cs.note_pending = false;
    cs.seat_serial  = 0u;
    cs.warp_handled = 0u;
}

void mp_scene_client_report(void)
{
    if (!cs.installed || (!mp_bridge_drain_is_client() && cs.n.taken == 0u)) {
        return;
    }
    cs.n.still_held = cs.mirror.locked ? 1u : 0u;
    log_info("  the scenes (a client): %u of the host's scenes held this player, the longest %u "
             "ms, %u let go because the host went quiet, %u still held at the end; %u gathered (%u "
             "of them from a note that said running, %u seated, %u given up), %u warped (%u "
             "caught up late, %u already near the target, %u given up when the wait for a movable "
             "body ran out); %u lock(s) and %u release(s) asked by a script of this machine and "
             "refused while held",
             (unsigned)cs.n.held, (unsigned)cs.n.longest_ms, (unsigned)cs.n.quiet,
             (unsigned)cs.n.still_held, (unsigned)cs.n.gathered, (unsigned)cs.n.late_gathered,
             (unsigned)cs.n.seated, (unsigned)cs.n.given_up, (unsigned)cs.n.warped,
             (unsigned)cs.n.late,
             (unsigned)cs.n.near_already, (unsigned)cs.n.warp_waited_out,
             (unsigned)cs.n.locks_refused, (unsigned)cs.n.releases_refused);
    log_info("  the scene notes (a client): %u taken, %u of another world, %u older than the one "
             "held, %u repeated, %u torn; the lock held on %u substep(s), not raised on %u because "
             "the player could not be moved yet", (unsigned)cs.n.taken, (unsigned)cs.n.foreign,
             (unsigned)cs.n.older, (unsigned)cs.n.repeated, (unsigned)cs.n.torn,
             (unsigned)cs.n.lock_substeps, (unsigned)cs.n.not_raised);
    log_info("  the moves (a client): %u seated, %u refused because the body was dead and %u for "
             "its mode (anything the engine would not park for a scene: a jump, a fall, the "
             "water, a ledge, a push block or a gun), %u given up at the deadline, %u not moved "
             "because the scene was over first; %u fade(s) ended on their own clock rather than "
             "the engine's", (unsigned)cs.n.seated, (unsigned)cs.n.refused_dead,
             (unsigned)cs.n.refused_mode, (unsigned)cs.n.given_up, (unsigned)cs.n.ran_first,
             (unsigned)cs.n.fades_on_clock);
}
