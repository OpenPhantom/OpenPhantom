/* mp_cutscene.c: the doors a scene comes through, held shut while an arena runs and while a
 * scene on this machine belongs to the host. See the header for why the lock is refused rather
 * than released, why the cell is the wrong lever, and the rule the whole file is held to.
 *
 * SIZE NOTE: five sites and five hulls. The grab of the hero and its put-back stay in one file
 * on purpose, because a pair split over two files is the very mistake the rule in the header
 * exists to stop. The seams taken instead are the pure arithmetic, in mp_scene_rule, and the
 * patterns, in mp_signatures_scene, whose comments carry the byte evidence of every site.
 */
#include "mp_cutscene.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_scene_rule.h"
#include "mp_signatures_scene.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A jmp rel32, which is what every detour in this tree leaves on the head it takes. The camera
 * hull reads the head before it installs its own, because afterwards the byte is its own. */
#define SCENE_BRANCH_OPCODE 0xE9u

/* Four of the five take one int and are cdecl. The return is carried through rather than dropped
 * even where the engine's own body sets none: a prototype that throws a return away is a trap
 * this tree has already paid for, and passing whatever is in the register costs nothing. */
typedef int32_t(__cdecl *scene_fn_t)(int32_t argument);

/* The put-back takes nothing at all, and its return is carried through for the same reason. */
typedef int32_t(__cdecl *scene_putback_fn_t)(void);

/* A site matched only to learn the address behind its call. */
typedef struct scene_take {
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t         size;
    size_t         after_the_call;
    const char    *what;
} scene_take_t;

static const scene_take_t SCENE_TAKES[] = {
    { SIG_SCENE_DOLLY_TAKE, MSK_SCENE_DOLLY_TAKE, sizeof SIG_SCENE_DOLLY_TAKE,
      SCENE_DOLLY_TAKE_RETURN, "the camera dolly opcode" },
    { SIG_SCENE_LOCK_TAKE, MSK_SCENE_LOCK_TAKE, sizeof SIG_SCENE_LOCK_TAKE,
      SCENE_LOCK_TAKE_RETURN, "the lock player opcode" },
    { SIG_SCENE_SPEAK_TAKE, MSK_SCENE_SPEAK_TAKE, sizeof SIG_SCENE_SPEAK_TAKE,
      SCENE_SPEAK_TAKE_RETURN, "a spoken line" },
};

#define SCENE_TAKE_COUNT (sizeof SCENE_TAKES / sizeof SCENE_TAKES[0])

/* The two of those a door listener is told where they stand, in the order above, and the spoken
 * line's, whose camera the judgement of the line decides. */
#define SCENE_TAKE_DOLLY 0u
#define SCENE_TAKE_LOCK  1u
#define SCENE_TAKE_SPEAK 2u

typedef struct cutscene {
    bool     installed;
    bool     suppressed;      /* the arena: a deathmatch has no scenes at all */
    bool     client_holds;    /* a client in a session: a scene belongs to the host */
    bool     gather_holds;    /* a host gathering the players: the grab waits for them */

    detour_t lock_hull;
    detour_t letterbox_hull;
    detour_t suspend_hull;
    detour_t putback_hull;
    detour_t camera_hull;
    bool     lock_bound;
    bool     letterbox_bound;
    bool     suspend_bound;
    bool     putback_bound;
    bool     camera_bound;

    /* The player record pointer, read again on every put-back rather than followed once: the
     * bank moves it while a far body is being written, so a pointer cached here would be the
     * wrong record for as long as that lasts. */
    uintptr_t pr_cell;
    uintptr_t script_returns[SCENE_TAKE_COUNT];
    size_t    script_return_count;
    size_t    camera_operands;        /* the calls that named the camera take, all agreeing */
    bool      camera_pattern_agrees;  /* and its own pattern resolved at the same address */
    bool      camera_head_branched;   /* its head was another module's branch already */
    uintptr_t take_return[SCENE_TAKE_COUNT];   /* by take, nought where it did not resolve */

    mp_cutscene_door_listener_t door_listener;
    mp_cutscene_line_camera_fn_t line_camera;

    uint32_t locks_refused;
    uint32_t locks_passed;
    uint32_t letterboxes_refused;
    uint32_t locks_refused_for_the_host;   /* and the same three, counted by the second reason */
    uint32_t letterboxes_refused_for_the_host;
    uint32_t suspends_refused;
    uint32_t suspends_passed;
    uint32_t putbacks_refused;
    uint32_t putbacks_passed;
    uint32_t putbacks_unread;    /* let through because the store could not be read at all */
    uint32_t modules_freed;      /* refused, and the stopped module set running again */
    uint32_t modules_not_freed;  /* idle, and left alone: no body, or a swapped bank */
    uint32_t cameras_refused;
    uint32_t cameras_passed;
    uint32_t cameras_refused_far;   /* the host's view kept from a far player's scene */
    uint32_t grabs;                 /* the engine's grab answered 1: a hero taken for a scene */
    uint32_t grabs_held;            /* asked while a gathering held it */
    uint32_t putbacks_held;         /* refused while a gathering held the grab */
    bool     putback_logged;
    bool     camera_logged;
} cutscene_t;

