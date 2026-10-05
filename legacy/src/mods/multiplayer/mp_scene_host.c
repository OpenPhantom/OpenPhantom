/* mp_scene_host.c: the host's scene. See the header.
 *
 * Three things about the code are worth having in front of a maintainer, because none is visible
 * in a type:
 *
 * The begin is split in two on purpose. A door is heard inside the engine, in the middle of an
 * actor's script, and what has to happen there is only what cannot wait a substep: the scene's
 * state and its two holds, because the hero's grab can be asked in the same substep. The place is
 * searched from the bridge's post-tick half of that substep, outside the engine, where the seat
 * search's probes are asked everywhere else too.
 *
 * Every way out goes through leave(). A hold left standing is a script that never runs again and a
 * hero that is never grabbed; a fade left held is a black screen; an input left held is a host who
 * cannot move. All three are what the one exit is for, and the scene has no other way to end.
 *
 * The hold of the hero's grab is a level, written in one place from the scene's own hold. A door,
 * a substep, a warp and the exit each change the scene and then ask that place; none of them
 * writes the level by itself, so no two of them can leave it standing apart.
 *
 * What a far player's lock took of the host at the door is given back in one place too, through
 * mp_scene_free and nothing of this file's own: for a lock that is no scene of the host's, and
 * for a release its script was refused and can no longer make itself.
 *
 * SIZE NOTE: over 600 lines. The scene, its door, its substep and its one exit stay together,
 * because each of them ends in that exit. The seams taken: the place is mp_scene_seats.c and the
 * host's own way to it mp_scene_own.c, with the record the three share in
 * mp_scene_host_internal.h; the decisions are mp_scene_flow's and mp_scene_room's, whose a
 * script's doors are mp_scene_claim's, the engine mp_scene_bind's and the report
 * mp_scene_host_report's. The next seam is the scene taken over with no door, which reads the
 * engine and begins a scene by a way of its own.
 */
#include "mp_scene_host.h"

#include "mp_body.h"
#include "mp_bridge.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_relay.h"
#include "mp_level_state_warp.h"
#include "mp_quest.h"
#include "mp_respawn.h"
#include "mp_scene_bind.h"
#include "mp_scene_claim.h"
#include "mp_scene_client.h"
#include "mp_scene_doorless.h"
#include "mp_scene_exit.h"
#include "mp_scene_free.h"
#include "mp_scene_hero_watch.h"
#include "mp_scene_host_internal.h"
#include "mp_scene_host_report.h"
#include "mp_scene_scope.h"
#include "mp_scratch.h"
#include "mp_seat.h"
#include "mp_target.h"
#include "mp_voice.h"

#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What only this file keeps: the scene as the door heard it, acted on in the substep, and the
 * warp's landing. */
typedef struct host_state {
    bool      begin_pending;
    uint32_t  adopt_seen;   /* substeps in a row a scene ran here with no door heard */
    bool      warp_left_running;
    bool      quest_known;
    uint8_t   quest_before[MP_SCENE_QUEST_WINDOW_BYTES];
    bool      let_go_owed;  /* a menu of the engine kept the lock: asked again every substep */
    bool      owed_in_a_scene;   /* and that lock is the standing scene's own, whose release was
                                  * made up: asked again although a scene stands */
} host_state_t;

static host_state_t hs;

/* The record the three files of the host's scene share; every one of them reads and writes the
 * scene through it. */
static mp_scene_host_shared_t sh;

mp_scene_host_shared_t *mp_scene_host_shared(void)
{
    return &sh;
}

/* ==============================================================================================
 * Small readings.
 * ============================================================================================ */

bool mp_scene_host_line_allowed(void)
{
    if (sh.scene_lines >= MP_SCENE_EVENT_LINES || sh.n.lines >= MP_SCENE_EVENT_LINES_ALL) {
        ++sh.n.lines_left_out;
        return false;
    }
    ++sh.scene_lines;
    ++sh.n.lines;
    return true;
}

