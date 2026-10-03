/* mp_cutscene.c: the doors a scene comes through, held shut while an arena runs and while a
 * scene on this machine belongs to the host, and on a host held for the script of a far player.
 * See the header for why the lock is refused rather than released, why the cell is the wrong
 * lever, and the rule the whole file is held to.
 *
 * SIZE NOTE: seven hulls: the lock, the bars, the camera, the grab of the hero and its put-back,
 * and the two releases a script gives a scene back through. Each pair stays in this one file on
 * purpose, the grab with its put-back and every take with its release, because a pair split over
 * two files is the very mistake the rule in the header exists to stop. The seams taken instead:
 * the pure arithmetic is mp_scene_rule; the patterns are mp_signatures_scene, whose comments
 * carry the byte evidence of every site; and where the sites are found on the running image, the
 * script's takes and ends and the two functions read out of their calls, is mp_cutscene_sites,
 * which left this file when the two releases came. The next seam is the reading of the cells
 * behind the hulls, the lock's level and the bars' target, which only read the hulls' addresses.
 */
#include "mp_cutscene.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_cutscene_sites.h"
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

/* Five of the seven take one int and are cdecl. The return is carried through rather than dropped
 * even where the engine's own body sets none: a prototype that throws a return away is a trap
 * this tree has already paid for, and passing whatever is in the register costs nothing. */
typedef int32_t(__cdecl *scene_fn_t)(int32_t argument);

/* The put-back and the clearing of the camera take nothing at all, and their return is carried
 * through for the same reason. */
typedef int32_t(__cdecl *scene_putback_fn_t)(void);

typedef struct cutscene {
    bool     installed;
    bool     suppressed;      /* the arena: a deathmatch has no scenes at all */
    bool     client_holds;    /* a client in a session: a scene belongs to the host */
    bool     gather_holds;    /* a host being brought to a scene's place: the grab waits */

    detour_t lock_hull;
    detour_t letterbox_hull;
    detour_t suspend_hull;
    detour_t putback_hull;
    detour_t camera_hull;
    detour_t lock_off_hull;
    detour_t camera_off_hull;
    bool     lock_bound;
    bool     letterbox_bound;
    bool     suspend_bound;
    bool     putback_bound;
    bool     camera_bound;
    bool     lock_off_bound;
    bool     camera_off_bound;
    bool     releases_tried;   /* the two releases were looked for, which they are once */

    /* The player record pointer, read again on every put-back rather than followed once: the
     * bank moves it while a far body is being written, so a pointer cached here would be the
     * wrong record for as long as that lasts. */
    uintptr_t pr_cell;
    mp_cutscene_takes_t    takes;      /* where a script takes, and the camera take itself */
    mp_cutscene_releases_t releases;   /* where it gives back */

    mp_cutscene_door_listener_t door_listener;
    mp_cutscene_grab_listener_t grab_listener;
    mp_cutscene_line_camera_fn_t line_camera;

    uint32_t locks_refused;
    uint32_t locks_passed;
    uint32_t letterboxes_refused;
    uint32_t locks_refused_for_the_host;   /* and the same three, counted by the second reason */
    uint32_t letterboxes_refused_for_the_host;
    uint32_t locks_refused_far;            /* on a host, by the door listener: a far player's */
    uint32_t bars_refused_far;
    uint32_t lock_offs_refused;            /* a script's release, refused by the door listener */
    uint32_t camera_offs_refused;
    uint32_t bars_offs_refused;
    uint32_t lock_offs_passed;             /* a script's release the listener let through */
    uint32_t camera_offs_passed;
    uint32_t suspends_refused;
    uint32_t suspends_passed;
    uint32_t putbacks_refused;
    uint32_t putbacks_passed;
    uint32_t putbacks_unread;    /* let through because the store could not be read at all */
    uint32_t modules_freed;      /* refused, and the stopped module set running again */
    uint32_t modules_not_freed;  /* idle, and left alone: no body, or a swapped bank */
    uint32_t cameras_refused;
    uint32_t cameras_passed;
    uint32_t cameras_refused_far;   /* the host's view kept from a far player's script */
    uint32_t cameras_refused_other; /* a camera of another group, refused on a client */
    uint32_t grabs;                 /* the engine's grab answered 1: a hero taken for a scene */
    uint32_t grabs_held;            /* asked while the host was brought to the scene's place */
    uint32_t putbacks_held;         /* refused while that hold stood */
    bool     putback_logged;
    bool     camera_logged;
    bool     camera_other_logged;
} cutscene_t;