static cutscene_t scene;

/* ==============================================================================================
 * The hulls.
 * ============================================================================================ */

/* A menu takes the lock at level one on every render it draws, and that is not a scene: refusing
 * it would leave a client unable to open its own pause screen. Only the level a script takes is
 * held back. */
#define SCENE_LOCK_LEVEL MP_CUTSCENE_LOCK_LEVEL

/* A raise let through is told to the door listener before it is made, so the listener can still
 * read the level the lock stood at.
 *
 * engine: int Dialog_EnterInputLock(int level) */
static int32_t __cdecl hook_lock_enter(int32_t level)
{
    scene_fn_t original = (scene_fn_t)scene.lock_hull.original;

    if (scene.client_holds && !scene.suppressed && level >= SCENE_LOCK_LEVEL) {
        ++scene.locks_refused_for_the_host;
        return 0;   /* the scene is the host's, and this side is not in it */
    }
    if (!scene.suppressed) {
        ++scene.locks_passed;
        if (scene.door_listener != NULL) {
            (void)scene.door_listener(MP_CUTSCENE_DOOR_LOCK, (uintptr_t)_ReturnAddress(), level);
        }
        return original(level);
    }
    /* Nought is the answer the engine's own body gives for "the lock was already held", which is
     * the nearest true thing to say: this side did not take it either. Nothing else is touched,
     * so the release that may arrive later finds a level of zero and does nothing, which is
     * exactly what it should do for a lock nobody took. */
    ++scene.locks_refused;
    return 0;
}

