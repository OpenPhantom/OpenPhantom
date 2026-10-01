/* mp_start.c: the lobby's way into a level, through the game's own front door. See the header.
 *
 * The two wishes carried out once a level is up, a pose and a hero, are mp_start_wish.c since
 * this file reached the hard limit: they reach the level flow here at one point only, the
 * question whether a level is running, and that question has a public name.
 */
#include "mp_start.h"
#include "mp_start_phase.h"
#include "mp_start_restore.h"
#include "mp_start_wish.h"

#include "mp_cells.h"
#include "mp_levels.h"
#include "mp_lobby.h"
#include "mp_respawn.h"
#include "mp_saves.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/patch.h"
#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The title screen's own widget ids, read out of its loop: the switch on the focused
 * id has one arm each for new game, load game, options and quit. */
#define TITLE_WIDGET_NEW_GAME  1
#define TITLE_WIDGET_LOAD_GAME 2

/* The navigation code the loop reads as "the focused widget was activated". */
#define SWNAV_NOTHING 0
#define SWNAV_ACCEPT  5

/* The player's ground contact block inside the hero block, the same 0x22 dwords the engine's own
 * teleport zeroes before it seats a body: the floor polygon and the mover the player rides live in
 * it as raw pointers. A restore into a process that has already run a level has to leave it
 * clean, or the first carry step walks a polygon of the world that was just freed. */
#define HERO_BLOCK_GROUND_CONTACT 0x2CCu
#define GROUND_CONTACT_POLY       0x14u
#define GROUND_CONTACT_BYTES      (0x22u * 4u)

/* The game mode cell: 2 is a level running. */
#define GAME_MODE_RUNNING 2u

/* The engine's own buffer for a level path taken verbatim. */
#define LOAD_NAME_BYTES 0x80u

/* save_restoregame at 0x0045158F answers 1 on failure and 0 on success, as its reconstruction
 * shows: a file that does not open, a header it does not accept, a level it cannot rebuild, a
 * module block the player aborted. Every one of those used to be swallowed by a hook that
 * answered "loaded" regardless, after which the campaign entered its level loop with no level. */
/* Two arguments, as the shipped LOAD arm calls it with (path, 1): the engine reads the second
 * at [ebp+0xc]. One argument left it undefined; cdecl kept the stack, the prototype lied. */
typedef uint32_t(__cdecl *save_load_fn)(const char *file, uint32_t flag);
typedef int32_t(__cdecl *load_screen_fn)(void);
typedef void(__cdecl *focus_by_id_fn)(int32_t widget_id);
typedef int32_t(__cdecl *focus_id_fn)(void);   /* which widget has the focus, -1 for none */

typedef struct start_state {
    bool installed;
    bool sites_missing_logged;

    focus_by_id_fn     focus_by_id;
    focus_id_fn        focus_id;      /* optional: without it the drive accepts unverified */
    save_load_fn       load_save;
    detour_t           load_screen_detour;
    bool               detoured;

    mp_start_machine_t machine;    /* the kind, the phase, and the two counts of one request */
    uint8_t         level_index;
    char            path[MP_LOBBY_LEVEL_MAX];   /* relative, as the wire carries it */
    char            save[MP_SAVES_FILE_MAX];
    bool            by_name_set;   /* the flag is ours to clear once a level runs */

    uint32_t focus_misses_total;
    uint32_t given_up;      /* requests abandoned because their screen never came up */
    void   (*given_up_listener)(void);   /* told when a request is abandoned */
    uint32_t ground_blocks_cleared;   /* restores that left a stale pointer in the ground block */
    uint32_t started;
    uint32_t refused;
    uint32_t restores;
    uint32_t restore_failures;   /* the engine answered 1 to a restore this module asked for */
    uint32_t steered;         /* rounds whose level was written into both engine counters */
} start_state_t;

static start_state_t start;

/* ==============================================================================================
 * The engine's cells, written only through here.
 * ============================================================================================ */

static bool write_u32(mp_cell_t cell, uint32_t value)
{
    uintptr_t address = mp_cells_address(cell);

    return address != 0u && patch_write_u32(address, value) == PATCH_RESULT_OK;
}