static const char *kind_text(mp_scene_kind_t kind)
{
    return kind == MP_SCENE_KIND_LOCK   ? "a lock"
           : kind == MP_SCENE_KIND_HERO ? "the hero as an actor"
                                        : "a warp";
}

/* The hero's grab waits exactly while the scene's hold stands. The one writer of that level. */
static void hold_the_grab(void)
{
    mp_cutscene_set_gather_holds(sh.flow.holds);
}

/* ==============================================================================================
 * The measuring line.
 * ============================================================================================ */

/* A line left out past its budget still moves the clock, so it is left out once and not on every
 * substep after it. */
static void measure(mp_scene_measure_when_t when)
{
    mp_scene_measure_t what;

    sh.measured_at = sh.substep;
    if (sh.measured >= MP_SCENE_MEASURE_LINES || sh.n.measures >= MP_SCENE_MEASURE_LINES_ALL) {
        ++sh.n.measures_left_out;
        return;
    }
    ++sh.measured;
    ++sh.n.measures;
    what.when       = when;
    what.serial     = sh.flow.serial;
    what.substep    = sh.substep;
    what.since      = sh.substep - sh.flow.began;
    what.door       = sh.actor;
    what.door_key   = sh.actor_key;
    what.door_keyed = sh.actor_keyed;
    mp_scene_host_measure(&what);
}

/* ==============================================================================================
 * The one exit.
 * ============================================================================================ */

static void leave(void)
{
    (void)mp_scene_host_leave(&sh.flow);
    hold_the_grab();
    mp_scene_own_forget(MP_SCENE_OWN_END_SCENE);
    mp_body_set_scene_passable(false);
    mp_scene_claim_scene_ended();
    sh.released_at_place = false;
    hs.begin_pending     = false;
    hs.owed_in_a_scene   = false;   /* what is still owed is owed with no scene from here */
    sh.actor             = 0u;
    sh.actor_keyed       = false;
    sh.actor_at_known    = false;
    sh.hero_keyed        = false;
    mp_scene_seats_forget();
}

/* ==============================================================================================
 * The host let go.
 * ============================================================================================ */

/* What a far player's lock took of the host is given back: the lock at a script's level, the
 * bars, the camera's override and the lock's input mode, through the one place a player is given
 * back what a scene holds, which decides by what it reads what there is to give. A menu of the
 * engine open over the lock keeps the lock, because the menu's close would put the lock's input
 * mode back over a lock of nought; that is owed, and asked again on every substep until it is
 * given or a scene of the host's stands. Returns what was given back. */
static uint32_t let_the_host_go(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;
    uint32_t             plan;
    uint32_t             given;

    mp_scene_free_look(&look, sh.substep);
    plan = mp_scene_free_plan(&look, MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS |
                                         MP_SCENE_FREE_CAMERA | MP_SCENE_FREE_INPUT_MODE,
                              MP_SCENE_LOCK_LEVEL, &left);
    given = mp_scene_free_now(plan, sh.substep);
    /* What the takers took is no longer taken once it is given back here, and an actor that kept
     * its mark would later give back another scene's. */
    if (given != 0u) {
        mp_scene_claim_forget_marks();
    }
    hs.let_go_owed = (left & MP_SCENE_FREE_LEFT_MENU) != 0u;
    sh.n.let_go_waits += hs.let_go_owed ? 1u : 0u;
    return given;
}

/* A held lock of a far player's was dropped by the flow: no scene of the host's. In the substep
 * of the drop, before the one exit: its actor's marks fall and its placement's doors are refused
 * until a run of it is the host's, or its script would raise the lock again on its next run. */
