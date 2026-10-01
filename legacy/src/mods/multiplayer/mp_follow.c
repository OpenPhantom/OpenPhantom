/* mp_follow.c: the host changes the world, and its clients go with it. See the header. */
#include "mp_follow.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_cells.h"
#include "mp_follow_difficulty.h"
#include "mp_settings.h"
#include "mp_start.h"
#include "mp_world_door.h"
#include "mp_levels.h"
#include "mp_lobby.h"
#include "mp_saves.h"
#include "mp_signatures.h"
#include "mp_start.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The game mode cell's "this level is complete". Written after the restore flag it means "and go
 * back to the title screen": the pair the engine's own "leave level" writes, and the one the end
 * of a session writes. */
#define GAME_MODE_LEVEL_DONE 3u

/* The restore by name, as the shipped load arm calls it: the file and a flag, and 0 for done. */
typedef uint32_t(__cdecl *restore_fn_t)(const char *file, uint32_t flag);

typedef struct follow_state {
    bool         installed;
    detour_t     restore_detour;
    restore_fn_t restore_original;

    /* The host's half. */
    bool     begin_seen;        /* a level has begun under the started session */
    uint8_t  begin_generation;  /* and the generation it began under */
    char     restoring[MP_SAVES_FILE_MAX];   /* the file a restore running now was handed */
    uint32_t changes;           /* world changes announced */
    uint32_t changes_from_save;
    uint32_t changes_as_before; /* a level the engine could not name, announced as the last one */
    uint32_t changes_at_movie;  /* announced as the host's movie began, not at its level begin */
    uint32_t movies_not_opening; /* a host's movie in a changed world that opened no level */

    /* The client's half. */
    bool     leaving;
    bool     at_title;          /* and the campaign has said it is on its way to the title */
    bool     left_known;
    uint8_t  left_generation;   /* the generation this side last left a level for */
    bool     leave_again_logged;
    bool     leave_fault_logged;
    bool     file_wait_logged;
    uint32_t leaves;
    uint32_t leave_faults;
    uint32_t starts;
    uint32_t start_refusals;
    uint32_t file_waits;

    /* The hero the pick is put on with, at every level begin of the session. */
    uint32_t heroes_asked;
    uint32_t heroes_refused;
} follow_state_t;

static follow_state_t follow;

/* ==============================================================================================
 * The pure decisions.
 * ============================================================================================ */

mp_follow_step_t mp_follow_client_step(bool new_world, bool level_running, bool leaving,
                                       bool at_title, bool start_pending)
{
    if (!new_world) {
        return MP_FOLLOW_NOTHING;
    }
    if (level_running) {
        return leaving ? MP_FOLLOW_WAIT : MP_FOLLOW_LEAVE;
    }
    if (!leaving) {
        return MP_FOLLOW_NOTHING;
    }
    return (!at_title || start_pending) ? MP_FOLLOW_WAIT : MP_FOLLOW_START;
}

bool mp_follow_host_changed_world(bool started, uint8_t generation, bool seen, uint8_t last)
{
    return started && seen && generation == last;
}

/* The ending's own movie, which plays after the last level and opens nothing. */
#define ENDING_MOVIE "scene8"

