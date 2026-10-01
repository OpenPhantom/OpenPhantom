/* The damage module's provocation schedule, its death routing, and its refusals with no game.
 *
 * Two pieces of pure decision live here. The schedule kills exactly once when the count reaches
 * the mark, revives exactly once at the second mark, and does nothing anywhere else, whatever
 * order or count the caller arrives with. The routing decides, for one call of the death entry,
 * whether the retail death runs untouched, runs and is then made inert, or is replaced by the
 * replica. Everything else in the module is engine choreography, and what this process can say
 * about it is that an install with nothing resolved refuses instead of writing a detour.
 *
 * The hit relay's death message is tested from here as well: its encoder, decoder and recogniser
 * are pure. The relay's engine side needs a game and is not touched.
 */
#include "unittest.h"

#include "mp_damage.h"
#include "mp_damage_entry.h"
#include "mp_hit_relay.h"

/* The lobby note is four bytes as well, and it travels on the same channel. The recogniser has to
 * tell the two apart by tag, which is the property the check below pins. Written out rather than
 * included, so this stays a test of the recogniser and not of one header agreeing with another. */
#define A_FOREIGN_FOUR_BYTE_TAG 0x91u

static void check_death_message(void)
{
    mp_death_note_t note;
    mp_death_note_t back;
    uint8_t         bytes[MP_EVENT_DEATH_BYTES];
    uint8_t         foreign[MP_EVENT_DEATH_BYTES];

    ut_section("the death message's recogniser");
    note.victim_slot = 1u;
    note.killer_slot = 0u;
    note.reason      = MP_DEATH_BY_HIT;
    ut_check(mp_death_encode(&note, bytes, sizeof bytes),
             "a death with a killer, a victim and a known reason encodes");
    ut_check(bytes[0] == (uint8_t)MP_EVENT_DEATH, "and it carries the death tag");
    ut_check(mp_death_is(bytes, sizeof bytes),
             "the recogniser claims its own four bytes");
    ut_check(!mp_death_is(bytes, sizeof bytes - 1u),
             "the right tag at the wrong length is not ours");
    ut_check(!mp_death_is(bytes, sizeof bytes + 1u),
             "and neither is the right tag one byte too long");

    foreign[0] = (uint8_t)A_FOREIGN_FOUR_BYTE_TAG;
    foreign[1] = 0u;
    foreign[2] = 0u;
    foreign[3] = 0u;
    ut_check(!mp_death_is(foreign, sizeof foreign),
             "another four byte message on the same channel is left alone");
    ut_check(!mp_death_is(NULL, MP_EVENT_DEATH_BYTES),
             "and nothing at all is not a death");

    ut_section("the death message's two ends");
    ut_check(mp_death_decode(bytes, sizeof bytes, &back),
             "what the encoder wrote the decoder reads");
    ut_check(back.victim_slot == note.victim_slot && back.killer_slot == note.killer_slot &&
                 back.reason == note.reason,
             "and every field comes back with the value it went in with");

    note.killer_slot = (uint8_t)MP_DEATH_NO_KILLER;
    note.reason      = MP_DEATH_BY_FALL;
    ut_check(mp_death_encode(&note, bytes, sizeof bytes) &&
                 mp_death_decode(bytes, sizeof bytes, &back) &&
                 back.killer_slot == (uint8_t)MP_DEATH_NO_KILLER,
             "a death with nobody to blame travels with the no killer slot");

    note.killer_slot = note.victim_slot;
    ut_check(!mp_death_encode(&note, bytes, sizeof bytes),
             "a slot may not be named as its own killer; that case is the suicide reason");

    note.killer_slot = 0u;
    note.reason      = (uint8_t)(MP_DEATH_REASON_MAX + 1u);
    ut_check(!mp_death_encode(&note, bytes, sizeof bytes),
             "a reason nothing knows is refused at the encoder");

    note.reason = MP_DEATH_BY_SUICIDE;
    ut_check(mp_death_encode(&note, bytes, sizeof bytes),
             "and the last known reason still encodes");
    ut_check(!mp_death_encode(&note, bytes, sizeof bytes - 1u),
             "an encoder given too little room writes nothing");
    ut_check(!mp_death_encode(NULL, bytes, sizeof bytes),
             "and an encoder given no note writes nothing either");

    bytes[3] = (uint8_t)(MP_DEATH_REASON_MAX + 1u);
    ut_check(!mp_death_decode(bytes, sizeof bytes, &back),
             "a reason nothing knows is refused at the decoder as well");
    bytes[3] = MP_DEATH_BY_HIT;
    bytes[2] = bytes[1];
    ut_check(!mp_death_decode(bytes, sizeof bytes, &back),
             "and so is a message claiming a slot killed itself");
    ut_check(!mp_death_decode(bytes, sizeof bytes, NULL),
             "a decoder with nowhere to put the answer refuses");

    ut_section("the hit relay with no game");
    mp_hit_relay_set_death_listener(NULL);
    mp_hit_relay_note_death(1u, 0u, MP_DEATH_BY_HIT);
    ut_check(!mp_hit_relay_installed(),
             "a death noted with nothing installed and nowhere to send it touches nothing");
    ut_check(mp_hit_relay_take_message(1u, bytes, MP_EVENT_DEATH_BYTES),
             "a death off the wire is claimed by its tag even with nothing installed");
}