static void the_lock_is_dropped(void)
{
    mp_scene_drop_t why       = sh.flow.dropped;
    int32_t         placement = mp_scene_claim_foreign();
    uint32_t        given     = let_the_host_go();
    uint8_t         slot      = 0u;

    ++sh.n.dropped[why == MP_SCENE_DROP_AT_THE_CAP ? 1u : 0u];
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    log_info("a lock for a far player is no scene of the host's: placement %d, the host is let "
             "go (%04X given back%s) because %s; its script plays on for world slot %u, and its "
             "doors are refused here until a run of it is the host's", (int)placement,
             (unsigned)given, hs.let_go_owed ? ", the lock owed under a menu of the engine" : "",
             mp_scene_drop_text(why), mp_body_bank_slot(sh.bank, &slot) ? (unsigned)slot : 255u);
}

/* A release of the lock that a far player's run was refused while a scene of the host's stood,
 * and whose actor has since taken itself away: nobody is left to make it, so it is made for the
 * host. The engine then ends the scene by itself, and the flow sees it on its next look.
 *
 * Only for a lock scene with no hero. That is the scene a lift's trigger opens and a second
 * actor at the other end gives back before it removes itself. A hero's scene is given back by
 * its hero, whose runs are the host's whoever stands nearer, so an actor refused there and gone
 * since had nothing to do with it. A menu of the engine open at that moment keeps the lock, and
 * the scene stands on; the lock is then asked for on every substep although a scene stands. */
static void make_up_an_owed_release(void)
{
    bool     lock_scene = mp_scene_host_stands(NULL) && sh.flow.kind == MP_SCENE_KIND_LOCK;
    uint32_t key        = 0u;
    uint32_t given;

    if (!mp_scene_claim_owed_due(lock_scene, &key)) {
        return;
    }
    given = let_the_host_go();
    hs.owed_in_a_scene = hs.let_go_owed;
    ++sh.n.owed_made_up;
    if (mp_scene_host_line_allowed()) {
        log_info("a release a far player's run was refused is made up for the host: placement "
                 "%u gave the lock back as that player's run and has gone since, so nobody is "
                 "left to give it back for scene %u (%04X given back%s)", (unsigned)key,
                 (unsigned)sh.flow.serial, (unsigned)given,
                 hs.let_go_owed ? ", the lock owed under a menu of the engine" : "");
    }
}

/* The lock a menu of the engine kept, asked for again. A scene of the host's that began since has
 * taken the lock over, and nothing is owed any more. The lock of a scene whose release was made
 * up is the other case: that scene stands exactly because its lock does, so it is asked for
 * until it is given. */
static void give_what_is_owed(void)
{
    if (!hs.let_go_owed) {
        hs.owed_in_a_scene = false;
        return;
    }
    if (sh.flow.phase != MP_SCENE_PHASE_NONE && !hs.owed_in_a_scene) {
        hs.let_go_owed = false;
        return;
    }
    (void)let_the_host_go();
    if (!hs.let_go_owed) {
        hs.owed_in_a_scene = false;
    }
}

/* A scene whose script never reaches its end runs until its level does; the end of the level is
 * where it is seen, and said. */
static void say_a_scene_still_running(void)
{
    if (!sh.installed || !mp_scene_host_still_running(&sh.flow)) {
        return;
    }
    ++sh.n.still_running;
    if (mp_scene_host_line_allowed()) {
        log_info("a scene was still running as the level ended: placement %d, %s, for %u "
                 "substep(s)", sh.actor_keyed ? (int)sh.actor_key : -1, kind_text(sh.flow.kind),
                 (unsigned)(sh.substep - sh.flow.began));
    }
}

void mp_scene_reset(void)
{
    say_a_scene_still_running();
    leave();
    hs.let_go_owed     = false;   /* the lock that was owed went with its world */
    hs.owed_in_a_scene = false;
    mp_scene_claim_world_ended();
    mp_scene_client_leave();
    mp_voice_world_ended();
    mp_seat_world_ended();
}

bool mp_scene_host_repair(void)
{
    if (!sh.installed || !mp_scene_host_stands(NULL)) {
        return false;
    }
    ++sh.n.repairs;
    log_info("a scene of the host's was ended by the player's own release: scene %u, %s, after "
             "%u substep(s); its holds fall, and the engine keeps whatever of it the release "
             "does not take back", (unsigned)sh.flow.serial, kind_text(sh.flow.kind),
             (unsigned)(sh.substep - sh.flow.began));
    leave();
    return true;
}