static bool read_u32(mp_cell_t cell, uint32_t *value)
{
    uintptr_t address = mp_cells_address(cell);

    return address != 0u && memory_try_read_u32(address, value);
}

bool mp_start_level_running(void)
{
    uint32_t mode = 0;

    return read_u32(MP_CELL_GAME_MODE, &mode) && mode == GAME_MODE_RUNNING;
}

/* The absolute path into the engine's own 128 byte buffer. The path is built against THIS
 * machine's data root, which is why the wire never carries an absolute one. */
static bool arm_by_name(const char *relative)
{
    char      absolute[MP_LEVELS_ABSOLUTE_MAX];
    uintptr_t buffer = mp_cells_address(MP_CELL_LOAD_NAME);
    size_t    length;

    if (buffer == 0u || !mp_levels_absolute(relative, absolute, sizeof absolute)) {
        return false;
    }
    length = strlen(absolute);
    if (length + 1u > LOAD_NAME_BYTES) {
        log_warning("the level path is too long for the engine's own buffer: %s", absolute);
        return false;
    }
    if (!memory_make_writable(buffer, LOAD_NAME_BYTES) ||
        !memory_try_write(buffer, absolute, length + 1u)) {
        return false;
    }
    if (!write_u32(MP_CELL_LOAD_BY_NAME, 1u)) {
        return false;
    }
    start.by_name_set = true;
    return true;
}

/* ==============================================================================================
 * The detour on the shipped load screen.
 * ============================================================================================ */

/* The restore's last phase seats the player through the engine's own teleport, which zeroes the
 * ground contact block first. In the field, a host that had played one level and then restored a
 * savegame in the same process died on the first substep in the carry step, reading the floor
 * polygon pointer of a world that no longer existed out of that block. Whether the teleport's
 * clear was undone afterwards or never reached is not known; what is known is that the block has
 * to be clean here, and the engine has already done everything it will do to it. A stale pointer
 * is said out loud with its value and cleared the way the teleport clears, so the log names the
 * case whichever it is. */
static void leave_the_ground_block_clean(const char *file)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  poly  = 0;
    uint8_t   zeros[GROUND_CONTACT_BYTES];

    if (block == 0u ||
        !memory_read_u32(block + HERO_BLOCK_GROUND_CONTACT + GROUND_CONTACT_POLY, &poly)) {
        return;
    }
    if (poly == 0u) {
        return;   /* as the teleport left it */
    }
    memset(zeros, 0, sizeof zeros);
    if (patch_write_bytes(block + HERO_BLOCK_GROUND_CONTACT, zeros, sizeof zeros)
            != PATCH_RESULT_OK) {
        log_error("the ground contact block still names polygon %08X after the restore of %s "
                  "and could not be cleared; the first substep will walk it", (unsigned)poly,
                  file);
        return;
    }
    ++start.ground_blocks_cleared;
    log_warning("the ground contact block still named polygon %08X after the restore of %s, "
                "which the engine's own teleport should have zeroed; it is cleared here, and the "
                "next ground probe fills it from the restored world", (unsigned)poly, file);
}

/* The title menu reads a 0 from this function as "a game was loaded" and answers 1, which makes
 * the campaign round skip its own level load. With a save chosen in the lobby the screen has
 * nothing to ask, so it restores and answers without ever drawing itself. */
