/* mp_level_state_apply.h: a client brings its level to the host's, part by part.
 *
 * Layer 2. mp_level_state.c decides whether a note is taken at all and in which order; this is the
 * matching behind that: the journal's entries in the host's order, then each emitter placement,
 * each light and the escort's bar compared with the note's state and switched with the engine's own
 * functions where they differ. Split from mp_level_state.c when the note grew its journal and the
 * file its second half; the two share the counters the report prints, handed in with each call.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_APPLY_H
#define MULTIPLAYER_MP_LEVEL_STATE_APPLY_H

#include "mp_level_state_journal_rule.h"
#include "mp_level_state_report.h"
#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* One note's application: this level's world and counts, what it made and could not, and the
 * counters of the report. */
typedef struct mp_level_state_apply {
    uint32_t                 world;
    uint32_t                 emitters;
    uint32_t                 lights;
    uint32_t                 made;
    uint32_t                 failed;
    mp_level_state_counts_t *n;
} mp_level_state_apply_t;

/* The journal's entries the plan names, in order. A first note plays nothing and shows no line;
 * after a jump only the lines are played, because the state that follows heals the rest. */
void mp_level_state_apply_journal(mp_level_state_apply_t *apply, const mp_level_state_note_t *note,
                                  const mp_level_journal_plan_t *plan);

/* The note's state: every placement, every light and the escort's bar, where they differ. */
void mp_level_state_apply_state(mp_level_state_apply_t *apply, const mp_level_state_note_t *note);

/* A client's per placement counts for the report, MP_LEVEL_STATE_MAX_EMITTERS each: the changes
 * made here, and the ones left alone for a slot that was not the placement's own. */
const uint32_t *mp_level_state_apply_changes(void);
const uint32_t *mp_level_state_apply_not_owned(void);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_APPLY_H */