void mp_scene_host_latch(uint32_t substep)
{
    uintptr_t driver = mp_scene_hero_watch_driver(sh.substep);
    uint32_t  key    = 0u;

    /* The hero this release sends away may not take the host again: its placement is written
     * down, for the ones that wait on it already and the ones a spawner puts there later. The
     * module still stands stopped here, because the actor was only told to leave, so the actor
     * that drives the body is still found. */
    if (driver != 0u && mp_enemy_bind_index(driver, &key)) {
        mp_scene_claim_latch_hero(key);
    }
    mp_scene_claim_latch();
    log_info("the latch after a player's own release is open for %u substep(s) from substep %u "
             "of the host's scene (the caller's own count reads %u): every actor whose script "
             "opens a door in them is written down, and its doors are refused until the level "
             "ends", (unsigned)MP_SCENE_LATCH_SUBSTEPS, (unsigned)sh.substep, (unsigned)substep);
}

size_t mp_scene_host_latched(uint32_t *keys, size_t max)
{
    return mp_scene_claim_latched(keys, max);
}

/* ==============================================================================================
 * The door.
 * ============================================================================================ */

/* A warp the engine takes: the waiting re-entry ends, because the warp's own spawn brings the host
 * back at its target, and a re-entry left waiting would put him back beside a far player the moment
 * he lands. And the quest window as it stands, for the count the landing makes. The far players
 * are told here, in the substep of the door and a fade before the host stands at the target, so
 * that each of them follows him on its own machine. This is the one place every taken warp
 * passes: the door returns before it only for a host with nobody joined and for a warp the engine
 * drops, and a warp over a standing scene still comes through it. */
static void a_warp_is_taken(const float *at, float heading, int32_t hero)
{
    ++sh.n.warps;
    memcpy(sh.warp_at, at, sizeof sh.warp_at);
    sh.warp_heading      = heading;
    sh.warp_hero         = hero;
    sh.warp_told         = mp_level_state_warp_say(hero, at);
    hs.warp_left_running = false;
    hs.quest_known       = mp_scene_bind_quest_window(hs.quest_before);
    if (mp_respawn_pending()) {
        mp_respawn_cancel();
        ++sh.n.reentries_ended;
        log_info("a warp ended a re-entry that waited for this host: the spawn of the warp brings "
                 "the host back at %.2f %.2f %.2f", (double)at[0], (double)at[1], (double)at[2]);
    }
}

/* What a door asks before anything, and what a scene taken over with no door asks as well: the
 * scene module bound, this machine hosting, and no arena, which holds every scene back. Whether a
 * far player is joined is asked after it, because a door counts that case apart. */
static bool may_begin(void)
{
    return sh.installed && mp_target_hosting() && mp_scene_bind_ready() &&
           !mp_cutscene_suppressed();
}

/* Whether a warp may be followed at all: the engine drops a respawn unless the module runs. */
static bool the_warp_is_taken(const mp_scene_door_t *door)
{
    uint32_t module = 0u;

    if (door->warp_at == NULL || !isfinite(door->warp_at[0]) || !isfinite(door->warp_at[1]) ||
        !isfinite(door->warp_at[2])) {
        return false;
    }
    if (mp_scene_bind_module_state(&module) && module == MP_HERO_MODULE_RUNNING) {
        return true;
    }
    ++sh.n.warps_dropped;
    if (mp_scene_host_line_allowed()) {
        log_warning("a script asked this host to warp while its player module was not running, "
                    "so the engine drops the warp");
    }
    return false;
}

/* A scene a far player set off is held until the host stands at its place; one the host set off
 * himself has nobody to wait for and runs. The position of the actor is read here, at the door,
 * where the actor is certain to be alive: the spawner of a hero may remove itself in this very
 * tick, and its pool slot keeps bytes that are no longer its own. */
