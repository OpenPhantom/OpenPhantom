/* mp_scene_host.c: the host's half of a scene for everybody. See the header.
 *
 * Two things about the code are worth having in front of a maintainer, because neither is visible
 * in a type:
 *
 * The begin is split in two on purpose. A door is heard inside the engine, in the middle of an
 * actor's script, and what has to happen there is only what cannot wait a substep: the scene's
 * state and its two holds, because the hero's grab can be asked in the same substep. The seats are
 * searched from the bridge's post-tick half of that substep, outside the engine, where the seat
 * search's probes are asked everywhere else too.
 *
 * Every way out goes through leave(). A hold left standing is a script that never runs again and a
 * hero that is never grabbed; a fade left held is a black screen. Both are what the one exit is
 * for, and the gathering has no other way to end.
 *
 * SIZE NOTE: over 600 lines. One state is touched by the door inside the engine, by the substep and
 * by the exit, and the seating, the host's own move, the warp's landing and the hold all read it; a
 * file each would put that state behind four interfaces. The seams taken are the decisions
 * (mp_scene_flow), the note's bytes and its cadence (mp_scene_note, mp_scene_send), the engine
 * (mp_scene_bind) and the host's report (mp_scene_host_report), which only reads.
 */
#include "mp_scene_host.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_enemy_bind.h"
#include "mp_quest.h"
#include "mp_respawn.h"
#include "mp_scene_bind.h"
#include "mp_scene_client.h"
#include "mp_scene_exit.h"
#include "mp_scene_host_report.h"
#include "mp_scene_note.h"
#include "mp_scratch.h"
#include "mp_seat.h"
#include "mp_session_now.h"
#include "mp_start.h"
#include "mp_target.h"
#include "mp_voice.h"

#include "common/logging.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(MP_BANK_FAR_MAX <= MP_SCENE_NOTE_MAX_SEATS,
               "a note carries a seat for every far player a session holds");
_Static_assert(MP_BANK_FAR_MAX + 1u <= MP_SCENE_SITTERS, "a sitter for the host and every bank");
_Static_assert(MP_SCENE_BODIES_MAX >= 1u + MP_BANK_FAR_MAX + 1u + MP_BANK_FAR_MAX,
               "a warp keeps clear of the host, every far body, its target and a seat per bank");

/* A scene's own lines are written this many times in a process, and after that only counted. */
#define EVENT_LINES_MAX 64u

/* Milliseconds a substep, for the note's age and the report. */
#define MS_A_SUBSTEP 31.25f

typedef struct far_seat {
    bool    wanted;     /* handed a seat for this scene */
    uint8_t slot;
    float   seat[3];
    bool    arrived;
} far_seat_t;

typedef struct host_state {
    bool                 installed;
    uint32_t             substep;
    mp_scene_host_flow_t flow;

    /* The scene as the door heard it, acted on in the substep. */
    bool      begin_pending;
    uint8_t   bank;
    uintptr_t actor;
    uint32_t  actor_key;
    bool      actor_keyed;
    float     warp_at[3];
    float     warp_heading;
    int32_t   warp_hero;
    bool      warp_left_running;
    bool      quest_known;
    uint8_t   quest_before[MP_SCENE_QUEST_WINDOW_BYTES];

    /* The place, and who sits where. */
    bool                 anchor_known;
    bool                 nobody_gathered;   /* no place to read, or none to sit on */
    uint32_t             unseated;          /* far players to be seated that no seat answered */
    float                anchor[3];
    float                heading;
    uint8_t              trigger_slot;
    bool                 own_wanted;
    float                own_seat[3];
    mp_scene_seat_flow_t own;
    uint32_t             own_fade_substeps;
    far_seat_t           far[MP_BANK_FAR_MAX + 1u];
    bool                 named_set;

    mp_scene_sender_t      sender;
    mp_seat_counts_t       seat_counts;
    mp_scene_host_counts_t n;
} host_state_t;

static host_state_t hs;

/* ==============================================================================================
 * Small readings.
 * ============================================================================================ */

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static bool line_allowed(void)
{
    if (hs.n.lines >= EVENT_LINES_MAX) {
        return false;
    }
    ++hs.n.lines;
    return true;
}

