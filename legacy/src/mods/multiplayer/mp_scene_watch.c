/* mp_scene_watch.c: the doors of a script on the host, heard and judged. See the header.
 *
 * SIZE NOTE: over 600 lines, a third of them the report and the lines. The hull that tells whose
 * script runs and the three listeners that hear a script's doors stay together, because each door
 * is judged for the actor the hull names. What is decided is not here: whose a run is and what it
 * may take is mp_scene_claim's, and what a scene's door begins is mp_scene_host's. The next seam
 * is the report with its counters, which the doors only add to.
 */
#include "mp_scene_watch.h"

#include "mp_arena.h"
#include "mp_body.h"
#include "mp_body_passable.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_enemy_bind.h"
#include "mp_lifecycle.h"
#include "mp_placements.h"
#include "mp_scene_bind.h"
#include "mp_scene_claim.h"
#include "mp_scene_hero_watch.h"
#include "mp_scene_host.h"
#include "mp_scene_rule.h"
#include "mp_signatures.h"
#include "mp_signatures_enemy.h"
#include "mp_target.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A scene's own line is written this many times in a process, and after that only counted. Scenes
 * are a handful per level, so the cap is there for a script that loops, not for the field. A door
 * of a far player's run that begins no scene has a smaller budget of its own, so that a script
 * raising its lock on every tick leaves the lines of the scenes alone. */
#define EVENT_LINES_MAX  64u
#define FAR_DOOR_LINES_MAX 16u

/* A warp refused on a client is said this many times, and after that the report's count. */
#define CLIENT_WARP_LINES_MAX 4u

/* A grab refused to a hero is said this many times: the engine asks on every tick. */
#define GRAB_LINES_MAX 4u

/* The lock opcode raises the lock right behind its camera take: `add esp,4; push 5; call`, so the
 * raise returns ten bytes past the take. */
#define LOCK_RAISE_PAST_THE_TAKE 10u

/* ai_run returns nothing the one caller reads; whatever is in the register is passed back anyway,
 * because a hull that drops a return value is a trap this tree has already paid for. */
typedef int32_t(__cdecl *ai_run_fn_t)(uintptr_t actor);

typedef enum scene_kind {
    KIND_LOCK = 0,
    KIND_HERO,
    KIND_WARP,
    KIND_COUNT
} scene_kind_t;

/* What became of a door of a far player's run that is a scene's door, a lock or a hero. */
typedef enum far_door {
    FAR_DOOR_THE_HOSTS = 0,   /* it began a scene of the host's, who is brought to it */
    FAR_DOOR_IN_A_SCENE,      /* a scene of the host's stood: the lock refused, the hero the
                               * engine's */
    FAR_DOOR_NOT_BEGUN,       /* the host's scene did not begin with it: refused */
    FAR_DOOR_OVER_A_LOCK,     /* a lock stood with no scene of the host's: refused */
    FAR_DOORS
} far_door_t;

typedef struct scene_watch {
    bool        installed;
    bool        hosted;        /* a script has run here while this side was hosting */
    detour_t    hull;
    ai_run_fn_t original;

    /* Where each door returns to, nought for a door that is not heard. */
    uintptr_t   lock_return;
    uintptr_t   warp_return;

    uint32_t began[KIND_COUNT];
    uint32_t by_rule[MP_SCENE_RUN_RULES];
    uint32_t far_by_rule[MP_SCENE_RUN_RULES];   /* of those, the rule named a far player */
    uint32_t own_none;                  /* why the actor's own answer did not decide */
    uint32_t own_stale;
    uint32_t own_not_a_player;
    uint32_t outside_a_script;          /* a door opened with no script running */
    uint32_t lines_written;
    uint32_t lines_left_out[KIND_COUNT];
    float    farthest;                  /* the farthest a far trigger stood from the host */

    uint32_t heroes;                    /* spawns of a placement carrying the handover flag */
    uint32_t heroes_by_a_script;
    uint32_t heroes_by_the_engine;      /* from inside the engine with no script running */
    uint32_t heroes_by_a_dll;
    uint32_t heroes_behind_the_lock;    /* by the scene's own actor in the substep of its lock */
    uint32_t spawns_unreadable;

    uint32_t host_dead;                 /* began while the host lay dead */
    uint32_t far_doors[FAR_DOORS][KIND_COUNT];
    uint32_t far_door_lines;
    uint32_t latched[KIND_COUNT];       /* doors of a placement whose doors are refused */

    uint32_t client_warps_refused;      /* on a client: a warp of a script of its own machine */

    uint32_t grab_askers_unknown;       /* a grab asked with a placement written down, and the
                                         * actor that asked not found */
    uint32_t grab_lines;
    uint32_t heroes_written_down;       /* heroes of a spawner written down, refused the grab */
    uint32_t heroes_on_a_written;       /* heroes spawned on a placement written down */
} scene_watch_t;

