/* mp_bank_spawn.c: the spawn's window, the half that knows the cells. See the header. */
#include "mp_bank_spawn.h"

#include "mp_bank_keep.h"
#include "mp_cells.h"
#include "mp_signatures.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

/* The part of status_setActivePlayer the proof reads. Its last operand the proof needs is at
 * +0x128 and the function runs on to its ret at +0x1D4, so nothing of a neighbour is counted. */
#define PROOF_BYTES 0x140u

/* The six bytes are the campaign bank's bytes 6 to 11, bits 48 to 95. */
#define FLAGS_IN_BANK 6u

typedef struct spawn_keep_state {
    int            proof;           /* 0 not asked, 1 proven, -1 refused for the process */
    bool           refusal_logged;
    bool           first_logged;
    uintptr_t      status_cell;
    uintptr_t      flags_cell;
    uintptr_t      records_cell;
    uintptr_t      current_cell;
    mp_bank_keep_t kept;

    uint32_t windows;
    uint32_t moved;                 /* windows in which the spawn moved something */
    uint32_t flag_bytes;
    uint32_t record_bytes;
    uint32_t current_changes;
    uint32_t write_refused;
    uint32_t still_apart;           /* bytes that differ after the write; must stay 0 */
    uint32_t refused;               /* spawns refused at the precondition */
    uint32_t unproven;              /* spawns refused for want of the proof */
} spawn_keep_state_t;

static spawn_keep_state_t keep;

static bool read_keep(mp_bank_keep_t *image)
{
    return memory_try_read(keep.flags_cell, image->flags, sizeof image->flags) &&
           memory_try_read(keep.records_cell, image->records, sizeof image->records) &&
           memory_read_u32(keep.current_cell, &image->current_player);
}

/* The cells come out of other sites' operands, and status_setActivePlayer names its own. Before
 * the first window or the first carry of a hero change, whichever comes first, the function is
 * asked, out of the file, whether it names the same ones: the flag bytes twice, the current
 * player once, the status pointer twice, the record base once. The answer is kept either way,
 * because it is a property of the executable, and a refusal stops both users. */
static bool prove(uintptr_t status_cell)
{
    uintptr_t site;
    uintptr_t story;
    uint8_t   code[PROOF_BYTES];
    size_t    flags_named;
    size_t    current_named;
    size_t    pointer_named;
    size_t    base_named;

    if (keep.proof != 0) {
        return keep.proof > 0;
    }
    keep.proof = -1;

    keep.status_cell  = status_cell;
    story             = mp_cells_address(MP_CELL_STORY_FLAGS);
    keep.records_cell = mp_cells_address(MP_CELL_PLR_STATUS_ARRAY);
    keep.current_cell = mp_cells_address(MP_CELL_CURRENT_PLAYER);
    site              = mp_signatures_address(MP_SITE_STATUS_SET_ACTIVE);
    if (story == 0u || keep.records_cell == 0u || keep.current_cell == 0u || site == 0u ||
        status_cell == 0u || !host_image_read_original(site, code, sizeof code)) {
        log_warning("far bodies are not spawned and a change of this player's hero carries "
                    "nothing: the cells cannot be proven, because the story bank (%08X), the "
                    "hero records (%08X), the current player (%08X) or status_setActivePlayer "
                    "(%08X) did not resolve or read. A spawn or a carry on cells it cannot name "
                    "would take this player's keys",
                    (unsigned)story, (unsigned)keep.records_cell, (unsigned)keep.current_cell,
                    (unsigned)site);
        return false;
    }
    keep.flags_cell = story + FLAGS_IN_BANK;

    flags_named   = mp_bank_keep_operands(code, sizeof code, (uint32_t)keep.flags_cell);
    current_named = mp_bank_keep_operands(code, sizeof code, (uint32_t)keep.current_cell);
    pointer_named = mp_bank_keep_operands(code, sizeof code, (uint32_t)status_cell);
    base_named    = mp_bank_keep_operands(code, sizeof code, (uint32_t)keep.records_cell);
    if (flags_named < 2u || current_named < 1u || pointer_named < 2u || base_named < 1u) {
        log_warning("far bodies are not spawned and a change of this player's hero carries "
                    "nothing: status_setActivePlayer at %08X names the flag bytes %u time(s), "
                    "the current player %u, the status pointer %u and the record base %u, where "
                    "the spawn's window and the carry need 2, 1, 2 and 1. A spawn or a carry on "
                    "cells it cannot name would take this player's keys",
                    (unsigned)site, (unsigned)flags_named, (unsigned)current_named,
                    (unsigned)pointer_named, (unsigned)base_named);
        return false;
    }
    keep.proof = 1;
    log_info("the spawn's window gives back what status_setActivePlayer at %08X and the starting "
             "kit take, and the function names the same cells: flag bytes %08X, hero records "
             "%08X, current player %08X, status pointer %08X",
             (unsigned)site, (unsigned)keep.flags_cell, (unsigned)keep.records_cell,
             (unsigned)keep.current_cell, (unsigned)status_cell);
    return true;
}

/* The status pointer the carry asks for is the same cell mp_bank hands the window, so whichever
 * user comes first proves the same four cells. */
