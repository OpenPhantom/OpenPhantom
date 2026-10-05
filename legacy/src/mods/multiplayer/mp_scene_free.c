/* mp_scene_free.c: the one place a player is given back what a scene holds of him. See the header
 * for the order and for the engine function each thing goes through. What is here: the binding of
 * the two releases with their byte evidence, the look, and the carrying out with its line and its
 * counts.
 */
#include "mp_scene_free.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_dialog_relay.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_relay.h"
#include "mp_respawn.h"
#include "mp_scene_bind.h"
#include "mp_scene_hero_watch.h"
#include "mp_scene_rule.h"
#include "mp_session_now.h"
#include "mp_signatures_scene.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Dialog_LeaveInputLock (level), cdecl, answering whether it released anything. */
typedef int32_t(__cdecl *release_fn_t)(int32_t level);

/* bapview_overrideOff (), cdecl: fifteen bytes that store a constant nought. */
typedef void(__cdecl *camera_off_fn_t)(void);

#define CAMERA_OFF_BYTES 15u

/* The level the lock is released at: the one the end of the camera dolly opcode pushes, `6A 63`,
 * which lets go of a lock at any level a script or a savegame can have left. */
#define RELEASE_LEVEL 99

/* The two script ends, the dolly's and the lock opcode's, in this order everywhere below. */
#define END_DOLLY 0u
#define END_LOCK  1u

/* A line for what was given back this many times in a process, and after that the counts; and
 * a smaller budget of its own for a plan that was not carried out whole. */
#define LINES_MAX    32u
#define WARNINGS_MAX 8u

/* The bits of a plan as rows of the counts. */
enum {
    AT_LOCK = 0,
    AT_BARS,
    AT_CAMERA,
    AT_INPUT_MODE,
    AT_MODULE,
    AT_ACTOR,
    AT_STORE,
    AT_MODULE_ALONE
};

_Static_assert((1u << AT_LOCK) == MP_SCENE_FREE_LOCK && (1u << AT_BARS) == MP_SCENE_FREE_BARS &&
                   (1u << AT_CAMERA) == MP_SCENE_FREE_CAMERA &&
                   (1u << AT_INPUT_MODE) == MP_SCENE_FREE_INPUT_MODE &&
                   (1u << AT_MODULE) == MP_SCENE_FREE_MODULE &&
                   (1u << AT_ACTOR) == MP_SCENE_FREE_ACTOR &&
                   (1u << AT_STORE) == MP_SCENE_FREE_STORE &&
                   (1u << AT_MODULE_ALONE) == MP_SCENE_FREE_MODULE_ALONE &&
                   (uint32_t)AT_MODULE_ALONE + 1u == MP_SCENE_FREE_BITS,
               "the rows of the counts are the bits of a plan");

typedef struct free_state {
    bool            installed;
    release_fn_t    release;      /* NULL where it did not resolve, or a script end disagrees */
    camera_off_fn_t camera_off;   /* NULL where the two script ends did not prove it */
    uintptr_t       pr_cell;

    uint32_t given[MP_SCENE_FREE_BITS];
    uint32_t not_given[MP_SCENE_FREE_BITS];
    uint32_t module_not_running;   /* an actor removed on a client, and the module did not run */
    uint32_t stale_drivers;        /* a look at a driver that was not its placement's live actor */
    uint32_t lines;
    uint32_t warnings;
    bool     said_stale;
    bool     said_not_running;
} free_state_t;

static free_state_t f;

/* What one carrying out learned on the way, for its line. */
typedef struct free_said {
    bool    client;
    int     placement;   /* of the actor that drove the body, -1 for none */
    int32_t level;       /* the lock's, before it was released */
    int32_t mode;        /* the input mode, before it was set */
} free_said_t;

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* The two script ends: where each one's call of the lock's release returns to, counted from the
 * camera take the scene gates resolved for that opcode, and the twelve bytes in front of it. An
 * end whose take did not resolve, or whose bytes do not read, is left unread. */