static uint8_t slot_of_bank(uint8_t bank)
{
    uint8_t slot = MP_SCENE_TRIGGER_UNKNOWN;

    return mp_body_bank_slot(bank, &slot) ? slot : (uint8_t)MP_SCENE_TRIGGER_UNKNOWN;
}

/* Where the player the script meant stands, and which way he faces. */
static bool pose_of_bank(uint8_t bank, float position[3], float *heading)
{
    mp_bridge_far_pose_t pose;

    if (bank == 0u) {
        return mp_scene_bind_own_pose(position, heading);
    }
    if (!mp_bridge_far_pose(bank, MP_FAR_READER_SCENE, &pose)) {
        return false;
    }
    memcpy(position, pose.position, 3u * sizeof(float));
    *heading = pose.heading;
    return true;
}

/* ==============================================================================================
 * The one exit.
 * ============================================================================================ */

static void forget_the_seats(void)
{
    memset(hs.far, 0, sizeof hs.far);
    hs.own_wanted      = false;
    hs.anchor_known    = false;
    hs.nobody_gathered = false;
    hs.unseated        = 0u;
    if (hs.named_set) {
        mp_seat_name_anchor(NULL, 0.0f);
        hs.named_set = false;
    }
}

static void do_seat(mp_scene_seat_act_t act)
{
    switch (act) {
    case MP_SCENE_SEAT_ACT_FADE_OUT:
        mp_scene_bind_fade_out(hs.own.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_PLACE:
        (void)mp_start_place_for_scene(hs.own_seat, hs.heading);
        break;
    case MP_SCENE_SEAT_ACT_FADE_IN:
        mp_scene_bind_fade_in(hs.own.fade_seconds);
        break;
    case MP_SCENE_SEAT_ACT_NONE:
    default:
        break;
    }
}

static void leave(void)
{
    (void)mp_scene_host_leave(&hs.flow);
    mp_cutscene_set_gather_holds(false);
    do_seat(mp_scene_seat_leave(&hs.own));
    hs.begin_pending = false;
    hs.actor         = 0u;
    hs.actor_keyed   = false;
    forget_the_seats();
}

void mp_scene_reset(void)
{
    leave();
    mp_scene_client_leave();
    mp_voice_world_ended();
    mp_seat_world_ended();
}

/* ==============================================================================================
 * The door.
 * ============================================================================================ */

/* A warp the engine takes: the waiting re-entry ends, because the warp's own spawn brings the host
 * back at its target, and a re-entry left waiting would put him back beside a far player the moment
 * he lands. And the quest window as it stands, for the count the landing makes. */
static void a_warp_is_taken(const float *at, float heading, int32_t hero)
{
    ++hs.n.warps;
    memcpy(hs.warp_at, at, sizeof hs.warp_at);
    hs.warp_heading      = heading;
    hs.warp_hero         = hero;
    hs.warp_left_running = false;
    hs.quest_known       = mp_scene_bind_quest_window(hs.quest_before);
    if (mp_respawn_pending()) {
        mp_respawn_cancel();
        ++hs.n.reentries_ended;
        log_info("a warp ended a re-entry that waited for this host: the spawn of the warp brings "
                 "the host back at %.2f %.2f %.2f", (double)at[0], (double)at[1], (double)at[2]);
    }
}

void mp_scene_host_began(mp_scene_kind_t kind, uintptr_t actor, uint8_t bank, int32_t hero,
                         const float *at, float heading)
{
    uint32_t         module = 0u;
    mp_scene_begin_t begun;

    if (!hs.installed || !mp_target_hosting() || !mp_scene_bind_ready() || kind >= MP_SCENE_KINDS) {
        return;
    }
    if (!mp_bridge_joined()) {
        ++hs.n.alone_doors;   /* nobody else to gather: the engine plays it as it would alone */
        return;
    }
    if (kind == MP_SCENE_KIND_WARP) {
        if (at == NULL || !isfinite(at[0]) || !isfinite(at[1]) || !isfinite(at[2])) {
            return;
        }
        /* The engine drops a respawn unless the module runs, and then only the far players
         * would follow it. */
        if (!mp_scene_bind_module_state(&module) || module != MP_HERO_MODULE_RUNNING) {
            ++hs.n.warps_dropped;
            if (line_allowed()) {
                log_warning("a script asked this host to warp while its player module was not "
                            "running, so the engine drops the warp here while the far players "
                            "would follow it; nobody is sent");
            }
            return;
        }
    }
    begun = mp_scene_host_begin(&hs.flow, kind, hs.substep);
    if (begun == MP_SCENE_BEGIN_SECOND) {
        ++hs.n.second;
        return;
    }
    if (begun == MP_SCENE_BEGIN_WARP_OVER) {
        do_seat(mp_scene_seat_leave(&hs.own));   /* the engine's fade replaces ours */
        forget_the_seats();
    }
    /* At once, inside the engine: the hosted actor may be ticked in this very substep. */
    mp_cutscene_set_gather_holds(hs.flow.holds);
    hs.begin_pending = true;
    hs.bank          = bank;
    hs.actor         = actor;
    hs.actor_keyed   = mp_enemy_bind_index(actor, &hs.actor_key);
    if (kind == MP_SCENE_KIND_WARP) {
        a_warp_is_taken(at, heading, hero);
    }
}

/* The actor whose script began the scene. An address the pool gave another actor since is not. */
static bool is_the_actor(uintptr_t actor)
{
    uint32_t key = 0u;

    return actor != 0u && actor == hs.actor &&
           (!hs.actor_keyed || (mp_enemy_bind_index(actor, &key) && key == hs.actor_key));
}

bool mp_scene_host_holds(uintptr_t actor)
{
    if (!hs.flow.holds || !is_the_actor(actor)) {
        return false;
    }
    ++hs.n.actor_held[hs.flow.kind];
    return true;
}

void mp_scene_host_note_respawn(mp_scene_respawn_caller_t caller)
{
    if (caller < MP_SCENE_RESPAWN_CALLERS) {
        ++hs.n.respawns[caller];
    }
}

/* ==============================================================================================
 * The seating, in the substep after the door.
 * ============================================================================================ */

/* The one seat search, as the seating asks it: beside the anchor, from the seated player's slot,
 * keeping clear of every body and every seat handed out already. Its counters are the scene's
 * own, apart from the re-entry's and the arrival's. */
static mp_scene_probe_t probe(void *context, const float anchor[3], uint8_t slot,
                              const mp_seat_body_t *bodies, size_t body_count, float seat[3])
{
    (void)context;
    switch (mp_seat_probe(anchor, true, slot, bodies, body_count, &hs.seat_counts, seat)) {
    case MP_SEAT_FOUND:
        return MP_SCENE_PROBE_FOUND;
    case MP_SEAT_ANCHOR_MOVING:
        return MP_SCENE_PROBE_ANCHOR;
    case MP_SEAT_NONE_FREE:
    case MP_SEAT_NO_LEVEL:
    case MP_SEAT_NO_PROBES:
    default:
        return MP_SCENE_PROBE_NONE;
    }
}

/* Who is to be seated and what they must keep clear of. The host is, beside the player the script
 * meant, unless it is that player or the engine moves it for a warp; so is every far player that
 * stands, but the one the script meant. A far player whose pose is another world's is on its way
 * into this one, as every client that follows its host is at the host's first substep of a new
 * level: seated like one who stands, and no body to keep clear of, because its place is not in
 * this world. */
static void who_sits(mp_scene_sitter_t sitters[MP_SCENE_SITTERS], mp_seat_body_t *bodies,
                     size_t *body_count, uint32_t *dead)
{
    bool   warp = hs.flow.kind == MP_SCENE_KIND_WARP;
    float  heading = 0.0f;
    size_t bank;

    memset(sitters, 0, MP_SCENE_SITTERS * sizeof sitters[0]);
    *body_count = 0u;
    *dead       = 0u;
    if (mp_scene_bind_own_pose(bodies[0].position, &heading)) {
        bodies[0].known  = true;
        bodies[0].stands = true;
        *body_count      = 1u;
    }
    sitters[0].slot   = slot_of_bank(0u);
    sitters[0].wanted = !warp && hs.bank != 0u;
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_bridge_far_pose_t pose;

        if (!mp_bridge_far_pose(bank, MP_FAR_READER_SCENE, &pose)) {
            sitters[bank].wanted =
                (warp || bank != hs.bank) &&
                mp_bridge_far_in_another_world(bank, MP_FAR_READER_SCENE, &sitters[bank].slot);
            continue;
        }
        bodies[*body_count].known  = true;
        bodies[*body_count].stands = mp_bridge_far_pose_stands(&pose);
        memcpy(bodies[*body_count].position, pose.position, sizeof pose.position);
        ++*body_count;
        sitters[bank].slot = pose.slot;
        if (!warp && bank == hs.bank) {
            continue;
        }
        if (!mp_bridge_far_pose_stands(&pose)) {
            ++*dead;
            continue;
        }
        sitters[bank].wanted = true;
    }
    if (warp) {   /* the host lands on the target itself */
        bodies[*body_count].known  = true;
        bodies[*body_count].stands = true;
        memcpy(bodies[*body_count].position, hs.anchor, sizeof hs.anchor);
        ++*body_count;
    }
}