static int32_t __cdecl hook_letterbox(int32_t on)
{
    scene_fn_t original = (scene_fn_t)scene.letterbox_hull.original;

    /* Only the drawing is refused, never the clearing. The same function does both directions,
     * and refusing both would mean that bars already on screen when an arena begins stay there
     * for the whole match with nothing able to take them down. */
    if (on == 0) {
        return original(on);
    }
    if (scene.client_holds && !scene.suppressed) {
        ++scene.letterboxes_refused_for_the_host;
        return 0;
    }
    if (!scene.suppressed) {
        return original(on);
    }
    ++scene.letterboxes_refused;
    return 0;
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* The hero as an actor, refused for the same reason and with the engine's own answer. A host that
 * gathers the players answers "not yet" the same way, and the engine asks again next substep. */
static int32_t __cdecl hook_suspend(int32_t actor)
{
    scene_fn_t original = (scene_fn_t)scene.suspend_hull.original;
    int32_t    answer;

    if (mp_scene_hero_is_gated_here(scene.client_holds, scene.suppressed, scene.gather_holds)) {
        if (scene.gather_holds && !scene.client_holds) {
            ++scene.grabs_held;
        } else {
            ++scene.suspends_refused;
        }
        return 0;
    }
    ++scene.suspends_passed;
    answer = original(actor);
    if (answer != 0) {
        ++scene.grabs;
    }
    return answer;
}

/* Whether a grab is standing on this machine, asked of the engine's own store of it.
 *
 * `readable` says whether the question could be put at all. A gate that cannot read its own
 * question must not answer it, so an unreadable store answers "allowed" and the caller counts
 * the case apart: that leaves the engine exactly as it behaved before this hull existed. */
static bool putback_allowed_here(bool *readable)
{
    uint32_t block = 0u;
    uint32_t saved = 0u;

    *readable = false;
    if (scene.pr_cell == 0u || !memory_read_u32(scene.pr_cell, &block) || block == 0u) {
        return true;
    }
    if (!memory_read_u32((uintptr_t)block + MP_HERO_BLOCK_SAVED_MODULE_STATE, &saved)) {
        return true;
    }
    *readable = true;
    return mp_scene_putback_allowed(saved);
}

/* Refusing is half a repair, and the field showed which half was missing.
 *
 * The refusal below stops a put-back writing nought over a running module. It does nothing at all
 * for a module that is ALREADY stopped with nothing parked, and on a client that is the state that
 * matters: the run of 2026-09-20 had one put-back refused, the module standing at nought for 448
 * substeps, and the player unable to move for the rest of the level. Nothing inside a level ever
 * writes that cell back to running, so refusing a write of nought onto nought left the client
 * exactly where it was.
 *
 * So where the pair is broken and the module is stopped, it is set running here. One is what the
 * engine's own respawn and every placement gate read as "a player exists and is not parked"
 * (mp_cells.h), and it is the value the module carries for the whole of an ordinary level.
 *
 * Only where nothing is parked. A module stopped WITH something parked is an ordinary scene in
 * progress, and the put-back that ends it is let through a few lines down. */
static bool give_the_module_back(void)
{
    uint32_t block = 0u;
    uint32_t state = 0u;
    uint32_t actor = 0u;

    /* THE BANK FIRST, because this one WRITES. The cell is swung to a far body's block while one
     * is being written, and reading the wrong block only answers wrongly; writing it puts this
     * repair into another player's record and leaves the one it was for exactly as it was. */
    if (mp_bank_is_swapped()) {
        ++scene.modules_not_freed;
        return false;
    }
    if (scene.pr_cell == 0u || !memory_read_u32(scene.pr_cell, &block) || block == 0u) {
        return false;
    }
    if (!memory_read_u32((uintptr_t)block + MP_HERO_BLOCK_MODULE_STATE, &state) || state != 0u) {
        return false;   /* not idle: 1 runs, and 2, 3 and 4 are quitting, dying and respawning */
    }
    /* AND A BODY, which is the guard this file owed. Nought is also the state between a despawn
     * and the next spawn, and a level end reaches it on every client: the player module is told
     * first and despawns, the enemy module is told second and its close deletes every actor, and
     * deleting an actor that hosts the player (state flag 0x2000) calls the put-back. Setting a
     * module running there starts the player's phases
     * on a player who has no body at all. The same test, for the same reason, is already made
     * where this side's own body is read (mp_bridge_world.c). */
    if (!memory_read_u32((uintptr_t)block + MP_HERO_BLOCK_OBJECT, &actor) || actor == 0u) {
        ++scene.modules_not_freed;
        return false;
    }
    if (!memory_try_write((uintptr_t)block + MP_HERO_BLOCK_MODULE_STATE,
                          &(uint32_t){ MP_HERO_MODULE_RUNNING }, sizeof(uint32_t))) {
        return false;
    }
    ++scene.modules_freed;
    return true;
}

/* The other half of the pair, and it asks TWO questions where it used to ask one.
 *
 * Second question first, because it is the one that was missing: is the hero this module's
 * business on this machine at all? Only where the grab is refused, which is a client holding back
 * a scene that belongs to the host. A host grabs and puts back exactly as the retail game does,
 * and the field run of 2026-09-20 is why that sentence is here: with this half unasked, a host
 * refused its own put-back at the start of a scene and stood in the caption fade with a black
 * screen. The one caller is the removal path and it asks for every placement carrying the handover
 * flag, whether a grab ever happened or not, so at the start of a scene the engine's store reads
 * nought on a perfectly healthy host.
 *
 * Then the first question, which is the rule this module was built for: a put-back goes through
 * only where a grab is standing. The predicate is the engine's store and not a mark of this
 * file's, so on a client it covers all three ways the pair can break: a grab this file refused, a
 * grab the engine itself refused because the player was in the air or on a gun, and a removal that
 * arrives for a placement nobody ever stood on. It needs no watchdog at the end of a scene and no
 * time limit, because the condition is the watchman.
 *
 * Nought is the answer the engine's own body gives for a player with no body, and the single
 * caller throws the answer away either way. */
static int32_t __cdecl hook_putback(void)
{
    uintptr_t caller   = (uintptr_t)_ReturnAddress();
    bool      readable = false;

    if (!mp_scene_hero_is_gated_here(scene.client_holds, scene.suppressed, scene.gather_holds)) {
        ++scene.putbacks_passed;
        return ((scene_putback_fn_t)scene.putback_hull.original)();
    }
    if (!putback_allowed_here(&readable)) {
        bool freed;

        /* A host that gathers holds a grab it has not made: its module runs, and the refused
         * put-back leaves it running. Counted apart, so the line the field runs carry keeps its
         * meaning. */
        if (scene.gather_holds && !scene.client_holds) {
            ++scene.putbacks_held;
            return 0;
        }
        freed = give_the_module_back();
        ++scene.putbacks_refused;
        if (!scene.putback_logged) {
            scene.putback_logged = true;
            log_info("a script let the hero go on this machine although nothing was ever parked "
                     "here, so the put-back was refused%s. Without the refusal the module would "
                     "have been written to nought, the phases would have stopped and no spawn "
                     "would have come for a living player. The removal that asked returns to %08X",
                     freed ? " and the module, which was already stopped, was set running"
                           : " and the module was left running",
                     (unsigned)caller);
        }
        return 0;
    }
    if (!readable) {
        ++scene.putbacks_unread;
    }
    ++scene.putbacks_passed;
    return ((scene_putback_fn_t)scene.putback_hull.original)();
}

/* The camera, refused for the three callers that are a script and for nobody else.
 *
 * The return address is the instrument, the same one the arena gate uses on the spawner, and it
 * fails open twice over: a site that did not resolve is not in the list, and an empty list
 * matches nothing. A call arriving from another module that detoured this function in front of
 * us carries that module's address and is let through as well, which loses the refusal rather
 * than the camera. A take let through is told to the door listener.
 *
 * engine: void bapview_overrideOn(int group) */
static int32_t __cdecl hook_camera_take(int32_t group)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();

    if ((scene.client_holds || scene.suppressed) &&
        mp_scene_camera_is_a_script(scene.script_returns, scene.script_return_count, caller)) {
        ++scene.cameras_refused;
        if (!scene.camera_logged) {
            scene.camera_logged = true;
            log_info("a script of this machine asked for the camera and was refused: the scene "
                     "it belongs to is not this side's to play. The end form of the same script "
                     "writes the resting value into that cell, so nothing is left held");
        }
        return 0;
    }
    /* A spoken line's camera goes with the line: where this machine does not present the line,
     * the view does not swing to its speaker either. The judgement of the line is the one
     * question asked at this take; the door listener below never refuses it. The release writes
     * a constant nought, so a refused take leaves nothing held. */
    if (scene.line_camera != NULL && caller != 0u &&
        caller == scene.take_return[SCENE_TAKE_SPEAK] && !scene.line_camera()) {
        return 0;
    }
    /* The listener may keep the host's view: a camera alone that a far player's scene asked for
     * would swing the host's camera across the map. The release writes a constant nought, so the
     * refused take leaves nothing held. */
    if (scene.door_listener != NULL &&
        !scene.door_listener(MP_CUTSCENE_DOOR_CAMERA, caller, group)) {
        ++scene.cameras_refused_far;
        return 0;
    }
    ++scene.cameras_passed;
    return ((scene_fn_t)scene.camera_hull.original)(group);
}