static void read_the_script_ends(mp_scene_free_end_t ends[MP_SCENE_FREE_ENDS])
{
    mp_cutscene_doors_t doors = { 0u, 0u, 0u };
    size_t              end;

    memset(ends, 0, MP_SCENE_FREE_ENDS * sizeof ends[0]);
    mp_cutscene_doors(&doors);
    if (doors.dolly_take_return != 0u) {
        ends[END_DOLLY].return_address = doors.dolly_take_return +
                                         SCENE_DOLLY_RELEASE_PAST_THE_TAKE;
    }
    if (doors.lock_take_return != 0u) {
        ends[END_LOCK].return_address = doors.lock_take_return + SCENE_LOCK_RELEASE_PAST_THE_TAKE;
    }
    for (end = 0u; end < MP_SCENE_FREE_ENDS; ++end) {
        ends[end].read = ends[end].return_address > MP_SCENE_FREE_END_BYTES &&
                         memory_read(ends[end].return_address - MP_SCENE_FREE_END_BYTES,
                                     ends[end].bytes, MP_SCENE_FREE_END_BYTES);
    }
}

/* The two releases.
 *
 * The lock's, Dialog_LeaveInputLock, is found by its own pattern, which sifts the tail where
 * another module's branch stands on the head, and is held against the two script ends, each of
 * which calls it. It is called at its head and never hulled here.
 *
 * The clearing of the camera's override, bapview_overrideOff, has no pattern of its own: it is
 * fifteen bytes, and a hull of another module takes thirteen of them. It is read out of the two
 * script ends. The retail image, the dolly's end first and the lock opcode's second:
 *
 *   00434F46  E8 BF 34 FE FF   call 0041840A (overrideOn)      returns to 00434F4B
 *   00434F4B  83 C4 04 / EB 19
 *   00434F50  E8 CC 34 FE FF   call 00418421 (overrideOff)
 *   00434F55  6A 63            push 99
 *   00434F57  E8 BC BF FF FF   call 00430F18 (the release)     returns to 00434F5C
 *
 *   00434F8B  E8 7A 34 FE FF   call 0041840A (overrideOn)      returns to 00434F90
 *   00434F90  83 C4 04 / 6A 05 / E8 3F BF FF FF (the lock's entry 00430ED9) / 83 C4 04 / EB 0F
 *   00434F9F  E8 7D 34 FE FF   call 00418421 (overrideOff)
 *   00434FA4  6A 05            push 5
 *   00434FA6  E8 6D BF FF FF   call 00430F18 (the release)     returns to 00434FAB
 *
 * 00434F5C less twelve is 00434F50 and 00434FAB less twelve is 00434F9F, and both calls there
 * name 00418421. Read the same way, both ends give these two addresses in all six executables
 * this tree is checked against, the Edit Tool's recompile included. The reading of those twelve
 * bytes is mp_scene_free_read_ends, pure; what is asked here on top of it is that the address
 * both ends name lies inside the image. Anything less leaves the camera alone.
 *
 * Whether an override stands is not read. In this function the cell is the operand of the store
 * that is its whole body, `C7 05 E8 B4 5B 00 00 00 00 00` at 00418424 in the retail image, and a
 * module that hulls the function overwrites exactly those bytes; reading the cell would take a
 * pattern of another function that names it. */
static bool bind_the_releases(const mp_scene_free_end_t ends[MP_SCENE_FREE_ENDS],
                              mp_scene_free_ends_t *said)
{
    uintptr_t address = signature_find_detour_target(SIG_SCENE_LOCK_LEAVE, MSK_SCENE_LOCK_LEAVE,
                                                     sizeof SIG_SCENE_LOCK_LEAVE,
                                                     SCENE_LOCK_LEAVE_PROLOGUE);

    mp_scene_free_read_ends(address, ends, said);
    if (address == 0u) {
        log_warning("the release of what a scene holds is not bound: the lock's release did not "
                    "resolve, so nothing here lets go of a lock or clears the camera's override, "
                    "and a client a savegame left locked in the middle of a scene stays locked");
        return false;
    }
    if (!said->release_holds) {
        log_warning("the release of what a scene holds is not bound: the lock's release "
                    "resolves at %08X by its pattern and a script end calls %08X in its place, "
                    "so nothing here lets go of a lock or clears the camera's override, and a "
                    "client a savegame left locked in the middle of a scene stays locked",
                    (unsigned)address, (unsigned)said->disagreeing);
        return false;
    }
    f.release = (release_fn_t)address;
    if (said->camera_off != 0u && memory_is_inside_image(said->camera_off, CAMERA_OFF_BYTES)) {
        f.camera_off = (camera_off_fn_t)said->camera_off;
    }
    return true;
}

