/* mp_body_death.c: the attribution behind the contact dispatcher, and the friendly fire gate.
 *
 * Everything here runs inside one contact delivery, on the scheduler's thread, with nothing of
 * this feature's swapped in.
 */
#include "mp_body_death.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_internal.h"
#include "mp_cells.h"
#include "mp_hit_relay.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The player record's dead flag. The death entry writes the 1 and nothing clears it in place, so
 * a 0 in front of one contact delivery and a 1 behind it is that contact having killed him. */
#define RECORD_DEAD 0x394u

/* How many banks carry a world slot: this machine's own player and the far ones. */
#define BANK_SLOTS (MP_BANK_FAR_MAX + 1u)

typedef struct mp_body_death_state {
    /* The two cells are optional: without them the dispatcher does everything it did before and
     * reports no death, which is said once at the install. */
    uintptr_t pr_cell;      /* the player record pointer, for the dead flag */
    uintptr_t self_cell;    /* the SENDER of a contact, for the attacker's class */
    mp_body_death_shot_test_fn_t is_a_shot;   /* whether a sender is a projectile */
    mp_body_death_ally_test_fn_t by_an_ally;  /* whether the contact is an ally's */
    mp_body_death_far_shot_fn_t  far_shot;    /* which far bank fired a sideless shot */
    bool      resolved;

    mp_body_death_fn_t       death_listener;
    mp_body_damage_gate_fn_t damage_gate;   /* whether this pair of players may hurt at all */

    uint8_t bank_slot[BANK_SLOTS];
    bool    bank_slot_known[BANK_SLOTS];

    bool watching;   /* this delivery was entered with the player alive, so it can be judged */

    uint32_t deaths_seen;
    uint32_t deaths_reported;
    uint32_t deaths_off_contact;      /* the death entry saw it and no contact was being
                                       * delivered */
    uint32_t deaths_without_killer;
    uint32_t deaths_unaddressed;   /* nobody set this machine's own world slot */
    uint32_t deaths_undelivered;   /* no listener was wired */
    uint32_t death_read_faults;    /* the dead flag did not read back after the handler */
    bool     death_slot_logged;
    bool     death_listener_logged;

    uint32_t contacts_refused;     /* player against player, refused by the rule set */
    uint32_t contacts_by_allies;   /* an ally's, refused whatever the rules say */
    uint32_t contacts_unjudged;    /* nothing to ask, or no slot for the body that was touched */

    /* Why a sender could not be placed behind a bank. These used to be one silent -1 that
     * the judge answered on its first comparison, ahead of every counter above, so the
     * commonest way for a contact to skip the gate entirely produced no number at all. */
    uint32_t senders_unreadable;       /* the cell, the sender or its class did not read */
    uint32_t senders_sideless;         /* a shot the engine left with no side: nobody's */
    uint32_t senders_sideless_named;   /* of those, the ones the shot owners placed */

    /* Which arm of the rule answered, counted per contact. The pair is the evidence that the
     * delivery to this machine's own player is a path players really reach: a run in which two of
     * them shoot at each other and the second number stays at zero says the reading is wrong. */
    uint32_t contacts_attacking;   /* judged with this machine's player as the attacker */
    uint32_t contacts_attacked;    /* judged with this machine's player as the one touched */
    uint32_t contacts_refused_on_him;   /* of those, the ones the rule refused */
    uint32_t contacts_between_far; /* two far bodies, left to the victim's own machine */
} mp_body_death_state_t;

static mp_body_death_state_t death;

bool mp_body_death_install(void)
{
    death.pr_cell   = mp_cells_address(MP_CELL_PR);
    death.self_cell = mp_cells_address(MP_CELL_MSG_SELF);
    death.resolved  = death.pr_cell != 0u && death.self_cell != 0u;
    return death.resolved;
}

bool mp_body_death_ready(void)
{
    return death.resolved;
}

void mp_body_set_bank_slot(size_t index, uint8_t world_slot)
{
    if (index < BANK_SLOTS) {
        death.bank_slot[index]       = world_slot;
        death.bank_slot_known[index] = true;
    }
}

bool mp_body_bank_slot(size_t index, uint8_t *world_slot)
{
    if (index >= BANK_SLOTS || !death.bank_slot_known[index] || world_slot == NULL) {
        return false;
    }
    *world_slot = death.bank_slot[index];
    return true;
}

