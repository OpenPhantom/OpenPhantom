/* mp_scene_watch.c: which player a scene's script meant, counted on the host. See the header. */
#include "mp_scene_watch.h"

#include "mp_arena.h"
#include "mp_body.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_enemy_bind.h"
#include "mp_lifecycle.h"
#include "mp_placements.h"
#include "mp_scene_bind.h"
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

/* The actor's own health: the field the engine's wait for a death, opcode 0x102, holds against
 * nought. An actor at or below it is one whose death a script is reacting to. */
#define ACTOR_HEALTH 0x38u

/* A scene's own line is written this many times in a process, and after that only counted. Scenes
 * are a handful per level, so the cap is there for a script that loops, not for the field. */
#define EVENT_LINES_MAX 64u

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
    KIND_CAMERA,
    KIND_COUNT
} scene_kind_t;

#define RULE_COUNT 3u

typedef struct scene_watch {
    bool        installed;
    bool        hosted;        /* a script has run here while this side was hosting */
    detour_t    hull;
    ai_run_fn_t original;
    uintptr_t   running;       /* the actor whose script runs now, nought outside every script */

    /* Where each door returns to, nought for a door that is not heard. */
    uintptr_t   lock_return;
    uintptr_t   camera_return;
    uintptr_t   warp_return;

    uint32_t began[KIND_COUNT];
    uint32_t by_rule[RULE_COUNT];
    uint32_t far_by_rule[RULE_COUNT];   /* of those, the rule named a far player */
    uint32_t own_none;                  /* why the actor's own answer did not decide */
    uint32_t own_stale;
    uint32_t own_not_a_player;
    uint32_t outside_a_script;          /* a door opened with no script running */
    uint32_t camera_in_a_lock;          /* a camera move inside a locked scene */
    uint32_t camera_for_all;            /* and inside a scene that runs for everybody */
    uint32_t lines_written;
    uint32_t lines_left_out;
    float    farthest;                  /* the farthest a far trigger stood from the host */

    uint32_t heroes;                    /* spawns of a placement carrying the handover flag */
    uint32_t heroes_by_a_script;
    uint32_t heroes_by_the_engine;      /* from inside the engine with no script running */
    uint32_t heroes_by_a_dll;
    uint32_t spawns_unreadable;

    uint32_t host_dead;                 /* began while the host lay dead */
    uint32_t cameras_kept;              /* a far player's camera alone, kept off the host */
    uint32_t cameras_let;
    bool     camera_said;
} scene_watch_t;

static scene_watch_t watch;

static const char *const KIND_TEXT[KIND_COUNT] = {
    "a lock", "the hero as an actor", "a warp", "the camera alone"
};

static const char *const RULE_TEXT[RULE_COUNT] = {
    "the actor's own last answer", "the last attacker", "the host as the anchor"
};

/* ==============================================================================================
 * The actor whose script runs.
 * ============================================================================================ */

/* Kept across a nested call rather than cleared behind it, so a script that ever ran another
 * actor's script from inside its own would give the outer one back.
 *
 * The actor whose script began a scene is held here while the players are gathered for it, so the
 * scene's first line is not spoken before everybody is there: until every player stands at its
 * seat or the host has stood for a second and a half, and while the host lies dead until he stands
 * again, twenty seconds at the most; with nobody else in the session not at all. Nothing else of
 * the actor stops; the one caller reads no answer.
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
    watch.hosted  = true;
    outer         = watch.running;
    watch.running = actor;
    answer        = watch.original(actor);
    watch.running = outer;
    return answer;
}

/* ==============================================================================================
 * A scene begins.
 * ============================================================================================ */

static mp_scene_evidence_t evidence_of(uintptr_t actor)
{
    mp_scene_evidence_t evidence;
    bool                player    = false;
    uint8_t             bank      = 0u;
    uint32_t            age       = 0u;
    bool                on_record = mp_target_last_answer(actor, &player, &bank, &age);
    int32_t             health    = 1;

    evidence.own            = mp_scene_answer_of(on_record, player, age);
    evidence.own_bank       = bank;
    evidence.died           = memory_try_read(actor + ACTOR_HEALTH, &health, sizeof health) &&
                              health <= 0;
    evidence.attacker_bank  = 0u;
    evidence.attacker_known = mp_target_last_attacker(actor, &evidence.attacker_bank);
    return evidence;
}

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