bool mp_scene_free_install(void)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;

    if (f.installed) {
        return f.release != NULL;
    }
    f.installed = true;
    f.pr_cell   = mp_cells_address(MP_CELL_PR);
    read_the_script_ends(ends);
    if (!bind_the_releases(ends, &said)) {
        return false;
    }
    if (f.camera_off != NULL) {
        log_info("the release of what a scene holds is bound: the lock's release at %08X, "
                 "called at its head and held against %u of the 2 script ends (they return to "
                 "%08X and %08X); the camera's override is cleared at %08X, the address both "
                 "ends call %u bytes before that return",
                 (unsigned)(uintptr_t)f.release, (unsigned)said.witnesses,
                 (unsigned)ends[END_DOLLY].return_address,
                 (unsigned)ends[END_LOCK].return_address, (unsigned)(uintptr_t)f.camera_off,
                 (unsigned)MP_SCENE_FREE_END_BYTES);
    } else {
        log_warning("the release of what a scene holds is bound without the camera: the lock's "
                    "release at %08X, called at its head and held against %u of the 2 script "
                    "ends (they return to %08X and %08X), which do not both name one address "
                    "inside the image for the clearing of the camera's override (read: %08X), so "
                    "a camera a scene left taken stays taken until the engine clears it itself",
                    (unsigned)(uintptr_t)f.release, (unsigned)said.witnesses,
                    (unsigned)ends[END_DOLLY].return_address,
                    (unsigned)ends[END_LOCK].return_address, (unsigned)said.camera_off);
    }
    return true;
}

/* ==============================================================================================
 * The look.
 * ============================================================================================ */

/* The actor that drives this machine's player's body, asked of its own record as well: the
 * finding is a remembered address confirmed by two reads of the actor, and a slot the pool took
 * back keeps its bytes. One that is not its placement's live actor drives nobody. */
static uintptr_t live_driver(uint32_t substep, uint32_t *key)
{
    uintptr_t driver = mp_scene_hero_watch_driver(substep);

    *key = 0u;
    if (driver == 0u) {
        return 0u;
    }
    if (mp_enemy_bind_index(driver, key) && mp_enemy_bind_is_live(driver, *key, NULL)) {
        return driver;
    }
    ++f.stale_drivers;
    if (!f.said_stale) {
        f.said_stale = true;
        log_warning("the actor remembered as driving this player's body, %08X carrying "
                    "placement %u, is not that placement's live actor, so nothing is removed "
                    "for it and a module parked under it stays parked", (unsigned)driver,
                    (unsigned)*key);
    }
    return 0u;
}

void mp_scene_free_look(mp_scene_free_look_t *out, uint32_t substep)
{
    uint32_t key = 0u;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->client       = mp_session_now_client_of_a_started_session(NULL, NULL);
    out->menu_open    = mp_scene_bind_menu_open();
    /* A lock is released only where an open menu can be told: without the menu's cell the lock
     * reads as one that cannot be released. The camera likewise where a gun cannot be told. */
    out->lock_level   = f.release != NULL && mp_scene_bind_menu_known() ? mp_cutscene_lock_level()
                                                                        : -1;
    out->input_mode   = mp_scene_bind_input_mode();
    out->bars_on      = mp_cutscene_bars_on();
    out->camera_bound = f.camera_off != NULL && mp_scene_bind_gun_known();
    out->dead         = mp_respawn_player_is_a_corpse();
    out->at_gun       = mp_scene_bind_mode_is_gun();
    out->bank_window  = mp_bank_active() != 0u;
    mp_scene_bind_module_cells(&out->module, &out->store, &out->has_body);
    out->driven       = live_driver(substep, &key) != 0u;
    out->conversation = mp_dialog_relay_answers_open();
}