/* The hook arrives as a plain pointer because the four bound here are not one prototype: three
 * take an int and the put-back takes nothing. The camera, the fifth, is found another way and is
 * bound by hull_the_camera. */
static bool hull_one(const uint8_t *bytes, const uint8_t *mask, size_t size, size_t prologue,
                     detour_t *hull, const void *hook, const char *what)
{
    uintptr_t address = signature_find_detour_target(bytes, mask, size, prologue);

    if (address == 0u) {
        log_warning("the arena cannot hold back %s: its pattern did not resolve, so a script that "
                    "asks for one still gets it", what);
        return false;
    }
    if (!detour_install(hull, address, hook, prologue)) {
        log_warning("the arena cannot hold back %s: the hull at %08X did not install", what,
                    (unsigned)address);
        return false;
    }
    return true;
}

/* The three script sites, resolved for their return addresses and for nothing else. A site that
 * does not resolve is left out of the list, and its opcode then keeps the camera. */
static void resolve_the_script_takes(void)
{
    size_t index;

    scene.script_return_count = 0u;
    for (index = 0; index < SCENE_TAKE_COUNT; ++index) {
        uintptr_t site = signature_find_unique(SCENE_TAKES[index].bytes, SCENE_TAKES[index].mask,
                                               SCENE_TAKES[index].size);

        if (site == 0u) {
            log_warning("the camera take of %s did not resolve, so a script of this machine can "
                        "still swing the view there", SCENE_TAKES[index].what);
            continue;
        }
        scene.script_returns[scene.script_return_count] =
            site + (uintptr_t)SCENE_TAKES[index].after_the_call;
        scene.take_return[index] = scene.script_returns[scene.script_return_count];
        ++scene.script_return_count;
    }
}