static int32_t __cdecl hook_load_game_screen(void)
{
    load_screen_fn original = (load_screen_fn)start.load_screen_detour.original;

    if (start.machine.kind == MP_START_SAVE && start.save[0] != '\0' &&
        start.load_save != NULL) {
        char file[MP_SAVES_FILE_MAX];

        memcpy(file, start.save, sizeof file);
        mp_start_machine_consume(&start.machine);   /* one shot: a second visit is the player's */
        start.save[0] = '\0';
        ++start.restores;
        log_info("the lobby restores %s instead of showing the load screen", file);
        if (start.load_save(file, 1u) != 0u) {
            /* Anything but 0 leaves the title loop standing and the campaign enters no level. That
             * is not the same as nothing loaded: a restore that failed half way may already have
             * freed the old level, or built a new one it then abandoned. */
            ++start.restore_failures;
            log_error("the restore of %s FAILED inside the engine, so the title screen stays "
                      "and the campaign enters no level", file);
            return 1;
        }
        leave_the_ground_block_clean(file);
        mp_start_restore_keep_mode();   /* what the shipped LOAD arm does next, see the file */
        return 0;
    }
    return original();
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

bool mp_start_install(void)
{
    uintptr_t focus_site;
    uintptr_t save_site;
    uintptr_t screen_site;

    if (start.installed) {
        return true;
    }
    focus_site    = mp_signatures_address(MP_SITE_SWWIDGET_FOCUS_BY_ID);
    save_site     = mp_signatures_address(MP_SITE_SAVE_LOAD_NAMED);
    screen_site   = mp_signatures_address(MP_SITE_LOAD_GAME_SCREEN);

    if (focus_site == 0u) {
        log_warning("the lobby cannot start a level: the focus setter did not resolve");
        return false;
    }
    start.focus_by_id = (focus_by_id_fn)focus_site;
    start.focus_id    = (focus_id_fn)mp_signatures_address(MP_SITE_SWMENU_FOCUS_ID);
    start.load_save   = save_site != 0u ? (save_load_fn)save_site : NULL;
    (void)mp_start_restore_install();
    /* The pose and the hero resolve their own two entry points and name them when missing. */
    mp_start_wish_install();

    if (screen_site != 0u && start.load_save != NULL) {
        start.detoured = detour_install(&start.load_screen_detour, screen_site,
                                        (const void *)&hook_load_game_screen,
                                        mp_signatures_prologue(MP_SITE_LOAD_GAME_SCREEN));
    }
    if (!start.detoured) {
        log_warning("the lobby cannot start from a savegame: the load screen would not take a "
                    "hull, or the restore did not resolve. A level still starts");
    }
    start.installed = true;
    return true;
}

/* ==============================================================================================
 * Asking for a start.
 * ============================================================================================ */

static bool accept(mp_start_kind_t kind)
{
    mp_start_machine_ask(&start.machine, kind);
    ++start.started;
    return true;
}

bool mp_start_shipped(uint8_t level_index)
{
    const mp_level_t *level = mp_levels_shipped(level_index);

    if (!start.installed) {
        log_warning("no level can be started: this module never installed, so nothing resolved "
                    "the focus setter it drives the title menu with");
        ++start.refused;
        return false;
    }
    if (level == NULL) {
        log_warning("level %u is not in the catalogue: %u level(s) were found, and a start needs "
                    "one of them", (unsigned)level_index, (unsigned)mp_levels_count());
        ++start.refused;
        return false;
    }
    if (!mp_levels_present(level->path)) {
        log_warning("the level %s is in the game's table but not on this disk", level->path);
        ++start.refused;
        return false;
    }
    /* The table path, so the level index stays sane and finishing the level advances the campaign
     * the way the campaign advances it.
     *
     * BOTH level counters are written, and the second one is what actually decides this round. The
     * campaign round copies the start-level cell into the current-level cell BEFORE it enters the
     * title menu, and a lobby lives inside that menu, so a write to the start cell alone arrives
     * one round too late: the round loads whatever the copy caught, which is the first row of the
     * table. The current-level cell is the one the loader indexes with and the one the level
     * completion increments, so writing it is also what keeps the campaign advancing from here.
     *
     * The start cell is written as well, and it is not redundant. The arm that leaves a level for
     * the front end jumps back into the round over the engine's own clearing of that cell, and the
     * round then copies it again: what should stand there is the level this lobby chose, not the
     * first one. */
    if (!write_u32(MP_CELL_LOAD_BY_NAME, 0u) ||
        !write_u32(MP_CELL_START_LEVEL, (uint32_t)level_index) ||
        !write_u32(MP_CELL_LEVEL_INDEX, (uint32_t)level_index)) {
        ++start.refused;
        return false;
    }
    ++start.steered;
    start.level_index = level_index;
    memcpy(start.path, level->path, sizeof start.path);
    log_info("the lobby starts level %u (%s)", (unsigned)level_index, level->title);
    return accept(MP_START_SHIPPED);
}

bool mp_start_by_path(const char *relative)
{
    if (!start.installed) {
        log_warning("no level can be started: this module never installed");
        ++start.refused;
        return false;
    }
    if (relative == NULL || relative[0] == '\0') {
        log_warning("no level can be started: nothing was chosen");
        ++start.refused;
        return false;
    }
    /* The branch that takes this path ignores the loader's answer, so the file is confirmed here
     * or the start does not happen at all. */
    if (!mp_levels_present(relative)) {
        log_warning("the lobby will not start %s: there is no such file under the data root",
                    relative);
        ++start.refused;
        return false;
    }
    if (!arm_by_name(relative)) {
        ++start.refused;
        return false;
    }
    memset(start.path, 0, sizeof start.path);
    memcpy(start.path, relative, strlen(relative) < sizeof start.path
                                     ? strlen(relative) : sizeof start.path - 1u);
    start.level_index = 0xFFu;
    log_info("the lobby starts %s, taken verbatim", relative);
    return accept(MP_START_BY_PATH);
}

bool mp_start_save(const char *file)
{
    if (!start.installed || !start.detoured) {
        log_warning("no savegame can be restored: the module is %s and the shipped load screen is "
                    "%s", start.installed ? "installed" : "NOT installed",
                    start.detoured ? "hooked" : "NOT hooked");
        ++start.refused;
        return false;
    }
    if (file == NULL || file[0] == '\0') {
        log_warning("no savegame can be restored: none was chosen");
        ++start.refused;
        return false;
    }
    /* A choice out of the catalogue, or a file that reads as a save wherever it came from: the
     * one a client received from its host is the second kind, and it is exactly as much a save as
     * the first. What is refused is a name that is neither. */
    if (mp_saves_find(file) == NULL) {
        mp_save_t header;

        if (!mp_saves_read(file, &header)) {
            log_warning("the lobby will not restore %s: it is not one of the savegames found and "
                        "does not read as one", file);
            ++start.refused;
            return false;
        }
    }
    if (!write_u32(MP_CELL_LOAD_BY_NAME, 0u)) {
        ++start.refused;
        return false;
    }
    memset(start.save, 0, sizeof start.save);
    memcpy(start.save, file, strlen(file) < sizeof start.save
                                 ? strlen(file) : sizeof start.save - 1u);
    log_info("the lobby restores %s", start.save);
    return accept(MP_START_SAVE);
}

bool mp_start_level(const char *level, uint8_t level_index, bool from_save, const char *save)
{
    if (from_save && save != NULL && save[0] != '\0') {
        return mp_start_save(save);
    }
    if (level_index != (uint8_t)MP_LOBBY_LEVEL_CUSTOM && mp_levels_shipped(level_index) != NULL) {
        return mp_start_shipped(level_index);
    }
    return mp_start_by_path(level);
}

bool mp_start_pending(void)
{
    return mp_start_machine_pending(&start.machine);
}

mp_start_kind_t mp_start_kind(void)
{
    return start.machine.kind;
}

void mp_start_cancel(void)
{
    mp_start_machine_cancel(&start.machine);
    if (start.by_name_set) {
        (void)write_u32(MP_CELL_LOAD_BY_NAME, 0u);
        start.by_name_set = false;
    }
    start.save[0] = '\0';
}

/* ==============================================================================================
 * Driving the title menu.
 * ============================================================================================ */

void mp_start_set_given_up_listener(void (*listener)(void))
{
    start.given_up_listener = listener;
}

/* The drive gave up. The request goes, and whoever promised the start to others is told. */
static void give_up(void)
{
    mp_start_cancel();
    ++start.given_up;
    if (start.given_up_listener != NULL) {
        start.given_up_listener();
    }
}

/* One frame of the title menu, decided by the start's phase (mp_start_phase.h) and carried out
 * here: the focus set on the start widget, the accept pressed, or the request given up.
 *
 * The loop samples the focus BEFORE it asks for a code, so a focus set on one frame is the one
 * it reads on the next, and nothing is claimed to have happened on the frame it was set. The
 * focus is read back before the accept, because the title menu moves it to whatever widget the
 * pointer moves over: a hand on the mouse takes it back the frame after the drive set it, and
 * the accept would then activate that widget. The drive sets it again and waits a frame, which a
 * line in the log says once. A savegame whose accept never reaches the load screen is driven
 * again, a bounded number of times: without the bound one accept went into whatever menu was
 * open, three reads apart, for the rest of the process. */
int32_t mp_start_drive(int32_t code)
{
    mp_start_phase_t before;
    int32_t          widget;
    int32_t          focused = -1;
    bool             holds   = true;

    if (!start.installed) {
        return code;
    }
    widget = start.machine.kind == MP_START_SAVE ? TITLE_WIDGET_LOAD_GAME : TITLE_WIDGET_NEW_GAME;
    if (mp_start_machine_wants_focus(&start.machine) && start.focus_id != NULL) {
        focused = start.focus_id();
        holds   = focused == widget;
    }
    before = start.machine.phase;
    switch (mp_start_machine_drive(&start.machine, holds)) {
    case MP_START_ACT_FOCUS:
        if (before == MP_START_PHASE_FOCUS_SET) {   /* the focus was found elsewhere */
            ++start.focus_misses_total;
            if (start.machine.focus_misses == 1u) {
                log_warning("the title's focus is on widget %d and not on %d, which the "
                            "lobby's start needs; the mouse pointer is probably resting on "
                            "it. The focus is set again every frame until it holds",
                            (int)focused, (int)widget);
            }
        }
        start.focus_by_id(widget);
        return SWNAV_NOTHING;
    case MP_START_ACT_ACCEPT:
        return SWNAV_ACCEPT;
    case MP_START_ACT_GIVE_UP:
        if (before == MP_START_PHASE_FOCUS_SET) {
            ++start.focus_misses_total;
            log_warning("the title's focus stayed away from widget %d for %u frames, so the "
                        "lobby's start is given up rather than pressed into whatever the "
                        "pointer is on", (int)widget, (unsigned)MP_START_FOCUS_MISSES_MAX);
        } else {
            log_warning("the lobby's start was driven %u times and the screen it aims at never "
                        "came up, so it is given up rather than pressed into the menu for ever",
                        (unsigned)MP_START_DRIVE_ROUNDS);
        }
        give_up();
        return code;
    case MP_START_ACT_PASS:
    default:
        return code;
    }
}

void mp_start_tick(void)
{
    if (start.by_name_set && mp_start_level_running()) {
        /* The round that loaded is over and the level is up. The engine's own save restore sets
         * this flag and clears it around one call; the campaign round never clears it, so a flag
         * left standing would reload this same path at the start of every later round. */
        (void)write_u32(MP_CELL_LOAD_BY_NAME, 0u);
        start.by_name_set = false;
    }
    mp_start_wish_tick();
}

void mp_start_report(void)
{
    if (!start.installed) {
        /* Said rather than skipped. A silent report is how this module once shipped built, tested
         * and never called, and the line that was missing is this one. */
        log_warning("  the start: this module never installed, so no level, pose or hero of the "
                    "lobby's choosing ever reached the engine");
        return;
    }
    log_info("  the start: %u asked for, %u refused, %u savegame restore(s) with %u failed inside "
             "the engine and %u that left a stale ground pointer, %u level(s) written into both "
             "engine counters, %u given up after driving, %u frame(s) the title's focus was "
             "found elsewhere; the load screen is %s",
             (unsigned)start.started, (unsigned)start.refused, (unsigned)start.restores,
             (unsigned)start.restore_failures, (unsigned)start.ground_blocks_cleared,
             (unsigned)start.steered, (unsigned)start.given_up,
             (unsigned)start.focus_misses_total,
             start.detoured ? "driven from the lobby" : "not hooked");
    mp_start_wish_report();
}
