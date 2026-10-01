/* mp_level_state_report.c: what the level's state says in the run report. See the header.
 *
 * Split from mp_level_state.c along the seam that file's size note had named: the report with the
 * per placement lines, which only reads. Everything here reads the counters it is handed and the
 * one answer the module gives anybody, and writes nothing.
 */
#include "mp_level_state_report.h"

#include "mp_level_state.h"
#include "mp_level_state_count.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

/* The placements listed in the report one by one; the rest are counted. */
#define PLACEMENTS_LISTED 16u

void mp_level_state_report_placements(bool host, const uint32_t *host_changes,
                                      const uint32_t *client_changes,
                                      const uint32_t *client_not_owned)
{
    uint32_t listed = 0;
    uint32_t more   = 0;
    uint32_t i;

    for (i = 0; i < MP_LEVEL_STATE_MAX_EMITTERS; ++i) {
        bool any = host ? host_changes[i] != 0u
                        : (client_changes[i] != 0u || client_not_owned[i] != 0u);

        if (!any) {
            continue;
        }
        if (listed >= PLACEMENTS_LISTED) {
            ++more;
            continue;
        }
        ++listed;
        if (host) {
            log_info("  the level's state (host), emitter placement %u: %u change(s) of its own",
                     (unsigned)i, (unsigned)host_changes[i]);
        } else {
            log_info("  the level's state (client), emitter placement %u: %u change(s) made here "
                     "to match, %u left alone for a slot not its own", (unsigned)i,
                     (unsigned)client_changes[i], (unsigned)client_not_owned[i]);
        }
    }
    if (more != 0u) {
        log_info("  the level's state, emitter placements past these: %u more with a change",
                 (unsigned)more);
    }
}

void mp_level_state_report_switches(bool host, bool arms_hulled)
{
    if (arms_hulled) {
        mp_level_state_count_report(host);
    }
}

static const char *escort_said(const mp_level_state_last_t *last)
{
    if (!last->escort_read) {
        return "unread";
    }
    return last->escort_shown ? "up" : "down";
}

static void report_host(const mp_level_state_counts_t *n, const mp_level_state_last_t *last)
{
    log_info("  the level's state (host): %u note(s) sent, %u unsent, %u on a change and %u as "
             "the repeat, %u change(s) held back by the throttle, %u refused as too large, %u the "
             "codec refused to write, %u without the lights because one did not read; the last: "
             "%u emitter placement(s) "
             "(%u on, %u off, %u never spawned, %u spawned and gone), %u of %u light(s) on, the "
             "shared fog's flags %02X, the escort's bar %s at %u, %u room fog run(s), the "
             "journal's newest %u",
             (unsigned)n->sent, (unsigned)n->unsent, (unsigned)n->on_change,
             (unsigned)n->as_repeat, (unsigned)n->held_back, (unsigned)n->too_large,
             (unsigned)n->unencodable, (unsigned)n->lights_left_out, (unsigned)last->emitters,
             (unsigned)last->emitters_on, (unsigned)last->emitters_off,
             (unsigned)last->emitters_never, (unsigned)last->emitters_gone,
             (unsigned)last->lights_on, (unsigned)last->lights, (unsigned)last->fog_flags,
             escort_said(last), (unsigned)last->escort_health, (unsigned)last->viewer_runs,
             (unsigned)mp_level_state_journal_newest());
    log_info("  the level's journal (host): %u entr(ies) (%u emitter, %u light, %u fog, %u crawl, "
             "%u escort)",
             (unsigned)(n->journal_entries[MP_LEVEL_JOURNAL_EMITTER] +
                        n->journal_entries[MP_LEVEL_JOURNAL_LIGHT] +
                        n->journal_entries[MP_LEVEL_JOURNAL_FOG] +
                        n->journal_entries[MP_LEVEL_JOURNAL_CRAWL] +
                        n->journal_entries[MP_LEVEL_JOURNAL_ESCORT]),
             (unsigned)n->journal_entries[MP_LEVEL_JOURNAL_EMITTER],
             (unsigned)n->journal_entries[MP_LEVEL_JOURNAL_LIGHT],
             (unsigned)n->journal_entries[MP_LEVEL_JOURNAL_FOG],
             (unsigned)n->journal_entries[MP_LEVEL_JOURNAL_CRAWL],
             (unsigned)n->journal_entries[MP_LEVEL_JOURNAL_ESCORT]);
}