static scene_watch_t watch;

static const char *const KIND_TEXT[KIND_COUNT] = {
    "a lock", "the hero as an actor", "a warp"
};

/* ==============================================================================================
 * The actor whose script runs.
 * ============================================================================================ */

/* The run is told to the claim, which keeps whose script runs for every door and for the
 * judgement of a spoken line; a nested run hands the outer one back.
 *
 * The actor whose script opened the door of a far player's scene is held here while the host is
 * brought to that player's place, so the scene's first line is not spoken before the host is
 * there: until he stands at the place, and while he lies dead until he stands again, twenty
 * seconds at the most; with nobody else in the session not at all. Nothing else of the actor
 * stops; the one caller reads no answer.
 *
 * engine: void ai_run(character *actor) */
static int32_t __cdecl hook_ai_run(uintptr_t actor)
{
    uintptr_t outer;
    int32_t   answer;

    if (!mp_target_hosting()) {
        return watch.original(actor);
    }
    if (mp_scene_host_holds(actor)) {
        return 0;
    }
    watch.hosted = true;
    outer        = mp_scene_claim_run_begins(actor);
    answer       = watch.original(actor);
    mp_scene_claim_run_ends(outer);
    return answer;
}

/* Whether a script of an actor runs now, for the count of contacts on a passable far body: one
 * delivered while a script runs is that script's message. */
static bool a_script_runs(void)
{
    return mp_scene_claim_running() != 0u;
}

/* ==============================================================================================
 * A scene begins.
 * ============================================================================================ */

static void count_why_not_its_own(mp_scene_answer_t own)
{
    if (own == MP_SCENE_ANSWER_NONE) {
        ++watch.own_none;
    } else if (own == MP_SCENE_ANSWER_STALE) {
        ++watch.own_stale;
    } else if (own == MP_SCENE_ANSWER_NOT_A_PLAYER) {
        ++watch.own_not_a_player;
    }
}

static void slot_text_of(uint8_t bank, char *out, size_t size)
{
    uint8_t slot = 0u;

    if (mp_body_bank_slot(bank, &slot)) {
        (void)text_format(out, size, "%u", (unsigned)slot);
    } else {
        (void)text_format(out, size, "unknown");
    }
}

static int32_t placement_of(uintptr_t actor)
{
    uint32_t key = 0u;

    return mp_enemy_bind_index(actor, &key) ? (int32_t)key : -1;
}

static void say_the_scene(scene_kind_t kind, int32_t hero_placement, uintptr_t actor,
                          const mp_scene_claim_run_t *run, const float at[3], bool placed,
                          float distance, bool host_dead)
{
    const char *host = host_dead ? "the host lay dead" : "the host stood";
    char        what[64];
    char        slot[16];

    if (watch.lines_written >= EVENT_LINES_MAX) {
        ++watch.lines_left_out[kind];
        return;
    }
    ++watch.lines_written;
    if (kind == KIND_HERO) {
        (void)text_format(what, sizeof what, "%s, the hero on placement %d", KIND_TEXT[kind],
                          (int)hero_placement);
    } else {
        (void)text_format(what, sizeof what, "%s", KIND_TEXT[kind]);
    }
    slot_text_of(run->bank, slot, sizeof slot);
    if (!placed) {
        log_info("a scene began here: placement %d, %s, its script meant world slot %s by %s, at "
                 "a place this machine could not read, %s", (int)placement_of(actor), what, slot,
                 mp_scene_run_text(run->rule), host);
        return;
    }
    log_info("a scene began here: placement %d, %s, its script meant world slot %s by %s, at "
             "%.2f %.2f %.2f, %.2f u from the host, %s", (int)placement_of(actor), what, slot,
             mp_scene_run_text(run->rule), (double)at[0], (double)at[1], (double)at[2],
             (double)distance, host);
}

/* The one place a beginning is counted. Every door of a scene comes here with the verdict of its
 * run, so the counters, the line and the host's scene are handed the same player. `hero_placement`
 * is the placement the hero is put on, for that door only. */
