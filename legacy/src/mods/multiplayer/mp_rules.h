/* mp_rules.h: the rule set of a round, as a value with no wire, no file and no engine in it.
 *
 * Layer 1, pure. Seven numbers decide what a deathmatch is: how many points end it, how long it
 * may run, whether there are teams at all, whether friendly fire counts, what killing your own
 * side and killing yourself cost, and how long a dead player waits. Nothing here is a mechanism;
 * every one of them is a number somebody set in a menu, and the point of keeping them in their
 * own module is that all of the awkward parts, the range of each field, what a stranger may send,
 * and the conversion into the simulation's own clock, can be driven in a test.
 *
 * The rule set belongs to the session, not to the machine. It therefore travels in the host's
 * setup note, which is repeated once a second, so a player who joins late learns the rules from
 * the next repeat rather than from a history nobody keeps. The nine bytes it weighs are written
 * and read here, by mp_rules_put and mp_rules_get, so that the encoder and the decoder of a field
 * sit beside each other and a range check cannot be added to one direction only.
 *
 * Teams on and teams off are one rule, not two. Teams off is the case where every player's team
 * is none, which is exactly the free for all the damage rule already answered before this module
 * existed. So this module does not carry a second damage rule; it maps the switch onto the team
 * a player is treated as having and asks the one that is already there.
 *
 * The time limit and the respawn delay become substeps, not milliseconds. The simulation runs a
 * fixed ladder of thirty two substeps a second, and under load simulated time falls behind the
 * wall clock and never ahead. A round timed against the wall clock would therefore end at a
 * different point in the simulation on every machine, and two machines have two starting points
 * for their wall clocks anyway. Seconds are what a player types; substeps are what a round is
 * measured in.
 */
#ifndef MULTIPLAYER_MP_RULES_H
#define MULTIPLAYER_MP_RULES_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the rule set weighs on the wire: two shorts, a flag byte, two penalties, the respawn
 * delay and one byte held back. */
#define MP_RULES_BYTES 9u

/* Points to win. Zero means the round is not ended by points, which is the only sensible reading
 * of "no limit" and is also what the field holds before anybody has chosen. The ceiling is three
 * digits because the score it is compared against is a signed short that penalties may drive
 * below zero, and because a menu column is three digits wide. */
#define MP_RULES_SCORE_LIMIT_DEFAULT 25u
#define MP_RULES_SCORE_LIMIT_MAX     999u

/* How long a round may run, in seconds; zero means it is not ended by time. The ceiling is an
 * hour, which is also what keeps the conversion below honest: an hour of substeps is 115200 and
 * fits an unsigned long with room to spare, so the multiplication can never wrap. */
#define MP_RULES_TIME_LIMIT_DEFAULT 900u
#define MP_RULES_TIME_LIMIT_MAX     3600u

/* The flag byte. Teams on is the default because a team deathmatch is what this was built for;
 * friendly fire off is the default because the alternative is two players who cannot tell why
 * they keep dying. An unknown bit is refused rather than ignored, in both directions, because
 * these bits decide who may be hurt. */
#define MP_RULES_F_TEAMS         0x01u
#define MP_RULES_F_FRIENDLY_FIRE 0x02u
#define MP_RULES_F_KNOWN         0x03u
#define MP_RULES_FLAGS_DEFAULT   MP_RULES_F_TEAMS

/* What killing your own side and killing yourself are worth. A penalty is at most nothing and at
 * least ten points, and it is deliberately not allowed to be positive: a positive number here
 * would pay a player for shooting their own team, which is not a rule anybody meant to write and
 * would arrive off the wire looking exactly like one that was. */
#define MP_RULES_PENALTY_MIN             (-10)
#define MP_RULES_PENALTY_MAX             0
#define MP_RULES_TEAM_PENALTY_DEFAULT    (-1)
#define MP_RULES_SUICIDE_PENALTY_DEFAULT (-1)

/* How long a dead player waits, in tenths of a second. Five seconds is the default and fifteen is
 * the ceiling; a whole second of resolution is too coarse for the shorter settings, and a
 * millisecond is a resolution the substep ladder does not have. */
