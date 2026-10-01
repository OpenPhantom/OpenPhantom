/* mp_crate_rule.c: every decision about a push block that needs no engine.
 *
 * The reference numbers are the engine's, written out here rather than taken from the header
 * under test: a substep is 1/32 s and a push moves a block 0.5 units a second, so a substep of
 * pushing is 1/64 of a unit. A test that took both sides of a comparison from the same header
 * would pass whatever the header said.
 */
#include "unittest.h"

#include "mp_crate_rule.h"
#include "mp_crate_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ENGINE_SUBSTEP  (1.0f / 32.0f)
#define ENGINE_SPEED    0.5f
#define ENGINE_STEP     (ENGINE_SPEED * ENGINE_SUBSTEP)

static void set3(float v[3], float x, float y, float z)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

static void check_who_holds_a_block(void)
{
    mp_crate_owner_t owner;

    ut_section("who holds a block");
    mp_crate_owner_init(&owner);
    ut_check(mp_crate_owner_holder(&owner, 100u) == MP_CRATE_NOBODY, "a new block is free");
    ut_check(mp_crate_owner_claim(&owner, 1u, 100u), "the first to push takes it");
    ut_check(mp_crate_owner_holder(&owner, 100u) == 1u, "and holds it");
    ut_check(!mp_crate_owner_claim(&owner, 2u, 110u), "a second player is refused while it does");
    ut_check(!mp_crate_owner_claim(&owner, 0u, 110u),
             "and so is the host's own player: the same rule, slot 0");
    ut_check(mp_crate_owner_claim(&owner, 1u, 120u), "the holder's next push keeps it");
    ut_check(mp_crate_owner_holder(&owner, 120u + 47u) == 1u, "for 47 substeps after it");
    ut_check(mp_crate_owner_holder(&owner, 120u + 48u) == MP_CRATE_NOBODY,
             "and not for 48: an idle holder holds nothing, whatever the record says");
    ut_check(mp_crate_owner_claim(&owner, 2u, 168u), "then the next player takes it");
    mp_crate_owner_release(&owner, 1u);
    ut_check(mp_crate_owner_holder(&owner, 168u) == 2u,
             "a let-go by somebody who does not hold it changes nothing");
    mp_crate_owner_release(&owner, 2u);
    ut_check(mp_crate_owner_holder(&owner, 169u) == MP_CRATE_NOBODY, "a let-go frees it at once");

    ut_check(mp_crate_owner_claim(&owner, 3u, 0xFFFFFFF0u), "held just before the counter wraps");
    ut_check(mp_crate_owner_holder(&owner, 0x00000010u) == 3u,
             "still held 32 substeps later, across the wrap");
    ut_check(mp_crate_owner_holder(&owner, 0x00000030u) == MP_CRATE_NOBODY,
             "and free 64 substeps later");
    ut_check(MP_CRATE_OWNER_TICKS >= 32u && MP_CRATE_OWNER_TICKS <= 64u,
             "the holding time lies between 32 and 64 substeps");
}

static void check_what_an_index_names(void)
{
    ut_section("an index off the wire, checked where the engine does not");
    ut_check(mp_crate_rule_index(78, 78u, true, 0x29, 7) == MP_CRATE_INDEX_OUT_OF_RANGE,
             "the mover count itself is out of range, which the engine's jle lets through");
    ut_check(mp_crate_rule_index(-1, 78u, true, 0x29, 7) == MP_CRATE_INDEX_OUT_OF_RANGE,
             "and so is a negative index");
    ut_check(mp_crate_rule_index(77, 78u, false, 0x29, 7) == MP_CRATE_INDEX_NO_RECORD,
             "an empty slot in the table has no record");
    ut_check(mp_crate_rule_index(10, 78u, true, 0x00, 2) == MP_CRATE_INDEX_NOT_A_BLOCK,
             "a door is no push block");
    ut_check(mp_crate_rule_index(41, 78u, true, 0x29, 4) == MP_CRATE_INDEX_SUNK,
             "a block that sank is no longer pushed");
    ut_check(mp_crate_rule_index(39, 78u, true, 0x04, 7) == MP_CRATE_INDEX_OK, "FEDSHIP 39 is");
}