/* ==============================================================================================
 * The carrying out.
 * ============================================================================================ */

/* The engine's store of a parked module, set to nought. The bank first, because this writes:
 * while a bank is active the record is a far body's. */
static bool clear_the_store(void)
{
    uint32_t block = 0u;

    return mp_bank_active() == 0u && f.pr_cell != 0u && memory_try_read_u32(f.pr_cell, &block) &&
           block != 0u &&
           memory_try_write((uintptr_t)block + MP_HERO_BLOCK_SAVED_MODULE_STATE,
                            &(uint32_t){ 0u }, sizeof(uint32_t));
}

/* A client's module under an actor: the actor leaves through the engine's own removal.
 *
 * What enemy_delete 0x00437850 does for an actor that carries the handover bit, read off its
 * body: it calls player_resume 0x00450FF1 first and turns whatever reason it was given into 3,
 * the release of the player. player_resume writes the module state out of the engine's store,
 * takes the record's position and rotation from the body, sets the body's two low flag bits and
 * puts the blade of the two sabre heroes back. It runs through the put-back's gate, which lets it
 * through on a client because the store holds a state. Where the player's body is the speaker of
 * a line, the line is cut off. For reason 3 the placement's spawn state is left alone and its
 * live word cleared, the actor's emitter goes, the body is not freed, and the slot goes back to
 * the pool.
 *
 * So the client's body is neither freed nor moved, its module runs again, and no actor is left
 * holding a pointer to that body. Parking the actor instead would leave exactly that: the enemy
 * sync lets a parked actor go that the host does not list, a respawn frees the body, and the
 * actor then ticks with a pointer to an object that is gone. The host cannot have the actor
 * built here again, because a placement with the handover bit is never created for the host.
 *
 * The engine never clears its store, so it is cleared here. Left standing, the next removal of
 * such an actor would write the module again, over whatever it is by then. */
static bool remove_the_driver_here(uint32_t substep, free_said_t *said)
{
    uint32_t  key    = 0u;
    uint32_t  module = 0u;
    uintptr_t driver = live_driver(substep, &key);

    if (driver == 0u || !mp_enemy_relay_remove_here(driver, key)) {
        return false;
    }
    said->placement = (int)key;
    (void)clear_the_store();
    if (!mp_scene_bind_module_state(&module) || module != MP_HERO_MODULE_RUNNING) {
        ++f.module_not_running;
        if (!f.said_not_running) {
            f.said_not_running = true;
            log_warning("the actor that drove this player's body was removed, placement %u, "
                        "and the player module reads %u after it, not running: the engine's "
                        "put-back was refused at its gate or found no body", (unsigned)key,
                        (unsigned)module);
        }
    }
    return true;
}

/* The same picture on a host: the engine is asked to remove the actor itself, at the end of the
 * actor's own next tick, by the reason a script's own removal writes. The script runs once more
 * before that. */
static bool ask_the_driver_to_leave(uint32_t substep, free_said_t *said)
{
    uint32_t  key    = 0u;
    uintptr_t driver = live_driver(substep, &key);

    if (driver == 0u || !mp_enemy_bind_remove_by_engine(driver, key)) {
        return false;
    }
    said->placement = (int)key;
    return true;
}

/* The lock and the input mode. The engine's release sets the mode to play as the lock falls, so
 * a mode planned along with the lock is dropped from the plan once it reads play: it was the
 * release's to set, and is neither given back here nor missed. The setter is called only with
 * the lock at nought, because under a lock the mode is the lock's. */