static void say_the_gathering(uint32_t seated, uint32_t dead, uint32_t unseated)
{
    char   host[96];
    char   slot[16];

    if (!line_allowed()) {
        return;
    }
    if (hs.flow.kind == MP_SCENE_KIND_WARP) {
        log_info("a script warped this host to %.2f %.2f %.2f as hero %d; %u far player(s) were "
                 "handed a seat around it, %u left where they stand because they were dead and %u "
                 "because no seat answered", (double)hs.anchor[0], (double)hs.anchor[1],
                 (double)hs.anchor[2], (int)hs.warp_hero, (unsigned)seated, (unsigned)dead,
                 (unsigned)unseated);
        return;
    }
    if (hs.bank == 0u) {
        (void)text_format(host, sizeof host, "stays where it is, being the one the script meant");
    } else if (hs.own_wanted) {
        (void)text_format(host, sizeof host, "is seated at %.2f %.2f %.2f facing %.2f",
                          (double)hs.own_seat[0], (double)hs.own_seat[1], (double)hs.own_seat[2],
                          (double)hs.heading);
    } else {
        (void)text_format(host, sizeof host, "has no seat and stays where it is");
    }
    if (hs.trigger_slot == MP_SCENE_TRIGGER_UNKNOWN) {
        (void)text_format(slot, sizeof slot, "unknown");
    } else {
        (void)text_format(slot, sizeof slot, "%u", (unsigned)hs.trigger_slot);
    }
    log_info("a scene is gathered: scene %u around %.2f %.2f %.2f, where world slot %s stands "
             "(%s), the host %s; %u far player(s) were handed a seat, %u left where they stand "
             "because they were dead and %u because no seat answered", (unsigned)hs.flow.serial,
             (double)hs.anchor[0], (double)hs.anchor[1], (double)hs.anchor[2], slot,
             hs.flow.kind == MP_SCENE_KIND_LOCK ? "a lock" : "the hero as an actor", host,
             (unsigned)seated, (unsigned)dead, (unsigned)unseated);
}

