/* mp_rules.c: the rule set of a round. See mp_rules.h. */
#include "mp_rules.h"

#include "mp_clock.h"
#include "mp_lobby.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two durations are converted against this module's own constant, and that constant is the
 * clock module's. Keeping the number in one place is not enough on its own, because a second
 * spelling of it is exactly the kind of thing that gets introduced and then diverges. */
_Static_assert(MP_RULES_SUBSTEPS_PER_SECOND == MP_CLOCK_TICK_RATE,
               "the rule set measures rounds in the clock's own substeps");

/* An hour of substeps, the largest value either conversion can produce. Checked here so that the
 * ceilings in the header are not merely documentation. */
_Static_assert((uint32_t)MP_RULES_TIME_LIMIT_MAX * MP_RULES_SUBSTEPS_PER_SECOND == 115200u,
               "the longest round fits an unsigned long with room to spare");

/* The penalties travel as their distance above the floor, so nothing on the wire depends on how a
 * compiler converts a byte above 127 into a signed char, and the range check is one comparison in
 * both directions. */
#define PENALTY_SPAN ((uint8_t)(MP_RULES_PENALTY_MAX - MP_RULES_PENALTY_MIN))

static uint16_t clamp_u16(uint32_t value, uint16_t ceiling)
{
    return value > (uint32_t)ceiling ? ceiling : (uint16_t)value;
}

void mp_rules_default(mp_rules_t *rules)
{
    if (rules == NULL) {
        return;
    }
    rules->score_limit     = (uint16_t)MP_RULES_SCORE_LIMIT_DEFAULT;
    rules->time_limit_s    = (uint16_t)MP_RULES_TIME_LIMIT_DEFAULT;
    rules->flags           = (uint8_t)MP_RULES_FLAGS_DEFAULT;
    rules->team_penalty    = (int8_t)MP_RULES_TEAM_PENALTY_DEFAULT;
    rules->suicide_penalty = (int8_t)MP_RULES_SUICIDE_PENALTY_DEFAULT;
    rules->respawn_tenths  = (uint8_t)MP_RULES_RESPAWN_TENTHS_DEFAULT;
    rules->reserved        = 0u;
}

bool mp_rules_valid(const mp_rules_t *rules)
{
    if (rules == NULL) {
        return false;
    }
    if (rules->score_limit > MP_RULES_SCORE_LIMIT_MAX) {
        return false;
    }
    if (rules->time_limit_s > MP_RULES_TIME_LIMIT_MAX) {
        return false;
    }
    if ((rules->flags & ~(uint8_t)MP_RULES_F_KNOWN) != 0u) {
        return false;
    }
    if (rules->team_penalty < MP_RULES_PENALTY_MIN ||
        rules->team_penalty > MP_RULES_PENALTY_MAX) {
        return false;
    }
    if (rules->suicide_penalty < MP_RULES_PENALTY_MIN ||
        rules->suicide_penalty > MP_RULES_PENALTY_MAX) {
        return false;
    }
    if (rules->respawn_tenths > MP_RULES_RESPAWN_TENTHS_MAX) {
        return false;
    }
    /* The held back byte is refused rather than ignored. A build that ignored it could never be
     * given a meaning for it later without talking past every build that had. */
    return rules->reserved == 0u;
}

bool mp_rules_clamp(mp_rules_t *rules)
{
    mp_rules_t before;

    if (rules == NULL) {
        return false;
    }
    before = *rules;

    rules->score_limit  = clamp_u16(rules->score_limit, (uint16_t)MP_RULES_SCORE_LIMIT_MAX);
    rules->time_limit_s = clamp_u16(rules->time_limit_s, (uint16_t)MP_RULES_TIME_LIMIT_MAX);
    rules->flags       &= (uint8_t)MP_RULES_F_KNOWN;
    if (rules->team_penalty < MP_RULES_PENALTY_MIN) {
        rules->team_penalty = (int8_t)MP_RULES_PENALTY_MIN;
    }
    if (rules->team_penalty > MP_RULES_PENALTY_MAX) {
        rules->team_penalty = (int8_t)MP_RULES_PENALTY_MAX;
    }
    if (rules->suicide_penalty < MP_RULES_PENALTY_MIN) {
        rules->suicide_penalty = (int8_t)MP_RULES_PENALTY_MIN;
    }
    if (rules->suicide_penalty > MP_RULES_PENALTY_MAX) {
        rules->suicide_penalty = (int8_t)MP_RULES_PENALTY_MAX;
    }
    if (rules->respawn_tenths > MP_RULES_RESPAWN_TENTHS_MAX) {
        rules->respawn_tenths = (uint8_t)MP_RULES_RESPAWN_TENTHS_MAX;
    }
    rules->reserved = 0u;

    return !mp_rules_equal(&before, rules);
}