bool mp_bank_spawn_cells(mp_bank_spawn_cells_t *out)
{
    if (out == NULL || !prove(mp_cells_address(MP_CELL_PLR_STATUS_POINTER))) {
        return false;
    }
    out->status_cell  = keep.status_cell;
    out->flags_cell   = keep.flags_cell;
    out->records_cell = keep.records_cell;
    out->current_cell = keep.current_cell;
    return true;
}

bool mp_bank_spawn_take(uintptr_t status_cell, size_t index, int32_t local_hero)
{
    uint32_t status_pointer = 0;
    uint32_t current        = 0;

    if (!prove(status_cell)) {
        ++keep.unproven;
        return false;
    }
    if (!memory_read_u32(keep.status_cell, &status_pointer) ||
        !memory_read_u32(keep.current_cell, &current) ||
        !mp_bank_keep_precondition(status_pointer, (uint32_t)keep.records_cell, current,
                                   local_hero)) {
        ++keep.refused;
        if (!keep.refusal_logged) {
            keep.refusal_logged = true;
            log_warning("bank %u's body is not spawned: the status pointer names %08X and the "
                        "current player is %u, where this machine's hero %d has its record at "
                        "%08X. The window puts the pointer back from what it saved and the index "
                        "back from the hero, so it would leave the two on different heroes; later "
                        "refusals are counted",
                        (unsigned)index, (unsigned)status_pointer, (unsigned)current,
                        (int)local_hero,
                        (unsigned)(keep.records_cell +
                                   (uintptr_t)(uint32_t)local_hero * MP_BANK_KEEP_RECORD_BYTES));
        }
        return false;
    }
    if (!read_keep(&keep.kept)) {
        ++keep.refused;
        return false;
    }
    return true;
}

void mp_bank_spawn_give_back(void)
{
    mp_bank_keep_t      live;
    mp_bank_keep_t      spawned;
    mp_bank_keep_t      after;
    mp_bank_keep_back_t back;
    size_t              record;
    uint32_t            refused = 0;
    uint32_t            apart   = 0;
    bool                read_back;

    ++keep.windows;
    if (!read_keep(&live)) {
        ++keep.write_refused;
        return;
    }
    spawned = live;
    if (!mp_bank_keep_restore(&keep.kept, &live, &back)) {
        return;
    }
    ++keep.moved;
    keep.flag_bytes += back.flag_bytes;
    keep.record_bytes += back.record_bytes;
    if (back.flags && !memory_try_write(keep.flags_cell, live.flags, sizeof live.flags)) {
        ++refused;
    }
    for (record = 0; record < MP_BANK_KEEP_RECORDS; ++record) {
        if (back.records[record] &&
            !memory_try_write(keep.records_cell + record * MP_BANK_KEEP_RECORD_BYTES,
                              live.records[record], MP_BANK_KEEP_RECORD_BYTES)) {
            ++refused;
        }
    }
    if (back.current_player) {
        ++keep.current_changes;
        if (!memory_try_write(keep.current_cell, &live.current_player,
                              sizeof live.current_player)) {
            ++refused;
        }
    }
    read_back = read_keep(&after);
    if (read_back) {
        apart = mp_bank_keep_differing(&keep.kept, &after);
    } else {
        ++refused;
        after = spawned;
    }
    keep.write_refused += refused;
    keep.still_apart += apart;
    if (!keep.first_logged) {
        keep.first_logged = true;
        log_info("a far body's spawn moved this player's own state and its window gave it back: "
                 "the inventory and key bytes were %02X %02X %02X %02X %02X %02X, the spawn left "
                 "%02X %02X %02X %02X %02X %02X and they read %02X %02X %02X %02X %02X %02X "
                 "after the give back%s; %u byte(s) of the four hero records and %u change(s) of "
                 "the current player went back, %u write(s) refused and %u byte(s) still apart "
                 "(both must be 0)",
                 (unsigned)keep.kept.flags[0], (unsigned)keep.kept.flags[1],
                 (unsigned)keep.kept.flags[2], (unsigned)keep.kept.flags[3],
                 (unsigned)keep.kept.flags[4], (unsigned)keep.kept.flags[5],
                 (unsigned)spawned.flags[0], (unsigned)spawned.flags[1],
                 (unsigned)spawned.flags[2], (unsigned)spawned.flags[3],
                 (unsigned)spawned.flags[4], (unsigned)spawned.flags[5],
                 (unsigned)after.flags[0], (unsigned)after.flags[1],
                 (unsigned)after.flags[2], (unsigned)after.flags[3],
                 (unsigned)after.flags[4], (unsigned)after.flags[5],
                 read_back ? "" : " (they did NOT read back, so this is what the spawn left)",
                 (unsigned)back.record_bytes, back.current_player ? 1u : 0u, (unsigned)refused,
                 (unsigned)apart);
    }
}

void mp_bank_spawn_report(void)
{
    log_info("  the spawn's window: %u window(s), %u of them with this player's own state moved "
             "by the spawn; %u inventory byte(s), %u hero record byte(s) and %u current player "
             "change(s) put back; %u write(s) refused and %u byte(s) still apart after the write "
             "(both must be 0); %u spawn(s) refused at the precondition, %u for want of the proof",
             (unsigned)keep.windows, (unsigned)keep.moved, (unsigned)keep.flag_bytes,
             (unsigned)keep.record_bytes, (unsigned)keep.current_changes,
             (unsigned)keep.write_refused, (unsigned)keep.still_apart, (unsigned)keep.refused,
             (unsigned)keep.unproven);
}