int main(void)
{
    ut_section("the provocation schedule");
    ut_check(mp_damage_provoke_decide(0, false, false) == MP_DAMAGE_PROVOKE_NOTHING,
             "substep zero does nothing");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT - 1u, false, false)
                 == MP_DAMAGE_PROVOKE_NOTHING,
             "one substep before the mark does nothing");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT, false, false)
                 == MP_DAMAGE_PROVOKE_KILL,
             "the mark kills");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT + 1u, true, false)
                 == MP_DAMAGE_PROVOKE_NOTHING,
             "a killed body is not killed again the next substep");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT + MP_DAMAGE_PROVOKE_REVIVE_AFTER
                                          - 1u, true, false)
                 == MP_DAMAGE_PROVOKE_NOTHING,
             "one substep before the revive mark stays dead");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT + MP_DAMAGE_PROVOKE_REVIVE_AFTER,
                                      true, false)
                 == MP_DAMAGE_PROVOKE_REVIVE,
             "the second mark revives");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT + MP_DAMAGE_PROVOKE_REVIVE_AFTER
                                          + 100u, true, true)
                 == MP_DAMAGE_PROVOKE_NOTHING,
             "a revived body is left alone for good");
    ut_check(mp_damage_provoke_decide(MP_DAMAGE_PROVOKE_KILL_AT * 10u, false, false)
                 == MP_DAMAGE_PROVOKE_KILL,
             "a mark long overshot still kills exactly once rather than never");

    ut_section("which death a call of the death entry is");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, 0, false)
                 == MP_DAMAGE_DEATH_RETAIL,
             "with the switch off the player's own death is the retail death");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, 0, true)
                 == MP_DAMAGE_DEATH_SURVIVE,
             "with the switch on a death from health at zero is made inert");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, 1, true)
                 == MP_DAMAGE_DEATH_SURVIVE,
             "and so is the hard landing");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, 2, true)
                 == MP_DAMAGE_DEATH_SURVIVE,
             "and the long fall");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, 3, true)
                 == MP_DAMAGE_DEATH_SURVIVE,
             "and the fire");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, MP_DAMAGE_CAUSE_SCRIPTED, true)
                 == MP_DAMAGE_DEATH_RETAIL,
             "a death a script ordered still ends the level, switch or no switch");
    ut_check(mp_damage_death_route(MP_DAMAGE_LOCAL_BANK_CLASS, MP_DAMAGE_CAUSE_FINISHED, true)
                 == MP_DAMAGE_DEATH_SURVIVE,
             "a cause the engine never passes in is treated as a world death, not as a script");
    ut_check(mp_damage_death_route(5, 0, false) == MP_DAMAGE_DEATH_NO_LATCH,
             "a far body takes the replica with the switch off");
    ut_check(mp_damage_death_route(5, MP_DAMAGE_CAUSE_SCRIPTED, true)
                 == MP_DAMAGE_DEATH_NO_LATCH,
             "and takes it for a scripted cause too: the campaign is never a far body's");
    ut_check(mp_damage_death_route(7, 0, true) == MP_DAMAGE_DEATH_NO_LATCH,
             "every far class routes the same way");

    ut_section("the survival switch");
    ut_check(!mp_damage_survives_death(), "the player's own death ends the level by default");
    mp_damage_set_survives_death(true);
    ut_check(mp_damage_survives_death(), "and the switch is what changes that");
    mp_damage_set_survives_death(false);
    ut_check(!mp_damage_survives_death(), "and it goes back");

    check_death_message();

    ut_section("one death entry per life");
    ut_check(mp_damage_entry_verdict(true, false) == MP_DAMAGE_ENTRY_ENTER,
             "a living body in a session enters its death");
    ut_check(mp_damage_entry_verdict(true, true) == MP_DAMAGE_ENTRY_REFUSE,
             "a corpse in a session is refused, and so is the note of it");
    ut_check(mp_damage_entry_verdict(false, true) == MP_DAMAGE_ENTRY_ENTER &&
                 mp_damage_entry_verdict(false, false) == MP_DAMAGE_ENTRY_ENTER,
             "without a session nothing is ever refused: the retail death runs as it always has");
    {
        uint32_t lives = 7u;

        ut_check(!mp_damage_entry_lives_ended(&lives) && lives == 7u,
                 "with no death hull standing, the count of lives says nothing and is not given");
    }

    ut_section("the module with no game");
    ut_check(!mp_damage_installed(), "nothing is installed before the installer ran");
    ut_check(!mp_damage_install(),
             "an install with no site resolved refuses before anything is written");
    ut_check(!mp_damage_installed(), "and the refusal leaves the module uninstalled");
    mp_damage_provoke_tick();
    mp_damage_report("a test process");
    ut_check(!mp_damage_installed(),
             "the tick and the report on an uninstalled module touch nothing");

    return ut_summary("mp_damage");
}