bool mp_rules_equal(const mp_rules_t *a, const mp_rules_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return a->score_limit == b->score_limit && a->time_limit_s == b->time_limit_s &&
           a->flags == b->flags && a->team_penalty == b->team_penalty &&
           a->suicide_penalty == b->suicide_penalty &&
           a->respawn_tenths == b->respawn_tenths && a->reserved == b->reserved;
}

bool mp_rules_teams(const mp_rules_t *rules)
{
    return rules != NULL && (rules->flags & (uint8_t)MP_RULES_F_TEAMS) != 0u;
}

bool mp_rules_friendly_fire(const mp_rules_t *rules)
{
    return rules != NULL && (rules->flags & (uint8_t)MP_RULES_F_FRIENDLY_FIRE) != 0u;
}

uint8_t mp_rules_effective_team(const mp_rules_t *rules, uint8_t team)
{
    if (!mp_rules_teams(rules)) {
        return (uint8_t)MP_LOBBY_TEAM_NONE;
    }
    if (team > MP_LOBBY_TEAM_MAX) {
        return (uint8_t)MP_LOBBY_TEAM_NONE;
    }
    return team;
}

bool mp_rules_may_damage(const mp_rules_t *rules, uint8_t mode, uint8_t attacker_team,
                         uint8_t victim_team)
{
    if (rules == NULL) {
        return false;
    }
    return mp_lobby_may_damage(mode, mp_rules_effective_team(rules, attacker_team),
                               mp_rules_effective_team(rules, victim_team),
                               mp_rules_friendly_fire(rules));
}

uint32_t mp_rules_seconds_to_substeps(uint32_t seconds)
{
    if (seconds > MP_RULES_TIME_LIMIT_MAX) {
        seconds = MP_RULES_TIME_LIMIT_MAX;
    }
    return seconds * MP_RULES_SUBSTEPS_PER_SECOND;
}

uint32_t mp_rules_tenths_to_substeps(uint32_t tenths)
{
    if (tenths > MP_RULES_RESPAWN_TENTHS_MAX) {
        tenths = MP_RULES_RESPAWN_TENTHS_MAX;
    }
    /* A tenth of a second is 3.2 substeps, so the division rounds to the nearest one rather than
     * down: five seconds has to come back as 160 and not as 159. */
    return (tenths * MP_RULES_SUBSTEPS_PER_SECOND + 5u) / 10u;
}

uint32_t mp_rules_time_limit_substeps(const mp_rules_t *rules)
{
    if (rules == NULL) {
        return 0u;
    }
    return mp_rules_seconds_to_substeps(rules->time_limit_s);
}

uint32_t mp_rules_respawn_substeps(const mp_rules_t *rules)
{
    if (rules == NULL) {
        return 0u;
    }
    return mp_rules_tenths_to_substeps(rules->respawn_tenths);
}

bool mp_rules_put(mp_wire_writer_t *writer, const mp_rules_t *rules)
{
    if (writer == NULL || !mp_rules_valid(rules)) {
        return false;
    }
    mp_wire_put_u16(writer, rules->score_limit);
    mp_wire_put_u16(writer, rules->time_limit_s);
    mp_wire_put_u8(writer, rules->flags);
    mp_wire_put_u8(writer, (uint8_t)(rules->team_penalty - MP_RULES_PENALTY_MIN));
    mp_wire_put_u8(writer, (uint8_t)(rules->suicide_penalty - MP_RULES_PENALTY_MIN));
    mp_wire_put_u8(writer, rules->respawn_tenths);
    mp_wire_put_u8(writer, rules->reserved);
    return !writer->overflowed;
}

bool mp_rules_get(mp_wire_reader_t *reader, mp_rules_t *out)
{
    uint8_t team_penalty = 0;
    uint8_t suicide_penalty = 0;

    if (reader == NULL || out == NULL) {
        return false;
    }
    mp_wire_get_u16(reader, &out->score_limit);
    mp_wire_get_u16(reader, &out->time_limit_s);
    mp_wire_get_u8(reader, &out->flags);
    mp_wire_get_u8(reader, &team_penalty);
    mp_wire_get_u8(reader, &suicide_penalty);
    mp_wire_get_u8(reader, &out->respawn_tenths);
    mp_wire_get_u8(reader, &out->reserved);
    if (reader->overran) {
        return false;
    }
    /* The bias is undone before the range check, and a byte outside the span is refused here
     * rather than turned into a penalty nobody chose. */
    if (team_penalty > PENALTY_SPAN || suicide_penalty > PENALTY_SPAN) {
        return false;
    }
    out->team_penalty    = (int8_t)((int)team_penalty + MP_RULES_PENALTY_MIN);
    out->suicide_penalty = (int8_t)((int)suicide_penalty + MP_RULES_PENALTY_MIN);
    return mp_rules_valid(out);
}