static uint32_t free_the_lock(uint32_t *plan, free_said_t *said)
{
    uint32_t given = 0u;

    if ((*plan & MP_SCENE_FREE_LOCK) != 0u) {
        said->level = mp_cutscene_lock_level();
        if (f.release != NULL && f.release(RELEASE_LEVEL) != 0) {
            given |= MP_SCENE_FREE_LOCK;
        }
    }
    if ((*plan & MP_SCENE_FREE_INPUT_MODE) != 0u) {
        said->mode = mp_scene_bind_input_mode();
        if (said->mode != MP_SCENE_INPUT_MODE_LOCK) {
            *plan &= ~MP_SCENE_FREE_INPUT_MODE;
        } else if (mp_cutscene_lock_level() == 0 && mp_scene_bind_set_play()) {
            given |= MP_SCENE_FREE_INPUT_MODE;
        }
    }
    return given;
}

/* The store alone, and a stopped module nobody drives. The engine's put-back writes the module
 * out of its store without asking what the module is, so it is called only for one that reads
 * stopped; with nothing in the store the module is set running by the one place that does. */
static uint32_t free_the_record(uint32_t plan)
{
    uint32_t given    = 0u;
    uint32_t module   = 1u;
    uint32_t store    = 0u;
    bool     has_body = false;

    if ((plan & MP_SCENE_FREE_STORE) != 0u && clear_the_store()) {
        given |= MP_SCENE_FREE_STORE;
    }
    if ((plan & MP_SCENE_FREE_MODULE_ALONE) != 0u) {
        mp_scene_bind_module_cells(&module, &store, &has_body);
        if (module == 0u && has_body &&
            (store != 0u ? mp_cutscene_engine_resume() : mp_cutscene_module_back())) {
            given |= MP_SCENE_FREE_MODULE_ALONE;
        }
    }
    return given;
}

static void count(uint32_t plan, uint32_t given)
{
    uint32_t row;

    for (row = 0u; row < MP_SCENE_FREE_BITS; ++row) {
        uint32_t bit = 1u << row;

        if ((given & bit) != 0u) {
            ++f.given[row];
        } else if ((plan & bit) != 0u) {
            ++f.not_given[row];
        }
    }
}

/* One line for what was given back. Each part is a short text of its own, so the line names
 * only what went; on a client of a started session it says what that is there, something a
 * scene left behind, because no scene holds a client. */
static void say(uint32_t plan, uint32_t given, const free_said_t *said)
{
    char        actor[64] = "";
    char        lock[64]  = "";
    char        mode[64]  = "";
    const char *camera    = (given & MP_SCENE_FREE_CAMERA) != 0u
                                ? "the camera's override cleared (whether one stood is not read), "
                                : "";
    const char *bars      = (given & MP_SCENE_FREE_BARS) != 0u ? "the bars taken down, " : "";
    const char *store     = (given & MP_SCENE_FREE_STORE) != 0u
                                ? "the engine's store of a parked module cleared, " : "";
    const char *alone     = (given & MP_SCENE_FREE_MODULE_ALONE) != 0u
                                ? "a stopped module nobody drove set running, " : "";

    if (given != plan && f.warnings < WARNINGS_MAX) {
        ++f.warnings;
        log_warning("a release of what a scene holds was not carried out whole: the plan was "
                    "%04X and %04X of it was given back (the lock's release let go of nothing, "
                    "the actor was not removed, the input mode's setter is not bound, the store "
                    "would not write or the put-back did nothing)", (unsigned)plan,
                    (unsigned)given);
    }
    if (given == 0u || f.lines >= LINES_MAX) {
        return;
    }
    ++f.lines;
    if ((given & MP_SCENE_FREE_MODULE) != 0u) {
        (void)text_format(actor, sizeof actor,
                          "the actor driving its body removed (placement %d), ", said->placement);
    } else if ((given & MP_SCENE_FREE_ACTOR) != 0u) {
        (void)text_format(actor, sizeof actor,
                          "the actor driving its body told to leave (placement %d), ",
                          said->placement);
    }
    if ((given & MP_SCENE_FREE_LOCK) != 0u) {
        (void)text_format(lock, sizeof lock, "the lock released at level %d, ", (int)said->level);
    }
    if ((given & MP_SCENE_FREE_INPUT_MODE) != 0u) {
        (void)text_format(mode, sizeof mode, "the input mode %d set to play, ", (int)said->mode);
    }
    if (said->client) {
        log_info("this player was held by what a scene left behind and is let go at once: "
                 "%s%s%s%s%s%s%sthe plan was %04X and %04X of it was given back", actor, camera,
                 lock, mode, bars, store, alone, (unsigned)plan, (unsigned)given);
    } else {
        log_info("this player is let go of what a scene held: %s%s%s%s%s%s%sthe plan was %04X "
                 "and %04X of it was given back", actor, camera, lock, mode, bars, store, alone,
                 (unsigned)plan, (unsigned)given);
    }
}