static void check_what_a_block_is(void)
{
    ut_section("a push block by its rig, never by its kind");
    ut_check(mp_crate_rule_is_block(0x29) && mp_crate_rule_is_block(0x04) &&
                 mp_crate_rule_is_block(0x21) && mp_crate_rule_is_block(0xE4),
             "the rigs of the shipped blocks, SWAMP 10's 0xE4 among them");
    ut_check(!mp_crate_rule_is_block(0x00) && !mp_crate_rule_is_block(0x10) &&
                 !mp_crate_rule_is_block(0x20),
             "and not a rig without any of the three low bits");
    ut_check(mp_crate_rule_can_sink(0x29) && mp_crate_rule_can_sink(0x01) &&
                 !mp_crate_rule_can_sink(0x04),
             "bit 0 sinks the block where it lands; FEDSHIP 41 does, 39 does not");

    ut_section("the opener: a sinking block is no trigger");
    ut_check(mp_crate_rule_opener_is_sink(0x29, 4), "a block retyped to 4 and opened is a sink");
    ut_check(mp_crate_rule_opener_is_sink(0x21, 3), "and to 3 as well");
    ut_check(!mp_crate_rule_opener_is_sink(0x29, 7), "a block still of kind 7 is not");
    ut_check(!mp_crate_rule_opener_is_sink(0x00, 4), "and an ordinary lift of kind 4 is a trigger");
}

static void check_the_flags_a_client_writes(void)
{
    ut_section("only the sink bit is the host's");
    ut_check(mp_crate_rule_merge_flags(0x05u, 0x10u) == 0x15u, "the host's sink bit is taken");
    ut_check(mp_crate_rule_merge_flags(0x15u, 0x00u) == 0x05u, "and its absence clears it here");
    ut_check(mp_crate_rule_merge_flags(0x00u, 0x05u) == 0x00u,
             "carried and carrying are not taken: attach sets them on both blocks of a pair");
    ut_check(mp_crate_rule_merge_flags(0x02u, 0x17u) == 0x12u,
             "and falling is this side's own, set only by its drop");
    ut_check(mp_crate_rule_flags_agree(0x05u, 0x01u) && mp_crate_rule_flags_agree(0x02u, 0x04u),
             "two blocks agree on the carried and sink bits whatever the rider and the drop say");
    ut_check(!mp_crate_rule_flags_agree(0x00u, 0x01u) && !mp_crate_rule_flags_agree(0x10u, 0x00u),
             "and disagree when one is carried or sinks and the other not");
}

static void check_the_host_steps(void)
{
    float at[3];
    float target[3];
    float step[3];

    ut_section("the host pushes toward a wish");
    ut_near(MP_CRATE_STEP_MAX, 1.25 * ENGINE_STEP, 1e-9,
            "the catch up is a quarter more than a substep of the engine's push, 1/64 unit");

    set3(at, 10.0f, 20.0f, 3.0f);
    set3(target, 11.0f, 20.0f, 8.0f);
    ut_check(mp_crate_rule_step_to(at, target, step), "a target a unit away is a step");
    ut_near(step[0], 1.25 * ENGINE_STEP, 1e-6, "of the catch up length");
    ut_check(step[1] == 0.0f && step[2] == 0.0f,
             "along the floor only: the height is the floor's business");

    set3(target, 10.01f, 20.0f, 3.0f);
    ut_check(mp_crate_rule_step_to(at, target, step) && fabsf(step[0] - 0.01f) < 1e-5f,
             "a target nearer than one step is reached in one, not overshot");
    set3(target, 10.005f, 20.0f, 9.0f);
    ut_check(!mp_crate_rule_step_to(at, target, step),
             "and one within 1/128 on the floor is there already");
    ut_near(mp_crate_rule_flat_distance(at, target), 0.005, 1e-5,
            "the distance measured along the floor");

    ut_section("what one push did, the fall asked first");
    ut_check(mp_crate_rule_after_push(0, true, 0.5f, 0.49f) == MP_CRATE_AFTER_FELL,
             "the engine answers 0 for a block that starts to fall, and that is a fall");
    ut_check(mp_crate_rule_after_push(1, true, 0.5f, 0.49f) == MP_CRATE_AFTER_FELL,
             "whatever it answered");
    ut_check(mp_crate_rule_after_push(0, false, 0.5f, 0.5f) == MP_CRATE_AFTER_REFUSED,
             "0 without a fall is a refusal");
    ut_check(mp_crate_rule_after_push(1, false, 0.5f, 0.004f) == MP_CRATE_AFTER_REACHED,
             "a push that arrives has reached");
    ut_check(mp_crate_rule_after_push(1, false, 0.5f, 0.5f) == MP_CRATE_AFTER_STALLED,
             "one that brings it no closer stalled: slid along a wall");
    ut_check(mp_crate_rule_after_push(1, false, 0.5f, 0.52f) == MP_CRATE_AFTER_STALLED,
             "and one that takes it further away as well");
    ut_check(mp_crate_rule_after_push(1, false, 0.5f, 0.49f) == MP_CRATE_AFTER_MOVING,
             "one that brings it closer is on its way");
}

