/* Which player a scene's script meant: the rule a host measures a scene's trigger by.
 *
 * From the actor's own last answer about a player while it is fresh and a player's, from its last
 * attacker if it died, and otherwise the host as the anchor. Never an ally, and never whoever
 * happens to stand nearest to the machine that runs the script. Every combination is run against
 * the rule written out a second time, and the rule is held against the old silence, which gave
 * every scene to the host.
 */
#include "unittest.h"

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a host could say before the rule existed: nothing asked, so every scene was the local
 * player's, which on a host is bank 0. */
static mp_scene_trigger_rule_t the_old_answer(const mp_scene_evidence_t *evidence, uint8_t *bank)
{
    (void)evidence;
    *bank = 0u;
    return MP_SCENE_BY_HOST_ANCHOR;
}

static mp_scene_evidence_t evidence_of(mp_scene_answer_t own, uint8_t own_bank, bool died,
                                       bool attacker_known, uint8_t attacker_bank)
{
    mp_scene_evidence_t evidence;

    evidence.own            = own;
    evidence.own_bank       = own_bank;
    evidence.died           = died;
    evidence.attacker_known = attacker_known;
    evidence.attacker_bank  = attacker_bank;
    return evidence;
}

static void check_the_answer_ages(void)
{
    ut_section("an actor's last answer about a player, by its age");

    ut_check(mp_scene_answer_of(false, true, 0u) == MP_SCENE_ANSWER_NONE,
             "no answer on record is none, whatever the other two say");
    ut_check(mp_scene_answer_of(true, true, 0u) == MP_SCENE_ANSWER_PLAYER,
             "a player answered in this very substep is a player");
    ut_check(mp_scene_answer_of(true, true, MP_SCENE_OWN_ANSWER_SUBSTEPS) ==
                 MP_SCENE_ANSWER_PLAYER,
             "and one exactly a second old still is");
    ut_check(mp_scene_answer_of(true, true, MP_SCENE_OWN_ANSWER_SUBSTEPS + 1u) ==
                 MP_SCENE_ANSWER_STALE,
             "one substep more and it is stale");
    ut_check(mp_scene_answer_of(true, false, 3u) == MP_SCENE_ANSWER_NOT_A_PLAYER,
             "a fresh answer that was an ally or nobody names no player");
    ut_check(mp_scene_answer_of(true, false, MP_SCENE_OWN_ANSWER_SUBSTEPS + 1u) ==
                 MP_SCENE_ANSWER_STALE,
             "and an old one of those is stale like any other");
    ut_check(mp_scene_answer_of(true, true, 0u - 5u) == MP_SCENE_ANSWER_STALE,
             "an answer stamped after now, which a restarted counter leaves, is stale, not fresh");
}