#define MP_RULES_RESPAWN_TENTHS_DEFAULT 50u
#define MP_RULES_RESPAWN_TENTHS_MAX     150u

/* The ladder the two durations are measured against: the simulation's own substep rate. The
 * clock module owns the number and this module is checked against it at compile time. */
#define MP_RULES_SUBSTEPS_PER_SECOND 32u

typedef struct mp_rules {
    uint16_t score_limit;      /* points to win, 0 for no limit */
    uint16_t time_limit_s;     /* seconds, 0 for no limit */
    uint8_t  flags;            /* MP_RULES_F_TEAMS, MP_RULES_F_FRIENDLY_FIRE */
    int8_t   team_penalty;     /* what killing your own side is worth, MIN..MAX */
    int8_t   suicide_penalty;  /* what killing yourself is worth, MIN..MAX */
    uint8_t  respawn_tenths;   /* how long a dead player waits, in tenths of a second */
    uint8_t  reserved;         /* zero on the wire, in both directions, so it stays free */
} mp_rules_t;

/* What a fresh session plays: twenty five points, fifteen minutes, teams on, friendly fire off,
 * a point off for a betrayal and one for a suicide, five seconds to get back up. */
void mp_rules_default(mp_rules_t *rules);

/* Whether every field is inside the range this build accepts. A rule set that is not valid is
 * never encoded and never acted on; the caller either clamps it or refuses it. */
bool mp_rules_valid(const mp_rules_t *rules);

/* Brings every field back inside its range and answers whether anything had to move. What a
 * caller with a number out of an ini or out of a slider uses, so that a stale file cannot leave
 * the session with a rule nobody can play. */
bool mp_rules_clamp(mp_rules_t *rules);

/* Field for field, so a host can tell whether the rules it is about to repeat are news. */
bool mp_rules_equal(const mp_rules_t *a, const mp_rules_t *b);

bool mp_rules_teams(const mp_rules_t *rules);
bool mp_rules_friendly_fire(const mp_rules_t *rules);

/* The team a player is TREATED as being on. With teams off that is none, for everybody, which is
 * what makes teams off cost nothing: a session where nobody has a team is the free for all, and
 * the damage rule below needs no second case for it. A team number this build does not know
 * answers none as well, because an unknown side is not a side. */
uint8_t mp_rules_effective_team(const mp_rules_t *rules, uint8_t team);

/* Whether this contact between two players is allowed to hurt. This is the rule set's only
 * consumer of the team byte and it does not decide anything itself: it maps the teams switch onto
 * the two teams and asks the one damage rule that already exists. A NULL rule set answers false,
 * because nothing is a licence to hurt anybody.
 *
 * `mode` is the game, co-op or deathmatch, in the numbers the handshake compares. */
bool mp_rules_may_damage(const mp_rules_t *rules, uint8_t mode, uint8_t attacker_team,
                         uint8_t victim_team);

/* The two durations in the unit a round is actually measured in. Zero out of the first means the
 * round is not ended by time; zero out of the second means a dead player is back at once. */
uint32_t mp_rules_time_limit_substeps(const mp_rules_t *rules);
uint32_t mp_rules_respawn_substeps(const mp_rules_t *rules);

/* The two conversions on their own, for a caller holding a number that is not in a rule set yet.
 * Both round to the nearest substep and both refuse to wrap: a value past what the rule set
 * allows is clamped to the rule set's own ceiling first. */
uint32_t mp_rules_seconds_to_substeps(uint32_t seconds);
uint32_t mp_rules_tenths_to_substeps(uint32_t tenths);

/* The nine bytes, written and read in one place so that a field cannot gain a range check in one
 * direction only. Both answer false on a rule set that is out of range, so a stranger's rules are
 * held to exactly what this build's encoder promises. */
bool mp_rules_put(mp_wire_writer_t *writer, const mp_rules_t *rules);
bool mp_rules_get(mp_wire_reader_t *reader, mp_rules_t *out);

#endif /* MULTIPLAYER_MP_RULES_H */
