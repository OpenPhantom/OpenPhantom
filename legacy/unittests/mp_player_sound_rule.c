/* mp_player_sound_rule.c: the decisions behind a far player's own sounds and shield, pure.
 *
 * What is held here is what the engine does and in which order, written down as the plan the far
 * side follows: the death cry for every cause but a scripted one, the burning cry and the burst on
 * top of it for fire, the named sounds out of the one table the engine reads them from, the key
 * with the engine's own guard against playing it twice at once, and a shield that lasts as long as
 * the moment said and not for good.
 */
#include "unittest.h"

#include "mp_events.h"
#include "mp_player_sound_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The retail name table and three of its entries, as the engine's operands name them. */
#define NAMES_TABLE      0x004AA978u
#define NAME_GROUND      0x004AAA48u   /* entry 52 */
#define NAME_PICKUP      0x004AAA4Cu   /* entry 53 */
#define NAME_KEY         0x004AAA60u   /* entry 58 */

static void check_who_may_send(void)
{
    ut_section("only this machine's own player, and only in a session");
    ut_check(mp_player_sound_rule_may_note(true, 1),
             "in a session the body of collision class 1 is this machine's player");
    ut_check(!mp_player_sound_rule_may_note(false, 1),
             "without a session nothing is sent: single player is left to the engine");
    ut_check(!mp_player_sound_rule_may_note(true, 5) && !mp_player_sound_rule_may_note(true, 0),
             "a bank window has swapped in another body, whose sounds are not this player's");
}

static void check_what_a_death_sends(void)
{
    uint8_t whats[2] = { 0xFFu, 0xFFu };
    int32_t cause;

    ut_section("a death sends the cries the engine plays, in its order");
    for (cause = 0; cause <= 2; ++cause) {
        ut_checkf(mp_player_sound_rule_death_moments(cause, whats) == 1u &&
                      whats[0] == MP_PLAYER_SOUND_DEATH,
                  "cause %d: the death cry alone", (int)cause);
    }
    ut_check(mp_player_sound_rule_death_moments(3, whats) == 2u &&
                 whats[0] == MP_PLAYER_SOUND_DEATH && whats[1] == MP_PLAYER_SOUND_BURN,
             "fire: the death cry and then the burning cry, two sounds as the engine plays two");
    ut_check(mp_player_sound_rule_death_moments(4, whats) == 1u &&
                 whats[0] == MP_PLAYER_SOUND_DEATH,
             "a scripted death still sends its moment, so the far side can count what it does "
             "not voice");
    ut_check(mp_player_sound_rule_death_moments(5, whats) == 0u &&
                 mp_player_sound_rule_death_moments(-1, whats) == 0u,
             "a cause the engine does not pass sends nothing");
}

static void check_the_far_sides_plan(void)
{
    mp_player_sound_plan_t p;

    ut_section("what the far side plays, from which table, with which flags");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_DEATH, 2u, 0u, &p);
    ut_check(p.valid && p.table == MP_PLAYER_SOUND_TABLE_DEATH && p.row == 2u && !p.silent &&
                 p.engine_flags == MP_PLAYER_SOUND_PLACED_FLAGS &&
                 p.cue == MP_PLAYER_SOUND_CUE_DEATH && !p.burst,
             "a death cry is read from this machine's own death voices by the hero row, 3D and "
             "placed, as the engine's own call passes it");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_DEATH, 2u, 4u, &p);
    ut_check(p.valid && p.silent, "a scripted death is counted and left silent");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_DEATH, 4u, 0u, &p);
    ut_check(!p.valid, "a hero row past the four names no voice");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_DEATH, 0u, 5u, &p);
    ut_check(!p.valid, "a cause past the five is torn");

    mp_player_sound_rule_plan(MP_PLAYER_SOUND_BURN, 1u, 3u, &p);
    ut_check(p.valid && p.table == MP_PLAYER_SOUND_TABLE_BURN && p.row == 1u && p.burst &&
                 p.engine_flags == MP_PLAYER_SOUND_PLACED_FLAGS && !p.silent,
             "the burning cry comes from the burning voices, and the body bursts before it");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_BURN, 1u, 0u, &p);
    ut_check(!p.valid, "a burning cry of any cause but fire is not something a sender makes");

    mp_player_sound_rule_plan(MP_PLAYER_SOUND_GROUND, 52u, 0u, &p);
    ut_check(p.valid && p.table == MP_PLAYER_SOUND_TABLE_NAMES && p.row == 52u &&
                 p.engine_flags == MP_PLAYER_SOUND_PLACED_FLAGS &&
                 p.cue == MP_PLAYER_SOUND_CUE_NAMED && !p.burst && !p.silent,
             "the burning ground is one name out of the table, played once, placed");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_KEY, 58u, 0u, &p);
    ut_check(p.valid && p.engine_flags ==
                            (MP_PLAYER_SOUND_PLACED_FLAGS | MP_PLAYER_SOUND_FLAG_DONT_DUP),
             "the key keeps the engine's own guard against the same sound twice at once");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_PICKUP, 53u, 0u, &p);
    ut_check(p.valid && p.engine_flags == MP_PLAYER_SOUND_PLACED_FLAGS, "a pickup, placed");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_WATER, 59u, 0u, &p);
    ut_check(p.valid && p.row == 59u, "the plunge into water, placed");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_WATER, 92u, 0u, &p);
    ut_check(!p.valid, "a name past the table's 92 entries is torn");

    mp_player_sound_rule_plan(MP_PLAYER_SOUND_SHIELD_ON, 100u, 0u, &p);
    ut_check(p.valid && p.shield_on && !p.shield_off && p.seconds == 100u &&
                 p.table == MP_PLAYER_SOUND_TABLE_NONE,
             "a shield rises for the hundred seconds its moment names, and nothing is played");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_SHIELD_ON, 0u, 0u, &p);
    ut_check(!p.valid, "a shield of no length is torn");
    mp_player_sound_rule_plan(MP_PLAYER_SOUND_SHIELD_OFF, 0u, 0u, &p);
    ut_check(p.valid && p.shield_off && !p.shield_on, "and ends when its moment says so");
    mp_player_sound_rule_plan((uint8_t)(MP_PLAYER_SOUND_KIND_MAX + 1u), 0u, 0u, &p);
    ut_check(!p.valid, "a kind past the eight is torn");
}

