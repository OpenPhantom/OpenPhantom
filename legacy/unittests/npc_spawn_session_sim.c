/* npc_spawn_session_sim.c: the panel's session rules against the real multiplayer, two machines.
 *
 * The model of two machines (mp_npc_copies_model.c) runs the multiplayer's own core on both sides
 * and an overlay of its own that answers the grant records. Here the overlays are the panel's:
 * npc_spawn_session, with the engine reached through the model's pool. What the model counts is
 * what should never happen whoever answers, so the same counts must stay at zero with the panel's
 * rules in place of the model's, and the run must be as busy.
 *
 * What would be silent without this: a rule of the panel that is right against a faked record and
 * wrong against the one the multiplayer really writes, in the order it really writes it.
 */
#include "unittest.h"

#include "mp_npc_copies_model.h"
#include "npc_spawn_session.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static npc_spawn_session_t s_panel[2];
static int                 s_machine[2] = { 0, 1 };

static npc_spawn_outcome_t build(void *user, const npc_spawn_desc_t *desc, uint32_t key,
                                 const npc_spawn_saved_t *saved)
{
    (void)desc;
    (void)saved;
    return npc_model_build(*(const int *)user, key) ? NPC_SPAWN_OUTCOME_DONE
                                                     : NPC_SPAWN_OUTCOME_REFUSED;
}

static npc_spawn_outcome_t remove_key(void *user, uint32_t key)
{
    return npc_model_remove(*(const int *)user, key) ? NPC_SPAWN_OUTCOME_DONE
                                                      : NPC_SPAWN_OUTCOME_REFUSED;
}

static const npc_spawn_session_ops_t OPS[2] = {
    { &build, &remove_key, &s_machine[0], NULL },
    { &build, &remove_key, &s_machine[1], NULL },
};

static void start(void *user, int machine, uint32_t first, bool absent)
{
    (void)user;
    npc_spawn_session_init(&s_panel[machine], absent ? 0u : (uint8_t)NPC_SPAWN_PANEL_BUILDER);
    s_panel[machine].record.first = first;   /* a new process; the model tracks by serial */
}

static uint32_t wish(void *user, int machine, uint8_t kind, const npc_spawn_note_desc_t *note)
{
    npc_spawn_session_t *p = &s_panel[machine];
    npc_spawn_desc_t     desc;
    npc_spawn_saved_t    saved;
    bool                 builds = kind == NPC_SPAWN_WISH_SPAWN || kind == NPC_SPAWN_WISH_RESTORE;

    (void)user;
    memset(&desc, 0, sizeof desc);
    memset(&saved, 0, sizeof saved);
    if (builds && !npc_spawn_session_from_note(note, &desc)) {
        return 0u;
    }
    saved.desc = desc;
    memcpy(saved.position, desc.position, sizeof saved.position);
    if (!npc_spawn_session_wish(p, kind, builds ? &desc : NULL,
                                kind == NPC_SPAWN_WISH_RESTORE ? &saved : NULL)) {
        return 0u;
    }
    /* Went into the record at once when nothing waits behind it: the last serial is this one. */
    return p->queued == 0u && p->record.count > 0u ? p->record.first + p->record.count - 1u : 0u;
}

static void step(void *user, int machine, const npc_spawn_grant_record_t *grants, bool now)
{
    (void)user;
    (void)npc_spawn_session_frame(&s_panel[machine], grants, now, &OPS[machine]);
    npc_spawn_session_published(&s_panel[machine], true);
}

static const npc_spawn_wish_record_t *record(void *user, int machine)
{
    (void)user;
    return npc_spawn_session_record(&s_panel[machine]);
}

static const npc_model_overlay_t OVERLAY = { &start, &wish, &step, &record, NULL };

int main(void)
{
    npc_model_result_t r;
    uint32_t           bad;
    uint32_t           granted = 0;
    uint32_t           built   = 0;
    uint32_t           rides   = 0;
    uint32_t           stale   = 0;
    uint32_t           ended   = 0;
    uint32_t           seed;

    ut_section("the panel's own rules on both machines, twenty thousand steps each, six times");
    bad = 0;
    for (seed = 1u; seed <= 6u; ++seed) {
        npc_model_run_with(seed * 7919u, NPC_MODEL_HONEST, &OVERLAY, &r);
        bad += r.doubles + r.built_over_an_actor + r.uncounted + r.live_without_actor +
               r.announced_absent + r.grants_overdue + r.answered_twice + r.unanswered +
               r.mismatched + r.granted_across_epochs + r.misrouted + r.records_refused;
        granted += r.granted;
        built += r.built;
        rides += r.rides;
        stale += s_panel[0].counters.cancel_stale + s_panel[1].counters.cancel_stale;
        ended += s_panel[0].counters.ended + s_panel[1].counters.ended;
        ut_checkf(r.doubles + r.built_over_an_actor == 0u,
                  "seed %u: no key held twice (%u), no build over a standing actor (%u)",
                  (unsigned)seed, (unsigned)r.doubles, (unsigned)r.built_over_an_actor);
        ut_checkf(r.answered_twice + r.unanswered == 0u,
                  "seed %u: every wish answered once (%u twice, %u not once)", (unsigned)seed,
                  (unsigned)r.answered_twice, (unsigned)r.unanswered);
        ut_checkf(r.mismatched + r.granted_across_epochs == 0u,
                  "seed %u: both machines agree after the quiet (%u), nothing built across an "
                  "epoch (%u)", (unsigned)seed, (unsigned)r.mismatched,
                  (unsigned)r.granted_across_epochs);
        ut_checkf(r.uncounted + r.live_without_actor + r.announced_absent + r.grants_overdue +
                          r.misrouted + r.records_refused == 0u,
                  "seed %u: the host's table stays true (%u %u %u %u %u %u)", (unsigned)seed,
                  (unsigned)r.uncounted, (unsigned)r.live_without_actor,
                  (unsigned)r.announced_absent, (unsigned)r.grants_overdue,
                  (unsigned)r.misrouted, (unsigned)r.records_refused);
    }
    ut_checkf(bad == 0u && granted > 1000u && built > 500u && rides > 0u,
              "and the runs were busy: %u granted, %u built on the client, %u rides, %u faults",
              (unsigned)granted, (unsigned)built, (unsigned)rides, (unsigned)bad);
    ut_checkf(ended > 0u, "the panels ended wishes of an old epoch (%u)", (unsigned)ended);
    ut_checkf(stale + rides > 0u, "and met cancels of lives they no longer held, or ridden copies "
              "(%u, %u)", (unsigned)stale, (unsigned)rides);
    return ut_summary("npc_spawn_session_sim");
}