static void check_the_chase(void)
{
    float here[3];
    float goal[3];
    float out[3];

    ut_section("a client follows the host's block");
    set3(here, 0.0f, 0.0f, 0.0f);
    set3(goal, 0.005f, 0.0f, 0.0f);
    ut_check(mp_crate_rule_chase(here, goal, out) == MP_CRATE_CHASE_AGREED,
             "within 1/128 the two agree");
    set3(goal, 0.1f, 0.0f, 0.0f);
    ut_check(mp_crate_rule_chase(here, goal, out) == MP_CRATE_CHASE_STEP,
             "within a quarter unit it is drawn there");
    ut_near(out[0], 1.25 * ENGINE_STEP, 1e-6, "at the host's own catch up pace, no faster");
    set3(goal, 0.0f, 0.0f, 0.01f);
    ut_check(mp_crate_rule_chase(here, goal, out) == MP_CRATE_CHASE_STEP && out[2] == 0.01f,
             "a height difference counts: a landed block is put on its floor");
    set3(goal, 0.3f, 0.0f, 0.0f);
    ut_check(mp_crate_rule_chase(here, goal, out) == MP_CRATE_CHASE_JUMP && out[0] == 0.3f,
             "past a quarter unit it is put there");
    ut_near(MP_CRATE_JUMP, 16.0 * ENGINE_STEP, 1e-6,
            "the quarter unit is sixteen substeps of pushing");
}

static mp_crate_view_t at_rest(float distance)
{
    mp_crate_view_t view;

    memset(&view, 0, sizeof view);
    view.distance    = distance;
    view.flags_agree = true;
    return view;
}

static void check_the_table(void)
{
    mp_crate_view_t view;

    ut_section("the client's table, row by row");
    view = at_rest(0.0f);
    view.host_sunk = true;
    view.here_sunk = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_BOTH_SUNK, "2: sunk on both sides");

    view = at_rest(1.0f);
    view.host_sunk    = true;
    view.here_falling = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_WAIT_TO_SINK,
             "3: sunk there and still falling here waits, rather than sinking in the air");
    view.sink_wait_over = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_SINK, "3: and sinks after two seconds");

    view = at_rest(1.0f);
    view.host_sunk = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_SINK, "4: sunk there, resting here");
    view.sink_held = true;
    view.mine      = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_SINK,
             "4: whoever pushed it, and with this side's landing held");

    view = at_rest(1.0f);
    view.here_sunk    = true;
    view.host_falling = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_UNREPAIRABLE,
             "5: sunk here and whole there cannot be put right here");

    view = at_rest(1.0f);
    view.host_falling = true;
    view.mine         = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_WAIT_HOST_FALL,
             "6: nothing is written into the host's fall, not even for this player's own block");

    view = at_rest(1.0f);
    view.here_falling = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_WAIT_HERE_FALL,
             "7: nor into this side's own");

    view = at_rest(0.2f);
    view.mine = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_RECONCILE,
             "8: this player's own block moves only on the host's answers");
    view.sink_held = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_RECONCILE,
             "8: its own landing held here waits for the host's fall, not cancelled");
    view.other_pusher = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_SECOND,
             "8: unless the host names another pusher: this player is the second at the block");

    view = at_rest(0.001f);
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_AGREED, "9: the same place and flags");
    view.flags_agree = false;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_CHASE,
             "9: the same place with other flags is put right, not agreed");

    view = at_rest(2.0f);
    view.host_carried = true;
    view.same_carrier = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_HANG,
             "10: carried by the same mover is left to the carrier, however far apart");
    view.same_carrier = false;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_JUMP,
             "10: on another carrier it is put where the host has it, once");

    view = at_rest(0.1f);
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_CHASE, "11: near is drawn there");
    view = at_rest(0.3f);
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_JUMP, "12: far is put there");

    view = at_rest(0.0f);
    view.sink_held = true;
    ut_check(mp_crate_rule_decide(&view) == MP_CRATE_ACT_CANCEL_SINK,
             "13: a landing held here that the host never made is given up");
}