uint32_t mp_scene_free_now(uint32_t plan, uint32_t substep)
{
    free_said_t said;
    uint32_t    given = 0u;

    if (!f.installed || plan == 0u) {
        return 0u;
    }
    memset(&said, 0, sizeof said);
    said.client    = mp_session_now_client_of_a_started_session(NULL, NULL);
    said.placement = -1;
    if ((plan & MP_SCENE_FREE_MODULE) != 0u && remove_the_driver_here(substep, &said)) {
        given |= MP_SCENE_FREE_MODULE;
    }
    if ((plan & MP_SCENE_FREE_ACTOR) != 0u && ask_the_driver_to_leave(substep, &said)) {
        given |= MP_SCENE_FREE_ACTOR;
    }
    if ((plan & MP_SCENE_FREE_CAMERA) != 0u && f.camera_off != NULL) {
        f.camera_off();
        given |= MP_SCENE_FREE_CAMERA;
    }
    given |= free_the_lock(&plan, &said);
    if ((plan & MP_SCENE_FREE_BARS) != 0u) {
        mp_cutscene_engine_bars(false);
        given |= MP_SCENE_FREE_BARS;
    }
    given |= free_the_record(plan);
    count(plan, given);
    say(plan, given, &said);
    return given;
}

bool mp_scene_free_fall_camera(void)
{
    if (!f.installed || f.camera_off == NULL) {
        return false;
    }
    f.camera_off();
    return true;
}

void mp_scene_free_report(void)
{
    if (!f.installed) {
        log_info("  the scene release: not bound");
        return;
    }
    log_info("  the scene release: %u lock(s) released, %u time(s) the bars taken down, %u "
             "camera override(s) cleared, %u input mode(s) set to play, %u actor(s) on a "
             "client's body removed, %u actor(s) on the host's body told to leave, %u store(s) "
             "cleared, %u stopped module(s) nobody drove set running%s%s",
             (unsigned)f.given[AT_LOCK], (unsigned)f.given[AT_BARS], (unsigned)f.given[AT_CAMERA],
             (unsigned)f.given[AT_INPUT_MODE], (unsigned)f.given[AT_MODULE],
             (unsigned)f.given[AT_ACTOR], (unsigned)f.given[AT_STORE],
             (unsigned)f.given[AT_MODULE_ALONE],
             f.release != NULL ? "" : " (the lock's release is not bound)",
             f.camera_off != NULL ? "" : " (the camera's is not bound)");
    log_info("  the scene release, planned and not given back: %u lock(s), %u camera override(s), "
             "%u input mode(s), %u actor(s) on a client, %u on the host, %u store(s), %u "
             "module(s); %u actor(s) removed with the module not running after, %u look(s) at "
             "a driver that was not its placement's live actor",
             (unsigned)f.not_given[AT_LOCK], (unsigned)f.not_given[AT_CAMERA],
             (unsigned)f.not_given[AT_INPUT_MODE], (unsigned)f.not_given[AT_MODULE],
             (unsigned)f.not_given[AT_ACTOR], (unsigned)f.not_given[AT_STORE],
             (unsigned)f.not_given[AT_MODULE_ALONE], (unsigned)f.module_not_running,
             (unsigned)f.stale_drivers);
}