/* A player the seating found no seat for around the place and seated beside one handed out. */
static void say_the_chain(const mp_scene_sitter_t *sitter)
{
    if (!sitter->chained || !line_allowed()) {
        return;
    }
    log_info("a seat for a scene was found beside a seat handed out first: scene %u, world slot %u "
             "at %.2f %.2f %.2f, %.2f u from the place it gathers around, beside the seat of world "
             "slot %u", (unsigned)hs.flow.serial, (unsigned)sitter->slot, (double)sitter->seat[0],
             (double)sitter->seat[1], (double)sitter->seat[2],
             (double)distance(sitter->seat, hs.anchor), (unsigned)sitter->beside_slot);
}

static void start_the_gathering(void)
{
    mp_scene_sitter_t        sitters[MP_SCENE_SITTERS];
    mp_seat_body_t           bodies[MP_SCENE_BODIES_MAX];
    size_t                   body_count = 0u;
    uint32_t                 dead       = 0u;
    uint32_t                 seated     = 0u;
    uint32_t                 unseated   = 0u;
    size_t                   bank;
    mp_scene_seating_t       seating;
    mp_scene_seating_tally_t tally;

    forget_the_seats();
    ++hs.n.gathered[hs.flow.kind];
    hs.trigger_slot = slot_of_bank(hs.bank);
    if (hs.flow.kind == MP_SCENE_KIND_WARP) {
        memcpy(hs.anchor, hs.warp_at, sizeof hs.anchor);
        hs.heading      = hs.warp_heading;
        hs.anchor_known = true;
    } else {
        hs.anchor_known = pose_of_bank(hs.bank, hs.anchor, &hs.heading);
    }
    if (!hs.anchor_known) {
        ++hs.n.not_gathered;
        hs.nobody_gathered = true;   /* the hold falls on the next look with the host standing */
        return;
    }
    if (!mp_scene_bind_player_stands() && hs.flow.holds && line_allowed()) {
        log_warning("the host is dead as scene %u begins, so its hold waits for the host to stand "
                    "again, and the host's re-entry takes the gathering seat",
                    (unsigned)hs.flow.serial);
    }
    memset(bodies, 0, sizeof bodies);
    who_sits(sitters, bodies, &body_count, &dead);
    seating = mp_scene_seat_everyone(hs.anchor, bodies, body_count, sitters, MP_SCENE_SITTERS,
                                     &probe, NULL);
    /* In the scene's seat line a search is a player to be seated, and one that found nothing a
     * player left without a seat; the chain's own count is beside it. */
    tally = mp_scene_seating_tally(sitters, MP_SCENE_SITTERS, seating);
    hs.seat_counts.searches      += tally.wanted;
    hs.seat_counts.found_nothing += tally.unseated;
    hs.n.around_none             += tally.around_none;
    hs.n.beside_a_seat           += tally.beside_a_seat;
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        unseated += sitters[bank].wanted && !sitters[bank].seated ? 1u : 0u;
    }
    hs.unseated = unseated;
    if (seating == MP_SCENE_SEATING_ANCHOR_REFUSED) {
        ++hs.n.not_gathered;
        hs.nobody_gathered = true;
        if (line_allowed()) {
            log_warning("a scene gathers nobody: scene %u, the place it gathers around, %.2f %.2f "
                        "%.2f, stands on a mover, over water or a drop, so every player is held "
                        "where it stands", (unsigned)hs.flow.serial, (double)hs.anchor[0],
                        (double)hs.anchor[1], (double)hs.anchor[2]);
        }
        return;
    }
    if (sitters[0].wanted && sitters[0].seated) {
        hs.own_wanted = true;
        memcpy(hs.own_seat, sitters[0].seat, sizeof hs.own_seat);
        mp_scene_seat_start(&hs.own, hs.substep, MP_SCENE_FADE_SECONDS);
        hs.own_fade_substeps = 0u;
        say_the_chain(&sitters[0]);
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        if (!sitters[bank].wanted) {
            continue;
        }
        if (!sitters[bank].seated) {
            if (line_allowed()) {
                log_warning("a seat for a scene did not answer: scene %u, around %.2f %.2f %.2f "
                            "nor beside the %u seat(s) handed out, for world slot %u, so that "
                            "player is held where it stands", (unsigned)hs.flow.serial,
                            (double)hs.anchor[0], (double)hs.anchor[1], (double)hs.anchor[2],
                            (unsigned)sitters[bank].tried_beside, (unsigned)sitters[bank].slot);
            }
            continue;
        }
        say_the_chain(&sitters[bank]);
        ++seated;
        hs.far[bank].wanted = true;
        hs.far[bank].slot   = sitters[bank].slot;
        memcpy(hs.far[bank].seat, sitters[bank].seat, sizeof hs.far[bank].seat);
    }
    say_the_gathering(seated, dead, unseated);
}

