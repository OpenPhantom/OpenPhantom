/* Which of the two rule sets a death falls under, and when a death may leave the level running.
 *
 * The decisions in this module are arithmetic with a known right answer and run here with no game
 * in the process: which rule a mode and a standing team mate produce, whether the survival switch
 * may go on at all, how long the rule makes a dead player wait, whether a death that arrived
 * belongs to this machine, which far player a re-entry stands beside, what the corpse watch
 * answers, and whether a note is a new death.
 *
 * The last of those is the one that is easy to get backwards. A machine that has never been told
 * its own world slot must treat every death as somebody else's, because acting on one would put
 * THIS player back for a death he did not suffer.
 *
 * The engine-side half is checked for refusing. Nothing resolves in a test process, so the switch
 * has to stay off and nothing may reach the re-entry machinery.
 */
#include "unittest.h"

#include "mp_lobby.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A mode neither side of the handshake would accept. */
#define NOT_A_MODE 7u

static const uint8_t COOP = (uint8_t)MP_LOBBY_MODE_COOP;
static const uint8_t TDM  = (uint8_t)MP_LOBBY_MODE_TDM;

/* The last argument is whether this machine is a client, and it only ever matters in the one
 * case where nobody is standing: the host's continue screen decides the next world for everybody,
 * so a client waits as a corpse instead of ending a level of its own. */
static void check_which_rule_applies(void)
{
    ut_section("with no started session there is no rule at all");
    ut_check(mp_reentry_rule_for(false, COOP, true, false) == MP_REENTRY_RULE_NONE,
             "a co-op mode outside a session decides nothing");
    ut_check(mp_reentry_rule_for(false, TDM, true, true) == MP_REENTRY_RULE_NONE,
             "and neither does a deathmatch one");

    ut_section("a deathmatch never asks whether anybody else is standing");
    ut_check(mp_reentry_rule_for(true, TDM, true, false) == MP_REENTRY_RULE_AT_POINT,
             "with an opponent up, the dead player goes to a spawn point");
    ut_check(mp_reentry_rule_for(true, TDM, false, true) == MP_REENTRY_RULE_AT_POINT,
             "and with nobody up he still does: a round with one player is waiting for a second");

    ut_section("co-op is the opposite, because coming back beside nobody is not a place");
    ut_check(mp_reentry_rule_for(true, COOP, true, false) == MP_REENTRY_RULE_BESIDE,
             "somebody standing is somebody to come back beside");
    ut_check(mp_reentry_rule_for(true, COOP, true, true) == MP_REENTRY_RULE_BESIDE,
             "and that is the same answer on either side of the wire");

    ut_section("and with nobody standing, the host ends the level and a client waits for it");
    ut_check(mp_reentry_rule_for(true, COOP, false, false) == MP_REENTRY_RULE_LAST_MAN,
             "the host's continue screen is the one that decides the next world");
    ut_check(mp_reentry_rule_for(true, COOP, false, true) == MP_REENTRY_RULE_WAIT_FOR_HOST,
             "a client waits as a corpse rather than beginning a second campaign of its own");

    ut_section("a mode this build does not know decides nothing");
    ut_check(mp_reentry_rule_for(true, NOT_A_MODE, true, false) == MP_REENTRY_RULE_NONE,
             "an unknown game is not quietly treated as one of the two");
}

static void check_when_a_death_may_leave_the_level_running(void)
{
    ut_section("the switch follows the rule and never disagrees with it");
    ut_check(mp_reentry_may_survive(true, TDM, false, true, false),
             "a deathmatch death is survivable with the re-entry standing");
    ut_check(mp_reentry_may_survive(true, COOP, true, true, false),
             "and so is a co-op death while the team mate is up");
    ut_check(!mp_reentry_may_survive(true, COOP, false, true, false),
             "on the host the last co-op player down is not survivable: that death ends the level");
    ut_check(mp_reentry_may_survive(true, COOP, false, true, true),
             "on a client it is: the death stays inert while the host's screen decides, and a "
             "revoked switch would hand the client its own continue screen after all");
    ut_check(!mp_reentry_may_survive(true, NOT_A_MODE, true, true, false),
             "and neither is a death in a game this build does not know");

    ut_section("no session means the retail death, byte for byte");
    ut_check(!mp_reentry_may_survive(false, TDM, true, true, false),
             "a run with no session dies exactly as the shipped game does");

    ut_section("and machinery that did not install may never grant it");
    ut_check(!mp_reentry_may_survive(true, TDM, true, false, false),
             "without the machinery the switch would hold a corpse for ever, which is worse than "
             "the death it replaced");
    ut_check(!mp_reentry_may_survive(true, COOP, true, false, false),
             "in either game");
}