void mp_body_set_death_listener(mp_body_death_fn_t listener)
{
    death.death_listener = listener;
}

void mp_body_set_damage_gate(mp_body_damage_gate_fn_t gate)
{
    death.damage_gate = gate;
}

bool mp_body_death_is_reported(void)
{
    return death.death_listener != NULL;
}

uint32_t mp_body_deaths_seen(void)     { return death.deaths_seen; }
uint32_t mp_body_deaths_reported(void) { return death.deaths_reported; }

/* The local player's dead flag, through the pointer the engine addresses him by. False when the
 * cell did not resolve or the record did not read, which is an answer and not a death. */
static bool player_dead(uint32_t *out)
{
    uint32_t record = 0;

    return death.pr_cell != 0 && memory_try_read_u32(death.pr_cell, &record) && record != 0u &&
           memory_try_read_u32((uintptr_t)record + RECORD_DEAD, out);
}

/* Which bank the contact being delivered came out of, or -1 for none. The sender is the message's
 * own self cell and the class it carries is the bank's own: bank 0 is class 1 and bank i is
 * class 4 + i, which is the rule mp_bank_class_of applies and this reads backwards. A body carries
 * it at +0x04. A projectile carries its side at +0x08 instead: the engine writes 1 there for the
 * local player's own shots and the shot hull stamps the firing bank's class over it for a
 * puppet's, while +0x04 of a projectile is its row of the shot table (0x20, 0x22, 0x24 or 0) and
 * names no bank. Read at +0x04, every bolt was nobody's: a bolt between players passed the gate
 * unjudged, and a death by one was reported without its killer.
 *
 * An attacker that is neither is an enemy, a mover or the level itself. Those classes are the
 * shipped ones, and the three co-op values appear in none of the 2250 shipped actor records,
 * which is what makes this test an identification rather than a guess. */
static int32_t bank_behind_contact(void)
{
    uint32_t self = 0;
    uint32_t class_word = 0;
    bool     a_shot;
    size_t   index;
    size_t   named = 0;

    if (death.self_cell == 0 || !memory_try_read_u32(death.self_cell, &self) || self == 0u) {
        ++death.senders_unreadable;
        return -1;
    }
    a_shot = death.is_a_shot != NULL && death.is_a_shot(self);
    if (!memory_try_read_u32((uintptr_t)self + (a_shot ? BAPOBJ_SHOOTER_CLASS
                                                   : BAPOBJ_OBJ_CLASS),
                         &class_word)) {
        ++death.senders_unreadable;
        return -1;
    }
    if ((int32_t)class_word == MP_BODY_CLASS_PLAYER0) {
        return 0;
    }
    for (index = 1u; index <= MP_BANK_FAR_MAX; ++index) {
        if ((int32_t)class_word == mp_bank_class_of(index)) {
            return (int32_t)index;
        }
    }
    /* A shot whose side reads zero belongs to nobody, and the engine writes that on
     * purpose: the thermal detonator clears its own side at birth so that it hurts its
     * thrower as well as everyone else. Read as a class, zero is neither this player nor
     * any bank, so the contact left here as -1 and the judge passed it on its first
     * comparison, ahead of the counters that were supposed to describe it.
     *
     * There is no other field to read, because the zero is deliberate. What answers is a
     * memory that outlives the side, and the shot owners ring is one: it remembers the
     * OBJECT a far player's shot flies as, and that the engine never clears. */
    if (a_shot && class_word == 0u) {
        ++death.senders_sideless;
        if (death.far_shot != NULL && death.far_shot(self, &named) &&
            named != 0u && named <= MP_BANK_FAR_MAX) {
            ++death.senders_sideless_named;
            return (int32_t)named;
        }
    }
    return -1;
}

/* The death message this machine is the only one that can write. A contact killed the local
 * player, and the bank behind that contact is the killer. Before this function the message
 * existed, encoded, decoded and refused a killer equal to the victim, and had no caller anywhere.
 *
 * Bank 0 behind it is the player's own weapon, blade or force. That travels as a suicide with no
 * killer named rather than as a hit by himself, because a slot cannot kill itself on the wire and
 * a rule set charges the two differently anyway. A death seen only by the death entry is reported
 * as a hit with no killer; the fall and environment reasons exist on the wire and decode, and
 * nothing here produces them. */
