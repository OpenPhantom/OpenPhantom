/* mp_hero_carry.c: the player's things go with him when the hero under him changes. See the
 * header. */
#include "mp_hero_carry.h"

#include "mp_bank.h"
#include "mp_bank_spawn.h"
#include "mp_hero_carry_rule.h"
#include "mp_session_now.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct hero_carry_state {
    bool                  pending;      /* a carry was made, its check after the original owed */
    int32_t               from;
    int32_t               to;
    uint8_t               window[MP_HERO_CARRY_INVENTORY_BYTES];
    mp_bank_spawn_cells_t cells;
    bool                  refusal_logged;
    bool                  access_logged;
    bool                  current_logged;

    uint32_t seen;                      /* spawns entered while a session ran here */
    uint32_t carried;
    uint32_t health_left;               /* carried with the incoming hero's own health */
    uint32_t same_hero;
    uint32_t in_window;
    uint32_t no_hero;
    uint32_t refused;                   /* at the precondition */
    uint32_t unproven;
    uint32_t access_refused;            /* a record or the six bytes refused a read or write */
    uint32_t moved;                     /* the six bytes differed after the original; stays 0 */
    uint32_t other_current;             /* another hero current after the original; stays 0 */
} hero_carry_state_t;

static hero_carry_state_t carry;

static uintptr_t record_of(uint32_t hero)
{
    return carry.cells.records_cell + (uintptr_t)hero * MP_HERO_CARRY_RECORD_BYTES;
}

static bool read_facts(int32_t hero_index, mp_hero_carry_facts_t *facts)
{
    facts->record_base = (uint32_t)carry.cells.records_cell;
    facts->hero_index  = hero_index;
    return memory_try_read_u32(carry.cells.current_cell, &facts->current_player) &&
           memory_try_read_u32(carry.cells.status_cell, &facts->status_pointer);
}

static void refuse(const mp_hero_carry_facts_t *facts, bool read)
{
    ++carry.refused;
    if (carry.refusal_logged) {
        return;
    }
    carry.refusal_logged = true;
    if (!read) {
        log_warning("a hero change to %d carries nothing: the current player or the status "
                    "pointer did not read; later refusals are counted", (int)facts->hero_index);
        return;
    }
    log_warning("a hero change to %d carries nothing: the current player reads %u and the status "
                "pointer %08X, with hero 0's record at %08X. Either index is past the four "
                "records, or the pointer does not name the current hero's record, and the "
                "engine's swap would then store the live bytes into another record than the one "
                "the carry read; later refusals are counted",
                (int)facts->hero_index, (unsigned)facts->current_player,
                (unsigned)facts->status_pointer, (unsigned)facts->record_base);
}

static void refuse_access(int32_t from, int32_t to)
{
    ++carry.access_refused;
    if (carry.access_logged) {
        return;
    }
    carry.access_logged = true;
    log_warning("the hero change %d -> %d carries nothing: a hero record at %08X or the inventory "
                "and key bytes at %08X did not read, or the incoming record did not take the "
                "write; later ones are counted", (int)from, (int)to,
                (unsigned)carry.cells.records_cell, (unsigned)carry.cells.flags_cell);
}

/* The incoming record is replaced in one write, so the engine never sees half of it. */
static void write_carry(const mp_hero_carry_facts_t *facts)
{
    uint8_t                 outgoing[MP_HERO_CARRY_RECORD_BYTES];
    uint8_t                 incoming[MP_HERO_CARRY_RECORD_BYTES];
    uint8_t                 window[MP_HERO_CARRY_INVENTORY_BYTES];
    mp_hero_carry_outcome_t outcome;
    int32_t                 from = (int32_t)facts->current_player;
    int32_t                 to   = facts->hero_index;

    if (!memory_try_read(record_of(facts->current_player), outgoing, sizeof outgoing) ||
        !memory_try_read(carry.cells.flags_cell, window, sizeof window) ||
        !memory_try_read(record_of((uint32_t)to), incoming, sizeof incoming)) {
        refuse_access(from, to);
        return;
    }
    (void)mp_hero_carry_rule_carry(outgoing, window, incoming, &outcome);
    if (outcome.bytes_changed != 0u &&
        !memory_try_write(record_of((uint32_t)to), incoming, sizeof incoming)) {
        refuse_access(from, to);
        return;
    }

    ++carry.carried;
    if (!outcome.health_carried) {
        ++carry.health_left;
    }
    carry.pending = true;
    carry.from    = from;
    carry.to      = to;
    memcpy(carry.window, window, sizeof window);
    log_info("the hero change %d -> %d carried this player's things: health %d%s, ammunition in "
             "%u of the %u carried slot(s), the inventory and key bytes %02X %02X %02X %02X %02X "
             "%02X; the new hero's own saber, zap gun and pistol counts were left as they were",
             (int)from, (int)to, (int)outcome.health,
             outcome.health_carried ? "" : " (left to the new hero, the old one had none)",
             (unsigned)outcome.ammo_holding, (unsigned)MP_HERO_CARRY_AMMO_CARRIED,
             (unsigned)window[0], (unsigned)window[1], (unsigned)window[2],
             (unsigned)window[3], (unsigned)window[4], (unsigned)window[5]);
}

