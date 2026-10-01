/* mp_damage_entry.c: one death entry per life of this machine's player. See the header.
 *
 * It runs inside the death hull, in front of the engine's death, on whatever path entered it: a
 * contact the engine delivered, a hit the host performed here, or the player's own phases.
 */
#include "mp_damage_entry.h"

#include "mp_armed.h"
#include "mp_body_gate.h"
#include "mp_cells.h"
#include "mp_damage.h"
#include "mp_respawn.h"
#include "mp_seat.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The causes the engine passes, 0 to 4; anything else is counted with none of them. */
#define DEATH_CAUSES 5u

/* How many entries a level writes a line for before it only counts them. A healthy level writes
 * one per death; sixteen is room for a bad run to show its shape without filling the log. */
#define ENTRY_LINES_PER_LEVEL 16u

typedef struct entry_state {
    uint32_t lives_ended;        /* entries on a living body, session or not */

    uint32_t entered;            /* in a session: on a living body, let through */
    uint32_t refused;            /* in a session: on a corpse, refused */
    uint32_t by_cause[DEATH_CAUSES];
    uint32_t from_host;
    uint32_t from_contact;
    uint32_t from_phases;
    uint32_t task_not_player;    /* entered with another task being run than the player's */

    uint32_t level_world;        /* the level the lines below were counted for */
    float    level_clock;
    uint32_t lines;
} entry_state_t;

static entry_state_t entry;

mp_damage_entry_verdict_t mp_damage_entry_verdict(bool session, bool corpse)
{
    return session && corpse ? MP_DAMAGE_ENTRY_REFUSE : MP_DAMAGE_ENTRY_ENTER;
}

/* Whether this entry still gets a line of its own. A new level starts the count again: another
 * world, or the same one with its clock started over. */
static bool line_allowed(void)
{
    uint32_t world = 0u;
    float    now = 0.0f;

    if (mp_seat_world_now(&world, &now) &&
        (world != entry.level_world || now < entry.level_clock)) {
        entry.level_world = world;
        entry.lines       = 0u;
    }
    entry.level_clock = now;
    if (entry.lines >= ENTRY_LINES_PER_LEVEL) {
        return false;
    }
    ++entry.lines;
    return true;
}

static const char *slot_words(mp_body_slot_word_t slot)
{
    switch (slot) {
    case MP_BODY_SLOT_DISPATCHER: return "the dispatcher";
    case MP_BODY_SLOT_ENGINE:     return "the engine's handler";
    case MP_BODY_SLOT_EMPTY:      return "nothing";
    case MP_BODY_SLOT_UNREAD:
    default:                      return "a word that did not read";
    }
}

static const char *source_words(mp_body_source_t source)
{
    switch (source) {
    case MP_BODY_SOURCE_HOST_HIT: return "a hit from the host was being performed";
    case MP_BODY_SOURCE_CONTACT:  return "a contact delivered here";
    case MP_BODY_SOURCE_PHASES:
    default:                      return "the engine's own phases";
    }
}

/* The counts of one entry in a session, and its line while the level has lines left. */
static void note_the_entry(int32_t cause, bool corpse, mp_damage_entry_verdict_t verdict,
                           uintptr_t caller)
{
    mp_body_gate_view_t view;
    uintptr_t           block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t            module = 0u;
    float               at[3] = { 0.0f, 0.0f, 0.0f };
    const char         *task;

    mp_body_gate_view(&view);
    if (verdict == MP_DAMAGE_ENTRY_REFUSE) {
        ++entry.refused;
    } else {
        ++entry.entered;
        if (cause >= 0 && (uint32_t)cause < DEATH_CAUSES) {
            ++entry.by_cause[cause];
        }
        entry.from_host    += view.source == MP_BODY_SOURCE_HOST_HIT ? 1u : 0u;
        entry.from_contact += view.source == MP_BODY_SOURCE_CONTACT ? 1u : 0u;
        entry.from_phases  += view.source == MP_BODY_SOURCE_PHASES ? 1u : 0u;
        entry.task_not_player += view.task_known && !view.task_is_player ? 1u : 0u;
    }
    if (!line_allowed()) {
        return;
    }
    (void)mp_cells_hero_position(at);
    if (block != 0u) {
        (void)memory_read_u32(block + MP_HERO_BLOCK_MODULE_STATE, &module);
    }
    task = !view.task_known ? "not known" : (view.task_is_player ? "the player's" : "another");
    /* One sentence either way; a refusal is a warning, because after the gate in front of the
     * handler no corpse should ever be asked into its death a second time. */
    if (verdict == MP_DAMAGE_ENTRY_REFUSE) {
        log_warning("a death entered here: cause %d, at %.2f %.2f %.2f, %s, module %u, the "
                    "contact slot held %s, the task being run was %s, %s, from %08X: refused",
                    (int)cause, (double)at[0], (double)at[1], (double)at[2],
                    "on a corpse (the death mode already up)", (unsigned)module,
                    slot_words(view.slot), task, source_words(view.source), (unsigned)caller);
        return;
    }
    log_info("a death entered here: cause %d, at %.2f %.2f %.2f, %s, module %u, the contact slot "
             "held %s, the task being run was %s, %s, from %08X: entered",
             (int)cause, (double)at[0], (double)at[1], (double)at[2],
             corpse ? "on a corpse (the death mode already up)" : "on a living body",
             (unsigned)module, slot_words(view.slot), task, source_words(view.source),
             (unsigned)caller);
}

mp_damage_entry_verdict_t mp_damage_entry_judge(int32_t cause, uintptr_t record,
                                                uintptr_t caller)
{
    bool                      corpse  = mp_respawn_record_is_a_corpse(record);
    bool                      session = mp_armed_transport();
    mp_damage_entry_verdict_t verdict = mp_damage_entry_verdict(session, corpse);

    if (!corpse) {
        ++entry.lives_ended;
    }
    if (session) {
        note_the_entry(cause, corpse, verdict, caller);
    }
    return verdict;
}

bool mp_damage_entry_lives_ended(uint32_t *lives)
{
    if (lives == NULL || !mp_damage_installed()) {
        return false;
    }
    *lives = entry.lives_ended;
    return true;
}

void mp_damage_entry_report(void)
{
    log_info("  the death entries: %u entered on a living body, %u refused on a corpse (must be "
             "0); by cause 0 %u, 1 %u, 2 %u, 3 %u, 4 %u; %u from a hit the host performed, %u "
             "from a contact delivered here, %u from the engine's own phases; %u with the task "
             "being run not the player's (must be 0)",
             (unsigned)entry.entered, (unsigned)entry.refused, (unsigned)entry.by_cause[0],
             (unsigned)entry.by_cause[1], (unsigned)entry.by_cause[2],
             (unsigned)entry.by_cause[3], (unsigned)entry.by_cause[4],
             (unsigned)entry.from_host, (unsigned)entry.from_contact,
             (unsigned)entry.from_phases, (unsigned)entry.task_not_player);
}