/* Where the camera take lives, read out of the calls the resolved script sites make to it. True
 * with the address in `entry` when every one of them calls the same place. A site whose bytes
 * cannot be read keeps its zeros, and zeros are not a call. */
static bool camera_entry_from_the_calls(uintptr_t *entry)
{
    mp_scene_call_site_t sites[SCENE_TAKE_COUNT] = { { 0u, { 0u } } };
    size_t               index;

    scene.camera_operands = 0u;
    for (index = 0; index < scene.script_return_count; ++index) {
        sites[index].return_address = scene.script_returns[index];
        (void)memory_read(scene.script_returns[index] - MP_SCENE_CALL_BYTES, sites[index].call,
                          MP_SCENE_CALL_BYTES);
    }
    switch (mp_scene_camera_callee(sites, scene.script_return_count, entry)) {
    case MP_SCENE_CALLEE_AGREED:
        scene.camera_operands = scene.script_return_count;
        return true;
    case MP_SCENE_CALLEE_NOT_A_CALL:
        log_warning("the arena cannot hold back the camera a script takes: a script site does "
                    "not end in a call, so where the take lives cannot be read from it");
        return false;
    case MP_SCENE_CALLEE_DISAGREE:
        log_warning("the arena cannot hold back the camera a script takes: its %u script sites "
                    "call different addresses, so which one is the take cannot be told",
                    (unsigned)scene.script_return_count);
        return false;
    case MP_SCENE_CALLEE_NO_SITES:
    default:
        return false;   /* every site that did not resolve has said so already */
    }
}

/* The camera take, bound at the address its callers name, and checked against its own pattern.
 *
 * Two ways, and they are independent. The callers name the address in their call operands. The
 * pattern finds it by its bytes, sifting the tail when a foreign branch has replaced the head.
 * Where both answer they must answer the same, or one of them has found something that is not
 * this function and nothing is hulled. Where the pattern answers nothing, the bytes at the called
 * address are proved instead, tail exactly and head as authored or already a branch.
 *
 * The hull chains in front of a branch that is already there, like every detour in this tree, so
 * the module that placed it still runs for every take this one lets through. The head is read
 * BEFORE the install, because after it the byte is this hull's own. */
static bool hull_the_camera(const uint8_t *bytes, const uint8_t *mask, size_t size,
                            size_t prologue)
{
    uintptr_t entry = 0u;
    uintptr_t found;
    uint8_t   head = 0u;

    scene.camera_pattern_agrees = false;
    scene.camera_head_branched  = false;
    if (scene.script_return_count == 0u || !camera_entry_from_the_calls(&entry)) {
        return false;
    }
    found = signature_find_detour_target(bytes, mask, size, prologue);
    if (found != 0u && found != entry) {
        log_warning("the arena cannot hold back the camera a script takes: its callers name "
                    "%08X and its pattern resolves at %08X", (unsigned)entry, (unsigned)found);
        return false;
    }
    if (found == 0u && signature_find_at(entry, bytes, mask, size, prologue) == 0u) {
        log_warning("the arena cannot hold back the camera a script takes: its callers name "
                    "%08X and the bytes there are not the function its pattern was cut from",
                    (unsigned)entry);
        return false;
    }
    scene.camera_head_branched = memory_read_u8(entry, &head) && head == SCENE_BRANCH_OPCODE;
    if (!detour_install(&scene.camera_hull, entry, (const void *)&hook_camera_take, prologue)) {
        log_warning("the arena cannot hold back the camera a script takes: the hull at %08X did "
                    "not install", (unsigned)entry);
        return false;
    }
    scene.camera_pattern_agrees = (found != 0u);
    return true;
}