static void check_names_and_seconds(void)
{
    uint8_t index = 0u;

    ut_section("a name's index out of the engine's operand, and a timer's seconds");
    ut_check(mp_player_sound_rule_name_index(NAME_GROUND, NAMES_TABLE, &index) && index == 52u,
             "the burning ground's operand is entry 52");
    ut_check(mp_player_sound_rule_name_index(NAME_PICKUP, NAMES_TABLE, &index) && index == 53u &&
                 mp_player_sound_rule_name_index(NAME_KEY, NAMES_TABLE, &index) && index == 58u,
             "the pickup's is 53 and the key's 58");
    ut_check(!mp_player_sound_rule_name_index(NAME_GROUND + 1u, NAMES_TABLE, &index),
             "an operand between two entries names none");
    ut_check(!mp_player_sound_rule_name_index(NAMES_TABLE - 4u, NAMES_TABLE, &index) &&
                 !mp_player_sound_rule_name_index(NAMES_TABLE + 92u * 4u, NAMES_TABLE, &index),
             "and one before the table or past its end names none either");

    ut_check(mp_player_sound_rule_seconds(100.0f) == 100u, "a hundred seconds are a hundred");
    ut_check(mp_player_sound_rule_seconds(99.2f) == 100u &&
                 mp_player_sound_rule_seconds(0.01f) == 1u,
             "part of a second counts as a whole one, so a shield is never cut short");
    ut_check(mp_player_sound_rule_seconds(0.0f) == 0u &&
                 mp_player_sound_rule_seconds(-1.0f) == 0u &&
                 mp_player_sound_rule_seconds((float)sqrt(-1.0)) == 0u,
             "a timer that is not running, or not a number, is no shield");
    ut_check(mp_player_sound_rule_seconds(1000.0f) == 255u, "and the byte holds 255 at most");
}

static void check_the_windows(void)
{
    ut_section("too late, and a shield worn too long");
    ut_check(!mp_player_sound_rule_too_late(100u, 5000u, false),
             "with no render tick a moment is due on arrival, never late");
    ut_check(!mp_player_sound_rule_too_late(100u, 110u, true),
             "ten substeps behind the body is still in time");
    ut_check(mp_player_sound_rule_too_late(100u, 111u, true),
             "eleven is too late, and the cry is counted rather than played at nothing");
    ut_check(!mp_player_sound_rule_too_late(100u, 90u, true),
             "a moment ahead of the body is not late");
    ut_check(!mp_player_sound_rule_too_late(0xFFFFFFFEu, 5u, true),
             "the substep counter's wrap is read as seven substeps, not four billion");

    ut_check(!mp_player_sound_rule_shield_expired((100u + 2u) * 32u, 100u),
             "a shield of a hundred seconds may be worn its length and two seconds of slack");
    ut_check(mp_player_sound_rule_shield_expired((100u + 2u) * 32u + 1u, 100u),
             "and not a substep more");

    ut_check(!mp_player_sound_rule_came_back(false, true),
             "a far player standing who was never seen lying has not come back");
    ut_check(mp_player_sound_rule_came_back(true, true),
             "one seen lying and standing again has: the far machine built him a new body");
    ut_check(!mp_player_sound_rule_came_back(true, false), "one still lying has not");
}

int main(void)
{
    check_who_may_send();
    check_what_a_death_sends();
    check_the_far_sides_plan();
    check_names_and_seconds();
    check_the_windows();
    return ut_summary("mp_player_sound_rule");
}
