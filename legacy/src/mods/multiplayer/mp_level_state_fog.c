/* mp_level_state_fog.c: the director's fog the level shares. See the header. */
#include "mp_level_state_fog.h"

#include "mp_cells.h"
#include "mp_fog_viewers.h"
#include "mp_level_state_bind.h"
#include "mp_level_state_drawn.h"
#include "mp_level_state_fog_rule.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct fog_counts {
    uint32_t seen[4];           /* start, end, ramp to green, ramp back */
    uint32_t withheld;          /* a client's own, refused: it plays the host's */
    uint32_t fail_open;         /* a client's own, let through: it cannot play the host's */
    uint32_t viewer_withheld;   /* the host's, refused before the engine: a room's fog */
    uint32_t viewer_own;        /* the host's, a room's its viewer could not take: its engine's */
    uint32_t changed;           /* the host's, let through, a change, journaled */
    uint32_t same;              /* the host's, let through, no change */
    uint32_t ignored;           /* the host's, let through, arguments the engine refuses */
    uint32_t too_long;          /* the host's, a change whose length a journal entry cannot carry */
    uint32_t alone;             /* no session */
    uint32_t replayed;          /* a client's journal entries played */
    uint32_t not_played;        /* and ones the director did not take */
    uint32_t heals;
    uint32_t heal_commands;
} fog_counts_t;

typedef struct fog_state {
    mp_level_fog_model_t model;   /* the host's, what its engine holds of the level's fog */
    fog_counts_t         n;
    bool                 too_long_said;
} fog_state_t;

static fog_state_t fog;

static size_t seen_slot(int32_t command)
{
    switch (command) {
    case MP_LEVEL_FOG_START:
        return 0u;
    case MP_LEVEL_FOG_END:
        return 1u;
    case MP_LEVEL_FOG_RAMP_GREEN:
        return 2u;
    default:
        return 3u;
    }
}

bool mp_level_state_fog_play(const char *whose, int32_t command, int32_t a1, uint32_t a2,
                             uint16_t sequence)
{
    if (!mp_level_state_bind_call_director(mp_level_state_bind_stand_in(), command, a1,
                                           (int32_t)a2)) {
        return false;
    }
    mp_level_state_drawn_note(whose, command, sequence);
    return true;
}

/* A change the host's engine is about to make, into the journal with the script's own values. The
 * length of a ramp travels in sixteen bits; the two edges do not read it. */
static void journal_change(int32_t command, int32_t a1, uint32_t a2)
{
    int32_t  length = mp_level_state_fog_is_ramp(command) ? a1 : 0;
    uint16_t sequence;

    if (!mp_level_state_fog_fits(length)) {
        ++fog.n.too_long;
        if (!fog.too_long_said) {
            fog.too_long_said = true;
            log_warning("a script ramped the fog over %d seconds, which a journal entry cannot "
                        "carry, so a client meets this ramp only in the state", (int)a1);
        }
        return;
    }
    ++fog.n.changed;
    sequence = mp_level_state_journal_note((uint8_t)MP_LEVEL_JOURNAL_FOG, (uint8_t)command,
                                           (uint16_t)length, a2);
    mp_level_state_drawn_note(MP_LEVEL_DRAWN_OWN, command, sequence);
}

bool mp_level_state_fog_hear(void *actor, int32_t command, int32_t a1, int32_t a2,
                             mp_level_side_t side, bool can_match)
{
    if (!mp_level_state_fog_command(command)) {
        return false;
    }
    ++fog.n.seen[seen_slot(command)];
    switch (side) {
    case MP_LEVEL_SIDE_CLIENT:
        if (can_match) {
            ++fog.n.withheld;
            return true;
        }
        ++fog.n.fail_open;
        return false;
    case MP_LEVEL_SIDE_HOST:
        /* A room's fog is each viewer's, played by the viewers' module for this side's own player;
         * the script runs on undisturbed, because the director answers 0 whatever its arm did. One
         * the viewer could not take is this side's engine's alone, and never the level's. */
        switch (mp_fog_viewers_hear((uintptr_t)actor)) {
        case MP_FOG_VIEWERS_WITHHELD:
            ++fog.n.viewer_withheld;
            return true;
        case MP_FOG_VIEWERS_OWN_ENGINE:
            ++fog.n.viewer_own;
            return false;
        case MP_FOG_VIEWERS_SHARED:
        default:
            break;
        }
        switch (mp_level_fog_apply(&fog.model, command, a1, (uint32_t)a2)) {
        case MP_LEVEL_FOG_CHANGED:
            journal_change(command, a1, (uint32_t)a2);
            break;
        case MP_LEVEL_FOG_IGNORED:
            ++fog.n.ignored;
            break;
        case MP_LEVEL_FOG_SAME:
        case MP_LEVEL_FOG_NOT_FOG:
        default:
            ++fog.n.same;
            break;
        }
        return false;
    case MP_LEVEL_SIDE_ALONE:
    default:
        ++fog.n.alone;
        return false;
    }
}