static void check_the_answers(void)
{
    float host[3];
    float target[3];
    float correction[3];

    ut_section("the host's answer on this player's own wish");
    set3(host, 5.0f, 5.0f, 1.0f);
    set3(target, 5.0f, 5.0f, 1.0f);
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_ON_THE_WAY, host, target, correction) ==
                 MP_CRATE_RECONCILE_NOTHING,
             "on the way says nothing yet");
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_STALE, host, target, correction) ==
                 MP_CRATE_RECONCILE_NOTHING,
             "nor does stale: a newer wish is on its way");
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_REACHED, host, target, correction) ==
                 MP_CRATE_RECONCILE_NOTHING,
             "reached where the wish put it needs nothing");
    set3(host, 5.1f, 5.0f, 1.0f);
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_REACHED, host, target, correction) ==
                     MP_CRATE_RECONCILE_CORRECT &&
                 fabsf(correction[0] - 0.1f) < 1e-5f && correction[1] == 0.0f,
             "reached somewhere else moves this side by the difference, keeping its own push");
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_REACHED, host, NULL, correction) ==
                 MP_CRATE_RECONCILE_NOTHING,
             "and a wish no longer remembered cannot be held against anything");
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_ENGINE, host, target, correction) ==
                 MP_CRATE_RECONCILE_TO_HOST,
             "refused by the engine goes back to the host's block");
    ut_check(mp_crate_rule_reconcile(MP_CRATE_VERDICT_OTHER_OWNER, host, target, correction) ==
                 MP_CRATE_RECONCILE_TO_HOST,
             "and so does another owner");

    ut_section("whether this player's own push goes to the engine");
    {
        mp_crate_gate_view_t gate;

        memset(&gate, 0, sizeof gate);
        gate.host_speaks = true;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_LET, "a free block is pushed");
        gate.ahead = 3u;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_LET, "three wishes ahead still are");
        gate.ahead = 4u;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_AHEAD,
                 "four ahead stop the player until the host catches up");
        gate.host_speaks = false;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_LET,
                 "but not before the host has said anything of this level: it may not speak it");
        gate.host_speaks     = true;
        gate.ahead           = 0u;
        gate.locked          = true;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_LOCKED, "a second after a refusal");
        gate.locked          = false;
        gate.falling_or_sunk = true;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_FALLING_OR_SUNK,
                 "a block that falls or sank");
        gate.other_holds = true;
        ut_check(mp_crate_rule_gate(&gate) == MP_CRATE_GATE_OTHER_OWNER,
                 "and another player's block first of all");
    }

    ut_section("sequence numbers, read with their sign");
    ut_check(mp_crate_rule_sequence_newer(1u, 0u) && mp_crate_rule_sequence_newer(0u, 65535u),
             "one on is newer, across the wrap as well");
    ut_check(!mp_crate_rule_sequence_newer(5u, 5u) && !mp_crate_rule_sequence_newer(4u, 5u),
             "the same and an older one are not");
    ut_check(mp_crate_rule_sequence_ahead(10u, 7u) == 3u &&
                 mp_crate_rule_sequence_ahead(2u, 65534u) == 4u &&
                 mp_crate_rule_sequence_ahead(5u, 9u) == 0u,
             "how far ahead: three, four across the wrap, and never below none");
    ut_check(mp_crate_rule_push_due(true, false, 10u, 9u) &&
                 mp_crate_rule_push_due(false, true, 10u, 9u),
             "the first wish and a let-go go at once");
    ut_check(!mp_crate_rule_push_due(false, false, 12u, 9u) &&
                 mp_crate_rule_push_due(false, false, 13u, 9u),
             "anything else every four substeps");
}

