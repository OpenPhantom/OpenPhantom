/* The client's offset onto the host, after his saved game or at a fresh co-operative level: the
 * three decisions behind it.
 *
 * Whether a run wants an offset at all, whether a resolved far pose is one to stand beside, and
 * what one tick does with an armed offset are arithmetic with a known right answer, and they run
 * here with no session and no game in the process.
 *
 * The bridge's answers are stubbed here, and that is deliberate rather than a shortcut. The
 * module under test is the real one; what is replaced is the WIRE, because the questions it asks
 * the bridge are the ones the test has to control, and linking the real bridge would drag
 * sixty files in to answer them with a session that does not exist. The seat search and the
 * placement are the real ones and refuse in a test process, which is what the last section pins:
 * an offset that reaches a machine with no resolved sites has to stay armed rather than move a
 * body to somewhere nothing was probed.
 */
#include "unittest.h"

#include "mp_arrival.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_respawn.h"
#include "mp_seat.h"
#include "mp_seat_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One search with nothing to keep away from but the bodies handed in. */
static mp_seat_outcome_t probe(const float target[3], bool beside, uint8_t slot,
                               const mp_seat_body_t *bodies, size_t body_count,
                               mp_seat_counts_t *counts, float seat[3])
{
    return mp_seat_probe_avoiding(target, beside, slot, bodies, body_count, NULL, counts,
                                  seat);
}

/* ---- the wire, as this test chooses to answer it -------------------------------------------- */

static bool                 stub_is_client;
static uint8_t              stub_my_slot;
static bool                 stub_setup_known;
static mp_lobby_setup_t     stub_setup;
static bool                 stub_pose_known;
static mp_bridge_far_pose_t stub_pose;

/* The host's history as the level begins: this test's holds nothing, so any pose it resolves
 * later is one sampled after the mark. */
bool mp_bridge_drain_host_history_mark(uint32_t *newest_tick, uint32_t *starts)
{
    *newest_tick = 0u;
    *starts      = 0u;
    return false;
}

bool mp_bridge_drain_host_elsewhere(void)
{
    return false;
}

bool mp_bridge_drain_is_client(void)
{
    return stub_is_client;
}

uint8_t mp_bridge_drain_my_slot(void)
{
    return stub_my_slot;
}

bool mp_bridge_drain_far_pose(mp_bridge_far_pose_t *out)
{
    if (out == NULL || !stub_pose_known) {
        return false;
    }
    *out = stub_pose;
    return true;
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (out == NULL || !stub_setup_known) {
        return false;
    }
    *out = stub_setup;
    return true;
}

/* ---- the decisions --------------------------------------------------------------------------- */

static void check_which_run_wants_an_offset(void)
{
    ut_section("a host is already where its own savegame or the level start put it");
    ut_check(mp_arrival_wanted_for(false, true, true, true) == MP_ARRIVAL_WANT_NO,
             "the side that loaded the file does not move");
    ut_check(mp_arrival_wanted_for(false, true, false, true) == MP_ARRIVAL_WANT_NO,
             "nor the side whose fresh level everybody else joins");
    ut_check(mp_arrival_wanted_for(false, false, false, false) == MP_ARRIVAL_WANT_NO,
             "and neither does a machine in no session at all");

    ut_section("a client of a fresh co-operative level is seated beside the host too");
    ut_check(mp_arrival_wanted_for(true, true, false, true) == MP_ARRIVAL_WANT_YES,
             "the level's one start point would put every player of a fresh level in one body, "
             "which is what the four player run showed");
    ut_check(mp_arrival_wanted_for(true, true, false, false) == MP_ARRIVAL_WANT_NO,
             "a fresh deathmatch level keeps its start point: beside the host is no rule there");

    ut_section("a client of a savegame belongs beside the host");
    ut_check(mp_arrival_wanted_for(true, true, true, true) == MP_ARRIVAL_WANT_YES,
             "this is the whole requirement");
    ut_check(mp_arrival_wanted_for(true, true, true, false) == MP_ARRIVAL_WANT_YES,
             "and it is the host's saved game whatever the game");

    ut_section("and a client that has not read the setup note yet cannot tell");
    ut_check(mp_arrival_wanted_for(true, false, false, false) == MP_ARRIVAL_WANT_UNKNOWN,
             "unknown rather than no, or the question is settled on the first frame and never "
             "revisited");
    ut_check(mp_arrival_wanted_for(true, false, true, true) == MP_ARRIVAL_WANT_UNKNOWN,
             "the savegame bit of a setup nobody has read is not evidence either");
}

