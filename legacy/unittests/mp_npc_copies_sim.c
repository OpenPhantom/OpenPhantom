/* mp_npc_copies_sim.c: the host's run and a client's run together for a long while.
 *
 * The model (mp_npc_copies_model.c) runs two machines through a level and counts what should never
 * happen. Honest runs must count nothing and must be busy; each of four runs that break one thing
 * on purpose must trip the one count that watches it, or the counts would pass for anything; and a
 * client without an overlay must hand nothing, since nothing would ever answer it.
 */
#include "unittest.h"

#include "mp_npc_copies_model.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    npc_model_result_t r;
    uint32_t           sum[15];
    uint32_t           broke[5];
    uint32_t           seed;

    memset(sum, 0, sizeof sum);
    ut_section("two machines, twenty thousand steps each, six times");
    for (seed = 1u; seed <= 6u; ++seed) {
        npc_model_run(seed * 7919u, NPC_MODEL_HONEST, &r);
        sum[0] += r.doubles;
        sum[1] += r.built_over_an_actor;
        sum[2] += r.uncounted;
        sum[3] += r.live_without_actor + r.announced_absent;
        sum[4] += r.grants_overdue;
        sum[5] += r.answered_twice;
        sum[6] += r.unanswered;
        sum[7] += r.mismatched;
        sum[8] += r.copies_seen;
        sum[9] += r.granted;
        sum[10] += r.built;
        sum[11] += r.rides;
        sum[12] += r.granted_across_epochs;
        sum[13] += r.misrouted;
        sum[14] += r.records_refused;
    }
    ut_checkf(sum[9] > 1000u && sum[10] > 500u && sum[8] > 100000u && sum[11] > 0u,
              "a busy level: %u grants, %u built on the client, %u copy-steps alive, %u rides",
              (unsigned)sum[9], (unsigned)sum[10], (unsigned)sum[8], (unsigned)sum[11]);
    ut_checkf(sum[0] == 0u && sum[1] == 0u,
              "no key ever held twice (%u) and no build over a standing actor (%u)",
              (unsigned)sum[0], (unsigned)sum[1]);
    ut_checkf(sum[2] == 0u, "every live copy counts against the cap (%u did not)",
              (unsigned)sum[2]);
    ut_checkf(sum[3] == 0u, "no live row without its actor, nothing announced that is not "
              "there (%u)", (unsigned)sum[3]);
    ut_checkf(sum[4] == 0u, "no grant outlives its deadline (%u did)", (unsigned)sum[4]);
    ut_checkf(sum[5] == 0u && sum[6] == 0u,
              "every wish answered once at its overlay (%u twice, %u not once)",
              (unsigned)sum[5], (unsigned)sum[6]);
    ut_checkf(sum[12] == 0u, "no wish of one world built in the next (%u were)",
              (unsigned)sum[12]);
    ut_checkf(sum[13] == 0u, "and no refusal sent to a slot that did not ask (%u were)",
              (unsigned)sum[13]);
    ut_checkf(sum[14] == 0u, "and no record the contract's publisher refused (%u were)",
              (unsigned)sum[14]);
    ut_checkf(sum[7] == 0u, "after the quiet, both machines hold the same copies (%u keys "
              "differ)", (unsigned)sum[7]);

    ut_section("a client without an overlay hands it nothing");
    npc_model_run(7919u, NPC_MODEL_NO_CLIENT_PANEL, &r);
    ut_checkf(r.handed_without_panel == 0u && r.entries_held_back > 0u && r.granted > 100u,
              "the host goes on granting (%u), the client keeps back %u entries and writes %u",
              (unsigned)r.granted, (unsigned)r.entries_held_back,
              (unsigned)r.handed_without_panel);

    ut_section("and each count trips when the one thing it watches is broken");
    memset(broke, 0, sizeof broke);
    for (seed = 1u; seed <= 3u; ++seed) {
        npc_model_run(seed * 104729u, NPC_MODEL_LYING_WALKS, &r);
        broke[NPC_MODEL_LYING_WALKS] += r.uncounted;
        npc_model_run(seed * 104729u, NPC_MODEL_HIDDEN_KEY, &r);
        broke[NPC_MODEL_HIDDEN_KEY] += r.built_over_an_actor + r.doubles;
        npc_model_run(seed * 104729u, NPC_MODEL_PHANTOM_LIVE, &r);
        broke[NPC_MODEL_PHANTOM_LIVE] += r.live_without_actor + r.announced_absent;
        npc_model_run(seed * 104729u, NPC_MODEL_SAID_TWICE, &r);
        broke[NPC_MODEL_SAID_TWICE] += r.answered_twice;
    }
    ut_checkf(broke[NPC_MODEL_LYING_WALKS] > 0u, "walks that stop short and say they did not: a "
              "live copy without a counting row (%u)", (unsigned)broke[NPC_MODEL_LYING_WALKS]);
    ut_checkf(broke[NPC_MODEL_HIDDEN_KEY] > 0u, "an actor no walk reports: its key handed out "
              "again, and a build over it (%u)", (unsigned)broke[NPC_MODEL_HIDDEN_KEY]);
    ut_checkf(broke[NPC_MODEL_PHANTOM_LIVE] > 0u, "a live row with no actor behind it: seen (%u)",
              (unsigned)broke[NPC_MODEL_PHANTOM_LIVE]);
    ut_checkf(broke[NPC_MODEL_SAID_TWICE] > 0u, "a wish said twice: seen (%u)",
              (unsigned)broke[NPC_MODEL_SAID_TWICE]);
    return ut_summary("mp_npc_copies_sim");
}
