#ifndef PLAYER_SITES_H
#define PLAYER_SITES_H

#include "mouse_look.h"
#include "player_record.h"

#include <stdbool.h>
#include <stdint.h>

/* Everything this DLL has to FIND in the running executable, and nothing it does with what it
 * finds. Kept apart from the feature because it is one job with one answer, "is this a build we
 * recognise, and where is it", and because the byte evidence for five patterns is long enough
 * to bury the code that uses them. */

/* Plr_RunPhases calls its phases through a pointer table in .data: 13 entries and a terminator
 * of 1. Entry 2 is the steering, entry 7 the integrator, the only place a substep's displacement
 * is built. Those two are the whole patch surface of this feature. */
#define PHASE_STEER      2
#define PHASE_INTEGRATE  7
#define PHASE_COUNT     13
#define PHASE_TERMINATOR 1

typedef struct player_sites {
    uint32_t          *phase_table;         /* 13 entries plus the terminator, shape-checked   */
    void *const       *mode_table;          /* NULL-terminated; NULL when it did not resolve   */
    uint8_t          **player_pointer;      /* pPlayer, read out of the instruction that loads */
    input_axis_fn_t    read_relative_axis;  /* the mouse                                       */
    input_axis_fn_t    read_absolute_axis;  /* the keys; NULL disables strafing                */
    set_node_yaw_fn_t  set_node_yaw;        /* NULL leaves the body facing the way it looks    */
} player_sites_t;

/* Resolves everything by signature and logs one line per site. Returns false when either of the
 * two things nothing can run without, the phase table and the mouse axis reader, is missing.
 * Every optional part is reported and left NULL rather than guessed at.
 *
 * `out` is fully written on success and must not be read after a failure. */
bool player_sites_resolve(player_sites_t *out);

/* The two questions every gate in this DLL asks about what was resolved, answered in one place
 * because three source files ask them and a second copy of either would drift.
 *
 * The record, or NULL. The WHOLE range this DLL reads or writes is checked, not just the first
 * field: the record is allocated once and lives for the process, but it is NULL before the first
 * level is entered and the phases can run against a record that is not yet what we take it for. */
uint8_t *player_sites_record(const player_sites_t *resolved);

/* The live mode: where pCurMode sits in the descriptor table, exactly as the engine's own save
 * recovers it. -1 when the table is unknown, the pointer is not in it, or the record is not ready,
 * and every one of those means "do not touch this frame", which is the safe answer.
 *
 * The field that LOOKS like a mode index (+0x39C) is dead during play: it is part of the save
 * tail and holds whatever the last save left, or -1. A gate built on it is permanently closed. */
int player_sites_mode_index(const player_sites_t *resolved, const uint8_t *record);

#endif /* PLAYER_SITES_H */