static void check_which_pose_is_an_anchor(void)
{
    const uint8_t host   = (uint8_t)MP_BRIDGE_HOST_SLOT;
    const uint8_t client = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;

    ut_section("a pose that was never resolved is not an anchor");
    ut_check(!mp_arrival_anchor_is_ready(false, host, client, true, false),
             "before a peer arrives there is nobody to stand beside");

    ut_section("the anchor has to be the host's own world slot");
    ut_check(mp_arrival_anchor_is_ready(true, host, client, true, false),
             "the host's slot, read by a client, is the pose this feature is about");
    ut_check(!mp_arrival_anchor_is_ready(true, 2u, client, true, false),
             "another client's slot is somebody else, and standing beside him is not the rule");

    ut_section("a machine whose own slot is the host's is the host");
    ut_check(!mp_arrival_anchor_is_ready(true, host, host, true, false),
             "a side reading its own echo moves nobody");

    ut_section("the far machine's own two words about the body, which are not each other");
    ut_check(!mp_arrival_anchor_is_ready(true, host, client, false, false),
             "neither alive nor dead is a body that does not exist yet: a host still loading");
    ut_check(!mp_arrival_anchor_is_ready(true, host, client, false, true),
             "a dead host is not a place to arrive at");
    ut_check(!mp_arrival_anchor_is_ready(true, host, client, true, true),
             "and a body called both is not trusted in the direction that moves somebody");

    ut_section("a pose sampled before the level began here is not an anchor, however alive");
    ut_check(!mp_arrival_pose_is_after(true, 100u, 1u, 100u, 1u),
             "the newest sample the history held at the mark is from before it");
    ut_check(!mp_arrival_pose_is_after(true, 100u, 1u, 97u, 1u), "and so is an older one");
    ut_check(mp_arrival_pose_is_after(true, 100u, 1u, 101u, 1u),
             "the next tick of the same history is new");
    ut_check(mp_arrival_pose_is_after(true, 100u, 1u, 5u, 2u),
             "a history begun again since holds nothing from before, whatever tick it carries");
    ut_check(mp_arrival_pose_is_after(false, 0u, 0u, 5u, 0u),
             "a history that held nothing at the mark has nothing from before either");
    ut_check(mp_arrival_pose_is_after(true, 0xFFFFFFF0u, 1u, 3u, 1u) &&
                 !mp_arrival_pose_is_after(true, 3u, 1u, 0xFFFFFFF0u, 1u),
             "and a tick counter that wraps is read the right way round");
}

static void check_what_one_tick_does(void)
{
    ut_section("nothing armed is nothing to do");
    ut_check(mp_arrival_step(false, MP_ARRIVAL_WANT_YES, true, 0u) == MP_ARRIVAL_STEP_IDLE,
             "an unarmed offset ignores a perfectly good anchor");

    ut_section("a settled no stands the offset down at once");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_NO, true, 0u) == MP_ARRIVAL_STEP_STAND_DOWN,
             "rather than asking the same question every frame for the rest of the level");

    ut_section("an unknown want waits, because the answer is still coming");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_UNKNOWN, true, 0u) == MP_ARRIVAL_STEP_WAIT,
             "an anchor is not a reason to act on a session nobody has described yet");

    ut_section("wanted, with the host standing, is the moment");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_YES, true, 0u) == MP_ARRIVAL_STEP_RUN,
             "and it is the only step that moves a body");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_YES, false, 0u) == MP_ARRIVAL_STEP_WAIT,
             "without one it waits, and never falls back on the level start early");

    ut_section("the deadline");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_YES, false,
                             MP_ARRIVAL_DEADLINE_SUBSTEPS - 1u) == MP_ARRIVAL_STEP_WAIT,
             "one substep short of it is still a wait");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_YES, false,
                             MP_ARRIVAL_DEADLINE_SUBSTEPS) == MP_ARRIVAL_STEP_DROP,
             "and on it the offset is given up rather than carried out at some later moment");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_UNKNOWN, true,
                             MP_ARRIVAL_DEADLINE_SUBSTEPS) == MP_ARRIVAL_STEP_DROP,
             "a client that never read a setup note gives up on the same deadline");

    ut_section("an anchor that arrives on the very substep the wait runs out is used");
    ut_check(mp_arrival_step(true, MP_ARRIVAL_WANT_YES, true,
                             MP_ARRIVAL_DEADLINE_SUBSTEPS) == MP_ARRIVAL_STEP_RUN,
             "the anchor is asked about before the deadline, and that order is the decision");
}

/* ---- the module, driven ---------------------------------------------------------------------- */

static void arm_a_client_of_a_savegame(void)
{
    stub_is_client   = true;
    stub_my_slot     = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
    stub_setup_known = true;
    memset(&stub_setup, 0, sizeof stub_setup);
    stub_setup.mode  = (uint8_t)MP_LOBBY_MODE_COOP;
    stub_setup.flags = (uint8_t)(MP_LOBBY_F_FROM_SAVE | MP_LOBBY_F_STARTED);

    stub_pose_known = true;
    memset(&stub_pose, 0, sizeof stub_pose);
    stub_pose.slot        = (uint8_t)MP_BRIDGE_HOST_SLOT;
    stub_pose.position[0] = 10.0f;
    stub_pose.position[1] = 20.0f;
    stub_pose.position[2] = 30.0f;
    stub_pose.heading     = 90.0f;
    stub_pose.alive       = true;

    mp_arrival_note_level_begin();
}