static void check_how_long_the_rule_makes_him_wait(void)
{
    mp_rules_t rules;

    mp_rules_default(&rules);

    ut_section("co-op waits not at all");
    ut_check(mp_reentry_wait_for(MP_REENTRY_RULE_BESIDE, &rules) == 0u,
             "the anchor is a body that keeps walking, so every substep of waiting moves it");

    ut_section("a deathmatch waits exactly what the host set");
    ut_check(mp_reentry_wait_for(MP_REENTRY_RULE_AT_POINT, &rules) ==
                 mp_rules_respawn_substeps(&rules),
             "the number in the rule set, in the substeps the rule set is written in");
    rules.respawn_tenths = 0u;
    ut_check(mp_reentry_wait_for(MP_REENTRY_RULE_AT_POINT, &rules) == 0u,
             "a host who set no wait gets none");

    ut_section("the two rules that are not a re-entry wait for nothing");
    ut_check(mp_reentry_wait_for(MP_REENTRY_RULE_NONE, &rules) == 0u,
             "no session, no wait");
    ut_check(mp_reentry_wait_for(MP_REENTRY_RULE_LAST_MAN, &rules) == 0u,
             "and the last player down is not waiting for anything either");
    ut_check(mp_reentry_wait_for(MP_REENTRY_RULE_AT_POINT, NULL) == 0u,
             "a missing rule set is a wait of nothing rather than a read of nothing");
}

static void check_the_wait_running_out(void)
{
    ut_section("a wait of nothing is over on the substep it started");
    ut_check(mp_reentry_wait_is_over(1000u, 1000u, 0u),
             "which is what makes the co-op rule immediate");

    ut_section("and one that was set runs out on its own substep, not before");
    ut_check(!mp_reentry_wait_is_over(1000u, 1159u, 160u), "one substep short is still waiting");
    ut_check(mp_reentry_wait_is_over(1000u, 1160u, 160u),
             "and the substep it names carries it out");
    ut_check(mp_reentry_wait_is_over(1000u, 5000u, 160u), "long past it, certainly");

    ut_section("a counter that has wrapped is still read right");
    ut_check(mp_reentry_wait_is_over(0xFFFFFFF0u, 0x00000010u, 32u),
             "the difference is what is compared, so the wrap costs nobody a re-entry");
    ut_check(!mp_reentry_wait_is_over(0xFFFFFFF0u, 0x00000000u, 32u),
             "and sixteen substeps short of it, just past the wrap, it is still waiting");
}

static void check_whose_death_it_is(void)
{
    ut_section("a machine that was never told its slot acts on nothing");
    ut_check(!mp_reentry_is_ours(0u, 0u, false),
             "a death naming slot 0 is not this player's while the slot is unset");
    ut_check(!mp_reentry_is_ours(1u, 1u, false), "nor one naming slot 1");

    ut_section("with the slot set, only its own death is acted on");
    ut_check(mp_reentry_is_ours(1u, 1u, true), "the victim slot this machine holds is this player");
    ut_check(!mp_reentry_is_ours(0u, 1u, true), "and the other player's death is not");
}

static void check_it_refuses_with_no_game(void)
{
    mp_death_note_t note;

    note.victim_slot = 0u;
    note.killer_slot = (uint8_t)MP_DEATH_NO_KILLER;
    note.reason      = (uint8_t)MP_DEATH_BY_HIT;

    ut_section("nothing resolves in a test process");
    ut_check(!mp_respawn_installed(),
             "the re-entry machinery is absent, which is what the switch below hangs on");

    ut_section("so a death reaches the machinery not at all");
    mp_reentry_note_no_session();
    mp_reentry_note_no_peer(0u);
    mp_reentry_note_my_slot(0u);
    mp_reentry_note_death(&note);
    ut_check(!mp_respawn_pending(), "a death outside a session is counted and dropped");
    mp_reentry_tick(0u);
    ut_check(!mp_respawn_pending(), "and a tick over it hands nothing over");
    mp_reentry_note_death(NULL);
    ut_check(!mp_respawn_pending(), "a null note is answered rather than dereferenced");
}

/* Which far player a co-op re-entry stands beside, out of all of them. Only the first far bank was
 * asked, so a client whose host was down counted as the last one standing while a second client
 * stood a step away, and the level ended for everybody. */
static void check_who_to_come_back_beside(void)
{
    static const float DIED_AT[3] = { 100.0f, 0.0f, 0.0f };
    mp_reentry_peer_t  peers[MP_REENTRY_MAX_PEERS];

    memset(peers, 0, sizeof peers);

    ut_section("nobody resolved, or nobody standing, is nobody to come back beside");
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, NULL) == -1,
             "no pose for anybody");
    peers[0].known = true;
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, NULL) == -1,
             "a host resolved and down: in co-op this player is the last one");
    ut_check(mp_reentry_pick_anchor(NULL, MP_REENTRY_MAX_PEERS, NULL) == -1, "and no list");

    ut_section("any far player standing is somebody, not only the first");
    peers[2].known       = true;
    peers[2].alive       = true;
    peers[2].position[0] = 400.0f;
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, NULL) == 2,
             "the host down and the third bank's player up: that one, and the level goes on");
    peers[1].alive       = true;   /* standing, but never resolved here */
    peers[1].position[0] = 90.0f;
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, DIED_AT) == 2,
             "a player this machine has no pose for is nobody, however near the wire says");

    ut_section("of several standing, the one nearest to where this player died");
    peers[1].known = true;
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, DIED_AT) == 1,
             "ten units away beats three hundred");
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, NULL) == 1,
             "and with no place of death known, the first one standing in bank order");
    peers[0].alive = true;
    ut_check(mp_reentry_pick_anchor(peers, MP_REENTRY_MAX_PEERS, NULL) == 0,
             "which is the host's on a client once it is up again");
    ut_check(mp_reentry_pick_anchor(peers, 1u, DIED_AT) == 0,
             "and the count bounds the list");
}