/* ==============================================================================================
 * A substep of the host's scene.
 * ============================================================================================ */

/* How a seat ended, once, as it ends: seated, refused by the body, the placement never taken, or
 * the scene running before the body could be moved. */
static void count_the_end(mp_scene_seat_stage_t before, mp_scene_seat_stage_t after,
                          mp_scene_move_t refused)
{
    if (after == MP_SCENE_SEAT_DONE) {
        ++hs.n.seated;
    } else if (after != MP_SCENE_SEAT_GIVEN_UP) {
        return;
    } else if (before == MP_SCENE_SEAT_PLACED) {
        ++hs.n.given_up;
    } else if (refused == MP_SCENE_MOVE_DEAD) {
        ++hs.n.refused_dead;
    } else if (refused == MP_SCENE_MOVE_MODE || refused == MP_SCENE_MOVE_NO_BODY) {
        ++hs.n.refused_mode;
    } else {
        ++hs.n.ran_first;
    }
}

static void step_own_seat(void)
{
    mp_scene_seat_look_t  look;
    float                 body[3];
    mp_scene_seat_act_t   act;
    mp_scene_seat_stage_t before;

    if (!hs.own_wanted) {
        return;
    }
    look.now       = hs.substep;
    look.live      = hs.flow.phase == MP_SCENE_PHASE_GATHERING;
    look.may_move  = mp_scene_bind_may_move();
    look.fade_done = mp_scene_bind_fade_done();
    look.at_seat   = mp_target_player_position(0u, body) &&
                   distance(body, hs.own_seat) < MP_SCENE_ARRIVED_DISTANCE;
    before = hs.own.stage;
    act    = mp_scene_seat_step(&hs.own, &look);
    if (act == MP_SCENE_SEAT_ACT_PLACE) {
        hs.own_fade_substeps = hs.substep - hs.own.since;
        hs.n.fades_on_clock += hs.own.fade_on_clock ? 1u : 0u;
    }
    if (hs.own.stage != before) {
        count_the_end(before, hs.own.stage, hs.own.refused);
    }
    do_seat(act);
}