static void report_death(int32_t attacker_bank)
{
    uint8_t killer = (uint8_t)MP_DEATH_NO_KILLER;
    uint8_t reason = (uint8_t)MP_DEATH_BY_HIT;

    ++death.deaths_seen;
    if (!death.bank_slot_known[0]) {
        ++death.deaths_unaddressed;
        if (!death.death_slot_logged) {
            death.death_slot_logged = true;
            log_warning("this player died and the death cannot be reported: nobody has said which "
                        "world slot this machine is. Later ones are counted, not logged");
        }
        return;
    }
    if (attacker_bank == 0) {
        reason = (uint8_t)MP_DEATH_BY_SUICIDE;
    } else if (attacker_bank > 0 && (size_t)attacker_bank < BANK_SLOTS &&
               death.bank_slot_known[(size_t)attacker_bank] &&
               death.bank_slot[(size_t)attacker_bank] != death.bank_slot[0]) {
        killer = death.bank_slot[(size_t)attacker_bank];
    }
    if (killer == (uint8_t)MP_DEATH_NO_KILLER && reason == (uint8_t)MP_DEATH_BY_HIT) {
        ++death.deaths_without_killer;   /* an enemy, a mover or the level: nothing to name */
    }

    ++death.deaths_reported;
    log_info("this player died on world slot %u: killer slot %u, reason %u",
             (unsigned)death.bank_slot[0], (unsigned)killer, (unsigned)reason);
    if (death.death_listener != NULL) {
        death.death_listener(death.bank_slot[0], killer, reason);
        return;
    }
    ++death.deaths_undelivered;
    if (!death.death_listener_logged) {
        death.death_listener_logged = true;
        log_warning("this player died with nothing listening for it, so no score can ever count "
                    "it. Wire mp_body_set_death_listener; later ones are counted, not logged");
    }
}

/* Told by the death hull, which sees every death this player dies rather than only the ones a
 * contact delivered. Inside a delivery it stands aside: `watch_end` is a few instructions away
 * and it has the attacker in front of it, and two reports of one death are two wishes for one
 * player, which is a body coming back twice. */
void mp_body_death_note_engine_death(void)
{
    if (death.watching) {
        return;
    }
    ++death.deaths_off_contact;
    report_death(-1);
}

void mp_body_death_watch_begin(void)
{
    uint32_t before = 0;

    death.watching = player_dead(&before) && before == 0u;
}

void mp_body_death_watch_end(void)
{
    uint32_t after = 0;

    if (!death.watching) {
        return;
    }
    death.watching = false;
    if (!player_dead(&after)) {
        ++death.death_read_faults;
        return;
    }
    if (after != 0u) {
        report_death(bank_behind_contact());
    }
}

/* A refusal is whole: no half damage and no shove. The two branches this sits in are the only
 * two ways a contact reaches a far body, the puppet branch (where a refusal means the hit is
 * never reported to the machine that would perform it) and the routed branch (where it means the
 * engine's handler never runs inside that bank's window). A shove that still landed would push a
 * co-op partner off a ledge, which is a kill with extra steps. The rule this asks had been built
 * and unit tested with no line in the game calling it until this gate. */
void mp_body_death_set_shot_test(mp_body_death_shot_test_fn_t test)
{
    death.is_a_shot = test;
}

void mp_body_death_set_ally_test(mp_body_death_ally_test_fn_t test)
{
    death.by_an_ally = test;
}

void mp_body_death_set_far_shot_test(mp_body_death_far_shot_fn_t test)
{
    death.far_shot = test;
}

/* The order of the arms is the decision, and the first of them is what keeps this cheap on the
 * path that carries nearly every contact in the game: a sender behind no bank leaves on one
 * comparison.
 *
 *   1. no bank behind it: an enemy, a mover, the world, a thrown body. Nothing to ask.
 *   2. sender and receiver are the same body: his own weapon, his own push, his own blast, which
 *      no rule set has an opinion about. A suicide is booked elsewhere.
 *   3. two far bodies: that pair belongs to the machines the two stand for, not to this one. The
 *      victim's machine replays the attacker's shot or swing on its own body and asks arm 4 there,
 *      so an answer from here would be a second source for the same hit.
 *   4. one of the two is this machine's player, so the other one is the far player the gate is
 *      asked about. Which of them swung does not enter, because the rule answers the same for
 *      both directions; that is what lets one question serve both.
 */