static cutscene_t scene;

/* ==============================================================================================
 * The hulls.
 * ============================================================================================ */

/* A menu takes the lock at level one on every render it draws, and that is not a scene: refusing
 * it would leave a client unable to open its own pause screen. Only the level a script takes is
 * held back. */
#define SCENE_LOCK_LEVEL MP_CUTSCENE_LOCK_LEVEL

/* What the door listener says about a door on the host's path: true with nobody listening. */
static bool the_listener_lets(mp_cutscene_door_t door, uintptr_t caller, int32_t argument)
{
    return scene.door_listener == NULL || scene.door_listener(door, caller, argument);
}

/* A raise is asked of the door listener before it is made, so the listener can still read the
 * level the lock stood at. A raise it refuses is a far player's script taking the host, and is
 * answered as every refusal here is.
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
        if (!the_listener_lets(MP_CUTSCENE_DOOR_LOCK, (uintptr_t)_ReturnAddress(), level)) {
            ++scene.locks_refused_far;
            return 0;
        }
        ++scene.locks_passed;
        return original(level);
    }
    /* Nought is the answer the engine's own body gives for "the lock was already held", which is
     * the nearest true thing to say: this side did not take it either. Nothing else is touched,
     * so the release that may arrive later finds a level of zero and does nothing, which is
     * exactly what it should do for a lock nobody took. */
    ++scene.locks_refused;
    return 0;
}

/* Both callers of the bars are a script's two opcodes, so every call here is a script's.
 *
 * For an arena and on a client only the drawing is refused, never the clearing. The same function
 * does both directions, and refusing both would mean that bars already on screen when an arena
 * begins stay there for the whole match with nothing able to take them down.
 *
 * On a host the door listener is asked for both directions. The clearing it refuses is a far
 * player's script taking down bars it did not put up, which are the host's own scene's; its own
 * bars were refused, so nothing of its own is left standing.
 *
 * engine: void fxfade_setLetterbox(int on) */
static int32_t __cdecl hook_letterbox(int32_t on)
{
    scene_fn_t original = (scene_fn_t)scene.letterbox_hull.original;
    uintptr_t  caller   = (uintptr_t)_ReturnAddress();

    if (on == 0) {
        if (!scene.suppressed && !scene.client_holds &&
            !the_listener_lets(MP_CUTSCENE_DOOR_BARS_OFF, caller, on)) {
            ++scene.bars_offs_refused;
            return 0;
        }
        return original(on);
    }
    if (scene.client_holds && !scene.suppressed) {
        ++scene.letterboxes_refused_for_the_host;
        return 0;
    }
    if (!scene.suppressed) {
        if (!the_listener_lets(MP_CUTSCENE_DOOR_BARS, caller, on)) {
            ++scene.bars_refused_far;
            return 0;
        }
        return original(on);
    }
    ++scene.letterboxes_refused;
    return 0;
}

/* A script's release of the lock, asked of the door listener on a host. Only the two script ends
 * are asked about, known by the address the call returns to; the dialogue's own releases and the
 * release of what a scene holds, which calls this at its head, go through unasked. Nought is the
 * engine's own answer for a release that let go of nothing.
 *
 * engine: int Dialog_LeaveInputLock(int level) */
static int32_t __cdecl hook_lock_leave(int32_t level)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();

    if (mp_scene_camera_is_a_script(scene.releases.lock_off_return, MP_CUTSCENE_ENDS, caller)) {
        if (!the_listener_lets(MP_CUTSCENE_DOOR_LOCK_OFF, caller, level)) {
            ++scene.lock_offs_refused;
            return 0;
        }
        ++scene.lock_offs_passed;
    }
    return ((scene_fn_t)scene.lock_off_hull.original)(level);
}