/* Whether the host and every far player handed a seat stand on it: the far players by their
 * bodies here, which is where the engine's own grab and every script measure them. A seat the host
 * gave up on is not waited for. */
static bool everyone_seated(uint32_t *arrived, uint32_t *wanted)
{
    bool   all = true;
    size_t bank;

    *arrived = 0u;
    *wanted  = 0u;
    if (hs.own_wanted && (hs.own.stage == MP_SCENE_SEAT_WAITING ||
                          hs.own.stage == MP_SCENE_SEAT_FADING ||
                          hs.own.stage == MP_SCENE_SEAT_PLACED)) {
        all = false;
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        float body[3];

        if (!hs.far[bank].wanted) {
            continue;
        }
        ++*wanted;
        if (!hs.far[bank].arrived && mp_target_player_position((uint8_t)bank, body) &&
            distance(body, hs.far[bank].seat) < MP_SCENE_ARRIVED_DISTANCE) {
            hs.far[bank].arrived = true;
        }
        if (hs.far[bank].arrived) {
            ++*arrived;
        } else {
            all = false;
        }
    }
    return all;
}

static void say_the_release(uint32_t arrived, uint32_t wanted)
{
    uint32_t held = hs.flow.hold_standing + hs.flow.hold_dead;
    char     host[80];

    if (held > hs.n.longest_hold) {
        hs.n.longest_hold = held;
    }
    if (hs.flow.released == MP_SCENE_RELEASE_SEATED) {
        ++hs.n.all_seated;
    } else if (hs.flow.released == MP_SCENE_RELEASE_SEATED_SOME) {
        ++hs.n.with_unseated;
    } else if (hs.flow.released == MP_SCENE_RELEASE_ALONE) {
        ++hs.n.left_alone;
    } else if (hs.flow.released == MP_SCENE_RELEASE_BOUND) {
        ++hs.n.went_on;
    }   /* one that gathered nobody was counted as not gathered when its seats were searched */
    if (!line_allowed()) {
        return;
    }
    if (!hs.own_wanted) {
        (void)text_format(host, sizeof host, "was not moved");
    } else {
        (void)text_format(host, sizeof host,
                          "was seated at %.2f %.2f %.2f after %u substep(s) of fade",
                          (double)hs.own_seat[0], (double)hs.own_seat[1], (double)hs.own_seat[2],
                          (unsigned)hs.own_fade_substeps);
    }
    log_info("a gathering for a scene ended: scene %u after %u substep(s), because %s, the host "
             "%s; %u of %u far player(s) stood at their seat, %u did not, %u had none",
             (unsigned)hs.flow.serial, (unsigned)held, mp_scene_release_text(hs.flow.released),
             host, (unsigned)arrived, (unsigned)wanted, (unsigned)(wanted - arrived),
             (unsigned)hs.unseated);
    if (hs.flow.kind == MP_SCENE_KIND_HERO) {
        log_info("the hero's grab was held for the gathering of scene %u for %u substep(s), the "
                 "host dead for %u of them; the put-back asks the same predicate",
                 (unsigned)hs.flow.serial, (unsigned)held, (unsigned)hs.flow.hold_dead);
    }
}