static void check_the_trigger_rules(void)
{
    mp_scene_evidence_t evidence;
    uint8_t             bank     = 0xFFu;
    uint8_t             old_bank = 0xFFu;

    ut_section("the rule a scene is attributed by");

    evidence = evidence_of(MP_SCENE_ANSWER_PLAYER, 2u, false, false, 0u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_OWN_ANSWER && bank == 2u,
             "a fresh answer of the actor's own names its player, a far one here");
    evidence = evidence_of(MP_SCENE_ANSWER_PLAYER, 0u, true, true, 3u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_OWN_ANSWER && bank == 0u,
             "and it comes first, even for an actor that died by another player's hand");
    evidence = evidence_of(MP_SCENE_ANSWER_STALE, 2u, true, true, 1u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_LAST_ATTACKER && bank == 1u,
             "without one, an actor that died was meant for the player who hurt it last");
    evidence = evidence_of(MP_SCENE_ANSWER_NOT_A_PLAYER, 2u, true, true, 3u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_LAST_ATTACKER && bank == 3u,
             "an ally's answer is no player's, so a death still goes to its attacker");
    evidence = evidence_of(MP_SCENE_ANSWER_NONE, 0u, false, true, 2u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_HOST_ANCHOR && bank == 0u,
             "an attacker alone names nobody while the actor lives: the scene is the host's");
    evidence = evidence_of(MP_SCENE_ANSWER_NONE, 0u, true, false, 0u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_HOST_ANCHOR && bank == 0u,
             "a death with no player behind it is anchored on the host");
    evidence = evidence_of(MP_SCENE_ANSWER_NOT_A_PLAYER, 1u, false, false, 0u);
    ut_check(mp_scene_trigger(&evidence, &bank) == MP_SCENE_BY_HOST_ANCHOR && bank == 0u,
             "and so is a scene whose actor last heard of an ally, never the bank that ally sits "
             "near");
    ut_check(mp_scene_trigger(NULL, &bank) == MP_SCENE_BY_HOST_ANCHOR && bank == 0u,
             "no evidence at all is the host's scene");
    evidence = evidence_of(MP_SCENE_ANSWER_PLAYER, 2u, false, false, 0u);
    ut_check(mp_scene_trigger(&evidence, NULL) == MP_SCENE_BY_OWN_ANSWER,
             "the rule is still answered with nowhere to put the bank");

    evidence = evidence_of(MP_SCENE_ANSWER_PLAYER, 2u, false, false, 0u);
    (void)the_old_answer(&evidence, &old_bank);
    (void)mp_scene_trigger(&evidence, &bank);
    ut_check(old_bank == 0u && bank == 2u,
             "a far client's fresh answer is where the rule parts from the old silence, which "
             "gave every scene to the host");
}

/* Every combination, held to the three claims the rule makes: the actor's own answer decides only
 * when it is a player's, the attacker only for a death and only without one, and everything else
 * is the host's with bank 0. */
static void check_every_combination(void)
{
    unsigned answer;
    unsigned bits;
    unsigned bank_own;
    unsigned bank_attacker;
    unsigned wrong = 0u;
    unsigned cases = 0u;

    ut_section("every combination of what an actor can tell");

    for (answer = (unsigned)MP_SCENE_ANSWER_NONE; answer <= (unsigned)MP_SCENE_ANSWER_PLAYER;
         ++answer) {
        mp_scene_answer_t own = (mp_scene_answer_t)answer;

        for (bits = 0u; bits < 4u; ++bits) {
            for (bank_own = 0u; bank_own < 4u; ++bank_own) {
                for (bank_attacker = 0u; bank_attacker < 4u; ++bank_attacker) {
                    bool                    died     = (bits & 1u) != 0u;
                    bool                    attacker = (bits & 2u) != 0u;
                    mp_scene_evidence_t     evidence = evidence_of(own, (uint8_t)bank_own, died,
                                                                   attacker,
                                                                   (uint8_t)bank_attacker);
                    uint8_t                 bank = 0xFFu;
                    mp_scene_trigger_rule_t rule = mp_scene_trigger(&evidence, &bank);
                    mp_scene_trigger_rule_t want;
                    uint8_t                 want_bank;

                    if (own == MP_SCENE_ANSWER_PLAYER) {
                        want      = MP_SCENE_BY_OWN_ANSWER;
                        want_bank = (uint8_t)bank_own;
                    } else if (died && attacker) {
                        want      = MP_SCENE_BY_LAST_ATTACKER;
                        want_bank = (uint8_t)bank_attacker;
                    } else {
                        want      = MP_SCENE_BY_HOST_ANCHOR;
                        want_bank = 0u;
                    }
                    ++cases;
                    wrong += (rule != want || bank != want_bank) ? 1u : 0u;
                }
            }
        }
    }
    ut_checkf(wrong == 0u, "all %u combinations name the rule and the bank they should (%u wrong)",
              cases, wrong);
}

int main(void)
{
    check_the_answer_ages();
    check_the_trigger_rules();
    check_every_combination();

    return ut_summary("the scene trigger");
}