bool mp_scene_host_began(const mp_scene_door_t *door)
{
    mp_scene_begin_t begun;

    if (door == NULL || !may_begin() || door->kind >= MP_SCENE_KINDS) {
        return false;
    }
    if (!mp_bridge_joined()) {
        ++sh.n.alone_doors;   /* nobody else in the session: the engine plays it as it would */
        return false;
    }
    if (door->kind == MP_SCENE_KIND_WARP && !the_warp_is_taken(door)) {
        return false;
    }
    begun = mp_scene_host_begin(&sh.flow, door->kind, sh.substep, door->bank != 0u);
    if (begun == MP_SCENE_BEGIN_SECOND) {
        ++sh.n.second;
        return false;
    }
    if (begun == MP_SCENE_BEGIN_WARP_OVER) {
        mp_scene_own_forget(MP_SCENE_OWN_END_WARP);   /* the engine's fade replaces ours */
        mp_scene_seats_forget();
        mp_scene_claim_scene_ended();                 /* a warp is no scene, and has no actors */
    }
    sh.released_at_place = false;
    sh.scene_lines       = 0u;
    sh.measured          = 0u;
    hs.let_go_owed       = false;   /* the scene that begins takes the lock over */
    hs.owed_in_a_scene   = false;
    hold_the_grab();   /* at once, in the engine: the hosted actor may be ticked this substep */
    hs.begin_pending  = true;
    sh.bank           = door->bank;
    sh.actor          = door->actor;
    sh.actor_keyed    = mp_enemy_bind_index(door->actor, &sh.actor_key);
    sh.actor_at_known = mp_scene_scope_actor(door->actor, sh.actor_at);
    sh.hero_keyed     = door->kind == MP_SCENE_KIND_HERO && door->hero_placement >= 0;
    sh.hero_key       = sh.hero_keyed ? (uint32_t)door->hero_placement : 0u;
    if (door->kind == MP_SCENE_KIND_WARP) {
        a_warp_is_taken(door->warp_at, door->warp_heading, door->warp_hero);
    } else {
        mp_scene_claim_scene_began(door->actor, sh.hero_keyed, sh.hero_key);
    }
    return true;
}

/* The scene's own actor spawned its hero right behind its lock: the scene is a hero's from here.
 * The hold stands already where the lock began it held, so the grab waits as it does for a hero's
 * own door, and the place is looked for as a hero's in the substep that follows. */
bool mp_scene_host_hero_behind_the_lock(uintptr_t actor, int32_t hero_placement)
{
    if (hero_placement < 0 || !mp_scene_host_is_the_actor(actor) ||
        !mp_scene_host_takes_the_hero(&sh.flow, sh.substep)) {
        return false;
    }
    sh.hero_keyed = true;
    sh.hero_key   = (uint32_t)hero_placement;
    mp_scene_claim_scene_hero(sh.hero_key);
    ++sh.n.hero_behind_lock;
    if (mp_scene_host_line_allowed()) {
        log_info("scene %u is a hero's from here: its actor spawned the hero on placement %d "
                 "right behind its lock, so its place is looked for as a hero's and the grab "
                 "%s", (unsigned)sh.flow.serial, (int)hero_placement,
                 sh.flow.holds ? "waits for the host" : "is the engine's at once");
    }
    return true;
}

bool mp_scene_host_is_the_actor(uintptr_t actor)
{
    uint32_t key = 0u;

    return actor != 0u && actor == sh.actor &&
           (!sh.actor_keyed || (mp_enemy_bind_index(actor, &key) && key == sh.actor_key));
}

bool mp_scene_host_holds(uintptr_t actor)
{
    if (!sh.flow.holds || !mp_scene_host_is_the_actor(actor)) {
        return false;
    }
    ++sh.n.actor_held[sh.flow.kind];
    return true;
}