/* The engine counts its ramp down by the time step its tasks are handed, the length of this
 * machine's substep: 1/32 s, or 1/64 s under the frame rate cheat. The cell holds that length only
 * inside the substep loop, where the level's state runs; outside it the same cell holds the time
 * of the last frame. */
void mp_level_state_fog_host_tick(void)
{
    uintptr_t cell    = mp_cells_address(MP_CELL_FRAME_DELTA);
    float     seconds = 0.0f;

    if (cell == 0u || !memory_try_read(cell, &seconds, sizeof seconds)) {
        seconds = 0.0f;
    }
    (void)mp_level_fog_tick(&fog.model, mp_level_fog_substep_seconds(seconds));
}

void mp_level_state_fog_describe(mp_level_state_note_t *note)
{
    if (note == NULL || fog.model.state.flags == 0u) {
        return;
    }
    note->parts |= (uint8_t)MP_LEVEL_STATE_PART_FOG;
    note->fog = fog.model.state;
}

bool mp_level_state_fog_replay(const mp_level_journal_entry_t *entry)
{
    int32_t command;
    int32_t a1;

    if (entry == NULL) {
        return false;
    }
    command = (int32_t)entry->a;
    a1      = mp_level_state_fog_is_ramp(command) ? (int32_t)entry->b : 0;
    if (!mp_level_state_fog_play(MP_LEVEL_DRAWN_REPLAYED, command, a1, entry->c,
                                 entry->sequence)) {
        ++fog.n.not_played;
        return false;
    }
    ++fog.n.replayed;
    return true;
}

void mp_level_state_fog_heal(const mp_level_state_note_t *note)
{
    mp_level_fog_command_t plan[MP_LEVEL_FOG_HEAL_MAX];
    size_t                 count;
    size_t                 i;

    if (note == NULL || (note->parts & MP_LEVEL_STATE_PART_FOG) == 0u) {
        return;
    }
    count = mp_level_fog_heal_plan(&note->fog, plan, MP_LEVEL_FOG_HEAL_MAX);
    if (count == 0u) {
        return;
    }
    ++fog.n.heals;
    for (i = 0; i < count; ++i) {
        if (mp_level_state_fog_play(MP_LEVEL_DRAWN_HEALING, plan[i].command, plan[i].a1,
                                    plan[i].a2, 0u)) {
            ++fog.n.heal_commands;
        }
    }
}

void mp_level_state_fog_reset(void)
{
    mp_level_fog_model_init(&fog.model);
}

void mp_level_state_fog_report(bool host)
{
    const fog_counts_t *n = &fog.n;

    log_info("  the director's fog (%s): %u start(s), %u end(s), %u ramp(s) to green, %u ramp(s) "
             "back; %u of this side's own withheld in a started session, %u let through because "
             "this side cannot match the host; as the host %u withheld for the viewers' fog (none "
             "journaled), %u of a room left to this side's own engine (none journaled), %u let "
             "through that changed the shared fog (journaled), %u that changed "
             "nothing, %u the engine refuses, %u too long for the journal; %u let through without "
             "a session | %u replayed from the journal, %u the director did not take, %u heal(s) "
             "from the state with %u command(s); the shared fog's flags %02X, %u s of a ramp left",
             host ? "host" : "client", (unsigned)n->seen[0], (unsigned)n->seen[1],
             (unsigned)n->seen[2], (unsigned)n->seen[3], (unsigned)n->withheld,
             (unsigned)n->fail_open, (unsigned)n->viewer_withheld, (unsigned)n->viewer_own,
             (unsigned)n->changed,
             (unsigned)n->same, (unsigned)n->ignored, (unsigned)n->too_long, (unsigned)n->alone,
             (unsigned)n->replayed, (unsigned)n->not_played, (unsigned)n->heals,
             (unsigned)n->heal_commands, (unsigned)fog.model.state.flags,
             (unsigned)fog.model.state.left);
}
