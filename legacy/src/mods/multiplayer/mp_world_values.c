/* mp_world_values.c: the values on this machine that decide the shared world, read and said.
 *
 * Reads only. The five cells are settings a session decides on the host's side: the difficulty
 * and the two shot cheats are held on the host's values on every client, the 60fps cheat is held
 * off everywhere, and the detail level gates the activation scan a client parks; the content
 * fingerprint hashes none of them. The game data is the damage table and the character roster,
 * the one thing two machines must hold identically whatever their settings. Each is printed under
 * its own label, so two runs, or a host and a client, compare line against line.
 */
#include "mp_world_values.h"

#include "mp_cells.h"
#include "mp_content.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct world_values {
    bool     read;
    uint32_t difficulty;
    uint32_t detail_level;
    uint32_t cheat_happy;
    uint32_t cheat_evil_force;
    uint32_t cheat_fast;
} world_values_t;

static struct {
    bool     first_said;
    bool     shot_init_said;
    bool     unread_said;
    uint32_t substeps;
    uint32_t substeps_odd;
    uint32_t substeps_unread;
    float    last_odd;
} said;

static bool read_cell(mp_cell_t cell, uint32_t *out)
{
    uintptr_t address = mp_cells_address(cell);

    return address != 0u && memory_read_u32(address, out);
}

static world_values_t read_values(void)
{
    world_values_t values = { false, 0u, 0u, 0u, 0u, 0u };

    values.read = read_cell(MP_CELL_IMPACT_DIFFICULTY, &values.difficulty) &&
                  read_cell(MP_CELL_DETAIL_LEVEL, &values.detail_level) &&
                  read_cell(MP_CELL_CHEAT_HAPPY, &values.cheat_happy) &&
                  read_cell(MP_CELL_CHEAT_EVIL_FORCE, &values.cheat_evil_force) &&
                  read_cell(MP_CELL_SUBSTEP_RATE_SWITCH, &values.cheat_fast);
    return values;
}

bool mp_world_values_substep_is_standard(float seconds)
{
    return seconds == MP_WORLD_VALUES_SUBSTEP_SECONDS;
}

bool mp_world_values_damage_table(uint32_t *hash, bool *seen)
{
    uintptr_t table = mp_cells_address(MP_CELL_SHOT_TABLE);
    uint32_t  impacts[MP_CELLS_SHOT_ROWS];
    bool      any_actor = false;
    size_t    row;

    if (hash == NULL || seen == NULL || table == 0u) {
        return false;
    }
    for (row = 0; row < MP_CELLS_SHOT_ROWS; ++row) {
        uintptr_t at    = table + row * MP_CELLS_SHOT_ROW_BYTES;
        uint32_t  actor = 0;

        if (!memory_try_read_u32(at + MP_CELLS_SHOT_ROW_IMPACT, &impacts[row]) ||
            !memory_try_read_u32(at + MP_CELLS_SHOT_ROW_ACTOR, &actor)) {
            return false;
        }
        any_actor = any_actor || actor != 0u;
    }
    *hash = mp_cells_damage_table_hash(impacts);
    *seen = any_actor;
    return true;
}

static void say_game_data(const char *when)
{
    uint32_t hash = 0;
    bool     shot_init = false;

    if (!mp_world_values_damage_table(&hash, &shot_init)) {
        log_info("the game data %s: the shot table did not resolve or read; character roster "
                 "%08X", when, (unsigned)mp_content_roster_fingerprint());
        return;
    }
    log_info("the game data %s: damage table %08X (shot init seen: %s), character roster %08X",
             when, (unsigned)hash, shot_init ? "yes" : "no",
             (unsigned)mp_content_roster_fingerprint());
}

void mp_world_values_note_level_begin(void)
{
    world_values_t values = read_values();

    if (values.read) {
        log_info("the world settings here: difficulty %u, detail level %u, cheats happy %u, evil "
                 "force %u, 60fps %u", (unsigned)values.difficulty,
                 (unsigned)values.detail_level, (unsigned)values.cheat_happy,
                 (unsigned)values.cheat_evil_force, (unsigned)values.cheat_fast);
    } else {
        log_info("the world settings here: a cell did not resolve or read");
    }
    say_game_data("at a level begin");
}

void mp_world_values_note_frame_begin(void)
{
    uint32_t hash = 0;
    bool     shot_init = false;

    if (said.shot_init_said) {
        return;
    }
    if (!mp_world_values_damage_table(&hash, &shot_init)) {
        if (!said.unread_said) {
            said.unread_said = true;
            say_game_data("before any level");
        }
        return;
    }
    if (shot_init || !said.first_said) {
        said.first_said     = true;
        said.shot_init_said = shot_init;
        say_game_data("before any level");
    }
}

void mp_world_values_note_substep(void)
{
    uintptr_t cell    = mp_cells_address(MP_CELL_FRAME_DELTA);
    float     seconds = 0.0f;

    ++said.substeps;
    if (cell == 0u || !memory_try_read(cell, &seconds, sizeof seconds)) {
        ++said.substeps_unread;
        return;
    }
    if (mp_world_values_substep_is_standard(seconds)) {
        return;
    }
    if (said.substeps_odd++ == 0u) {
        log_warning("a substep ran %.5f s instead of 1/32: the ladder a session counts in has "
                    "moved, which the 60fps cheat does when framerate_fix does not pin it",
                    (double)seconds);
    }
    said.last_odd = seconds;
}

void mp_world_values_report(void)
{
    world_values_t values = read_values();

    log_info("  the world settings at this report%s: difficulty %u, detail level %u, cheats happy "
             "%u, evil force %u, 60fps %u; %u substep(s) measured, %u of them not 1/32 long "
             "(the last %.5f s), %u unread",
             values.read ? "" : " (a cell did not resolve or read)",
             (unsigned)values.difficulty, (unsigned)values.detail_level,
             (unsigned)values.cheat_happy, (unsigned)values.cheat_evil_force,
             (unsigned)values.cheat_fast, (unsigned)said.substeps, (unsigned)said.substeps_odd,
             (double)said.last_odd, (unsigned)said.substeps_unread);
}