bool mp_cutscene_install(void)
{
    if (scene.installed) {
        return true;
    }
    scene.lock_bound = hull_one(SIG_SCENE_LOCK_ENTER, MSK_SCENE_LOCK_ENTER,
                                sizeof SIG_SCENE_LOCK_ENTER, SCENE_LOCK_ENTER_PROLOGUE,
                                &scene.lock_hull, (const void *)&hook_lock_enter,
                                "the player lock");
    scene.letterbox_bound = hull_one(SIG_SCENE_LETTERBOX, MSK_SCENE_LETTERBOX,
                                     sizeof SIG_SCENE_LETTERBOX, SCENE_LETTERBOX_PROLOGUE,
                                     &scene.letterbox_hull, (const void *)&hook_letterbox,
                                     "the letterbox bars");

    scene.suspend_bound = hull_one(SIG_SCENE_SUSPEND, MSK_SCENE_SUSPEND,
                                   sizeof SIG_SCENE_SUSPEND, SCENE_SUSPEND_PROLOGUE,
                                   &scene.suspend_hull, (const void *)&hook_suspend,
                                   "the hero as a script's actor");

    /* The grab and the put-back are bound together, in this order, so that a reader looking for
     * one of them finds both. */
    scene.pr_cell = mp_cells_address(MP_CELL_PR);
    if (scene.pr_cell == 0u) {
        log_warning("the player record pointer did not resolve, so a put-back cannot be asked "
                    "whether anything was ever parked and every one of them is let through");
    }
    scene.putback_bound = hull_one(SIG_SCENE_RESUME, MSK_SCENE_RESUME, sizeof SIG_SCENE_RESUME,
                                   SCENE_RESUME_PROLOGUE, &scene.putback_hull,
                                   (const void *)&hook_putback, "the put-back of the hero");

    resolve_the_script_takes();
    scene.camera_bound = hull_the_camera(SIG_SCENE_VIEW_OVERRIDE, MSK_SCENE_VIEW_OVERRIDE,
                                         sizeof SIG_SCENE_VIEW_OVERRIDE,
                                         SCENE_VIEW_OVERRIDE_PROLOGUE);

    scene.installed = scene.lock_bound || scene.letterbox_bound || scene.suspend_bound ||
                      scene.putback_bound || scene.camera_bound;
    if (!scene.installed) {
        log_warning("no part of a cutscene can be held back, so an arena plays them as the "
                    "campaign does");
        return false;
    }
    /* The lock is the one that matters. The other two are what a scene LOOKS like; this one is
     * what takes the player away, and it is the one that can leave somebody standing on a lift
     * with no way to move again. */
    log_info("the scene gates are bound: player lock %s, letterbox %s, the hero as an actor %s, "
             "the put-back of the hero %s, the camera %s over %u script site(s), its entry read "
             "from %u call operand(s) that agree%s%s",
             scene.lock_bound ? "held" : "NOT HELD",
             scene.letterbox_bound ? "held" : "not held",
             scene.suspend_bound ? "held" : "not held",
             scene.putback_bound ? "held" : "NOT HELD",
             scene.camera_bound ? "held" : "not held",
             (unsigned)scene.script_return_count, (unsigned)scene.camera_operands,
             !scene.camera_bound ? ""
                 : (scene.camera_pattern_agrees ? ", where its own pattern resolves as well"
                                                : ", where only the bytes could be proved"),
             (scene.camera_bound && scene.camera_head_branched)
                 ? ", its head already a branch of another module" : "");
    /* Said once, at the site, because it is the sentence the missing half cost a field run to
     * learn. */
    log_info("the hero as a script's actor is a pair: what this side did not park it does not "
             "put back either, and the put-back has one caller");
    return true;
}

/* Whether a scene on THIS machine is the host's business rather than this side's. Decided from the
 * session as the host said it, once per frame by the pump, rather than read here: this hook runs
 * inside the engine and must not go asking the bridge anything. */
void mp_cutscene_set_client_holds_back(bool holds)
{
    if (scene.client_holds == holds) {
        return;
    }
    scene.client_holds = holds;
    log_info("a scene on this machine %s", holds
                 ? "belongs to the host: no script of this side takes the player, draws bars or "
                   "puts the hero on a placement"
                 : "is this side's own again");
}

void mp_cutscene_doors(mp_cutscene_doors_t *doors)
{
    if (doors != NULL) {
        doors->lock_entry        = scene.lock_bound ? scene.lock_hull.target : 0u;
        doors->lock_take_return  = scene.take_return[SCENE_TAKE_LOCK];
        doors->dolly_take_return = scene.camera_bound ? scene.take_return[SCENE_TAKE_DOLLY] : 0u;
    }
}