void mp_scene_host_note_respawn(mp_scene_respawn_caller_t caller)
{
    if (caller < MP_SCENE_RESPAWN_CALLERS) {
        ++sh.n.respawns[caller];
    }
}

/* ==============================================================================================
 * A substep of the host's scene.
 * ============================================================================================ */

static void say_the_release(void)
{
    uint32_t held = sh.flow.hold_standing + sh.flow.hold_dead;
    char     host[96];

    if (held > sh.n.longest_hold) {
        sh.n.longest_hold = held;
    }
    if (sh.flow.released == MP_SCENE_RELEASE_AT_THE_PLACE) {
        ++sh.n.at_place;
    } else if (sh.flow.released == MP_SCENE_RELEASE_ALONE) {
        ++sh.n.left_alone;
    }   /* one with no place was counted when its place was looked for */
    sh.released_at_place = sh.flow.released == MP_SCENE_RELEASE_AT_THE_PLACE &&
                           !mp_scene_own_away();
    if (mp_scene_host_line_allowed()) {
        mp_scene_own_stage(host, sizeof host);
        log_info("the wait for the host ended: scene %u after %u substep(s), because %s; the "
                 "host %s", (unsigned)sh.flow.serial, (unsigned)held,
                 mp_scene_release_text(sh.flow.released), host);
        if (sh.flow.kind == MP_SCENE_KIND_HERO) {
            log_info("the hero's grab was held for scene %u for %u substep(s), the host dead for "
                     "%u of them; the put-back asks the same predicate",
                     (unsigned)sh.flow.serial, (unsigned)held, (unsigned)sh.flow.hold_dead);
        }
    }
    measure(MP_SCENE_MEASURE_RELEASED);
}