static char folded(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Two names, whole, the way the engine compares a file name: without regard to case. */
static bool same_name(const char *a, const char *b)
{
    while (*a != '\0' && folded(*a) == folded(*b)) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool mp_follow_movie_opens_level(const char *stem, const char *film)
{
    const char *base;
    const char *scan;

    if (stem == NULL || film == NULL || stem[0] == '\0' || same_name(stem, ENDING_MOVIE)) {
        return false;
    }
    base = film;
    for (scan = film; *scan != '\0'; ++scan) {
        if (*scan == '\\' || *scan == '/') {
            base = scan + 1;
        }
    }
    return same_name(stem, base);
}

/* ==============================================================================================
 * The host's half.
 * ============================================================================================ */

/* Every restore by name, the lobby's and the pause menu's, passes here. The file is kept for the
 * length of the call, because the level begin that makes a restore a world change arrives inside
 * it, and a restore that fails before its level begins must not name the next level that does. */
static uint32_t __cdecl hook_restore(const char *file, uint32_t flag)
{
    uint32_t answer;
    size_t   at = 0;

    memset(follow.restoring, 0, sizeof follow.restoring);
    while (file != NULL && at + 1u < sizeof follow.restoring && file[at] != '\0') {
        follow.restoring[at] = file[at];
        ++at;
    }
    if (file != NULL && file[at] != '\0') {
        follow.restoring[0] = '\0';   /* longer than any save this build names: none of ours */
    }
    answer = follow.restore_original(file, flag);
    follow.restoring[0] = '\0';
    mp_follow_difficulty_note_restore();   /* the restore put the file's difficulty back */
    return answer;
}

static bool read_u32(mp_cell_t cell, uint32_t *value)
{
    uintptr_t address = mp_cells_address(cell);

    return address != 0u && memory_read_u32(address, value);
}

/* A level of the game's own table into the note, by its index. */
static bool name_shipped(mp_lobby_setup_t *next, uint32_t index)
{
    const mp_level_t *level = index <= 0xFFu ? mp_levels_shipped((uint8_t)index) : NULL;

    if (level == NULL) {
        return false;
    }
    next->level_index = (uint8_t)index;
    mp_lobby_clean_field(level->path, next->level, sizeof next->level);
    mp_lobby_clean_field(level->title, next->title, sizeof next->title);
    return true;
}

/* The world the host is in now, under a new generation. Out of the savegame when this level began
 * inside a restore: its file is offered as the lobby offers one and the level is the one its header
 * names. Otherwise the level the engine's own counter says it is playing; a level loaded by path
 * has no index there, and a change inside it is that level begun again, so the note keeps it. */
static void announce_the_new_world(const mp_lobby_setup_t *current)
{
    mp_lobby_setup_t next = *current;
    mp_save_t        header;
    uint32_t         index = 0;

    if (follow.restoring[0] != '\0' && mp_saves_read(follow.restoring, &header)) {
        next.flags |= (uint8_t)MP_LOBBY_F_FROM_SAVE;
        if (!mp_bridge_savefile_offer(follow.restoring, &next.save_id, &next.save_bytes)) {
            next.save_id    = 0u;
            next.save_bytes = 0u;
        }
        if (!name_shipped(&next, header.level_index)) {
            ++follow.changes_as_before;
        }
        ++follow.changes_from_save;
    } else {
        next.flags     &= (uint8_t)~(uint8_t)MP_LOBBY_F_FROM_SAVE;
        next.save_id    = 0u;
        next.save_bytes = 0u;
        mp_bridge_savefile_withdraw();
        if (!read_u32(MP_CELL_LEVEL_INDEX, &index) || !name_shipped(&next, index)) {
            ++follow.changes_as_before;
        }
    }
    ++follow.changes;
    log_info("the host is in a new world, %s (%s)%s; its players follow it there", next.title,
             next.level, (next.flags & MP_LOBBY_F_FROM_SAVE) != 0u ? ", out of a savegame" : "");
    mp_bridge_lobby_set_setup(&next);
    mp_bridge_lobby_start();   /* the new generation, and the line that says so */
}

/* The difficulty the damage table is indexed by, said once at every level begin on both sides.
 *
 * It is campaign progress rather than a setting: the engine raises it by one for every level
 * completed and puts it back to four when a new game begins. A client that follows its host
 * through the title screen takes the second path while the host takes the first, so the two sides
 * can index different columns of the damage table without anything on either screen saying so.
 * The number is printed rather than inferred, because the sides only differ after a level end and
 * nobody would look for it there. */
static void say_the_difficulty(void)
{
    uintptr_t cell  = mp_cells_address(MP_CELL_IMPACT_DIFFICULTY);
    uint32_t  value = 0;

    if (cell != 0u && memory_read_u32(cell, &value)) {
        log_info("a level begins on this side at difficulty %u", (unsigned)value);
    }
}

/* The hero a player picked, put on at EVERY level begin of a session.
 *
 * The engine decides a hero at every level begin of its own: a fresh level takes the one its
 * header names, and a restore takes the one the savegame was written with. The lobby screen used
 * to be the only place that asked for the pick instead, so the pick survived exactly one level: a
 * client that followed its host into the next world played whatever that world prescribed, and
 * the player list went on showing the choice. That is one entrance into a state with three doors,
 * and this is the door all three go through, because the engine's own level begin broadcast is
 * what every one of them ends in.
 *
 * The session is read from the setup note rather than from the drain: at a level begin no substep
 * of this level has run yet, so `joined` is still whatever the last one left, and the note is the
 * thing the host actually said. */
static void put_on_the_chosen_hero(void)
{
    mp_lobby_setup_t setup;
    bool             is_host;

    if (!mp_bridge_lobby_setup(&setup) || (setup.flags & MP_LOBBY_F_STARTED) == 0u ||
        (setup.flags & MP_LOBBY_F_ENDED) != 0u) {
        return;   /* no session's level: the engine's own hero stands, as in single player */
    }
    is_host = !mp_bridge_drain_is_client();
    if (!mp_settings_side_chooses_hero(is_host, setup.mode == MP_LOBBY_MODE_TDM
                                                    ? MP_SETTINGS_MODE_TDM
                                                    : MP_SETTINGS_MODE_COOP)) {
        return;
    }
    ++follow.heroes_asked;
    if (!mp_start_apply_hero(mp_bridge_get_lobby_hero())) {
        ++follow.heroes_refused;
    }
}

void mp_follow_note_level_begin(void)
{
    mp_lobby_setup_t setup;
    bool             started;

    say_the_difficulty();
    put_on_the_chosen_hero();
    if (mp_bridge_drain_is_client()) {
        follow.leaving  = false;   /* whatever this side left, a level is here now */
        follow.at_title = false;
        mp_bridge_far_enter_world(mp_bridge_lobby_world());   /* the change it acted on */
        return;
    }
    started = mp_bridge_lobby_setup(&setup) && (setup.flags & MP_LOBBY_F_STARTED) != 0u &&
              (setup.flags & MP_LOBBY_F_ENDED) == 0u;
    if (!started) {
        follow.begin_seen = false;
        mp_bridge_far_enter_world(mp_bridge_lobby_world());
        return;
    }
    if (mp_follow_host_changed_world(started, setup.generation, follow.begin_seen,
                                     follow.begin_generation)) {
        announce_the_new_world(&setup);
        (void)mp_bridge_lobby_setup(&setup);   /* the generation the announcement raised */
    }
    follow.begin_seen       = true;
    follow.begin_generation = setup.generation;
    /* The world every body this side sends from now on stands in, and the one it takes poses
     * of: the generation the announcement just raised, which is what its clients will act on. */
    mp_bridge_far_enter_world(mp_bridge_lobby_world());
}

void mp_follow_note_host_movie(const char *stem)
{
    uint32_t index = 0;
    char     film[MP_LOBBY_LEVEL_MAX];

    if (!read_u32(MP_CELL_LEVEL_INDEX, &index) || !mp_levels_movie_of(index, film, sizeof film)) {
        film[0] = '\0';   /* no row of the table: a level loaded by path, or no table at all */
    }
    mp_follow_note_host_movie_in(stem, film);
}

void mp_follow_note_host_movie_in(const char *stem, const char *film)
{
    mp_lobby_setup_t setup;
    bool             started;

    if (mp_bridge_drain_is_client() || stem == NULL) {
        return;
    }
    memset(&setup, 0, sizeof setup);
    started = mp_bridge_lobby_setup(&setup) && (setup.flags & MP_LOBBY_F_STARTED) != 0u &&
              (setup.flags & MP_LOBBY_F_ENDED) == 0u;
    /* The question the level begin asks, so the two cannot disagree about what a world change
     * is: the first level of a generation is the one its start named, and no movie of that level
     * announces anything. */
    if (!mp_follow_host_changed_world(started, setup.generation, follow.begin_seen,
                                      follow.begin_generation)) {
        return;
    }
    if (!mp_follow_movie_opens_level(stem, film)) {
        ++follow.movies_not_opening;
        return;
    }
    announce_the_new_world(&setup);
    (void)mp_bridge_lobby_setup(&setup);   /* the generation the announcement raised */
    ++follow.changes_at_movie;
    log_info("the host's movie \"%s\" begins the next world: %s (%s) is announced now under "
             "generation %u, so its players load it while the movie runs", stem, setup.title,
             setup.level, (unsigned)setup.generation);
}

/* ==============================================================================================
 * The client's half.
 * ============================================================================================ */

static bool write_cell(mp_cell_t cell, uint32_t value)
{
    uintptr_t address = mp_cells_address(cell);

    return address != 0u && patch_write_u32(address, value) == PATCH_RESULT_OK;
}

/* The restore flag first, then the outcome, as the end of a session writes them; the outcome is
 * the trigger, and nothing re-enters the engine between the two writes. */
static void leave_for(const mp_lobby_setup_t *setup)
{
    /* Once for a generation. A level that is running again under the same unanswered change is
     * one the player began on their own after the follow went wrong, and leaving it again would
     * throw them out of every level they try. */
    if (follow.left_known && follow.left_generation == setup->generation) {
        if (!follow.leave_again_logged) {
            follow.leave_again_logged = true;
            log_warning("the host's world change of generation %u was left for once and never "
                        "arrived; this level is not left for it a second time",
                        (unsigned)setup->generation);
        }
        return;
    }
    if (!write_cell(MP_CELL_RESTORE_PENDING, 1u) ||
        !write_cell(MP_CELL_GAME_MODE, GAME_MODE_LEVEL_DONE)) {
        ++follow.leave_faults;
        if (!follow.leave_fault_logged) {
            follow.leave_fault_logged = true;
            log_error("the host is in %s and this side could not leave its own level for it: the "
                      "two cells the engine's own leave writes would not take a write",
                      setup->level);
        }
        return;
    }
    follow.leaving         = true;
    follow.at_title        = false;
    follow.left_known      = true;
    follow.left_generation = setup->generation;
    ++follow.leaves;
    log_info("the host is in %s (%s), generation %u: this side leaves its level to follow it",
             setup->title, setup->level, (unsigned)setup->generation);
}

/* The host's world, entered the way the lobby enters one. A world out of a savegame waits for the
 * file, as the lobby's start does: begun fresh it would be a second campaign with its own doors,
 * pickups and story flags, and nothing on screen would say so. */
static void start(const mp_lobby_setup_t *setup)
{
    mp_lobby_setup_t taken;
    bool             from_save = (setup->flags & MP_LOBBY_F_FROM_SAVE) != 0u;
    const char      *save      = NULL;

    if (from_save && setup->save_id != 0u) {
        mp_save_t header;

        if (!mp_bridge_savefile_ready(setup->save_id, setup->save_bytes)) {
            if (!follow.file_wait_logged) {
                follow.file_wait_logged = true;
                ++follow.file_waits;
                log_info("the host's world is out of a savegame, and this side waits for the file "
                         "before it follows (%u %% here)", (unsigned)mp_bridge_savefile_percent());
            }
            return;
        }
        save = mp_bridge_savefile_path();
        if (!mp_saves_read(save, &header)) {
            log_warning("the host's savegame is here but names no level of the game's own table, "
                        "so this side begins the level fresh");
            save = NULL;
        }
    }
    follow.file_wait_logged = false;
    (void)mp_bridge_lobby_take_start(&taken);
    follow.leaving  = false;
    follow.at_title = false;
    if (mp_start_level(taken.level, taken.level_index, from_save, save)) {
        ++follow.starts;
        return;
    }
    ++follow.start_refusals;
    log_warning("the host is in %s, which this side cannot start, so it stays at the title and "
                "in the session", taken.level);
}

void mp_follow_tick(void)
{
    mp_lobby_setup_t setup;
    bool             new_world;

    if (!mp_bridge_drain_is_client()) {
        return;
    }
    new_world = mp_bridge_lobby_peek_start(&setup);
    switch (mp_follow_client_step(new_world, mp_start_level_running(), follow.leaving,
                                  follow.at_title, mp_start_pending())) {
    case MP_FOLLOW_LEAVE:
        leave_for(&setup);
        break;
    case MP_FOLLOW_START:
        if (mp_bridge_lobby_connected()) {
            start(&setup);   /* a host that has gone is not followed into anything */
        }
        break;
    case MP_FOLLOW_WAIT:
    case MP_FOLLOW_NOTHING:
    default:
        break;
    }
    mp_follow_difficulty_hold(follow.leaving);
}

bool mp_follow_leaving(void)
{
    return follow.leaving;
}

void mp_follow_forget(void)
{
    follow.leaving          = false;
    follow.at_title         = false;
    follow.left_known       = false;
    follow.begin_seen       = false;
    follow.file_wait_logged = false;
}

bool mp_follow_take_new_game(void)
{
    if (!follow.leaving) {
        return false;
    }
    follow.at_title = true;
    return true;
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

bool mp_follow_install(void)
{
    /* The doors are held whether or not the restore hull below stands: they are what keeps a
     * client's own world from drifting away from the session's, and they are independent. */
    (void)mp_world_door_install();

    uintptr_t site;

    /* The host's difficulty rides the note the lobby repeats; the lobby reads no cell itself. */
    mp_bridge_lobby_set_difficulty_source(&mp_follow_difficulty_to_say);
    if (follow.installed) {
        return follow.restore_original != NULL;
    }
    follow.installed = true;
    site = mp_signatures_address(MP_SITE_SAVE_LOAD_NAMED);
    if (site == 0u ||
        !detour_install(&follow.restore_detour, site, (const void *)&hook_restore,
                        mp_signatures_prologue(MP_SITE_SAVE_LOAD_NAMED))) {
        log_warning("the savegame restore would not take a hull, so a host that loads a savegame "
                    "inside its level is followed into that level fresh rather than into the file");
        return false;
    }
    follow.restore_original = (restore_fn_t)follow.restore_detour.original;
    log_info("the host's world changes are followed: a level it begins after the start, or a "
             "savegame it restores in one, goes to its players under a new generation");
    return true;
}

void mp_follow_report(void)
{
    mp_world_door_report();
    mp_follow_difficulty_report();
    log_info("  the chosen hero at a level begin: %u asked for, %u refused before the engine was "
             "even asked; a side that does not pick asks for none",
             (unsigned)follow.heroes_asked, (unsigned)follow.heroes_refused);
    log_info("  the host's world changes: %u announced (%u out of a savegame, %u a level the "
             "engine could not name and so announced as before); the restore is %s",
             (unsigned)follow.changes, (unsigned)follow.changes_from_save,
             (unsigned)follow.changes_as_before,
             follow.restore_original != NULL ? "hulled" : "NOT hulled");
    log_info("  the host's movies: %u opened the next world and announced it as they began, %u "
             "began in a changed world and opened no level of the table",
             (unsigned)follow.changes_at_movie, (unsigned)follow.movies_not_opening);
    log_info("  following the host: %u level(s) left to follow it (%u refused by the cells), %u "
             "start(s) asked, %u refused, %u wait(s) for its savegame; %s",
             (unsigned)follow.leaves, (unsigned)follow.leave_faults, (unsigned)follow.starts,
             (unsigned)follow.start_refusals, (unsigned)follow.file_waits,
             follow.leaving ? "LEAVING NOW, not arrived" : "not on the way anywhere");
}
