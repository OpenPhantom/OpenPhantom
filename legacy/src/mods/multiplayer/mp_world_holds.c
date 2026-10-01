/* mp_world_holds.c: the cheat cells and the 60fps cell, held for a session. See the header. */
#include "mp_world_holds.h"

#include "mp_armed.h"
#include "mp_cells.h"
#include "mp_host_settings.h"
#include "mp_host_settings_rule.h"
#include "mp_session_now.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { HELD_HAPPY, HELD_EVIL_FORCE, HELD_FAST, HELD_COUNT };

/* The three cells, each a 32 bit word the engine's cheat console toggles with an exclusive or of 1
 * and every reader tests for not zero; so 1 is on and 0 is off. */
static const mp_cell_t HELD_CELLS[HELD_COUNT] = {
    MP_CELL_CHEAT_HAPPY, MP_CELL_CHEAT_EVIL_FORCE, MP_CELL_SUBSTEP_RATE_SWITCH,
};
static const char *const HELD_NAMES[HELD_COUNT] = { "happy", "evil force", "60fps" };

typedef struct held_cell {
    bool     holding;   /* this side's own value is kept and the cell is held */
    uint32_t own;       /* what the cell held when the hold began */
    uint32_t held_at;   /* the value the last frame held it on */
    uint32_t frames;
    uint32_t writes;    /* frames on which the cell held something else and was put back */
} held_cell_t;

typedef struct world_holds {
    held_cell_t cells[HELD_COUNT];
    uint32_t    frames_at_host_value;        /* a client's frames on the host's cheat bits */
    uint32_t    frames_without_host_value;   /* and its frames at 0 for want of them */
    uint32_t    refusals;          /* reads and writes a cell would not take */
    uint32_t    exits;             /* holds that ended, by either way */
    bool        last_exit_whole;   /* the last one put every held cell back */
    bool        cheats_known;
    uint8_t     cheats_said;       /* the host's bits the last line named */
} world_holds_t;

static world_holds_t holds;

/* The cell onto `target`, keeping what it held before the first frame of the hold. */
static void hold(size_t index, uint32_t target)
{
    held_cell_t *held    = &holds.cells[index];
    uintptr_t    address = mp_cells_address(HELD_CELLS[index]);
    uint32_t     value   = 0u;

    if (address == 0u || !memory_try_read_u32(address, &value)) {
        ++holds.refusals;
        return;
    }
    if (!held->holding) {
        held->holding = true;
        held->own     = value;
    }
    ++held->frames;
    held->held_at = target;
    if (value == target) {
        return;
    }
    if (!memory_try_write(address, &target, sizeof target)) {
        ++holds.refusals;
        return;
    }
    ++held->writes;
}

/* This side's own value back into a held cell. True when it took it, or when nothing was held. */
static bool put_back(size_t index)
{
    held_cell_t *held = &holds.cells[index];
    uintptr_t    address;

    if (!held->holding) {
        return true;
    }
    held->holding = false;
    address = mp_cells_address(HELD_CELLS[index]);
    if (address == 0u || !memory_try_write(address, &held->own, sizeof held->own)) {
        ++holds.refusals;
        return false;
    }
    return true;
}

static bool any_held(void)
{
    size_t index;

    for (index = 0; index < HELD_COUNT; ++index) {
        if (holds.cells[index].holding) {
            return true;
        }
    }
    return false;
}

/* "happy 1, evil force 0, 60fps 0" for the cells being held, from `own` or from `held_at`. */
static void name_the_cells(char *out, size_t size, bool own)
{
    size_t used = 0;
    size_t index;

    out[0] = '\0';
    for (index = 0; index < HELD_COUNT && used + 1u < size; ++index) {
        const held_cell_t *held = &holds.cells[index];

        if (!held->holding) {
            continue;
        }
        used += text_format(out + used, size - used, "%s%s %u", used == 0u ? "" : ", ",
                            HELD_NAMES[index], (unsigned)(own ? held->own : held->held_at));
    }
}

/* The one way every hold ends, whichever door it came by. */
static void end_holds(const char *why)
{
    char   own[96];
    bool   whole = true;
    size_t index;

    if (!any_held()) {
        return;
    }
    name_the_cells(own, sizeof own, true);
    for (index = 0; index < HELD_COUNT; ++index) {
        whole = put_back(index) && whole;
    }
    ++holds.exits;
    holds.last_exit_whole = whole;
    holds.cheats_known    = false;
    if (whole) {
        log_info("the world holds end (%s): this side's own values are back, %s", why, own);
    } else {
        log_warning("the world holds end (%s): a cell would not take this side's own value back "
                    "(own %s)", why, own);
    }
}

