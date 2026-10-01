/* mp_follow_difficulty.c: the host's difficulty on every player of the session. See the header. */
#include "mp_follow_difficulty.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_cells.h"
#include "mp_lobby.h"
#include "mp_start.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct difficulty_state {
    /* The client's. */
    uint32_t begins_put;         /* level begins put on the host's value */
    uint32_t begins_over;        /* of them over another value here */
    uint32_t begins_none;        /* the host said none */
    uint32_t begins_ending;      /* not written because an exit waited */
    uint32_t put_back;           /* while a level ran, back to the note after a change here */
    uint32_t put_back_restore;   /* of them after a savegame's restore */
    uint32_t taken_over;         /* a newer note's value taken while a level ran */
    uint32_t write_refusals;     /* the cell would not read or would not take the write */
    bool     applied_known;
    uint32_t applied;            /* the note's value the cell was last put on or found at */

    /* The host's. */
    uint32_t said;
    uint32_t said_none;
    uint32_t said_last;

    bool (*exit_waits)(void);
} difficulty_state_t;

static difficulty_state_t dif;

void mp_follow_difficulty_set_exit_question(bool (*exit_waits)(void))
{
    dif.exit_waits = exit_waits;
}

static bool an_exit_waits(void)
{
    return dif.exit_waits != NULL && dif.exit_waits();
}

/* What the note says to this side. */
typedef enum difficulty_said {
    SAID_NOTHING,   /* no level of a session this side is a client of */
    SAID_NONE,      /* the host says none, so the cell keeps this side's own */
    SAID_VALUE
} difficulty_said_t;

static difficulty_said_t host_says(uint32_t *value)
{
    mp_lobby_setup_t setup;

    if (!mp_armed_transport() || !mp_bridge_drain_is_client() || !mp_bridge_lobby_setup(&setup) ||
        (setup.flags & MP_LOBBY_F_STARTED) == 0u || (setup.flags & MP_LOBBY_F_ENDED) != 0u) {
        return SAID_NOTHING;
    }
    if (setup.host_difficulty == MP_LOBBY_DIFFICULTY_NONE) {
        return SAID_NONE;
    }
    *value = (uint32_t)setup.host_difficulty - 1u;
    return SAID_VALUE;
}

/* The cell onto `value`. True when it held another one, which `before` then says; a cell that will
 * not read or will not take the write is counted and left as it is. */
static bool put_on(uint32_t value, uint32_t *before)
{
    uintptr_t address = mp_cells_address(MP_CELL_IMPACT_DIFFICULTY);

    *before = value;
    if (address == 0u || !memory_try_read_u32(address, before)) {
        ++dif.write_refusals;
        return false;
    }
    if (*before == value) {
        return false;
    }
    if (!memory_try_write(address, &value, sizeof value)) {
        ++dif.write_refusals;
        return false;
    }
    return true;
}

static void note_applied(uint32_t value)
{
    dif.applied       = value;
    dif.applied_known = true;
}

uint8_t mp_follow_difficulty_to_say(void)
{
    uintptr_t address = mp_cells_address(MP_CELL_IMPACT_DIFFICULTY);
    uint32_t  value   = 0u;

    ++dif.said;
    if (address == 0u || !memory_try_read_u32(address, &value) ||
        value >= MP_LOBBY_DIFFICULTY_MAX) {
        ++dif.said_none;
        return (uint8_t)MP_LOBBY_DIFFICULTY_NONE;
    }
    dif.said_last = value;
    return (uint8_t)(value + 1u);
}

/* Before anything of the level begin reads the cell, the world's own line included, so that line
 * says the value this level is played at. */
void mp_follow_difficulty_note_level_begin(void)
{
    difficulty_said_t said;
    uint32_t          value  = 0u;
    uint32_t          before = 0u;

    said = host_says(&value);
    if (said == SAID_NOTHING) {
        return;
    }
    /* A level that begins while an exit waits is single player, and the host's value would stay
     * in it. */
    if (an_exit_waits()) {
        ++dif.begins_ending;
        return;
    }
    if (said == SAID_NONE) {
        ++dif.begins_none;
        return;
    }
    ++dif.begins_put;
    note_applied(value);
    if (put_on(value, &before)) {
        ++dif.begins_over;
        log_info("the host's difficulty %u is put over this side's %u as the level begins, "
                 "because the host sets it for every player", (unsigned)value, (unsigned)before);
    }
}

/* The engine restores the whole skill block after the level begin of a savegame, the difficulty
 * the file was written with among it, so the level begin's write does not survive a restore. */
void mp_follow_difficulty_note_restore(void)
{
    uint32_t value  = 0u;
    uint32_t before = 0u;

    if (host_says(&value) != SAID_VALUE || an_exit_waits()) {
        return;
    }
    note_applied(value);
    if (put_on(value, &before)) {
        ++dif.put_back;
        ++dif.put_back_restore;
    }
}

/* The note, not a value kept from the level begin: after a savegame only a later repeat carries
 * the host's own restored value. Only while the game mode says a level runs, and not on the way
 * out of one, whose level end raises the cell here too and would be counted as a change. Nor while
 * an exit waits: the session is over for this side, and a change it makes is its own. */
void mp_follow_difficulty_hold(bool leaving)
{
    uint32_t value  = 0u;
    uint32_t before = 0u;

    if (leaving || host_says(&value) != SAID_VALUE || !mp_start_level_running() ||
        an_exit_waits()) {
        return;
    }
    if (put_on(value, &before)) {
        if (dif.applied_known && dif.applied != value) {
            ++dif.taken_over;
        } else {
            ++dif.put_back;
        }
    }
    note_applied(value);
}

void mp_follow_difficulty_report(void)
{
    log_info("  the host's difficulty (client): %u level begin(s) put on the host's value, %u of "
             "them over another one here, %u level begin(s) with none said by the host, %u not "
             "written because the session was ending; while a level ran %u put back after this "
             "side's own change (%u of them after a savegame's restore), %u taken over from a "
             "newer note; %u time(s) the cell would not take it",
             (unsigned)dif.begins_put, (unsigned)dif.begins_over, (unsigned)dif.begins_none,
             (unsigned)dif.begins_ending, (unsigned)dif.put_back,
             (unsigned)dif.put_back_restore, (unsigned)dif.taken_over,
             (unsigned)dif.write_refusals);
    log_info("  the host's difficulty (host): said in %u setup note(s), the last %u, %u of them "
             "saying none because the cell did not read or held no difficulty",
             (unsigned)dif.said, (unsigned)dif.said_last, (unsigned)dif.said_none);
}