static void check_it_moves_nobody_with_no_game(void)
{
    float            anchor[3] = { 1.0f, 2.0f, 3.0f };
    float            seat[3]   = { 0.0f, 0.0f, 0.0f };
    mp_seat_counts_t counts;
    uint32_t         substep;

    memset(&counts, 0, sizeof counts);
    ut_section("a module nobody has told about a level begin says so rather than staying silent");
    mp_arrival_report();

    ut_section("nothing resolves in a test process");
    ut_check(!mp_respawn_installed(),
             "the world probes are absent, which is what the seat search hangs on");
    ut_check(probe(anchor, true, 1u, NULL, 0u, &counts, seat) != MP_SEAT_FOUND,
             "so no point beside anybody is ever produced");
    ut_check(seat[0] == 0.0f && seat[1] == 0.0f && seat[2] == 0.0f,
             "and a refused search writes no seat, so a caller cannot use one it never got");

    ut_section("a host stands its offset down and moves nobody");
    stub_is_client = false;
    mp_arrival_note_level_begin();
    mp_arrival_tick(1u);
    mp_arrival_tick(2u);
    ut_check(!mp_respawn_pending(),
             "the offset never goes through the door a dead player comes back by");

    ut_section("a client of a savegame waits rather than seating a body nowhere");
    arm_a_client_of_a_savegame();
    /* Past the seat's three seconds: the search falls back, finds no authored point in a process
     * with no level, and goes on looking rather than handing the placement a point nothing
     * vouched for. */
    for (substep = 10u; substep < 10u + 3u * MP_SEAT_GIVE_UP_SUBSTEPS; ++substep) {
        mp_arrival_tick(substep);
    }
    ut_check(!mp_respawn_pending(),
             "the search answered nothing, and nothing was seated on the host instead");

    ut_section("an offset that has been stood down is not re-armed by anything but a level begin");
    stub_is_client = false;
    mp_arrival_tick(1000u);
    stub_is_client = true;
    mp_arrival_tick(1001u);
    mp_arrival_tick(1002u);
    ut_check(!mp_respawn_pending(),
             "which is what makes it at most once per level rather than once per frame");

    ut_section("a client of a fresh co-operative level is armed as well");
    arm_a_client_of_a_savegame();
    stub_setup.flags = (uint8_t)MP_LOBBY_F_STARTED;
    mp_arrival_tick(2000u);
    ut_check(!mp_respawn_pending(), "and waits for a seat beside the host the same way");

    ut_section("and the report can name what it did");
    mp_arrival_report();
    ut_check(true, "three level begins: two stood down, one for a savegame, one for a fresh level");
}

/* The anchor was the host alone, and only a living one: a player who arrived while the host lay
 * dead stood at the level start until the deadline, whoever else stood beside the host. */
static void check_who_is_arrived_beside(void)
{
    mp_seat_body_t      others[3];
    const float         host_at[3] = { 0.0f, 0.0f, 0.0f };
    size_t              chosen     = 99u;
    mp_arrival_anchor_t kind;

    memset(others, 0, sizeof others);
    others[0].known = true;
    others[0].stands = true;
    others[0].position[0] = 40.0f;   /* slot 1, standing, forty units from the host */
    others[1].known = true;
    others[1].stands = true;
    others[1].position[0] = 10.0f;   /* slot 2, standing, ten */

    ut_section("the host when he stands");
    ut_check(mp_arrival_pick_anchor(true, false, host_at, others, 2u, &chosen) ==
                 MP_ARRIVAL_ANCHOR_HOST,
             "a standing host is the anchor whoever else stands");

    ut_section("while he lies dead, the standing far player nearest him");
    kind = mp_arrival_pick_anchor(false, true, host_at, others, 2u, &chosen);
    ut_checkf(kind == MP_ARRIVAL_ANCHOR_OTHER && chosen == 1u,
              "two stand: the one ten units from the host rather than forty (index %u)",
              (unsigned)chosen);
    others[1].stands = false;
    kind = mp_arrival_pick_anchor(false, true, host_at, others, 2u, &chosen);
    ut_checkf(kind == MP_ARRIVAL_ANCHOR_OTHER && chosen == 0u,
              "host dead, slot 1 stands and slot 2 lies: slot 1 (index %u)", (unsigned)chosen);
    others[0].stands = false;
    ut_check(mp_arrival_pick_anchor(false, true, host_at, others, 2u, &chosen) ==
                 MP_ARRIVAL_ANCHOR_NONE,
             "nobody stands: nobody, and the offset waits as it always did");

    ut_section("a host who is neither standing nor dead is no reason to go elsewhere");
    others[0].stands = true;
    ut_check(mp_arrival_pick_anchor(false, false, host_at, others, 2u, &chosen) ==
                 MP_ARRIVAL_ANCHOR_NONE,
             "a host still loading: the players around him may not have their own seats yet");
    ut_check(mp_arrival_pick_anchor(false, true, NULL, others, 2u, &chosen) ==
                 MP_ARRIVAL_ANCHOR_NONE,
             "and a dead host with no place to measure from is none either");
}

int main(void)
{
    check_which_run_wants_an_offset();
    check_which_pose_is_an_anchor();
    check_who_is_arrived_beside();
    check_what_one_tick_does();
    check_it_moves_nobody_with_no_game();

    return ut_summary("mp_arrival");
}