void mp_cutscene_set_door_listener(mp_cutscene_door_listener_t listener,
                                   mp_cutscene_doors_t *doors)
{
    scene.door_listener = listener;
    mp_cutscene_doors(doors);
}

void mp_cutscene_set_gather_holds(bool holds)
{
    scene.gather_holds = holds;
}

void mp_cutscene_set_line_camera(mp_cutscene_line_camera_fn_t judge)
{
    scene.line_camera = judge;
}

/* The engine's own lock and bars, past the gates above: what a client's mirror of the host's
 * scene raises is the host's scene, not one of this machine's scripts. */
int32_t mp_cutscene_engine_lock(int32_t level)
{
    return scene.lock_bound ? ((scene_fn_t)scene.lock_hull.original)(level) : 0;
}

void mp_cutscene_engine_bars(bool on)
{
    if (scene.letterbox_bound) {
        (void)((scene_fn_t)scene.letterbox_hull.original)(on ? 1 : 0);
    }
}

void mp_cutscene_counts(mp_cutscene_counts_t *out)
{
    if (out == NULL) {
        return;
    }
    out->grabs                      = scene.grabs;
    out->grabs_held                 = scene.grabs_held;
    out->putbacks_held              = scene.putbacks_held;
    out->cameras_refused_far        = scene.cameras_refused_far;
    out->locks_refused_for_the_host = scene.locks_refused_for_the_host;
}

/* The one place the cell is derived: the compare and the load behind the hull have to name the
 * same cell, and the cell has to lie in the image. */
uintptr_t mp_cutscene_lock_level_cell(void)
{
    uintptr_t entry    = scene.lock_hull.target;
    uint32_t  compared = 0u;
    uint32_t  loaded   = 0u;
    uint8_t   load     = 0u;

    if (!scene.lock_bound || !memory_try_read_u32(entry + SCENE_LOCK_LEVEL_OPERAND, &compared) ||
        !memory_try_read_u8(entry + SCENE_LOCK_LEVEL_LOAD, &load) ||
        load != SCENE_LOAD_EAX_OPCODE ||
        !memory_try_read_u32(entry + SCENE_LOCK_LEVEL_LOAD_OPERAND, &loaded) || compared == 0u ||
        compared != loaded || !memory_is_inside_image(compared, sizeof(int32_t))) {
        return 0u;
    }
    return compared;
}

int32_t mp_cutscene_lock_level(void)
{
    uintptr_t cell  = mp_cutscene_lock_level_cell();
    int32_t   level = -1;

    if (cell == 0u || !memory_try_read(cell, &level, sizeof level)) {
        return -1;
    }
    return level;
}

void mp_cutscene_set_suppressed(bool suppressed)
{
    if (scene.suppressed == suppressed) {
        return;
    }
    scene.suppressed = suppressed;
    log_info("scenes are %s", suppressed ? "held back: no script may take the player, draw bars "
                                           "or move the camera while this arena runs"
                                         : "played again, as a campaign plays them");
}

void mp_cutscene_report(void)
{
    if (!scene.installed) {
        log_info("  the scene gates: not bound");
        return;
    }
    log_info("  the scene gates: %s; %u lock(s) refused and %u let through, %u letterbox(es) "
             "refused", scene.suppressed ? "holding" : (scene.client_holds ? "the host's" : "open"),
             (unsigned)scene.locks_refused, (unsigned)scene.locks_passed,
             (unsigned)scene.letterboxes_refused);
    log_info("    a scene is the host's: %u lock(s), %u bar(s) and %u hosting(s) of the hero "
             "asked by a script of this machine and refused; %u hosting(s) let through, %u "
             "put-back(s) refused (%u of them gave a stopped module back, %u left an idle one "
             "alone) and %u let through "
             "(%u of those unread), %u camera take(s) refused and %u let through",
             (unsigned)scene.locks_refused_for_the_host,
             (unsigned)scene.letterboxes_refused_for_the_host,
             (unsigned)scene.suspends_refused, (unsigned)scene.suspends_passed,
             (unsigned)scene.putbacks_refused, (unsigned)scene.modules_freed,
             (unsigned)scene.modules_not_freed, (unsigned)scene.putbacks_passed,
             (unsigned)scene.putbacks_unread,
             (unsigned)scene.cameras_refused, (unsigned)scene.cameras_passed);
}
