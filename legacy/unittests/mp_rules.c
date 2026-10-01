/* The rule set of a round, driven with no wire, no file and no game.
 *
 * Every field of it decides something a player cannot take back, so every field is checked at its
 * floor, at its ceiling, one past each, and through a round trip. The two that are not plain
 * numbers get more than that: the flag byte, because an unknown bit there decides who may be
 * hurt, and the held back byte, because a build that ignored it could never be given a meaning
 * for it later without talking past every build that had.
 */
#include "unittest.h"

#include "mp_lobby.h"
#include "mp_rules.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool round_trip(const mp_rules_t *rules, mp_rules_t *back)
{
    uint8_t          bytes[MP_RULES_BYTES];
    mp_wire_writer_t w;
    mp_wire_reader_t r;

    mp_wire_writer_init(&w, bytes, sizeof bytes);
    if (!mp_rules_put(&w, rules) || w.at != MP_RULES_BYTES) {
        return false;
    }
    mp_wire_reader_init(&r, bytes, sizeof bytes);
    return mp_rules_get(&r, back);
}

static void check_the_defaults(void)
{
    mp_rules_t rules;

    ut_section("what a fresh session plays");

    mp_rules_default(&rules);
    ut_check(rules.score_limit == 25u, "twenty five points win it");
    ut_check(rules.time_limit_s == 900u, "fifteen minutes end it");
    ut_check(mp_rules_teams(&rules), "teams are on, because a team deathmatch is what this is for");
    ut_check(!mp_rules_friendly_fire(&rules),
             "and friendly fire is off, because the alternative is two players who cannot tell "
             "why they keep dying");
    ut_check(rules.team_penalty == -1 && rules.suicide_penalty == -1,
             "a betrayal and a suicide each cost a point");
    ut_check(rules.respawn_tenths == 50u, "a dead player waits five seconds");
    ut_check(rules.reserved == 0u, "and the held back byte is held back");
    ut_check(mp_rules_valid(&rules), "the defaults are inside every range this build accepts");
    ut_check(!mp_rules_clamp(&rules), "so clamping them moves nothing");
}

static void check_every_range(void)
{
    mp_rules_t rules;

    ut_section("each field at its floor, at its ceiling, and one past");

    mp_rules_default(&rules);
    rules.score_limit = 0u;
    ut_check(mp_rules_valid(&rules), "no points limit at all is a setting, not a fault");
    rules.score_limit = (uint16_t)MP_RULES_SCORE_LIMIT_MAX;
    ut_check(mp_rules_valid(&rules), "and so is the largest one");
    rules.score_limit = (uint16_t)(MP_RULES_SCORE_LIMIT_MAX + 1u);
    ut_check(!mp_rules_valid(&rules), "one past it is not");
    ut_check(mp_rules_clamp(&rules) && rules.score_limit == MP_RULES_SCORE_LIMIT_MAX,
             "clamping brings it back to the ceiling and says that it had to");

    mp_rules_default(&rules);
    rules.time_limit_s = 0u;
    ut_check(mp_rules_valid(&rules), "no time limit is a setting too");
    rules.time_limit_s = (uint16_t)MP_RULES_TIME_LIMIT_MAX;
    ut_check(mp_rules_valid(&rules), "an hour is the longest round");
    rules.time_limit_s = (uint16_t)(MP_RULES_TIME_LIMIT_MAX + 1u);
    ut_check(!mp_rules_valid(&rules), "and a second past the hour is refused");

    mp_rules_default(&rules);
    rules.team_penalty = (int8_t)MP_RULES_PENALTY_MIN;
    ut_check(mp_rules_valid(&rules), "ten points off is the harshest betrayal penalty");
    rules.team_penalty = (int8_t)(MP_RULES_PENALTY_MIN - 1);
    ut_check(!mp_rules_valid(&rules), "eleven is not");
    rules.team_penalty = 1;
    ut_check(!mp_rules_valid(&rules),
             "and a penalty may not be positive: that would pay a player for shooting their own "
             "team, and off the wire it would look exactly like a rule somebody meant");
    ut_check(mp_rules_clamp(&rules) && rules.team_penalty == 0, "clamping takes it back to none");

    mp_rules_default(&rules);
    rules.suicide_penalty = 3;
    ut_check(!mp_rules_valid(&rules), "the same holds for the suicide penalty");

    mp_rules_default(&rules);
    rules.respawn_tenths = 0u;
    ut_check(mp_rules_valid(&rules), "getting back up at once is a setting");
    rules.respawn_tenths = (uint8_t)MP_RULES_RESPAWN_TENTHS_MAX;
    ut_check(mp_rules_valid(&rules), "fifteen seconds is the longest wait");
    rules.respawn_tenths = (uint8_t)(MP_RULES_RESPAWN_TENTHS_MAX + 1u);
    ut_check(!mp_rules_valid(&rules), "and one tenth past it is refused");

    ut_check(!mp_rules_valid(NULL), "nothing is not a rule set");
    ut_check(!mp_rules_clamp(NULL), "and nothing cannot be clamped");
}