static void say_the_scene(scene_kind_t kind, int32_t hero_placement, uintptr_t actor,
                          uint8_t bank, mp_scene_trigger_rule_t rule, const float at[3],
                          bool placed, float distance, bool host_dead)
{
    const char *host = host_dead ? "the host lay dead" : "the host stood";
    char     what[64];
    char     slot_text[16];
    uint8_t  slot = 0u;
    uint32_t key  = 0u;
    int32_t  placement = mp_enemy_bind_index(actor, &key) ? (int32_t)key : -1;

    if (watch.lines_written >= EVENT_LINES_MAX) {
        ++watch.lines_left_out;
        return;
    }
    ++watch.lines_written;
    if (kind == KIND_HERO) {
        (void)text_format(what, sizeof what, "%s, the hero on placement %d", KIND_TEXT[kind],
                          (int)hero_placement);
    } else {
        (void)text_format(what, sizeof what, "%s", KIND_TEXT[kind]);
    }
    if (mp_body_bank_slot(bank, &slot)) {
        (void)text_format(slot_text, sizeof slot_text, "%u", (unsigned)slot);
    } else {
        (void)text_format(slot_text, sizeof slot_text, "unknown");
    }
    if (!placed) {
        log_info("a scene began here: placement %d, %s, its script meant world slot %s by %s, at "
                 "a place this machine could not read, %s", (int)placement, what, slot_text,
                 RULE_TEXT[rule], host);
        return;
    }
    log_info("a scene began here: placement %d, %s, its script meant world slot %s by %s, at "
             "%.2f %.2f %.2f, %.2f u from the host, %s", (int)placement, what, slot_text,
             RULE_TEXT[rule], (double)at[0], (double)at[1], (double)at[2], (double)distance,
             host);
}

/* The one place a beginning is counted. Every door comes here, so the rule, the counters and the
 * line cannot come to different answers for the same scene, and the gathering and the camera's
 * verdict are handed the same player the line names. `hero_placement` is the placement the hero is
 * put on, for that door only. False for a door opened with no script running, which is no scene. */