/* The wait for the host ran out: the scene is over for everybody and the engine plays it here as
 * it would alone. */
static void say_the_giving_up(void)
{
    mp_scene_move_t host = hs.flow.given_up_for;

    hs.n.gave_up[host <= MP_SCENE_MOVE_MODE ? host : MP_SCENE_MOVE_YES] += 1u;
    if (!line_allowed()) {
        return;
    }
    log_warning("a scene for everybody was given up: scene %u after %u substep(s), because the "
                "host %s; the far players are let go, and the scene plays here as it would with "
                "nobody else", (unsigned)hs.flow.serial, (unsigned)(hs.substep - hs.flow.began),
                mp_scene_given_up_text(host));
}

/* The landing of a warp the engine took: the module left its running state for the respawn and is
 * back in it. What the spawn did to the quest window is counted there. */
static bool warp_landed(void)
{
    uint32_t module = 0u;
    uint8_t  after[MP_SCENE_QUEST_WINDOW_BYTES];
    uint32_t in_story = 0u;
    uint32_t changed;

    if (hs.flow.kind != MP_SCENE_KIND_WARP || !mp_scene_bind_module_state(&module)) {
        return false;
    }
    if (module != MP_HERO_MODULE_RUNNING) {
        hs.warp_left_running = true;
        return false;
    }
    if (!hs.warp_left_running) {
        return false;
    }
    hs.warp_left_running = false;
    if (hs.quest_known && mp_scene_bind_quest_window(after)) {
        changed = mp_scene_bits_changed(hs.quest_before, after, sizeof after,
                                        MP_SCRATCH_HERO_FIRST * 8u, MP_QUEST_FIRST_BIT,
                                        MP_QUEST_LAST_BIT, &in_story);
        hs.n.warp_bits += changed;
        log_info("a warp reloaded the quest bits: hero %d, %u of bits 48 to 95 changed, %u of "
                 "them in the shared story 51 to 84, sent as the host's", (int)hs.warp_hero,
                 (unsigned)changed, (unsigned)in_story);
    }
    return true;
}

/* While a gathering holds and the host lies dead, its re-entry is anchored on the seat the
 * gathering handed it: the scene is played there. */
static void name_the_anchor(bool stands)
{
    bool want = hs.flow.holds && hs.own_wanted && !stands;

    if (want && !hs.named_set) {
        mp_seat_name_anchor(hs.own_seat, hs.heading);
        hs.named_set = true;
        ++hs.n.named;
    } else if (!want && hs.named_set) {
        mp_seat_name_anchor(NULL, 0.0f);
        hs.named_set = false;
    }
}

void mp_scene_host_tick(uint32_t substep)
{
    mp_scene_host_look_t look;
    uint32_t             arrived = 0u;
    uint32_t             wanted  = 0u;
    bool                 held;
    bool                 given_up;

    hs.substep = substep;
    if (!hs.installed || !mp_target_hosting()) {
        return;
    }
    if (hs.begin_pending) {
        hs.begin_pending = false;
        start_the_gathering();
    }
    if (hs.flow.phase == MP_SCENE_PHASE_NONE) {
        return;
    }
    step_own_seat();
    look.now             = substep;
    look.host_stands     = mp_scene_bind_player_stands();
    look.everyone_seated = everyone_seated(&arrived, &wanted);
    look.running         = mp_scene_bind_running();
    look.warp_landed     = warp_landed();
    look.alone           = !mp_bridge_joined();
    look.may_move        = mp_scene_bind_may_move();
    look.nobody_gathered = hs.nobody_gathered;
    look.unseated        = hs.unseated;
    held                 = hs.flow.holds;
    given_up             = hs.flow.given_up;
    name_the_anchor(look.host_stands);
    (void)mp_scene_host_step(&hs.flow, &look);
    if (held && !hs.flow.holds) {
        mp_cutscene_set_gather_holds(false);
        name_the_anchor(true);
    }
    if (!given_up && hs.flow.given_up) {
        say_the_giving_up();
    } else if (held && !hs.flow.holds) {
        say_the_release(arrived, wanted);
    }
    if (hs.flow.phase == MP_SCENE_PHASE_NONE) {
        leave();
    }
}