static void check_the_cadence(void)
{
    mp_crate_cadence_t cadence;

    ut_section("when the host's note goes");
    mp_crate_cadence_init(&cadence);
    ut_check(mp_crate_cadence_due(&cadence, 5u, false) == MP_CRATE_DUE_WHOLE,
             "the first note of a level is whole");
    mp_crate_cadence_sent(&cadence, MP_CRATE_DUE_WHOLE, 5u);
    ut_check(mp_crate_cadence_due(&cadence, 6u, true) == MP_CRATE_DUE_NONE,
             "a change right after it waits");
    ut_check(mp_crate_cadence_due(&cadence, 9u, true) == MP_CRATE_DUE_CHANGE,
             "and goes four substeps later");
    mp_crate_cadence_sent(&cadence, MP_CRATE_DUE_CHANGE, 9u);
    ut_check(mp_crate_cadence_due(&cadence, 12u, true) == MP_CRATE_DUE_NONE &&
                 mp_crate_cadence_due(&cadence, 13u, true) == MP_CRATE_DUE_CHANGE,
             "at most once in four");
    ut_check(mp_crate_cadence_due(&cadence, 30u, false) == MP_CRATE_DUE_NONE,
             "nothing changed, nothing goes");
    ut_check(mp_crate_cadence_due(&cadence, 37u, false) == MP_CRATE_DUE_WHOLE,
             "and once a second the whole note regardless");
    mp_crate_cadence_want_whole(&cadence);
    ut_check(mp_crate_cadence_due(&cadence, 14u, false) == MP_CRATE_DUE_WHOLE,
             "a peer that arrives gets a whole one first");

    ut_section("whether an entry changed");
    {
        mp_crate_entry_t sent;
        mp_crate_entry_t now;

        memset(&sent, 0, sizeof sent);
        sent.id          = 41u;
        sent.kind        = (uint8_t)MP_CRATE_KIND_BLOCK;
        sent.position[0] = 134.5f;
        sent.pusher      = (uint8_t)MP_CRATE_NOBODY;
        now              = sent;
        ut_check(!mp_crate_rule_entry_differs(&sent, &now), "the same entry is no change");
        now.position[0] = 134.5f + 1.0f / 1024.0f;
        ut_check(!mp_crate_rule_entry_differs(&sent, &now),
                 "nor is a move the wire cannot carry");
        now.position[0] = 134.5f + 1.0f / 128.0f;
        ut_check(mp_crate_rule_entry_differs(&sent, &now), "a move it can carry is one");
        sent.flags = MP_CRATE_FLAG_FALLING;
        now.flags  = MP_CRATE_FLAG_FALLING;
        ut_check(!mp_crate_rule_entry_differs(&sent, &now),
                 "but not while the block falls: nobody writes into a fall");
        now          = sent;
        now.verdict  = (uint8_t)MP_CRATE_VERDICT_REACHED;
        ut_check(mp_crate_rule_entry_differs(&sent, &now), "a verdict is a change");
        now          = sent;
        now.sequence = 1u;
        ut_check(mp_crate_rule_entry_differs(&sent, &now), "and so is a sequence number");
        now          = sent;
        now.kind     = (uint8_t)MP_CRATE_KIND_SUNK;
        ut_check(mp_crate_rule_entry_differs(&sent, &now), "and a block that sank");
    }
}

static void check_the_small_ones(void)
{
    float block[3];
    float player[3];
    float delta[3];
    float body[3];

    ut_section("pull or push, and the crush cylinder");
    set3(block, 1.0f, 0.0f, 0.0f);
    set3(player, 0.0f, 0.0f, 0.0f);
    set3(delta, -0.015f, 0.0f, 0.0f);
    ut_check(mp_crate_rule_is_pull(block, player, delta), "toward the player is a pull");
    set3(delta, 0.015f, 0.0f, 0.0f);
    ut_check(!mp_crate_rule_is_pull(block, player, delta), "away from the player a push");

    set3(block, 0.0f, 0.0f, 0.0f);
    set3(body, 0.6f, 0.0f, 0.0f);
    ut_check(mp_crate_rule_in_cylinder(block, 0.5f, 0.5f, body, 0.2f, 1.8f),
             "a body whose circle overlaps the cylinder's is inside");
    set3(body, 1.0f, 0.0f, 0.0f);
    ut_check(!mp_crate_rule_in_cylinder(block, 0.5f, 0.5f, body, 0.2f, 1.8f),
             "one beside it is not");
    set3(body, 0.0f, 0.0f, 0.6f);
    ut_check(!mp_crate_rule_in_cylinder(block, 0.5f, 0.5f, body, 0.2f, 1.8f),
             "nor one standing above its top");
    set3(body, 0.0f, 0.0f, -2.0f);
    ut_check(!mp_crate_rule_in_cylinder(block, 0.5f, 0.5f, body, 0.2f, 1.8f),
             "nor one whose head stays below its base");
}

int main(void)
{
    check_who_holds_a_block();
    check_what_an_index_names();
    check_what_a_block_is();
    check_the_flags_a_client_writes();
    check_the_host_steps();
    check_the_chase();
    check_the_table();
    check_the_answers();
    check_the_cadence();
    check_the_small_ones();
    return ut_summary("the push block rules");
}