/* The net under a corpse nothing is bringing back.
 *
 * The state is a rule rather than a flag in the watch because of the defect it is: the watch had
 * ONE latch for everything it says, so a corpse that was waiting for a wish at the moment the
 * patience ran out was described once and never looked at again. When the wish was given up
 * twenty seconds later, the net that would have let the player out was already mute, and the
 * player sat in a level with no pause menu and no way out of it (field run 2026-09-17, team
 * deathmatch on bridge.b3d). So WAITING and STUCK are two answers to the same facts minus the
 * wish, and each carries its own latch at the caller.
 */
static void check_what_the_corpse_watch_answers(void)
{
    const uint32_t PAST   = MP_REENTRY_CORPSE_PATIENCE_FRAMES;
    const uint32_t INSIDE = MP_REENTRY_CORPSE_PATIENCE_FRAMES - 1u;

    ut_section("a corpse with a way back being tried is waiting, and without one it is stuck");

    ut_check(mp_reentry_corpse_state(true, PAST, true, false, true, true) ==
                 MP_REENTRY_CORPSE_WAITING,
             "a wish is being tried: say so, and let it be tried");
    ut_check(mp_reentry_corpse_state(true, PAST, false, false, true, true) ==
                 MP_REENTRY_CORPSE_STUCK,
             "the same corpse once the wish is gone is stuck: the answer does not depend on what "
             "was said while the wish was still there");
    ut_check(mp_reentry_corpse_state(true, PAST, true, true, true, true) ==
                 MP_REENTRY_CORPSE_HOST,
             "waiting for the host to pick the next world is its own state, not a defect");

    ut_section("and nothing is said or done before there is a reason");

    ut_check(mp_reentry_corpse_state(false, PAST, false, false, true, true) ==
                 MP_REENTRY_CORPSE_NO,
             "a living player is no corpse");
    ut_check(mp_reentry_corpse_state(true, INSIDE, false, false, true, true) ==
                 MP_REENTRY_CORPSE_NO,
             "and a corpse inside the patience is a death the engine is still playing out");
    ut_check(mp_reentry_corpse_state(true, PAST, false, false, false, true) ==
                 MP_REENTRY_CORPSE_NO,
             "with no session there is nothing to be stuck in");
    ut_check(mp_reentry_corpse_state(true, PAST, false, false, true, false) ==
                 MP_REENTRY_CORPSE_NO,
             "and a level the engine has ended itself is already showing a screen to leave by");
}

/* The field run: thirty five notes of one death before the first fade was over, then two of the
 * next body's, then four more. The notes are counted as the rule set counts them; before the life
 * count every one of them was a wish, and each wish replaced the one before. */
static void check_one_wish_per_life(void)
{
    uint32_t lives = 1u;
    bool     down_known = false;
    uint32_t down_life = 0u;
    unsigned wishes = 0u;
    unsigned old_wishes = 0u;
    unsigned note;

    ut_section("one wish per life, however many notes of the death arrive");
    for (note = 0u; note < 35u; ++note) {
        old_wishes += 1u;
        if (mp_reentry_death_is_new(true, lives, down_known, down_life)) {
            ++wishes;
            down_known = true;
            down_life  = lives;
        }
    }
    ut_checkf(wishes == 1u, "thirty five notes of one life make one wish (%u), where each of "
              "them made one before (%u)", wishes, old_wishes);

    lives = 2u;   /* the next body stood, and died */
    ut_check(mp_reentry_death_is_new(true, lives, down_known, down_life),
             "the death of the next life is a new wish, even one before the landing was seen");
    ut_check(!mp_reentry_death_is_new(true, 1u, true, 1u) &&
                 mp_reentry_death_is_new(false, 1u, true, 1u),
             "without the death hull's count every note is a death, as it was");
    ut_check(mp_reentry_death_is_new(true, 0u, false, 0u),
             "and the first note ever is one");
}

int main(void)
{
    check_which_rule_applies();
    check_when_a_death_may_leave_the_level_running();
    check_how_long_the_rule_makes_him_wait();
    check_the_wait_running_out();
    check_whose_death_it_is();
    check_it_refuses_with_no_game();
    check_who_to_come_back_beside();
    check_what_the_corpse_watch_answers();
    check_one_wish_per_life();

    return ut_summary("mp_reentry");
}