/* ==============================================================================================
 * The note.
 * ============================================================================================ */

/* The scene as it stands, every seat of a far player with it. */
static void build(mp_scene_note_t *note)
{
    uint8_t  generation = 0u;
    uint32_t age        = hs.substep - hs.flow.phase_since;
    size_t   bank;

    memset(note, 0, sizeof *note);
    (void)mp_session_now_client_of_a_started_session(NULL, &generation);
    note->serial       = hs.flow.serial;
    note->generation   = generation;
    note->phase        = (uint8_t)hs.flow.phase;
    note->what         = mp_scene_what_of(hs.flow.kind);
    note->trigger_slot = hs.trigger_slot;
    note->warp_serial  = hs.flow.warp_serial;
    note->age_ms       = mp_scene_note_age((uint32_t)((float)age * MS_A_SUBSTEP));
    if (hs.anchor_known) {
        memcpy(note->anchor, hs.anchor, sizeof note->anchor);
        note->heading = hs.heading;
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX && note->seats < MP_SCENE_NOTE_MAX_SEATS; ++bank) {
        mp_scene_seat_t *seat = &note->seat[note->seats];

        if (!hs.far[bank].wanted || hs.far[bank].slot > MP_SCENE_SLOT_MAX) {
            continue;
        }
        seat->slot  = hs.far[bank].slot;
        seat->flags = hs.flow.kind == MP_SCENE_KIND_WARP ? (uint8_t)MP_SCENE_SEAT_F_WARP_FADE : 0u;
        memcpy(seat->position, hs.far[bank].seat, sizeof seat->position);
        seat->heading = hs.heading;
        ++note->seats;
    }
}

void mp_scene_host_send(uint32_t substep, mp_scene_send_fn_t send)
{
    mp_scene_note_t note;

    if (!hs.installed || !mp_target_hosting()) {
        return;
    }
    hs.substep = substep;
    build(&note);
    (void)mp_scene_sender_offer(&hs.sender, &note, substep, send);
}

/* ==============================================================================================
 * Installation, the one question and the report.
 * ============================================================================================ */

bool mp_scene_install(void)
{
    if (hs.installed) {
        return true;
    }
    hs.installed = true;
    (void)mp_scene_bind_install();
    (void)mp_scene_client_install();
    mp_scene_exit_set(&mp_scene_reset);
    return true;
}

/* The host's own player was gathered when it is the player the script meant, standing at the
 * place already, or when its seat for the scene was reached. */
bool mp_scene_for_all(mp_scene_known_t *known)
{
    if (!mp_target_hosting()) {
        return mp_scene_client_for_all(known);
    }
    if (known != NULL) {
        known->anchor_known = hs.anchor_known;
        memcpy(known->anchor, hs.anchor, sizeof known->anchor);
        known->gathered = hs.bank == 0u || (hs.own_wanted && hs.own.stage == MP_SCENE_SEAT_DONE);
        known->serial   = hs.flow.serial;
    }
    return mp_scene_for_all_now((uint8_t)hs.flow.phase, mp_scene_what_of(hs.flow.kind));
}

/* The host's lines where this side hosted or gathered, a client's where it is one, and the one
 * question on both. */
void mp_scene_report(void)
{
    if (!hs.installed) {
        log_info("  the scene gathering is not bound: no scene of this session gathered anybody");
        return;
    }
    if (mp_target_hosting() || hs.flow.serial != 0u) {
        mp_scene_host_report(&hs.n, &hs.sender, &hs.seat_counts);
    }
    mp_scene_client_report();
    log_info("  the scene for all: %s at the report",
             mp_scene_for_all(NULL) ? "a scene runs" : "none runs");
}