static void check_the_flag_byte(void)
{
    mp_rules_t rules;
    mp_rules_t back;

    ut_section("the flag byte, where an unknown bit decides who may be hurt");

    mp_rules_default(&rules);
    rules.flags = 0u;
    ut_check(mp_rules_valid(&rules) && !mp_rules_teams(&rules) &&
                 !mp_rules_friendly_fire(&rules),
             "no bits at all is a free for all with no friendly fire");
    rules.flags = (uint8_t)MP_RULES_F_KNOWN;
    ut_check(mp_rules_valid(&rules) && mp_rules_teams(&rules) && mp_rules_friendly_fire(&rules),
             "both bits set is teams with friendly fire");
    rules.flags = 0x80u;
    ut_check(!mp_rules_valid(&rules), "a bit this build does not know is refused, not ignored");
    ut_check(!round_trip(&rules, &back), "so it never goes out either");
    ut_check(mp_rules_clamp(&rules) && rules.flags == 0u, "clamping drops it");

    ut_section("the held back byte");

    mp_rules_default(&rules);
    rules.reserved = 1u;
    ut_check(!mp_rules_valid(&rules),
             "a build that ignored it could never be given a meaning for it later without "
             "talking past every build that had");
    ut_check(!round_trip(&rules, &back), "and it is refused in both directions");
}

static void check_the_round_trip(void)
{
    mp_rules_t rules;
    mp_rules_t back;
    uint8_t    bytes[MP_RULES_BYTES];
    uint8_t    short_buffer[MP_RULES_BYTES - 1u];

    mp_wire_writer_t w;
    mp_wire_reader_t r;

    ut_section("nine bytes, out and back");

    mp_rules_default(&rules);
    rules.score_limit     = 7u;
    rules.time_limit_s    = 61u;
    rules.flags           = (uint8_t)MP_RULES_F_FRIENDLY_FIRE;
    rules.team_penalty    = -10;
    rules.suicide_penalty = 0;
    rules.respawn_tenths  = 3u;
    ut_check(round_trip(&rules, &back), "encoded and decoded");
    ut_check(mp_rules_equal(&rules, &back), "field for field, as sent");
    ut_check(back.team_penalty == -10 && back.suicide_penalty == 0,
             "the penalties survive as the negative numbers they are");

    ut_section("what the nine bytes refuse");

    mp_wire_writer_init(&w, short_buffer, sizeof short_buffer);
    ut_check(!mp_rules_put(&w, &rules), "a buffer one byte short of the field");

    mp_wire_writer_init(&w, bytes, sizeof bytes);
    ut_check(mp_rules_put(&w, &rules), "a sound set exists to forge from");
    bytes[5] = 200u;   /* the biased betrayal penalty, past the span it is allowed */
    mp_wire_reader_init(&r, bytes, sizeof bytes);
    ut_check(!mp_rules_get(&r, &back),
             "a penalty byte outside the span is refused rather than turned into a number "
             "nobody chose");

    mp_wire_writer_init(&w, bytes, sizeof bytes);
    ut_check(mp_rules_put(&w, &rules), "and another");
    bytes[0] = 0xFFu;   /* the low half of the points limit */
    bytes[1] = 0xFFu;
    mp_wire_reader_init(&r, bytes, sizeof bytes);
    ut_check(!mp_rules_get(&r, &back), "a points limit past the ceiling does not decode");

    mp_wire_reader_init(&r, bytes, MP_RULES_BYTES - 1u);
    ut_check(!mp_rules_get(&r, &back), "and neither does a short read");
    ut_check(!mp_rules_put(NULL, &rules) && !mp_rules_get(NULL, &back), "nor does nothing");
}