/* A script's clearing of the camera's override, the same way: the two script ends are asked
 * about, and the engine's own four callers and the release of what a scene holds are not.
 *
 * engine: void bapview_overrideOff(void) */
static int32_t __cdecl hook_camera_off(void)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();

    if (mp_scene_camera_is_a_script(scene.releases.camera_off_return, MP_CUTSCENE_ENDS, caller)) {
        if (!the_listener_lets(MP_CUTSCENE_DOOR_CAMERA_OFF, caller, 0)) {
            ++scene.camera_offs_refused;
            return 0;
        }
        ++scene.camera_offs_passed;
    }
    return ((scene_putback_fn_t)scene.camera_off_hull.original)();
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* The hero as an actor, refused for the same reason and with the engine's own answer. A host that
 * is being brought to the place of a scene answers "not yet" the same way, and the engine asks
 * again next substep. So does a host for a hero the door listener refuses: the engine asks for
 * every actor carrying the handover bit on every tick, and nought is its own "not now". */
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
    if (!the_listener_lets(MP_CUTSCENE_DOOR_GRAB, (uintptr_t)_ReturnAddress(), 0)) {
        return 0;   /* counted by the listener, which knows the actor that asked */
    }
    ++scene.suspends_passed;
    answer = original(actor);
    if (answer != 0) {
        ++scene.grabs;
        if (scene.grab_listener != NULL) {
            scene.grab_listener();
        }
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
 * progress, and the put-back that ends it is let through a few lines down.
 *
 * Two callers: the put-back's gate below, and the release of what a scene holds, for a stopped
 * module with nothing in the engine's store that nobody drives (mp_scene_free). */
bool mp_cutscene_module_back(void)
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

        /* A host on his way to a scene's place holds a grab it has not made: its module runs, and
         * the refused put-back leaves it running. Counted apart, so the line the field runs carry
         * keeps its meaning. */
        if (scene.gather_holds && !scene.client_holds) {
            ++scene.putbacks_held;
            return 0;
        }
        freed = mp_cutscene_module_back();
        ++scene.putbacks_refused;
        if (!scene.putback_logged) {
            scene.putback_logged = true;
            log_info("a script let the hero go on this machine with nothing parked here, never "
                     "or no longer, so the put-back was refused%s. Without the refusal the "
                     "module would have been written to nought, the phases would have stopped "
                     "and no spawn would have come for a living player. The removal that asked "
                     "returns to %08X",
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

/* The camera, refused for the three callers that are a script, and on a client for every group
 * but the engine's own.
 *
 * The return address is the instrument, the same one the arena gate uses on the spawner, and it
 * fails open twice over: a site that did not resolve is not in the list, and an empty list
 * matches nothing. A call arriving from another module that detoured this function in front of
 * us carries that module's address and is let through as well, which loses the refusal rather
 * than the camera. On a host the door listener is asked about the takes of the two opcodes and
 * told of a spoken line's once it goes through.
 *
 * engine: void bapview_overrideOn(int group) */
static int32_t __cdecl hook_camera_take(int32_t group)
{
    uintptr_t        caller = (uintptr_t)_ReturnAddress();
    const uintptr_t *take   = scene.takes.take_return;

    if ((scene.client_holds || scene.suppressed) &&
        mp_scene_camera_is_a_script(scene.takes.script_returns, scene.takes.script_return_count,
                                    caller)) {
        ++scene.cameras_refused;
        if (!scene.camera_logged) {
            scene.camera_logged = true;
            log_info("a script of this machine asked for the camera and was refused: the scene "
                     "it belongs to is not this side's to play. The end form of the same script "
                     "writes the resting value into that cell, so nothing is left held");
        }
        return 0;
    }
    /* What is left on a client of a take of another group is a savegame restoring the camera
     * its scene had when it was saved, from the restore's own call and not a script's. A scene
     * there is the host's, and this side keeps its own view; the engine's own group, the fall,
     * the gun and the loading screen, goes through as it does alone. Refused here, nothing has
     * to be given back later. */
    if (mp_scene_camera_refused_on_a_client(scene.client_holds, scene.suppressed, group)) {
        ++scene.cameras_refused_other;
        if (!scene.camera_other_logged) {
            scene.camera_other_logged = true;
            log_info("a camera take of group %d was refused on this client, where a scene is "
                     "the host's and this side keeps its own view; only the engine's own group "
                     "%d goes through. The take returns to %08X", (int)group,
                     (int)MP_SCENE_CAMERA_GROUP_ENGINE, (unsigned)caller);
        }
        return 0;
    }
    /* A spoken line's camera goes with the line: where this machine does not present the line,
     * the view does not swing to its speaker either. The judgement of the line is the one
     * question asked at this take; the door listener is only told that it went through, because
     * the speaker has then taken the camera here and may give it back. The release writes a
     * constant nought, so a refused take leaves nothing held. */
    if (caller != 0u && caller == take[MP_CUTSCENE_TAKE_SPEAK]) {
        if (scene.line_camera != NULL && !scene.line_camera()) {
            return 0;
        }
        (void)the_listener_lets(MP_CUTSCENE_DOOR_LINE_CAMERA, caller, group);
    } else if (caller != 0u && (caller == take[MP_CUTSCENE_TAKE_DOLLY] ||
                                caller == take[MP_CUTSCENE_TAKE_LOCK])) {
        /* The listener may keep the host's view: a camera that a far player's script asked for
         * would swing the host's camera across the map. Nothing is held by the refused take. */
        if (!the_listener_lets(MP_CUTSCENE_DOOR_CAMERA, caller, group)) {
            ++scene.cameras_refused_far;
            return 0;
        }
    }
    ++scene.cameras_passed;
    return ((scene_fn_t)scene.camera_hull.original)(group);
}

static uintptr_t bars_cell(void);

/* The hook arrives as a plain pointer because the four bound here are not one prototype: three
 * take an int and the put-back takes nothing. The camera and the two releases are found another
 * way, out of the calls a script makes (mp_cutscene_sites), and are bound where that is asked. */
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

/* The camera take, bound at the address its callers name (mp_cutscene_sites_takes). The hull
 * chains in front of a branch that is already there, like every detour in this tree, so the
 * module that placed it still runs for every take this one lets through. */
static bool hull_the_camera(void)
{
    uintptr_t entry = scene.takes.camera_entry;

    if (entry == 0u) {
        return false;   /* why not is said where the sites are read */
    }
    if (!detour_install(&scene.camera_hull, entry, (const void *)&hook_camera_take,
                        SCENE_VIEW_OVERRIDE_PROLOGUE)) {
        log_warning("the arena cannot hold back the camera a script takes: the hull at %08X did "
                    "not install", (unsigned)entry);
        return false;
    }
    return true;
}

/* The two releases a script gives a scene back through, bound once, with the door listener: only
 * a listener is ever asked at them, and it is set on a session's way in, so a game with no session
 * runs neither hull. Each chains in front of a branch another module left on the head, and as the
 * last installer it is the outermost hull, which is what makes the address it reads the script's
 * own. Eleven bytes of the lock's release and thirteen of the camera's clearing are taken; both
 * end on an instruction and neither holds a relative operand (mp_signatures_scene.c). */
static void hull_the_releases(void)
{
    const mp_cutscene_releases_t *at = &scene.releases;

    if (scene.releases_tried) {
        return;
    }
    scene.releases_tried = true;
    mp_cutscene_sites_releases(&scene.takes, &scene.releases);
    scene.lock_off_bound = at->lock_off_entry != 0u &&
                           detour_install(&scene.lock_off_hull, at->lock_off_entry,
                                          (const void *)&hook_lock_leave,
                                          SCENE_LOCK_LEAVE_PROLOGUE);
    scene.camera_off_bound = at->camera_off_entry != 0u &&
                             detour_install(&scene.camera_off_hull, at->camera_off_entry,
                                            (const void *)&hook_camera_off,
                                            SCENE_VIEW_RELEASE_PROLOGUE);
    log_info("the releases of a script are bound for a host: the lock's release at %08X %s "
             "(the script ends return to %08X and %08X), the clearing of the camera at %08X %s "
             "(they return to %08X and %08X); a return of nought is an end that is not asked "
             "about",
             (unsigned)at->lock_off_entry, scene.lock_off_bound ? "held" : "NOT HELD",
             (unsigned)at->lock_off_return[MP_CUTSCENE_END_DOLLY],
             (unsigned)at->lock_off_return[MP_CUTSCENE_END_LOCK],
             (unsigned)at->camera_off_entry, scene.camera_off_bound ? "held" : "NOT HELD",
             (unsigned)at->camera_off_return[MP_CUTSCENE_END_DOLLY],
             (unsigned)at->camera_off_return[MP_CUTSCENE_END_LOCK]);
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

    /* The sites before the hull: the head of the camera take is read as another module left it. */
    mp_cutscene_sites_takes(&scene.takes);
    scene.camera_bound = hull_the_camera();

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
             (unsigned)scene.takes.script_return_count, (unsigned)scene.takes.camera_operands,
             !scene.camera_bound ? ""
                 : (scene.takes.camera_pattern_agrees ? ", where its own pattern resolves as well"
                                                      : ", where only the bytes could be proved"),
             (scene.camera_bound && scene.takes.camera_head_branched)
                 ? ", its head already a branch of another module" : "");
    /* Said once, at the site, because it is the sentence the missing half cost a field run to
     * learn. */
    log_info("the hero as a script's actor is a pair: what this side did not park it does not "
             "put back either, and the put-back has one caller");
    if (scene.letterbox_bound && bars_cell() != 0u) {
        log_info("the bars' target cell is at %08X, read out of the letterbox's own compare and "
                 "store behind its hull", (unsigned)bars_cell());
    } else if (scene.letterbox_bound) {
        log_warning("the bars' target cell did not resolve out of the letterbox's own compare and "
                    "store, so bars a savegame left on a client with no lock under them stay up");
    }
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

bool mp_cutscene_client_holds_back(void)
{
    return scene.client_holds;
}

void mp_cutscene_doors(mp_cutscene_doors_t *doors)
{
    if (doors != NULL) {
        doors->lock_entry        = scene.lock_bound ? scene.lock_hull.target : 0u;
        doors->lock_take_return  = scene.takes.take_return[MP_CUTSCENE_TAKE_LOCK];
        doors->dolly_take_return = scene.camera_bound
                                       ? scene.takes.take_return[MP_CUTSCENE_TAKE_DOLLY] : 0u;
    }
}

void mp_cutscene_set_door_listener(mp_cutscene_door_listener_t listener,
                                   mp_cutscene_doors_t *doors)
{
    scene.door_listener = listener;
    if (listener != NULL) {
        hull_the_releases();
    }
    mp_cutscene_doors(doors);
}

void mp_cutscene_set_gather_holds(bool holds)
{
    scene.gather_holds = holds;
}

void mp_cutscene_set_grab_listener(mp_cutscene_grab_listener_t listener)
{
    scene.grab_listener = listener;
}

void mp_cutscene_set_line_camera(mp_cutscene_line_camera_fn_t judge)
{
    scene.line_camera = judge;
}

/* The engine's own bars and camera take, past the gates above. A host makes up through them what
 * a far player's run was refused before it reached the door of a scene, and the release of what a
 * scene holds takes the bars down through the first with false. */
void mp_cutscene_engine_bars(bool on)
{
    if (scene.letterbox_bound) {
        (void)((scene_fn_t)scene.letterbox_hull.original)(on ? 1 : 0);
    }
}

void mp_cutscene_engine_camera(int32_t group)
{
    if (scene.camera_bound) {
        (void)((scene_fn_t)scene.camera_hull.original)(group);
    }
}

bool mp_cutscene_engine_resume(void)
{
    uint32_t block = 0u;

    /* THE BANK FIRST: the put-back writes the record the pointer names, which inside a window
     * is a far body's. */
    if (!scene.putback_bound || mp_bank_is_swapped() || scene.pr_cell == 0u ||
        !memory_read_u32(scene.pr_cell, &block) || block == 0u) {
        return false;
    }
    if (((scene_putback_fn_t)scene.putback_hull.original)() == 0) {
        return false;   /* no body to come back to: the put-back changed nothing */
    }
    /* The engine's own put-back never clears its store. Left standing, the removal of the
     * actor later would be let through as a second put-back and write the module back over
     * whatever it is by then, dying or coming back. */
    return memory_try_write((uintptr_t)block + MP_HERO_BLOCK_SAVED_MODULE_STATE,
                            &(uint32_t){ 0u }, sizeof(uint32_t));
}

/* The bars' target cell, out of the letterbox's own compare and its own store behind the hull,
 * which have to agree and lie in the image; 0 when the letterbox is not hulled or they do not. */
static uintptr_t bars_cell(void)
{
    uintptr_t entry    = scene.letterbox_hull.target;
    uint32_t  compared = 0u;
    uint32_t  stored   = 0u;
    uint8_t   store    = 0u;

    if (!scene.letterbox_bound ||
        !memory_try_read_u32(entry + SCENE_BARS_TARGET_OPERAND, &compared) ||
        !memory_try_read_u8(entry + SCENE_BARS_TARGET_STORE, &store) ||
        store != SCENE_STORE_EAX_OPCODE ||
        !memory_try_read_u32(entry + SCENE_BARS_TARGET_STORE_OPERAND, &stored) ||
        compared == 0u || compared != stored ||
        !memory_is_inside_image(compared, sizeof(int32_t))) {
        return 0u;
    }
    return compared;
}

bool mp_cutscene_bars_on(void)
{
    uintptr_t cell   = bars_cell();
    int32_t   target = 0;

    return cell != 0u && memory_try_read(cell, &target, sizeof target) && target != 0;
}

void mp_cutscene_counts(mp_cutscene_counts_t *out)
{
    if (out == NULL) {
        return;
    }
    out->grabs         = scene.grabs;
    out->grabs_held    = scene.grabs_held;
    out->putbacks_held = scene.putbacks_held;
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

bool mp_cutscene_suppressed(void)
{
    return scene.suppressed;
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
             "(%u of those unread), %u camera take(s) refused, %u camera take(s) of another "
             "group refused on a client, and %u let through",
             (unsigned)scene.locks_refused_for_the_host,
             (unsigned)scene.letterboxes_refused_for_the_host,
             (unsigned)scene.suspends_refused, (unsigned)scene.suspends_passed,
             (unsigned)scene.putbacks_refused, (unsigned)scene.modules_freed,
             (unsigned)scene.modules_not_freed, (unsigned)scene.putbacks_passed,
             (unsigned)scene.putbacks_unread,
             (unsigned)scene.cameras_refused, (unsigned)scene.cameras_refused_other,
             (unsigned)scene.cameras_passed);
    log_info("    a far player's script on a host: %u lock(s), %u bar(s) and %u camera take(s) "
             "refused at the gate; of a script's releases, %u of the lock and %u of the camera "
             "let through and %u of the lock, %u of the camera and %u of the bars refused; the "
             "lock's release %s, the clearing of the camera %s",
             (unsigned)scene.locks_refused_far, (unsigned)scene.bars_refused_far,
             (unsigned)scene.cameras_refused_far, (unsigned)scene.lock_offs_passed,
             (unsigned)scene.camera_offs_passed, (unsigned)scene.lock_offs_refused,
             (unsigned)scene.camera_offs_refused, (unsigned)scene.bars_offs_refused,
             scene.lock_off_bound ? "held" : (scene.releases_tried ? "NOT HELD" : "not asked for"),
             scene.camera_off_bound ? "held"
                                    : (scene.releases_tried ? "NOT HELD" : "not asked for"));
}