/* The wait for the host ran out: the engine plays the scene here as it would alone. */
static void say_the_giving_up(void)
{
    mp_scene_move_t host = sh.flow.given_up_for;

    sh.released_at_place = false;   /* the engine plays it where the host was left */
    sh.n.gave_up[host < MP_SCENE_MOVES ? host : MP_SCENE_MOVE_YES] += 1u;
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    log_warning("a scene of the host's was given up: scene %u after %u substep(s), because the "
                "host %s; nothing is held any more, and the scene plays here as it would with "
                "nobody else", (unsigned)sh.flow.serial, (unsigned)(sh.substep - sh.flow.began),
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

    if (sh.flow.kind != MP_SCENE_KIND_WARP || !mp_scene_bind_module_state(&module)) {
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
        sh.n.warp_bits += changed;
        log_info("a warp reloaded the quest bits: hero %d, %u of bits 48 to 95 changed, %u of "
                 "them in the shared story 51 to 84, sent as the host's", (int)sh.warp_hero,
                 (unsigned)changed, (unsigned)in_story);
    }
    return true;
}

/* A scene that runs here with no door heard, a savegame having restored it or the engine having
 * begun it past every door, taken as the host's on its evidence (mp_scene_doorless). It runs
 * already, so it runs at once: nothing is held, nobody is moved, and its place is where the host
 * stands. The actor that drives the host's body, where there is one, stands in for the actor of
 * the door nobody heard, so that the scene's line and its measure name somebody. */
static void take_over_a_scene_without_a_door(void)
{
    mp_scene_adopt_look_t look;
    mp_scene_kind_t       kind;
    uintptr_t             driver = 0u;
    uint8_t               slot   = MP_SCENE_TRIGGER_UNKNOWN;

    memset(&look, 0, sizeof look);
    /* A lock that stands only because a menu kept it from being given back is no scene: it is
     * owed, and taking it over would leave it with nobody to end it. */
    look.may_begin     = may_begin() && mp_bridge_joined() && !hs.let_go_owed;
    look.phase         = sh.flow.phase;
    look.given_up      = sh.flow.given_up;
    look.lock_level    = mp_cutscene_lock_level();
    look.module_parked = mp_scene_bind_parked();
    if (look.module_parked) {
        driver = mp_scene_hero_watch_driver(sh.substep);
    }
    look.driven = driver != 0u;
    switch (mp_scene_adopt_step(&hs.adopt_seen, &look)) {
    case MP_SCENE_ADOPT_HERO:
        kind = MP_SCENE_KIND_HERO;
        break;
    case MP_SCENE_ADOPT_LOCK:
        kind = MP_SCENE_KIND_LOCK;
        break;
    case MP_SCENE_ADOPT_NO:
    case MP_SCENE_ADOPT_WAIT:
    default:
        return;
    }
    if (!mp_scene_host_adopt(&sh.flow, kind, sh.substep)) {
        return;
    }
    mp_scene_claim_scene_adopted(driver);
    mp_scene_seats_forget();
    hold_the_grab();
    sh.released_at_place = false;
    sh.scene_lines       = 0u;
    sh.measured          = 0u;
    sh.bank              = 0u;
    sh.actor             = driver;
    sh.actor_keyed       = driver != 0u && mp_enemy_bind_index(driver, &sh.actor_key);
    sh.actor_at_known    = false;
    sh.hero_keyed        = false;
    sh.trigger_slot      = mp_body_bank_slot(0u, &slot) ? slot
                                                      : (uint8_t)MP_SCENE_TRIGGER_UNKNOWN;
    sh.anchor_known      = mp_scene_bind_own_pose(sh.anchor, &sh.heading);
    ++sh.n.adopted[kind];
    if (mp_scene_host_line_allowed()) {
        log_info("a scene ran here without a door (a savegame restored it, or the engine began "
                 "it): scene %u, %s, the lock at %d, the module %s; it is taken as the host's "
                 "and runs, its place where the host stands (%.2f %.2f %.2f%s)",
                 (unsigned)sh.flow.serial, kind_text(kind), (int)look.lock_level,
                 look.driven ? "parked under an actor that drives the player's body"
                             : "not parked by a scene",
                 (double)sh.anchor[0], (double)sh.anchor[1], (double)sh.anchor[2],
                 sh.anchor_known ? "" : ", not read");
    }
    measure(MP_SCENE_MEASURE_BEGUN);
}

/* The look of the flow's step, read once a substep. */
static void look_at_the_scene(mp_scene_host_look_t *look)
{
    memset(look, 0, sizeof *look);
    look->now            = sh.substep;
    look->host_stands    = mp_scene_bind_player_stands();
    look->running        = mp_scene_bind_running();
    look->warp_landed    = warp_landed();
    look->alone          = !mp_bridge_joined();
    look->may_move       = mp_scene_bind_may_move();
    look->has_place      = mp_scene_own_wanted();
    look->no_place       = sh.no_place;
    look->place_pending  = mp_scene_seats_place_pending();
    look->away           = mp_scene_own_away();
    look->own_respawning = mp_scene_own_respawning();
}

void mp_scene_host_tick(uint32_t substep)
{
    mp_scene_host_look_t look;
    bool                 held;
    bool                 given_up;

    sh.substep = substep;
    /* Ahead of every return below: a scene the engine plays on alone after it was given up
     * has no hold here any more, and its hero still walks and still needs the far bodies
     * out of its way. The watch is asked first, so the walk it reports is this substep's. */
    mp_scene_hero_watch_tick(substep, sh.flow.serial);
    mp_body_set_scene_passable(mp_target_hosting() &&
                               (mp_scene_host_stands(NULL) ||
                                (mp_scene_hero_watch_walking() && mp_scene_bind_running())));
    mp_scene_own_hold_input();
    /* The clock of the doors and the actor that drives the host's body, for the scripts that run
     * until the next substep. On a client nothing is asked of either. */
    mp_scene_claim_tick(substep, sh.installed && mp_target_hosting()
                                     ? mp_scene_hero_watch_driver(substep) : 0u);
    if (!sh.installed || !mp_target_hosting()) {
        return;
    }
    give_what_is_owed();
    if (hs.begin_pending) {
        hs.begin_pending = false;
        mp_scene_seats_start();
        measure(MP_SCENE_MEASURE_BEGUN);
    }
    /* Every substep, so that its count of substeps in a row starts over whenever a scene of the
     * host's own stands. */
    take_over_a_scene_without_a_door();
    make_up_an_owed_release();
    if (sh.flow.phase == MP_SCENE_PHASE_NONE) {
        return;
    }
    mp_scene_seats_step();
    mp_scene_own_step();
    mp_scene_own_hold_input();   /* a seat that began its fade now is held for the next tick */
    look_at_the_scene(&look);
    held     = sh.flow.holds;
    given_up = sh.flow.given_up;
    mp_scene_own_name_the_anchor(look.host_stands);
    (void)mp_scene_host_step(&sh.flow, &look);
    hold_the_grab();
    if (sh.flow.dropped != MP_SCENE_DROP_NONE) {
        the_lock_is_dropped();
    } else if (!given_up && sh.flow.given_up) {
        say_the_giving_up();
    } else if (held && !sh.flow.holds) {
        say_the_release();
    }
    if (sh.flow.phase == MP_SCENE_PHASE_NONE) {
        /* The end of a scene, said once: the line a scene's beginning has its other end in. A
         * lock that was dropped has said its own. */
        if (sh.flow.dropped == MP_SCENE_DROP_NONE && mp_scene_host_line_allowed()) {
            log_info("scene %u is over: %s, after %u substep(s)", (unsigned)sh.flow.serial,
                     kind_text(sh.flow.kind), (unsigned)(substep - sh.flow.began));
        }
        leave();
        return;
    }
    if (mp_scene_host_stands(NULL) && substep - sh.measured_at >= MP_SCENE_MEASURE_SUBSTEPS) {
        measure(MP_SCENE_MEASURE_STANDING);
    }
}

/* ==============================================================================================
 * Installation, the one question and the report.
 * ============================================================================================ */

/* From the removal's hull, inside the engine, on a host: the actor that carried `key` is gone.
 * What it last heard about the player and what it took for a scene are not its placement's next
 * life's. */
static void an_actor_was_removed(uint32_t key)
{
    mp_target_forget_answer(key);
    mp_scene_claim_actor_removed(key);
}

bool mp_scene_install(void)
{
    if (sh.installed) {
        return true;
    }
    sh.installed = true;
    (void)mp_scene_bind_install();
    (void)mp_scene_client_install();
    mp_scene_exit_set(&mp_scene_reset);
    mp_cutscene_set_grab_listener(&mp_scene_own_grabbed);
    mp_target_set_claim(&mp_scene_claim_answer_evidence);
    mp_enemy_relay_set_removed_listener(&an_actor_was_removed);
    return true;
}

/* A client is in no scene, so there the answer is no and nothing is known. */
bool mp_scene_host_stands(mp_scene_known_t *known)
{
    if (!mp_target_hosting()) {
        if (known != NULL) {
            memset(known, 0, sizeof *known);
        }
        return false;
    }
    if (known != NULL) {
        known->anchor_known = sh.anchor_known;
        memcpy(known->anchor, sh.anchor, sizeof known->anchor);
        known->gathered = true;
        known->serial   = sh.flow.serial;
    }
    return mp_scene_host_stands_now(&sh.flow);
}

/* The host's lines where this side hosted a scene, a client's where it is one, and the one
 * question. */
void mp_scene_report(void)
{
    if (!sh.installed) {
        log_info("  the scenes of the host are not bound: no scene of this session moved the "
                 "host");
        return;
    }
    if (mp_target_hosting() || sh.flow.serial != 0u) {
        mp_scene_host_report(&sh.n, &sh.seat_counts);
        mp_scene_claim_report();
    }
    mp_scene_hero_watch_report();
    mp_scene_client_report();
    mp_scene_bind_report();
    log_info("  the scene of the host: %s at the report",
             mp_scene_host_stands(NULL) ? "one stands" : "none stands");
}