mp_contact_verdict_t mp_body_death_judge_contact(int32_t from, size_t victim_bank)
{
    size_t other;
    bool   on_him;

    if (from < 0 || from == (int32_t)victim_bank || victim_bank >= BANK_SLOTS) {
        return MP_CONTACT_ALLOWED;
    }
    if (victim_bank != 0u && from != 0) {
        ++death.contacts_between_far;
        return MP_CONTACT_NOT_OURS;
    }
    other  = from == 0 ? victim_bank : (size_t)from;
    on_him = victim_bank == 0u;
    if (on_him) {
        ++death.contacts_attacked;
    } else {
        ++death.contacts_attacking;
    }
    if (death.by_an_ally != NULL && death.by_an_ally()) {
        ++death.contacts_by_allies;
        return MP_CONTACT_REFUSED;
    }
    if (death.damage_gate == NULL || !death.bank_slot_known[other]) {
        ++death.contacts_unjudged;
        return MP_CONTACT_ALLOWED;
    }
    if (death.damage_gate(death.bank_slot[other])) {
        return MP_CONTACT_ALLOWED;
    }
    ++death.contacts_refused;
    if (on_him) {
        ++death.contacts_refused_on_him;
    }
    return MP_CONTACT_REFUSED;
}

mp_contact_verdict_t mp_body_death_contact_verdict(size_t victim_bank)
{
    return mp_body_death_judge_contact(bank_behind_contact(), victim_bank);
}

void mp_body_death_report(void)
{
    char   far_slots[32];
    size_t at = 0;
    size_t index;

    /* Every far bank's slot, a dash for one nobody set: with more than two players the first
     * bank alone says nothing about who a death was charged to. */
    far_slots[0] = '\0';
    for (index = 1u; index < BANK_SLOTS; ++index) {
        at += death.bank_slot_known[index]
                  ? text_format(far_slots + at, sizeof far_slots - at, "%s%u",
                                index == 1u ? "" : " ", (unsigned)death.bank_slot[index])
                  : text_format(far_slots + at, sizeof far_slots - at, "%s-",
                                index == 1u ? "" : " ");
    }
    log_info("  the friendly fire gate: %u contact(s) of an ally's refused first",
             (unsigned)death.contacts_by_allies);
    log_info("  the friendly fire gate: %s, %u contact(s) between players refused whole (%u of "
             "them ON this player by a far one), %u judged with this player attacking and %u with "
             "him being touched, %u left unjudged for want of a gate or of a world slot for the "
             "body that was touched",
             death.damage_gate != NULL ? "wired"
                                       : "NOT WIRED, so every player may hurt every other",
             (unsigned)death.contacts_refused, (unsigned)death.contacts_refused_on_him,
             (unsigned)death.contacts_attacking, (unsigned)death.contacts_attacked,
             (unsigned)death.contacts_unjudged);
    /* A sentence of its own, so the line above keeps the words it is found by when two runs'
     * reports are compared. The middle number is the size of a hole that used to be invisible, and
     * the last is how much of it the shot owners close; the difference is what still skips the
     * gate. */
    log_info("  the friendly fire gate, senders it could not place: %u unreadable, %u "
             "shot(s) the engine left with no side of their own, %u of those placed by the "
             "shot owners and judged",
             (unsigned)death.senders_unreadable, (unsigned)death.senders_sideless,
             (unsigned)death.senders_sideless_named);
    /* Its own sentence for the same reason. From three players on every machine meets the case: a
     * far player's shot or blade replayed here on another far player's puppet. The victim's machine
     * judges each of them; on the host each is a hit it no longer reports, on a client there never
     * was a report, so a count above nought on a client is expected. */
    log_info("  the friendly fire gate between two far players: %u contact(s) left to their own "
             "machines", (unsigned)death.contacts_between_far);
    log_info("  this player's deaths: %u seen (%u of them off a contact, so the death entry was "
             "the only witness), %u reported (%u of them with no killer behind the contact), %u "
             "dropped for a world slot nobody set, %u dropped with no listener, %u "
             "dead flag read fault(s); this machine holds world slot %u, %s, and the far banks "
             "stand for slots %s",
             (unsigned)death.deaths_seen, (unsigned)death.deaths_off_contact,
             (unsigned)death.deaths_reported,
             (unsigned)death.deaths_without_killer, (unsigned)death.deaths_unaddressed,
             (unsigned)death.deaths_undelivered, (unsigned)death.death_read_faults,
             (unsigned)death.bank_slot[0], death.bank_slot_known[0] ? "set" : "NEVER SET",
             far_slots);
}