static void check_the_clock(void)
{
    mp_rules_t rules;

    ut_section("seconds and tenths become substeps, because a round is measured in substeps");

    mp_rules_default(&rules);
    ut_check(mp_rules_time_limit_substeps(&rules) == 900u * 32u,
             "fifteen minutes is 28800 substeps");
    ut_check(mp_rules_respawn_substeps(&rules) == 160u, "five seconds is 160 of them");

    ut_check(mp_rules_seconds_to_substeps(0u) == 0u, "no limit converts to no limit");
    ut_check(mp_rules_seconds_to_substeps(1u) == 32u, "a second is the ladder's own rate");
    ut_check(mp_rules_seconds_to_substeps(MP_RULES_TIME_LIMIT_MAX) == 115200u,
             "and the longest round fits a long with room to spare");
    ut_check(mp_rules_seconds_to_substeps(0xFFFFFFFFu) == 115200u,
             "a value past the ceiling is clamped rather than allowed to wrap");

    ut_check(mp_rules_tenths_to_substeps(0u) == 0u, "no wait is no substeps");
    ut_check(mp_rules_tenths_to_substeps(10u) == 32u, "ten tenths is one second");
    ut_check(mp_rules_tenths_to_substeps(1u) == 3u,
             "a tenth is 3.2 substeps and rounds to the nearest, not down");
    ut_check(mp_rules_tenths_to_substeps(0xFFFFFFFFu) == mp_rules_tenths_to_substeps(150u),
             "and the wait is clamped for the same reason");

    rules.time_limit_s   = 0u;
    rules.respawn_tenths = 0u;
    ut_check(mp_rules_time_limit_substeps(&rules) == 0u && mp_rules_respawn_substeps(&rules) == 0u,
             "no limit and no wait stay nothing");
    ut_check(mp_rules_time_limit_substeps(NULL) == 0u && mp_rules_respawn_substeps(NULL) == 0u,
             "and nothing answers nothing rather than reading it");
}

/* The one consumer of the team byte, fed by the rule set. The team byte and the rule set have to
 * be one rule and not two, so these cases check that the teams switch decides the rule itself and
 * adds no second rule beside it. */
static void check_who_may_hurt_whom(void)
{
    const uint8_t coop = (uint8_t)MP_LOBBY_MODE_COOP;
    const uint8_t tdm  = (uint8_t)MP_LOBBY_MODE_TDM;
    const uint8_t none = (uint8_t)MP_LOBBY_TEAM_NONE;
    mp_rules_t    rules;

    ut_section("teams off is the free for all, and needs no second rule");

    mp_rules_default(&rules);
    rules.flags = 0u;   /* teams off, friendly fire off */
    ut_check(mp_rules_effective_team(&rules, 1u) == none,
             "with teams off every player is treated as having none");
    ut_check(mp_rules_may_damage(&rules, tdm, 1u, 1u),
             "so two players who both picked team one still fight");
    ut_check(mp_rules_may_damage(&rules, tdm, 1u, 2u), "and so do two who picked different ones");
    ut_check(!mp_rules_may_damage(&rules, coop, 1u, 2u),
             "co-op is still co-op: two players on the same campaign are one side");

    ut_section("teams on is the rule the team byte was carried for");

    rules.flags = (uint8_t)MP_RULES_F_TEAMS;
    ut_check(mp_rules_effective_team(&rules, 2u) == 2u, "a numbered team is itself");
    ut_check(mp_rules_effective_team(&rules, 200u) == none,
             "and a number this build does not know is nobody's side rather than a new one");
    ut_check(!mp_rules_may_damage(&rules, tdm, 1u, 1u), "the same team does not fight");
    ut_check(mp_rules_may_damage(&rules, tdm, 1u, 2u), "opposite teams do");
    ut_check(mp_rules_may_damage(&rules, tdm, none, 1u),
             "and somebody who took no team may be hit by one that did");

    ut_section("friendly fire outranks the teams, in both games");

    rules.flags = (uint8_t)(MP_RULES_F_TEAMS | MP_RULES_F_FRIENDLY_FIRE);
    ut_check(mp_rules_may_damage(&rules, tdm, 1u, 1u), "the same team fights with it on");
    ut_check(mp_rules_may_damage(&rules, coop, none, none), "and so does a co-op pair");

    ut_check(!mp_rules_may_damage(NULL, tdm, 1u, 2u),
             "no rule set is not a licence to hurt anybody");
    ut_check(!mp_rules_may_damage(&rules, 0u, 1u, 2u), "and neither is a game that is neither");
}

int main(void)
{
    check_the_defaults();
    check_every_range();
    check_the_flag_byte();
    check_the_round_trip();
    check_the_clock();
    check_who_may_hurt_whom();
    return ut_summary("mp_rules");
}