static void scene_began(scene_kind_t kind, int32_t hero_placement, uintptr_t actor,
                        const mp_scene_claim_run_t *run)
{
    float at[3]    = { 0.0f, 0.0f, 0.0f };
    float host[3]  = { 0.0f, 0.0f, 0.0f };
    float distance = 0.0f;
    bool  placed;
    bool  host_dead;

    ++watch.began[kind];
    host_dead = !mp_scene_bind_player_stands();   /* the scene's own predicate */
    if (host_dead) {
        ++watch.host_dead;
    }
    ++watch.by_rule[run->rule];
    if (run->rule != MP_SCENE_RUN_BY_OWN_ANSWER) {
        count_why_not_its_own(run->own);
    }
    placed = mp_target_player_position(run->bank, at) && mp_target_player_position(0u, host);
    if (placed) {
        float dx = at[0] - host[0];
        float dy = at[1] - host[1];
        float dz = at[2] - host[2];

        distance = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    if (run->bank != 0u) {
        ++watch.far_by_rule[run->rule];
        if (placed && distance > watch.farthest) {
            watch.farthest = distance;
        }
    }
    say_the_scene(kind, hero_placement, actor, run, at, placed, distance, host_dead);
}

/* A door of the kind a scene of the host's begins at, handed on with the player its script
 * meant. True when a scene of the host's began with it. */
static bool begin_a_scene(mp_scene_kind_t kind, uintptr_t actor, uint8_t bank,
                          int32_t hero_placement)
{
    mp_scene_door_t door;

    memset(&door, 0, sizeof door);
    door.kind           = kind;
    door.actor          = actor;
    door.bank           = bank;
    door.hero_placement = hero_placement;
    return mp_scene_host_began(&door);
}

/* ==============================================================================================
 * A door of a far player's run.
 * ============================================================================================ */

static const char *const FAR_DOOR_TEXT[FAR_DOORS] = {
    "",
    "a scene of the host's stands, and the engine has one lock, one camera and one hero",
    "the host's scene could not begin with it",
    "a lock stands here that is no scene of the host's"
};

/* A scene's door of a far player's run that does not become a scene of the host's: counted, and
 * said as a warning within its own budget. A lock is refused and its scene plays on without the
 * host; a hero is spawned already and is the engine's to take, where the host stands once his own
 * scene is over. */
static void a_far_door_is_left(far_door_t why, scene_kind_t kind, uintptr_t actor,
                               const mp_scene_claim_run_t *run)
{
    char slot[16];

    ++watch.far_doors[why][kind];
    if (watch.far_door_lines >= FAR_DOOR_LINES_MAX) {
        return;
    }
    ++watch.far_door_lines;
    slot_text_of(run->bank, slot, sizeof slot);
    log_warning("a door of a far player's scene is not the host's: placement %d, %s, its script "
                "meant world slot %s by %s; %s, so %s", (int)placement_of(actor), KIND_TEXT[kind],
                slot, mp_scene_run_text(run->rule), FAR_DOOR_TEXT[why],
                kind == KIND_LOCK ? "the lock is refused and that scene plays without the host"
                                  : "the hero is left to the engine, which takes the host where "
                                    "he stands");
}

/* The lock is a scene's door when the lock opcode raises it from below five: a second raise inside
 * a running scene raises nothing, and the menu's level one on every render is no scene. It is
 * asked before the raise is made, so the level read here is the one it stood at, and the scene
 * begins before the lock does.
 *
 * A run of the host's takes the lock and marks it. A far player's is the door of his scene: with
 * no scene of the host's standing it becomes the host's at once, what the run was refused before
 * it is made up, the lock goes through, and the host's scene holds the actor while the host is
 * brought. With a scene standing it is refused, and nothing is made up. */
static bool on_the_lock(uintptr_t actor, const mp_scene_claim_run_t *run)
{
    int32_t level = mp_cutscene_lock_level();
    bool    below = level >= 0 && level < MP_CUTSCENE_LOCK_LEVEL;
    bool    hosts = run->bank == 0u;

    if (mp_scene_claim_latched_door(actor, hosts)) {
        ++watch.latched[KIND_LOCK];
        return false;
    }
    if (hosts) {
        mp_scene_claim_took(actor, MP_SCENE_MARK_LOCK, false);
        if (below) {
            scene_began(KIND_LOCK, -1, actor, run);
            (void)begin_a_scene(MP_SCENE_KIND_LOCK, actor, 0u, -1);
        }
        return true;
    }
    if (mp_scene_host_stands(NULL)) {
        a_far_door_is_left(FAR_DOOR_IN_A_SCENE, KIND_LOCK, actor, run);
        return false;
    }
    if (!below) {
        a_far_door_is_left(FAR_DOOR_OVER_A_LOCK, KIND_LOCK, actor, run);
        return false;
    }
    scene_began(KIND_LOCK, -1, actor, run);
    if (!begin_a_scene(MP_SCENE_KIND_LOCK, actor, run->bank, -1)) {
        a_far_door_is_left(FAR_DOOR_NOT_BEGUN, KIND_LOCK, actor, run);
        return false;
    }
    (void)mp_scene_claim_make_up(actor);
    mp_scene_claim_took(actor, MP_SCENE_MARK_LOCK, false);
    ++watch.far_doors[FAR_DOOR_THE_HOSTS][KIND_LOCK];
    return true;
}

/* ==============================================================================================
 * The doors.
 * ============================================================================================ */

/* The engine's grab of the host, asked by the actor list's tick for the actor it stands at. A
 * hero on a placement the player's own release wrote down does not take the host again: neither
 * one that waited beside the hero the release sent away, nor one a spawner puts there later.
 * Who asks is looked for only while a placement is written down, and an asker that is not found
 * is let through, which is what the engine does alone. */
static bool the_grab_is_refused(void)
{
    uintptr_t asker;

    if (!mp_scene_claim_any_written()) {
        return false;
    }
    asker = mp_scene_hero_watch_asker();
    if (asker == 0u) {
        ++watch.grab_askers_unknown;
        return false;
    }
    if (!mp_scene_claim_grab_refused(asker)) {
        return false;
    }
    if (watch.grab_lines < GRAB_LINES_MAX) {
        ++watch.grab_lines;
        log_info("the grab of the host was refused to the hero on placement %d: the player's own "
                 "release wrote that placement down, so no hero on it takes the host before the "
                 "level ends; its actor waits, and whatever waits for it goes on waiting",
                 (int)placement_of(asker));
    }
    return true;
}

/* Every door a script takes and gives back through on the host, asked from the scene gates inside
 * the engine. The one question is whose the run is (mp_scene_claim): a run of the host's takes
 * and gives back as the engine does alone, and a far player's takes nothing and gives back only
 * what its own actor took. A door with no script running is the engine's own or a module's and
 * goes through. */
static bool on_scene_door(mp_cutscene_door_t door, uintptr_t caller, int32_t argument)
{
    uintptr_t            actor;
    mp_scene_claim_run_t run;

    if (!mp_target_hosting()) {
        return true;
    }
    if (door == MP_CUTSCENE_DOOR_GRAB) {
        return !the_grab_is_refused();
    }
    if (door == MP_CUTSCENE_DOOR_LOCK &&
        (watch.lock_return == 0u || caller != watch.lock_return ||
         argument < MP_CUTSCENE_LOCK_LEVEL)) {
        return true;   /* a menu's lock or the dialogue's, not the lock opcode's */
    }
    actor = mp_scene_claim_running();
    if (door == MP_CUTSCENE_DOOR_LINE_CAMERA) {
        if (actor != 0u) {
            mp_scene_claim_took(actor, MP_SCENE_MARK_CAMERA, true);
        }
        return true;
    }
    if (actor == 0u) {
        ++watch.outside_a_script;
        return true;
    }
    mp_scene_claim_whose(actor, &run);
    switch (door) {
    case MP_CUTSCENE_DOOR_LOCK:
        return on_the_lock(actor, &run);
    case MP_CUTSCENE_DOOR_CAMERA:
        return mp_scene_claim_take(actor, &run, MP_SCENE_MARK_CAMERA, argument) ==
               MP_SCENE_CLAIM_PASSES;
    case MP_CUTSCENE_DOOR_BARS:
        return mp_scene_claim_take(actor, &run, MP_SCENE_MARK_BARS, argument) ==
               MP_SCENE_CLAIM_PASSES;
    case MP_CUTSCENE_DOOR_LOCK_OFF:
        return mp_scene_claim_gives_back(actor, &run, MP_SCENE_MARK_LOCK);
    case MP_CUTSCENE_DOOR_CAMERA_OFF:
        return mp_scene_claim_gives_back(actor, &run, MP_SCENE_MARK_CAMERA);
    case MP_CUTSCENE_DOOR_BARS_OFF:
        return mp_scene_claim_gives_back(actor, &run, MP_SCENE_MARK_BARS);
    case MP_CUTSCENE_DOOR_LINE_CAMERA:
    default:
        return true;
    }
}

/* A warp a script of a client's own machine asks for. A client's actors are the host's replicas,
 * parked, and their scripts do not run; one that runs all the same, let go or woken here, would
 * respawn this player at the warp's target with the warp's hero, which is the host's to be sent
 * and nobody else's. The engine's respawn is not called at all, so nothing of it is left behind.
 * Known by the address the call returns to, the warp opcode's own, and asked of the one level the
 * scene gates hold a client's scripts back by. */
static bool refuse_a_client_its_warp(uintptr_t caller, int32_t hero)
{
    if (watch.warp_return == 0u || caller != watch.warp_return ||
        !mp_cutscene_client_holds_back()) {
        return false;
    }
    ++watch.client_warps_refused;
    if (watch.client_warps_refused <= CLIENT_WARP_LINES_MAX) {
        log_info("a script of this machine asked to warp this player as hero %d and was "
                 "refused: a warp is the host's, and a client stays where it is and what it is",
                 (int)hero);
    }
    return true;
}

/* Every respawn the host is asked for is sorted by who asked; a script's warp is counted as a
 * scene of its own kind, with the target the engine is about to take. The engine sends the host
 * whoever the script meant, so the warp is never refused on a host. Answers whether the respawn
 * may be begun. */
static bool on_respawn(uintptr_t caller, int32_t hero, const float *at, float heading)
{
    mp_scene_respawn_caller_t who;
    uintptr_t                 actor;

    if (!mp_target_hosting()) {
        return !refuse_a_client_its_warp(caller, hero);
    }
    who = mp_scene_respawn_caller(caller, watch.warp_return, mp_scene_bind_swap_return(),
                                  memory_is_inside_image(caller, 1u));
    mp_scene_host_note_respawn(who);
    actor = mp_scene_claim_running();
    if (who == MP_SCENE_RESPAWN_BY_WARP && actor == 0u) {
        ++watch.outside_a_script;
    } else if (who == MP_SCENE_RESPAWN_BY_WARP) {
        mp_scene_claim_run_t run;
        mp_scene_door_t      door;

        mp_scene_claim_whose(actor, &run);
        scene_began(KIND_WARP, -1, actor, &run);
        memset(&door, 0, sizeof door);
        door.kind           = MP_SCENE_KIND_WARP;
        door.actor          = actor;
        door.bank           = run.bank;
        door.hero_placement = -1;
        door.warp_hero      = hero;
        door.warp_at        = at;
        door.warp_heading   = heading;
        (void)mp_scene_host_began(&door);
    }
    return true;
}

/* The hero's door of a script. The spawn is made already, so nothing here refuses it: the host's
 * scene holds the grab instead.
 *
 * The scene's own actor spawning its hero in the substep of its lock is the same scene, which is
 * a hero's from there. Otherwise a run of the host's begins a scene that runs at once. A far
 * player's, with no scene of the host's standing, begins one the host is brought to, and what the
 * run was refused before it is made up; with a scene standing the hero is left to the engine. */
static void on_the_hero(uintptr_t actor, int32_t index)
{
    mp_scene_claim_run_t run;
    bool                 hosts;

    mp_scene_claim_whose(actor, &run);
    hosts = run.bank == 0u;
    if (mp_scene_claim_latched_door(actor, hosts)) {
        ++watch.latched[KIND_HERO];
        /* The spawn is made, and the hero would take the host where he stands: the hero of a
         * spawner the player's own release wrote down is written down with it. */
        if (index >= 0 && mp_scene_claim_repaired(actor)) {
            mp_scene_claim_latch_hero((uint32_t)index);
            ++watch.heroes_written_down;
        }
        return;
    }
    if (index >= 0 && mp_scene_claim_key_repaired((uint32_t)index)) {
        ++watch.heroes_on_a_written;   /* it will not take the host, so it begins no scene */
        return;
    }
    if (hosts) {
        if (mp_scene_host_hero_behind_the_lock(actor, index)) {
            ++watch.heroes_behind_the_lock;
            return;
        }
        scene_began(KIND_HERO, index, actor, &run);
        (void)begin_a_scene(MP_SCENE_KIND_HERO, actor, 0u, index);
        return;
    }
    if (mp_scene_host_stands(NULL)) {
        a_far_door_is_left(FAR_DOOR_IN_A_SCENE, KIND_HERO, actor, &run);
        return;
    }
    scene_began(KIND_HERO, index, actor, &run);
    if (!begin_a_scene(MP_SCENE_KIND_HERO, actor, run.bank, index)) {
        a_far_door_is_left(FAR_DOOR_NOT_BEGUN, KIND_HERO, actor, &run);
        return;
    }
    (void)mp_scene_claim_make_up(actor);
    ++watch.far_doors[FAR_DOOR_THE_HOSTS][KIND_HERO];
}

/* A placement carrying the handover flag puts the hero on it. From a script it is a scene, and the
 * actor running that script is the one that spawned it, not the placement it spawned. From inside
 * the engine with no script running it is a savegame being restored, and from outside the engine
 * it is a DLL; both are counted and neither is a scene. */
static void on_spawn(uintptr_t placement, int32_t index, uintptr_t caller)
{
    mp_placement_t record;
    uintptr_t      actor = mp_scene_claim_running();

    if (!mp_target_hosting()) {
        return;
    }
    if (!mp_placements_read_at(placement, &record)) {
        ++watch.spawns_unreadable;
        return;
    }
    if ((record.flags & MP_PLACEMENT_F_HOSTS_PLAYER) == 0u) {
        return;
    }
    ++watch.heroes;
    switch (mp_scene_hero_origin(memory_is_inside_image(caller, 1u), actor != 0u)) {
    case MP_SCENE_HERO_BY_DLL:
        ++watch.heroes_by_a_dll;
        return;
    case MP_SCENE_HERO_BY_ENGINE:
        ++watch.heroes_by_the_engine;
        return;
    case MP_SCENE_HERO_BY_SCRIPT:
    default:
        break;
    }
    ++watch.heroes_by_a_script;
    /* The spawner's own script is the scene's actor; the hosted actor has no body yet and waits
     * for the grab, which the scene holds from here while the host is brought, before it can be
     * ticked. */
    on_the_hero(actor, index);
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

/* Whether the five bytes in front of `return_address` call `callee`, decoded the way the camera's
 * callers are. */
static bool calls(uintptr_t return_address, uintptr_t callee)
{
    mp_scene_call_site_t call   = { 0u, { 0u } };
    uintptr_t            target = 0u;

    if (return_address == 0u || callee == 0u) {
        return false;
    }
    call.return_address = return_address;
    (void)memory_read(return_address - MP_SCENE_CALL_BYTES, call.call, MP_SCENE_CALL_BYTES);
    return mp_scene_camera_callee(&call, 1u, &target) == MP_SCENE_CALLEE_AGREED &&
           target == callee;
}

/* Where the script's warp returns to, found by its own bytes and proved by its own call to the
 * respawn this feature resolved. Nought when either fails, and the warp is then not heard. */
static uintptr_t find_the_warp(void)
{
    uintptr_t respawn = mp_signatures_address(MP_SITE_PLAYER_RESPAWN_AT);
    uintptr_t site    = signature_find_unique(SIG_MP_WARP_RESPAWN_CALL, MSK_MP_WARP_RESPAWN_CALL,
                                              sizeof SIG_MP_WARP_RESPAWN_CALL);

    if (site == 0u || !calls(site + WARP_RESPAWN_CALL_RETURN, respawn)) {
        return 0u;
    }
    return site + WARP_RESPAWN_CALL_RETURN;
}

/* The lock's door is its raise by the lock opcode, ten bytes past that opcode's camera take, and
 * the call there has to name the lock. It is heard only with the lock's level cell known
 * (mp_cutscene derives it), because the level is what says whether a raise begins a scene.
 * Anything short of that leaves the door at nought, and every raise then goes through. */
static void resolve_the_doors(const mp_cutscene_doors_t *doors)
{
    uintptr_t lock = doors->lock_entry;

    if (lock == 0u || mp_cutscene_lock_level_cell() == 0u) {
        return;
    }
    if (doors->lock_take_return != 0u &&
        calls(doors->lock_take_return + LOCK_RAISE_PAST_THE_TAKE, lock)) {
        watch.lock_return = doors->lock_take_return + LOCK_RAISE_PAST_THE_TAKE;
    }
}

static const char *heard(bool yes)
{
    return yes ? "heard" : "NOT HEARD";
}

bool mp_scene_watch_install(void)
{
    uintptr_t           site;
    size_t              prologue;
    mp_cutscene_doors_t doors = { 0u, 0u, 0u };
    bool                warp_heard;
    bool                hero_heard;

    if (watch.installed) {
        return true;
    }
    site     = mp_signatures_address(MP_SITE_AI_RUN);
    prologue = mp_signatures_prologue(MP_SITE_AI_RUN);
    if (site == 0u || prologue == 0u ||
        !detour_install(&watch.hull, site, (const void *)&hook_ai_run, prologue)) {
        log_warning("the scene watch did not bind: ai_run did not resolve or refused the hull, so "
                    "no script's door is judged in this session: every script takes and gives "
                    "back on the host as it does alone");
        return false;
    }
    watch.original  = (ai_run_fn_t)watch.hull.original;
    watch.installed = true;

    /* Every door's address before its listener, and every listener after the hull, so that no
     * door is heard before the actor behind it can be. The cutscene hands its doors over with the
     * listener, and a door heard before they are resolved matches nothing. */
    watch.warp_return = find_the_warp();
    mp_cutscene_set_door_listener(&on_scene_door, &doors);
    resolve_the_doors(&doors);
    warp_heard = mp_lifecycle_set_respawn_listener(&on_respawn) && watch.warp_return != 0u;
    hero_heard = mp_arena_set_placement_listener(&on_spawn);
    mp_body_passable_set_script_probe(&a_script_runs);
    log_info("the scene watch is bound at ai_run %08X: it tells whose script runs, and every door "
             "a script takes or gives back through on the host is judged by whose its run is. "
             "Its doors: the lock's raise by the lock opcode %s (returns to %08X), the camera of "
             "the dolly %s (returns to %08X), the warp %s (returns to %08X), the hero as an "
             "actor %s",
             (unsigned)site, heard(watch.lock_return != 0u), (unsigned)watch.lock_return,
             heard(doors.dolly_take_return != 0u), (unsigned)doors.dolly_take_return,
             heard(warp_heard), (unsigned)watch.warp_return, heard(hero_heard));
    return true;
}

static uint32_t far_doors_of(far_door_t why)
{
    return watch.far_doors[why][KIND_LOCK] + watch.far_doors[why][KIND_HERO];
}

void mp_scene_watch_report(void)
{
    uint32_t began;

    if (!watch.installed) {
        log_info("the scene watch is not bound: no script's door is judged");
        return;
    }
    if (!watch.hosted) {
        log_info("the scene watch is bound and idle: no script ran here while this side was "
                 "hosting, and a client is in no scene; %u warp(s) of a script of this machine "
                 "refused on a client", (unsigned)watch.client_warps_refused);
        return;
    }
    began = watch.began[KIND_LOCK] + watch.began[KIND_HERO] + watch.began[KIND_WARP];
    log_info("the scenes (the host): %u began (%u by a lock, %u by the hero as an actor, %u by a "
             "warp), %u of them met a far player first (%u by the actor's own last answer, %u by "
             "the last attacker, %u by the player its placement woke for), %u while the host lay "
             "dead, the farthest trigger %.2f u away",
             (unsigned)began, (unsigned)watch.began[KIND_LOCK], (unsigned)watch.began[KIND_HERO],
             (unsigned)watch.began[KIND_WARP],
             (unsigned)(watch.far_by_rule[MP_SCENE_RUN_BY_OWN_ANSWER] +
                        watch.far_by_rule[MP_SCENE_RUN_BY_LAST_ATTACKER] +
                        watch.far_by_rule[MP_SCENE_RUN_BY_THE_WAKING]),
             (unsigned)watch.far_by_rule[MP_SCENE_RUN_BY_OWN_ANSWER],
             (unsigned)watch.far_by_rule[MP_SCENE_RUN_BY_LAST_ATTACKER],
             (unsigned)watch.far_by_rule[MP_SCENE_RUN_BY_THE_WAKING], (unsigned)watch.host_dead,
             (double)watch.farthest);
    log_info("the triggers of the scenes (the host): %u by the actor's own last answer, %u by the "
             "last attacker, %u by the player its placement woke for, %u with the host as the "
             "anchor, %u by an actor of the host's own scene, %u by an actor that had taken here "
             "and not given back, %u with nobody else in the session; where the actor had no "
             "fresh player answer, %u had none on record, %u a stale one and %u one that was not "
             "a player (an ally or nobody); %u door(s) opened with no script running, %u event "
             "line(s) left out after the first %u (%u of a lock, %u of the hero as an actor, %u "
             "of a warp)",
             (unsigned)watch.by_rule[MP_SCENE_RUN_BY_OWN_ANSWER],
             (unsigned)watch.by_rule[MP_SCENE_RUN_BY_LAST_ATTACKER],
             (unsigned)watch.by_rule[MP_SCENE_RUN_BY_THE_WAKING],
             (unsigned)watch.by_rule[MP_SCENE_RUN_BY_HOST_ANCHOR],
             (unsigned)watch.by_rule[MP_SCENE_RUN_OF_THE_SCENE],
             (unsigned)watch.by_rule[MP_SCENE_RUN_OF_A_TAKER],
             (unsigned)watch.by_rule[MP_SCENE_RUN_ALONE], (unsigned)watch.own_none,
             (unsigned)watch.own_stale, (unsigned)watch.own_not_a_player,
             (unsigned)watch.outside_a_script,
             (unsigned)(watch.lines_left_out[KIND_LOCK] + watch.lines_left_out[KIND_HERO] +
                        watch.lines_left_out[KIND_WARP]),
             (unsigned)EVENT_LINES_MAX, (unsigned)watch.lines_left_out[KIND_LOCK],
             (unsigned)watch.lines_left_out[KIND_HERO], (unsigned)watch.lines_left_out[KIND_WARP]);
    log_info("the hero's placements (the host): %u spawned with the handover flag, %u by a "
             "script, %u by the engine outside a script (a savegame being restored), %u by a DLL; "
             "%u of the script's by the scene's own actor right behind its lock, which made that "
             "scene a hero's; %u spawn record(s) did not read",
             (unsigned)watch.heroes, (unsigned)watch.heroes_by_a_script,
             (unsigned)watch.heroes_by_the_engine, (unsigned)watch.heroes_by_a_dll,
             (unsigned)watch.heroes_behind_the_lock, (unsigned)watch.spawns_unreadable);
    log_info("the doors of a far player's scenes (the host): %u became the host's (%u by a lock, "
             "%u by the hero as an actor); %u left alone while a scene of the host's stood (%u "
             "lock(s) refused, %u hero(es) left to the engine); %u the host's scene did not begin "
             "with (%u lock(s) refused, %u hero(es) left to the engine); %u lock(s) refused over "
             "a lock that stood with no scene of the host's; %u lock(s) and %u hero(es) of a "
             "placement whose doors are refused; %u warp(s) of a script of this machine refused "
             "while it was a client",
             (unsigned)far_doors_of(FAR_DOOR_THE_HOSTS),
             (unsigned)watch.far_doors[FAR_DOOR_THE_HOSTS][KIND_LOCK],
             (unsigned)watch.far_doors[FAR_DOOR_THE_HOSTS][KIND_HERO],
             (unsigned)far_doors_of(FAR_DOOR_IN_A_SCENE),
             (unsigned)watch.far_doors[FAR_DOOR_IN_A_SCENE][KIND_LOCK],
             (unsigned)watch.far_doors[FAR_DOOR_IN_A_SCENE][KIND_HERO],
             (unsigned)far_doors_of(FAR_DOOR_NOT_BEGUN),
             (unsigned)watch.far_doors[FAR_DOOR_NOT_BEGUN][KIND_LOCK],
             (unsigned)watch.far_doors[FAR_DOOR_NOT_BEGUN][KIND_HERO],
             (unsigned)watch.far_doors[FAR_DOOR_OVER_A_LOCK][KIND_LOCK],
             (unsigned)watch.latched[KIND_LOCK], (unsigned)watch.latched[KIND_HERO],
             (unsigned)watch.client_warps_refused);
    log_info("the heroes a player's own release keeps off the host (the host): %u hero(es) of a "
             "spawner written down, written down with it; %u hero(es) spawned on a placement "
             "written down, which began no scene; a grab was asked %u time(s) with a placement "
             "written down and the actor that asked not found, and let through",
             (unsigned)watch.heroes_written_down, (unsigned)watch.heroes_on_a_written,
             (unsigned)watch.grab_askers_unknown);
}