static void report_client(const mp_level_state_counts_t *n)
{
    log_info("  the level's state (client): %u note(s) taken (%u while one still waited), %u "
             "torn, %u sent to a host, %u about another level, %u of an older generation, %u "
             "older than one already taken, %u with no level open, %u that this side could not "
             "apply | applied from %u substep(s): emitters %u on and %u off here to match, %u not "
             "owned here and left alone, %u spawned nothing; lights %u on and %u off, %u the "
             "engine kept its own; %u count(s) that differ from this level; the escort's bar set "
             "%u time(s) to match, %u where it stayed apart, %u unread; %s",
             (unsigned)n->taken, (unsigned)n->taken_while_held, (unsigned)n->torn,
             (unsigned)n->at_host, (unsigned)n->other_level, (unsigned)n->old_generation,
             (unsigned)n->old_tick, (unsigned)n->no_level, (unsigned)n->cannot_apply,
             (unsigned)n->applied, (unsigned)n->emitters_on, (unsigned)n->emitters_off,
             (unsigned)n->not_owned, (unsigned)n->spawned_nothing, (unsigned)n->lights_on,
             (unsigned)n->lights_off, (unsigned)n->kept_own, (unsigned)n->counts_differ,
             (unsigned)n->escort_set, (unsigned)n->escort_kept, (unsigned)n->escort_unread,
             mp_level_state_holds_a_note() ? "a note waits for the next substep"
                                           : "no note waits");
    log_info("  the level's journal (client): %u entr(ies) replayed in order (%u emitter, %u "
             "light, %u fog, %u crawl, %u escort), %u this side already had, %u the engine did "
             "not take; %u jump(s) past the window healed from the state, %u entr(ies) lost in "
             "them; %u first note(s), %u crawl(s) not shown on a first note",
             (unsigned)(n->replayed[MP_LEVEL_JOURNAL_EMITTER] +
                        n->replayed[MP_LEVEL_JOURNAL_LIGHT] + n->replayed[MP_LEVEL_JOURNAL_FOG] +
                        n->replayed[MP_LEVEL_JOURNAL_CRAWL] +
                        n->replayed[MP_LEVEL_JOURNAL_ESCORT]),
             (unsigned)n->replayed[MP_LEVEL_JOURNAL_EMITTER],
             (unsigned)n->replayed[MP_LEVEL_JOURNAL_LIGHT],
             (unsigned)n->replayed[MP_LEVEL_JOURNAL_FOG],
             (unsigned)n->replayed[MP_LEVEL_JOURNAL_CRAWL],
             (unsigned)n->replayed[MP_LEVEL_JOURNAL_ESCORT], (unsigned)n->already,
             (unsigned)n->not_taken, (unsigned)n->jumps, (unsigned)n->lost, (unsigned)n->firsts,
             (unsigned)n->crawls_first);
}

void mp_level_state_report_counts(bool host, const mp_level_state_counts_t *n,
                                  const mp_level_state_last_t *last)
{
    log_info("  the director's escort and crawl (%s): escort %u seen, %u withheld here, %u to the "
             "engine; crawl %u seen, %u withheld here, %u to the engine (%u of them journaled)",
             host ? "host" : "client", (unsigned)n->escort_seen, (unsigned)n->escort_withheld,
             (unsigned)n->escort_through, (unsigned)n->crawl_seen, (unsigned)n->crawl_withheld,
             (unsigned)n->crawl_through, (unsigned)n->crawl_journaled);
    if (host) {
        report_host(n, last);
    } else {
        report_client(n);
    }
}