static void say_begun(void)
{
    char held[96];
    char own[96];

    name_the_cells(held, sizeof held, false);
    name_the_cells(own, sizeof own, true);
    log_info("the world holds begin: %s held for the session; this side's own %s come back at "
             "the exit", held, own);
}

/* A change of the host's cheats in the middle of a session, said once per change. */
static void note_the_host_cheats(uint8_t cheats, bool from_host)
{
    if (holds.cheats_known && holds.cheats_said == cheats) {
        return;
    }
    holds.cheats_known = true;
    holds.cheats_said  = cheats;
    log_info("the world holds: cheats happy %u and evil force %u are held at %s",
             (unsigned)((cheats & MP_HOST_SETTINGS_CHEAT_HAPPY) != 0u),
             (unsigned)((cheats & MP_HOST_SETTINGS_CHEAT_EVIL_FORCE) != 0u),
             from_host ? "the host's values" : "0, because no value of the host's has come yet");
}

void mp_world_holds_frame(void)
{
    bool    client = false;
    bool    from_host;
    bool    began  = false;
    uint8_t cheats = 0u;

    if (!mp_armed_transport()) {
        return;
    }
    /* The one answer the host's settings and the hero carry take as well. A host's goodbye ends
     * it for a client at once, although the host's last setup note still says started, and then
     * every cell goes back, the 60fps one included, rather than waiting for the exit. */
    if (!mp_session_now_plays_in_a_running_session(&client)) {
        end_holds("this machine no longer plays in a running session");
        return;
    }
    began = !any_held();
    hold(HELD_FAST, 0u);
    if (client) {
        from_host = mp_host_settings_cheats(&cheats);
        if (from_host) {
            ++holds.frames_at_host_value;
        } else {
            cheats = 0u;
            ++holds.frames_without_host_value;
        }
        hold(HELD_HAPPY, (cheats & MP_HOST_SETTINGS_CHEAT_HAPPY) != 0u ? 1u : 0u);
        hold(HELD_EVIL_FORCE, (cheats & MP_HOST_SETTINGS_CHEAT_EVIL_FORCE) != 0u ? 1u : 0u);
        note_the_host_cheats(cheats, from_host);
    } else {
        /* A host's cheats are the session's. A cheat cell held while this side was a client goes
         * back the moment it is not one any more. */
        (void)put_back(HELD_HAPPY);
        (void)put_back(HELD_EVIL_FORCE);
    }
    if (began && any_held()) {
        say_begun();
    }
}

void mp_world_holds_withdraw(void)
{
    end_holds("the transport came down");
}

void mp_world_holds_counts(mp_world_holds_counts_t *out)
{
    const held_cell_t *happy = &holds.cells[HELD_HAPPY];
    const held_cell_t *evil  = &holds.cells[HELD_EVIL_FORCE];
    const held_cell_t *fast  = &holds.cells[HELD_FAST];

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->happy_held_at             = happy->held_at;
    out->evil_force_held_at        = evil->held_at;
    out->frames_at_host_value      = holds.frames_at_host_value;
    out->frames_without_host_value = holds.frames_without_host_value;
    out->cheat_writes              = happy->writes + evil->writes;
    out->fast_frames               = fast->frames;
    out->fast_writes               = fast->writes;
    out->exits                     = holds.exits;
    out->last_exit_whole           = holds.last_exit_whole;
    out->refusals                  = holds.refusals;
}

void mp_world_holds_report(void)
{
    mp_world_holds_counts_t counts;
    const char             *back;

    mp_world_holds_counts(&counts);
    back = counts.exits == 0u ? "no exit yet" : counts.last_exit_whole ? "yes" : "no";
    log_info("  the world holds: cheats happy %u and evil force %u held at the host's values on %u "
             "frame(s) and at 0 for want of a host value on %u frame(s) (%u write(s) that changed "
             "a cell), 60fps held at 0 on %u frame(s) (%u write(s)); this side's own values put "
             "back at the exit: %s; %u exit(s), %u read(s) or write(s) a cell would not take",
             (unsigned)counts.happy_held_at, (unsigned)counts.evil_force_held_at,
             (unsigned)counts.frames_at_host_value, (unsigned)counts.frames_without_host_value,
             (unsigned)counts.cheat_writes, (unsigned)counts.fast_frames,
             (unsigned)counts.fast_writes, back, (unsigned)counts.exits,
             (unsigned)counts.refusals);
}