static bool scene_began(scene_kind_t kind, int32_t hero_placement, uint8_t *meant)
{
    uintptr_t               actor = watch.running;
    mp_scene_evidence_t     evidence;
    mp_scene_trigger_rule_t rule;
    uint8_t                 bank    = 0u;
    float                   at[3]   = { 0.0f, 0.0f, 0.0f };
    float                   host[3] = { 0.0f, 0.0f, 0.0f };
    float                   distance = 0.0f;
    bool                    placed;
    bool                    host_dead;

    *meant = 0u;
    if (actor == 0u) {
        ++watch.outside_a_script;
        return false;
    }
    ++watch.began[kind];
    host_dead = !mp_scene_bind_player_stands();   /* the gathering's own predicate */
    if (host_dead) {
        ++watch.host_dead;
    }
    evidence = evidence_of(actor);
    rule     = mp_scene_trigger(&evidence, &bank);
    ++watch.by_rule[rule];
    if (rule != MP_SCENE_BY_OWN_ANSWER) {
        count_why_not_its_own(evidence.own);
    }
    placed = mp_target_player_position(bank, at) && mp_target_player_position(0u, host);
    if (placed) {
        float dx = at[0] - host[0];
        float dy = at[1] - host[1];
        float dz = at[2] - host[2];

        distance = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    if (bank != 0u) {
        ++watch.far_by_rule[rule];
        if (placed && distance > watch.farthest) {
            watch.farthest = distance;
        }
    }
    say_the_scene(kind, hero_placement, actor, bank, rule, at, placed, distance, host_dead);
    *meant = bank;
    return true;
}

/* ==============================================================================================
 * The doors.
 * ============================================================================================ */

/* The lock is a scene when the lock opcode raises it from below five: a second raise inside a
 * running scene raises nothing, and the menu's level one on every render is no scene. It is told
 * before the raise is made, so the level read here is the one it stood at, and the gathering begins
 * before the lock does. The camera dolly is a scene only while no lock is up and no scene runs for
 * everybody: inside either it is that scene moving its camera (mp_scene_camera_owner).
 *
 * The camera alone is refused here when the player its script meant is a far one. It is no scene of
 * its own and gathers nobody, and on the host it would swing the host's view to a place a far
 * player stands. The answer comes from the same rule as the line, never from a second question. A
 * take inside a locked scene or a scene for everybody is that scene's and always goes. */
static bool on_scene_door(mp_cutscene_door_t door, uintptr_t caller, int32_t argument)
{
    int32_t level;
    uint8_t bank = 0u;

    if (!mp_target_hosting()) {
        return true;
    }
    if (door == MP_CUTSCENE_DOOR_LOCK) {
        if (watch.lock_return == 0u || caller != watch.lock_return ||
            argument < MP_CUTSCENE_LOCK_LEVEL) {
            return true;
        }
        level = mp_cutscene_lock_level();
        if (level >= 0 && level < MP_CUTSCENE_LOCK_LEVEL && scene_began(KIND_LOCK, -1, &bank)) {
            mp_scene_host_began(MP_SCENE_KIND_LOCK, watch.running, bank, 0, NULL, 0.0f);
        }
        return true;
    }
    if (watch.camera_return == 0u || caller != watch.camera_return) {
        return true;
    }
    switch (mp_scene_camera_owner(mp_cutscene_lock_level(), mp_scene_for_all(NULL))) {
    case MP_SCENE_CAMERA_OF_THE_LOCK:
        ++watch.camera_in_a_lock;
        return true;
    case MP_SCENE_CAMERA_OF_ALL:
        ++watch.camera_for_all;
        return true;
    case MP_SCENE_CAMERA_OF_ITS_OWN:
    default:
        break;
    }
    if (!scene_began(KIND_CAMERA, -1, &bank) || bank == 0u) {
        ++watch.cameras_let;
        return true;
    }
    ++watch.cameras_kept;
    if (!watch.camera_said) {
        uint8_t slot = 0u;

        watch.camera_said = true;
        log_info("the camera alone was refused on the host: its scene was meant for world slot "
                 "%u, a far player, so the host's view stays its own; later ones are counted",
                 mp_body_bank_slot(bank, &slot) ? (unsigned)slot : 255u);
    }
    return false;
}

/* Every respawn the host is asked for is sorted by who asked; a script's warp is a scene, which the
 * gathering follows with the target the engine is about to take. */
static void on_respawn(uintptr_t caller, int32_t hero, const float *at, float heading)
{
    mp_scene_respawn_caller_t who;
    uint8_t                   bank = 0u;

    if (!mp_target_hosting()) {
        return;
    }
    who = mp_scene_respawn_caller(caller, watch.warp_return, mp_scene_bind_swap_return(),
                                  memory_is_inside_image(caller, 1u));
    mp_scene_host_note_respawn(who);
    if (who == MP_SCENE_RESPAWN_BY_WARP && scene_began(KIND_WARP, -1, &bank)) {
        mp_scene_host_began(MP_SCENE_KIND_WARP, watch.running, bank, hero, at, heading);
    }
}

/* A placement carrying the handover flag puts the hero on it. From a script it is a scene, and the
 * actor running that script is the one that spawned it, not the placement it spawned. From inside
 * the engine with no script running it is a savegame being restored, and from outside the engine
 * it is a DLL; both are counted and neither is a scene. */
static void on_spawn(uintptr_t placement, int32_t index, uintptr_t caller)
{
    mp_placement_t record;
    uint8_t        bank = 0u;

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
    switch (mp_scene_hero_origin(memory_is_inside_image(caller, 1u), watch.running != 0u)) {
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
     * for the grab, which the gathering holds from here, before it can be ticked. */
    if (scene_began(KIND_HERO, index, &bank)) {
        mp_scene_host_began(MP_SCENE_KIND_HERO, watch.running, bank, 0, NULL, 0.0f);
    }
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
 * the call there has to name the lock. Both doors are heard only with the lock's level cell known
 * (mp_cutscene derives it), because the level is what says whether a raise begins a scene and
 * whether a dolly's take is a scene of its own. Anything short of that leaves a door at nought. */
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
    watch.camera_return = doors->dolly_take_return;
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
                    "no scene is attributed to a player in this session");
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
    log_info("the scene watch is bound at ai_run %08X: it reads the actor whose script runs, "
             "counts which player a scene's script meant and hands that player to the gathering, "
             "which may hold the actor. Its doors: the lock %s (returns to %08X), the camera alone "
             "%s (returns to %08X), the warp %s (returns to %08X), the hero as an actor %s",
             (unsigned)site, heard(watch.lock_return != 0u), (unsigned)watch.lock_return,
             heard(watch.camera_return != 0u), (unsigned)watch.camera_return, heard(warp_heard),
             (unsigned)watch.warp_return, heard(hero_heard));
    return true;
}

void mp_scene_watch_report(void)
{
    uint32_t began;

    if (!watch.installed) {
        log_info("the scene watch is not bound: no scene is attributed to a player");
        return;
    }
    if (!watch.hosted) {
        log_info("the scene watch is bound and idle: no script ran here while this side was "
                 "hosting, and a client's scenes belong to the host");
        return;
    }
    began = watch.began[KIND_LOCK] + watch.began[KIND_HERO] + watch.began[KIND_WARP] +
            watch.began[KIND_CAMERA];
    log_info("the scenes (the host): %u began (%u by a lock, %u by the hero as an actor, %u by a "
             "warp, %u by the camera alone), %u of them met a far player first (%u by the actor's "
             "own last answer, %u by the last attacker), %u while the host lay dead, the farthest "
             "trigger %.2f u away",
             (unsigned)began, (unsigned)watch.began[KIND_LOCK], (unsigned)watch.began[KIND_HERO],
             (unsigned)watch.began[KIND_WARP], (unsigned)watch.began[KIND_CAMERA],
             (unsigned)(watch.far_by_rule[MP_SCENE_BY_OWN_ANSWER] +
                        watch.far_by_rule[MP_SCENE_BY_LAST_ATTACKER]),
             (unsigned)watch.far_by_rule[MP_SCENE_BY_OWN_ANSWER],
             (unsigned)watch.far_by_rule[MP_SCENE_BY_LAST_ATTACKER], (unsigned)watch.host_dead,
             (double)watch.farthest);
    log_info("the triggers of the scenes (the host): %u by the actor's own last answer, %u by the "
             "last attacker, %u with the host as the anchor; where the actor had no fresh player "
             "answer, %u had none on record, %u a stale one and %u one that was not a player (an "
             "ally or nobody); %u camera move(s) inside a locked scene were no scene of their own; "
             "%u door(s) opened with no script running, %u event line(s) left out after the first "
             "%u",
             (unsigned)watch.by_rule[MP_SCENE_BY_OWN_ANSWER],
             (unsigned)watch.by_rule[MP_SCENE_BY_LAST_ATTACKER],
             (unsigned)watch.by_rule[MP_SCENE_BY_HOST_ANCHOR], (unsigned)watch.own_none,
             (unsigned)watch.own_stale, (unsigned)watch.own_not_a_player,
             (unsigned)watch.camera_in_a_lock, (unsigned)watch.outside_a_script,
             (unsigned)watch.lines_left_out, (unsigned)EVENT_LINES_MAX);
    log_info("the hero's placements (the host): %u spawned with the handover flag, %u by a "
             "script, %u by the engine outside a script (a savegame being restored), %u by a DLL; "
             "%u spawn record(s) did not read",
             (unsigned)watch.heroes, (unsigned)watch.heroes_by_a_script,
             (unsigned)watch.heroes_by_the_engine, (unsigned)watch.heroes_by_a_dll,
             (unsigned)watch.spawns_unreadable);
    log_info("the camera alone (the host): %u take(s) refused because a far player's scene would "
             "have moved the host's view, %u let through; %u take(s) inside a scene that runs for "
             "everybody were that scene's and went", (unsigned)watch.cameras_kept,
             (unsigned)watch.cameras_let, (unsigned)watch.camera_for_all);
}