void mp_hero_carry_before(int32_t hero_index, bool lent)
{
    mp_hero_carry_facts_t   facts;
    mp_hero_carry_verdict_t verdict;

    memset(&facts, 0, sizeof facts);
    carry.pending = false;
    if (!mp_session_now_plays_in_a_running_session(NULL)) {
        return;
    }
    ++carry.seen;
    if (lent || mp_bank_active() != 0u) {
        ++carry.in_window;
        return;
    }
    if (!mp_bank_spawn_cells(&carry.cells)) {
        ++carry.unproven;
        return;
    }
    if (!read_facts(hero_index, &facts)) {
        refuse(&facts, false);
        return;
    }
    verdict = mp_hero_carry_rule_judge(&facts);
    if (verdict == MP_HERO_CARRY_SAME_HERO) {
        ++carry.same_hero;
    } else if (verdict == MP_HERO_CARRY_NO_HERO) {
        ++carry.no_hero;
    } else if (verdict == MP_HERO_CARRY_REFUSED) {
        refuse(&facts, true);
    } else {
        write_carry(&facts);
    }
}

void mp_hero_carry_after(void)
{
    uint8_t  now[MP_HERO_CARRY_INVENTORY_BYTES];
    uint32_t current = 0;

    if (!carry.pending) {
        return;
    }
    carry.pending = false;
    if (!memory_try_read(carry.cells.flags_cell, now, sizeof now) ||
        !memory_try_read_u32(carry.cells.current_cell, &current)) {
        refuse_access(carry.from, carry.to);
        return;
    }
    if (memcmp(now, carry.window, sizeof now) != 0) {
        ++carry.moved;
        log_warning("the hero change %d -> %d still moved the inventory and key bytes (%02X %02X "
                    "%02X %02X %02X %02X before, %02X %02X %02X %02X %02X %02X after), so the "
                    "story relay will see it", (int)carry.from, (int)carry.to,
                    (unsigned)carry.window[0], (unsigned)carry.window[1],
                    (unsigned)carry.window[2], (unsigned)carry.window[3],
                    (unsigned)carry.window[4], (unsigned)carry.window[5], (unsigned)now[0],
                    (unsigned)now[1], (unsigned)now[2], (unsigned)now[3], (unsigned)now[4],
                    (unsigned)now[5]);
    }
    if (current != (uint32_t)carry.to) {
        ++carry.other_current;
        if (!carry.current_logged) {
            carry.current_logged = true;
            log_warning("after the hero change %d -> %d the current player reads %u, so the "
                        "engine's swap did not end on the hero the carry wrote; later ones are "
                        "counted", (int)carry.from, (int)carry.to, (unsigned)current);
        }
    }
}

void mp_hero_carry_report(void)
{
    log_info("  the hero changes: %u seen by the spawn, %u carried (%u with the health left to the "
             "new hero, the old one had none), %u to the same hero, %u inside a far body's window, "
             "%u with no hero before, %u refused at the precondition, %u for want of the proof, "
             "%u read(s) or write(s) refused; the inventory and key bytes moved at %u change(s) "
             "and %u change(s) left another hero current (both must be 0)",
             (unsigned)carry.seen, (unsigned)carry.carried, (unsigned)carry.health_left,
             (unsigned)carry.same_hero, (unsigned)carry.in_window, (unsigned)carry.no_hero,
             (unsigned)carry.refused, (unsigned)carry.unproven, (unsigned)carry.access_refused,
             (unsigned)carry.moved, (unsigned)carry.other_current);
}
