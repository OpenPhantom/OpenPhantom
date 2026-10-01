/* mp_level_state_report.h: what the level's state says in the run report.
 *
 * The counters are counted in mp_level_state.c and printed in mp_level_state_report.c. This
 * header is the one record the two agree on, and the report is handed it, so the level's state
 * stays private to its file. The director's fog, the fog of the viewer and the fog as drawn keep
 * their own counters and print their own lines.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_REPORT_H
#define MULTIPLAYER_MP_LEVEL_STATE_REPORT_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* What the last note built on the host said, for its line. */
typedef struct mp_level_state_last {
    uint32_t emitters;
    uint32_t emitters_on;
    uint32_t emitters_off;
    uint32_t emitters_never;
    uint32_t emitters_gone;
    uint32_t lights;
    uint32_t lights_on;
    uint32_t escort_health;
    bool     escort_read;
    bool     escort_shown;
    uint32_t fog_flags;
    uint32_t viewer_runs;
} mp_level_state_last_t;

typedef struct mp_level_state_counts {
    /* the host */
    uint32_t sent;
    uint32_t unsent;
    uint32_t on_change;
    uint32_t as_repeat;
    uint32_t held_back;
    uint32_t too_large;
    uint32_t unencodable;       /* notes the codec refused to write */
    uint32_t lights_left_out;   /* notes sent without the lights, one of which did not read */
    uint32_t journal_entries[MP_LEVEL_JOURNAL_KINDS];   /* journaled, by kind */
    /* a client */
    uint32_t taken;
    uint32_t taken_while_held;  /* a note arrived while the one before still waited */
    uint32_t torn;
    uint32_t at_host;
    uint32_t other_level;
    uint32_t old_generation;
    uint32_t old_tick;
    uint32_t no_level;
    uint32_t cannot_apply;
    uint32_t applied;
    uint32_t emitters_on;
    uint32_t emitters_off;
    uint32_t not_owned;
    uint32_t spawned_nothing;
    uint32_t lights_on;
    uint32_t lights_off;
    uint32_t kept_own;
    uint32_t counts_differ;
    uint32_t escort_set;
    uint32_t escort_kept;       /* the setter ran and the bar still differs */
    uint32_t escort_unread;
    uint32_t replayed[MP_LEVEL_JOURNAL_KINDS];   /* journal entries played, by kind */
    uint32_t already;           /* entries whose change this side already had */
    uint32_t not_taken;         /* entries the engine did not take */
    uint32_t jumps;
    uint32_t lost;
    uint32_t crawls_first;      /* lines not shown on a first note */
    uint32_t firsts;
    /* the director's escort and crawl, on either side */
    uint32_t escort_seen;
    uint32_t escort_withheld;
    uint32_t escort_through;
    uint32_t crawl_seen;
    uint32_t crawl_withheld;
    uint32_t crawl_through;
    uint32_t crawl_journaled;
} mp_level_state_counts_t;

/* The level's own switches when their arms are hulled. */
void mp_level_state_report_switches(bool host, bool arms_hulled);

/* The director's escort and crawl, then the host's line or the client's, then the journal's. */
void mp_level_state_report_counts(bool host, const mp_level_state_counts_t *n,
                                  const mp_level_state_last_t *last);

/* The emitter placements with a change, one line each for the first of them and a count of the
 * rest. Each array holds MP_LEVEL_STATE_MAX_EMITTERS counts, one per placement. */
void mp_level_state_report_placements(bool host, const uint32_t *host_changes,
                                      const uint32_t *client_changes,
                                      const uint32_t *client_not_owned);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_REPORT_H */
