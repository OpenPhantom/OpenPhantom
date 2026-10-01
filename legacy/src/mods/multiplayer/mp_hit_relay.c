/* mp_hit_relay.c: the client notices, the host performs.
 *
 * SIZE NOTE: over 600 lines. The file is three crossings of the same six engine globals, and they
 * are one file because they share the detour, the cell table and the replay guard that stops a
 * replayed contact being reported back to the machine it came from. Splitting the mirror out would
 * put that guard on one side of a translation unit boundary and two of its three writers on the
 * other. The death message, the seam this note used to name, is in mp_hit_relay_death.c: the one
 * setter of the send function hands it on, and the slot comes with each death, so nothing is set in
 * two places. The next seam, if one is needed, is the five notes about who fired a shot. They only
 * write the shot owners' table, but sender_of_contact and sent_by_an_ally read it, so it would move
 * with an accessor rather than for free.
 */
#include "mp_hit_relay.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_death.h"
#include "mp_hit_relay_death.h"
#include "mp_hit_attacker.h"
#include "mp_knockback.h"
#include "mp_body.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_spawn.h"
#include "mp_target.h"
#include "mp_events.h"
#include "mp_node_map.h"
#include "mp_signatures.h"
#include "mp_trust.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A body carries the actor it belongs to; the handler finds its own victim the same way. A census
 * over the whole retail image finds exactly two writers that put a pointer here: the character
 * spawn at 00437250 stores the actor, the shot spawn at 00453CD2 stores the shot record, and a
 * third site restores a shot after a savegame. Every other store to +0xA0 in the image is the
 * player record's moved this frame flag or an unrelated structure, so the owner is an actor or a
 * shot record and there is no third kind. */
#define BODY_OWNER  0xA0u

/* And an object carries the impact code that travels with every message it sends. */
#define BODY_IMPACT 0x0Cu

/* The skeleton node whose sphere is the body's armed weapon right now. Zero is a body with
 * nothing swinging, and two of the handler's arms refuse damage on exactly that. */
#define BODY_CONTACT_NODE 0xA8u

/* The player record's sabre node, which is the node a hero's swing arms. Read out of the
 * puppet's own bank when the replay has to arm a blade the host is not holding. */
#define RECORD_SABRE_NODE 0x4Cu

/* `perform` without an impact code of its own reads the attacker's, which is what the pickup
 * path wants: a touch, and whatever the puppet happens to carry. */
#define IMPACT_FROM_ATTACKER 0x100u

/* The actor's body, which is what the contact globals name rather than the actor itself. It is
 * also half of the test that tells an actor from a shot record: an actor names its body here. */
#define ACTOR_BODY  0x34u

/* And a shot record names the body it flies as here, at the same offset the body names its owner
 * from. Only one of the two round trips can close on a given owner, which is what makes the pair
 * a decision rather than a guess. A shot record's +0x18, where an actor keeps its placement
 * index, is its lifetime as a float, named by the engine's own assert on it being at least zero;
 * the shot table at 004B5710, 38 rows of 0x44 bytes, carries a life of 0.0 in row 36, and the
 * spawn copies the descriptor's +0x18 into the record's. Zero seconds is bits 00000000, a
 * placement index of 0 and inside the 256 the old test allowed, so placement 0 of the level was
 * named as the attacker; every other kind's lifetime falls out of range on its own, which is why
 * the guess looked like it worked. */
#define SHOT_BODY   0xA0u

/* Where the second body's own object sits in the hero block, written there by the spawn. */
#define SECOND_BODY_OBJECT 0x0Cu

/* The shooter side of an object: 1 for a player's shot, an actor's class for its bolt, 0 for the
 * engine's effect objects. */
#define OBJECT_SIDE 0x08u

/* An actor's class, the side it fights on: 1 is the player's, an ally. */
#define ACTOR_CLASS 0x1Cu

/* Six bytes of prologue, one instruction boundary, no relative operand in them. */
#define ON_CONTACT_PROLOGUE 6u

/* No arguments, proven at the one place that calls it: no push before the indirect call and no
 * add esp after it. The handler reads the six globals instead, which is what makes a replay
 * possible at all. */
typedef int32_t(__cdecl *on_contact_fn_t)(void);

typedef struct hit_relay_state {
    bool                   installed;
    bool                   is_client;
    bool                   replaying;
    detour_t               detour;
    on_contact_fn_t        original;
    mp_hit_relay_send_fn_t send;
    uintptr_t              self_cell;
    uintptr_t              other_cell;
    uintptr_t              code_cell;
    uintptr_t              impact_cell;
    uintptr_t              a_cell;
    uintptr_t              b_cell;
    uintptr_t              node_cell;
    uint32_t               reported;
    uint32_t               unreported;
    uint32_t               performed;
    uint32_t               refused_checked;   /* a client's hit, in a deathmatch */
    uint32_t               no_victim;
    uint32_t               no_attacker;
    uint32_t               armed_for_the_call;
    uint32_t               slot;              /* the world slot this machine holds */
    uint32_t               mirrored;          /* hits on a far player sent from here */
    uint32_t               mirror_unsent;
    uint32_t               mirror_applied;    /* and performed on the player here */
    uint32_t               mirror_not_mine;   /* addressed to another slot */
    uint32_t               torn;              /* a hit message of ours that did not decode */
    uint32_t               mirror_no_slot;    /* a far bank nobody had told a slot for */
    uint32_t               mirror_no_attacker;
    uint32_t               owner_is_a_shot;   /* owners that were a projectile, not a placement */
    uint32_t               ally_bolt_hits;    /* an ally's bolt on a far player, not sent */
    uint32_t               ally_body_hits;    /* an ally's own body on a far player, not sent */
    uint32_t               owner_unknown;     /* and owners that were neither */
    uint32_t               to_victim;         /* hits on a far player sent to its peer alone */
    uint32_t               to_no_peer;        /* ... addressed to a slot nobody plays here */
    uint32_t               to_everybody;      /* ... broadcast, with no addressed sender set */
    mp_hit_relay_send_to_slot_fn_t send_to_slot;

    mp_hit_relay_npc_owned_fn_t npc_owned;   /* is this sender an NPC's bolt that travels */

    mp_hit_shot_owners_t   shot_owners;       /* which player fired a player side shot here */
    uint32_t               by_sender[MP_HIT_ATTACKER_COUNT];   /* contacts on parked actors */
} hit_relay_state_t;

static hit_relay_state_t hit;

/* ==============================================================================================
 * The client's half: notice a contact against something it does not own.
 * ============================================================================================ */

/* The actor a body belongs to, or zero when the body belongs to something that is not an actor.
 * The shot round trip is tested first, so a projectile can never be mistaken for a placement
 * however its float fields happen to be laid out; an owner that closes neither round trip is
 * refused, because there is no third kind to guess at. */
static uintptr_t actor_owner_of(uint32_t body)
{
    uint32_t owner = 0;
    uint32_t back  = 0;

    if (body == 0u || !memory_try_read_u32((uintptr_t)body + BODY_OWNER, &owner) || owner == 0u) {
        return 0;
    }
    if (memory_try_read_u32((uintptr_t)owner + SHOT_BODY, &back) && back == body) {
        ++hit.owner_is_a_shot;
        return 0;
    }
    if (memory_try_read_u32((uintptr_t)owner + ACTOR_BODY, &back) && back == body) {
        return (uintptr_t)owner;
    }
    ++hit.owner_unknown;
    return 0;
}

static uintptr_t victim_actor(void)
{
    uint32_t body = 0;

    if (!memory_try_read_u32(hit.other_cell, &body)) {
        return 0;
    }
    return actor_owner_of(body);
}

/* Which shape of contact carries a node at all. The pair pass sends four and only the
 * armed one does; the cell is not cleared between the others, so reading it on any of
 * them hands on what was struck last, somewhere else. It is the same gate the
 * dismemberment mod in this build uses to decide it may sever. */
static bool contact_carries_a_node(uint8_t flags)
{
    return (flags & MP_HIT_FLAG_A) != 0u && (flags & MP_HIT_FLAG_B) == 0u;
}

/* The body of an actor, for the two places that have the actor and want the model. */
static uint32_t body_of_actor(uintptr_t actor)
{
    uint32_t body = 0;

    if (actor == 0 || !memory_try_read_u32(actor + ACTOR_BODY, &body)) {
        return 0u;
    }
    return body;
}

/* Who sent the contact now in the globals: this player's body, a shot and whose, or anything else.
 * The player's body is the hero block's object, which is where the spawn writes it. */
static mp_hit_attacker_t sender_of_contact(void)
{
    uintptr_t block    = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  self     = 0;
    uint32_t  own_body = 0;
    uint32_t  side     = 0;
    uint32_t  node     = 0;
    bool      is_shot;

    if (!memory_try_read_u32(hit.self_cell, &self) || self == 0u) {
        return MP_HIT_ATTACKER_OTHER;
    }
    if (block != 0u) {
        (void)memory_try_read_u32(block + SECOND_BODY_OBJECT, &own_body);
    }
    is_shot = mp_hit_relay_is_a_shot(self);
    /* Both are used whether they read or not, and a read that faulted part way may have left
     * bytes behind, so a refusal puts the default back. */
    if (!memory_try_read_u32((uintptr_t)self + OBJECT_SIDE, &side)) {
        side = 0u;
    }
    if (!memory_try_read_u32((uintptr_t)self + BODY_CONTACT_NODE, &node)) {
        node = 0u;
    }
    return mp_hit_attacker_of(self, own_body, is_shot, side, false,
                              mp_hit_shot_owners_is_far(&hit.shot_owners, self), node != 0u);
}

static int32_t __cdecl hook_on_contact(void)
{
    uintptr_t victim;
    uint32_t  index  = 0;
    uint32_t  code   = 0;
    uint32_t  impact = 0;
    uint32_t  a      = 0;
    uint32_t  b      = 0;
    uint32_t  self   = 0;
    uint32_t  node   = 0;
    uint8_t   flags  = 0;
    uint8_t   note[MP_HIT_RELAY_BYTES];
    mp_hit_note_t hit_note;

    /* The host watches every contact go by and remembers which PLAYER's body touched which
     * actor, so the target resolver can answer with that one. A replayed report is not among
     * them: the replay calls the trampoline and never passes this hull, and a shot or a wave is
     * no player's body in any case. The contacts with code 0x22 are counted around the call. */
    if (!mp_armed_transport()) {
        return hit.original();   /* no session: the engine's own contact, and nothing noted */
    }
    if (!hit.is_client) {
        uint32_t             attacker = 0;
        mp_knockback_watch_t watch;
        int32_t              handled;

        victim = victim_actor();
        if (victim != 0 && memory_try_read_u32(hit.self_cell, &attacker)) {
            mp_target_note_attack(victim, attacker);
        }
        mp_knockback_before(&watch, victim, attacker, hit.code_cell);
        handled = hit.original();
        mp_knockback_after(&watch);
        return handled;
    }

    /* A replay is this module calling the handler on purpose, and it must not be reported back
     * to the machine it came from. */
    if (hit.replaying) {
        return hit.original();
    }

    victim = victim_actor();
    if (victim == 0) {
        ++hit.no_victim;
        return hit.original();
    }

    /* A copy of the host's bolt: the host's own lands on the host's actor there. */
    if (hit.npc_owned != NULL && memory_try_read_u32(hit.self_cell, &self) && hit.npc_owned(self)) {
        if (mp_enemy_bind_is_parked(victim)) {
            ++hit.by_sender[MP_HIT_ATTACKER_NPC_BOLT];
        }
        return hit.original();
    }

    /* Parked means the far side owns it, and of those only the contacts this machine's player
     * made are its hits. Everything else touching a parked actor, another actor, the far
     * player's puppet or a copy of a shot that player fired, happens on the host too. */
    if (mp_enemy_bind_is_parked(victim)) {
        mp_hit_attacker_t sender = sender_of_contact();

        ++hit.by_sender[sender];
        if (!mp_hit_attacker_is_own(sender)) {
            return hit.original();
        }
    }
    if (mp_enemy_bind_is_parked(victim) &&
        mp_enemy_bind_index(victim, &index) && index < MP_WIRE_KEY_COUNT &&
        memory_try_read_u32(hit.code_cell, &code) &&
        memory_try_read_u32(hit.impact_cell, &impact)) {
        /* The two channels and the attacker's armed node: what the fork downstream reads and
         * what a host looking at its own puppet a moment later cannot tell any more. */
        if (hit.a_cell != 0 && memory_try_read_u32(hit.a_cell, &a) && a != 0u) {
            flags |= (uint8_t)MP_HIT_FLAG_A;
        }
        if (hit.b_cell != 0 && memory_try_read_u32(hit.b_cell, &b) && b != 0u) {
            flags |= (uint8_t)MP_HIT_FLAG_B;
        }
        if (memory_try_read_u32(hit.self_cell, &self) && self != 0u &&
            memory_try_read_u32((uintptr_t)self + BODY_CONTACT_NODE, &node) && node != 0u) {
            flags |= (uint8_t)MP_HIT_FLAG_ARMED;
        }
        hit_note.victim = (uint16_t)index;
        hit_note.code   = (uint8_t)(code & 0xFFu);
        hit_note.impact = (uint8_t)(impact & 0xFFu);
        hit_note.flags  = flags;
        hit_note.node   = mp_node_map_take(body_of_actor(victim),
                                           contact_carries_a_node(flags));
        if (hit.send != NULL &&
            mp_hit_note_encode(&hit_note, note, sizeof note) == MP_HIT_RELAY_BYTES &&
            hit.send(note, sizeof note)) {
            ++hit.reported;
        } else {
            ++hit.unreported;
        }
    }

    /* The handler is still called. For a parked victim it turns back at its own first gate,
     * `cmp dword [victim+0x20],3`, so this costs nothing and keeps every other contact behaving
     * exactly as it always did. That gate is also why the previous form of this file, which
     * detoured the handler to SUPPRESS those contacts, did nothing at all: it repeated the
     * engine's own test six bytes early. */
    return hit.original();
}

/* ==============================================================================================
 * The host's half: set the six globals and let the engine do the rest.
 * ============================================================================================ */

/* The far body of bank `bank` on this machine, which is who a reported hit came from: the bank of
 * the peer that reported it. */
static bool far_body_of(size_t bank, uint32_t *out)
{
    *out = 0u;
    if (!mp_body_exists_at(bank)) {
        return false;
    }
    return mp_bank_read_at(bank, SECOND_BODY_OBJECT, out, sizeof *out) && *out != 0u;
}

/* The seventh cell, and the reason it is written before every replay.
 *
 * The pair pass publishes six globals through post_contact and writes the node a blade struck into
 * a seventh one just before it. Nothing clears that seventh between contacts, so a replay that
 * sets six of them runs the engine's receiver with whatever the last real contact on this machine
 * struck. Two readers act on it: the receiver spawns the impact effect at that node for melee code
 * 0x21, and the dismemberment mod, which ships in the same build, picks the limb it severs from
 * it. A client's kill would take a limb off a piece nobody hit.
 *
 * What a replay knows about the node today is nothing, so it says nothing. When the note carries
 * the node this becomes a write of that value instead. */
/* A touch with no damage, for a pickup claim: the claimant's own far body touches the pickup. It
 * used to be the first far body whoever claimed, which is the host's view of the first client. */
bool mp_hit_relay_perform(size_t bank, uint32_t key, uint8_t code_byte)
{
    return mp_hit_relay_perform_full(bank, key, code_byte, (uint8_t)0u, (uint8_t)0u,
                                     (uint8_t)MP_NODE_ID_NONE);
}

/* The replay runs from the drain, in the task half, while the engine's own dispatches happen in
 * the collision pass at the end of a substep, so the two never overlap; the replaying flag is
 * for the other direction, and stops a host's own replay from being seen as a fresh contact
 * worth reporting, which on a listen host would be a loop.
 *
 * Every global the handler reads, and the one field on the attacker that two of its arms test.
 * The node is written only when the client says its attacker was armed and the puppet's own
 * body is not: the puppet swings on the host too, and a swing in progress there is the better
 * answer than anything written here. Whatever is written is put back afterwards. */
bool mp_hit_relay_perform_full(size_t bank, uint32_t key, uint8_t code_byte, uint8_t impact_byte,
                               uint8_t flags, uint8_t node_id)
{
    uintptr_t victim;
    uint32_t  attacker = 0;
    uint32_t  body     = 0;
    uint32_t  impact   = 0;
    uint32_t  code     = code_byte;
    uint32_t  a        = 0;
    uint32_t  b        = 0;
    uint32_t  node     = 0;
    bool      armed    = false;
    bool      performed = false;

    if (!hit.installed || hit.is_client) {
        return false;
    }
    victim = mp_enemy_sync_actor_for(key);
    if (victim == 0 || !memory_read_u32(victim + ACTOR_BODY, &body) || body == 0u) {
        ++hit.no_victim;
        return false;
    }
    if (!far_body_of(bank, &attacker) || attacker == 0u ||
        !memory_read_u32((uintptr_t)attacker + BODY_IMPACT, &impact)) {
        ++hit.no_attacker;
        return false;   /* without an attacker there is no body to answer for the hit */
    }
    if (impact_byte != 0u) {
        impact = impact_byte;   /* what the client saw, which is the only honest damage */
    }

    /* All six, and the sixth is the one that is easy to forget: it is stamped from the sender
     * rather than passed, so leaving it holds whatever the last contact on this machine dealt. */
    a = (flags & MP_HIT_FLAG_A) != 0u ? 1u : 0u;
    b = (flags & MP_HIT_FLAG_B) != 0u ? 1u : 0u;

    /* Arm the blade for the length of the call when the client says its own was armed and this
     * side's puppet is between swings. The node is the puppet's own sabre node out of its bank,
     * so the hit lands on the piece the engine would have chosen; a bank that cannot answer
     * leaves a 1, which is a node and passes the two tests that are all this decides. */
    if ((flags & MP_HIT_FLAG_ARMED) != 0u &&
        memory_read_u32((uintptr_t)attacker + BODY_CONTACT_NODE, &node) && node == 0u) {
        uint32_t sabre = 0;

        if (!mp_bank_read_at(bank, RECORD_SABRE_NODE, &sabre, sizeof sabre) || sabre == 0u) {
            sabre = 1u;
        }
        armed = memory_try_write((uintptr_t)attacker + BODY_CONTACT_NODE, &sabre,
                                 sizeof sabre);
        if (armed) {
            ++hit.armed_for_the_call;
        }
    }

    mp_node_map_put(body, node_id);
    hit.replaying = true;
    if (memory_try_write(hit.self_cell, &attacker, sizeof attacker) &&
        memory_try_write(hit.other_cell, &body, sizeof body) &&
        memory_try_write(hit.code_cell, &code, sizeof code) &&
        memory_try_write(hit.impact_cell, &impact, sizeof impact) &&
        (hit.a_cell == 0 || memory_try_write(hit.a_cell, &a, sizeof a)) &&
        (hit.b_cell == 0 || memory_try_write(hit.b_cell, &b, sizeof b))) {
        (void)hit.original();
        ++hit.performed;
        performed = true;
    }
    hit.replaying = false;
    if (armed) {
        (void)memory_try_write((uintptr_t)attacker + BODY_CONTACT_NODE, &node, sizeof node);
    }
    return performed;
}

void mp_hit_relay_note_own_shot(uint32_t object)
{
    mp_hit_shot_owners_note(&hit.shot_owners, object, false);
}

void mp_hit_relay_note_far_shot(uint32_t object)
{
    mp_hit_shot_owners_note(&hit.shot_owners, object, true);
}

void mp_hit_relay_note_far_shot_of(uint32_t object, size_t bank)
{
    mp_hit_shot_owners_note_far_bank(&hit.shot_owners, object,
                                     bank != 0u && bank <= MP_BANK_FAR_MAX
                                         ? (uint8_t)bank
                                         : (uint8_t)MP_HIT_SHOT_NO_BANK);
}

bool mp_hit_relay_far_shot_bank(uint32_t object, size_t *bank)
{
    uint8_t named = 0;

    if (!mp_hit_shot_owners_far_bank(&hit.shot_owners, object, &named)) {
        return false;
    }
    if (bank != NULL) {
        *bank = (size_t)named;
    }
    return true;
}

void mp_hit_relay_note_ally_shot(uint32_t object)
{
    mp_hit_shot_owners_note_ally(&hit.shot_owners, object);
}

bool mp_hit_relay_is_a_shot(uint32_t object)
{
    uint32_t owner = 0;
    uint32_t back  = 0;

    return object != 0u && memory_try_read_u32((uintptr_t)object + BODY_OWNER, &owner) &&
           owner != 0u && memory_try_read_u32((uintptr_t)owner + SHOT_BODY, &back) &&
           back == object;
}

/* Whether the contact's sender fights on the player's side without being a player: an ally's
 * bolt, or an ally's own body. Such a contact never reaches the local player in single player,
 * because the engine's pair filter keeps a class from hitting its own class; a far player's
 * puppet is of a class of its own and would be hit, and only this keeps the ally off it. */
static bool sent_by_an_ally(uint32_t self, uintptr_t owner)
{
    uint32_t side = 0;

    if (mp_hit_relay_is_a_shot(self) && mp_hit_shot_owners_is_ally(&hit.shot_owners, self) &&
        memory_try_read_u32((uintptr_t)self + OBJECT_SIDE, &side) && side == 1u) {
        ++hit.ally_bolt_hits;
        return true;
    }
    if (owner != 0u && memory_try_read_u32(owner + ACTOR_CLASS, &side) && side == 1u) {
        ++hit.ally_body_hits;
        return true;
    }
    return false;
}

bool mp_hit_relay_sender_is_an_ally(void)
{
    uint32_t self = 0;

    return hit.self_cell != 0 && memory_try_read_u32(hit.self_cell, &self) && self != 0u &&
           sent_by_an_ally(self, actor_owner_of(self));
}

void mp_hit_relay_set_slot(uint32_t slot)
{
    hit.slot = slot;
}

/* ==============================================================================================
 * The other crossing: what the host drops for a far player, performed where that player is.
 * ============================================================================================ */

void mp_hit_relay_note_puppet_hit(size_t bank)
{
    uint8_t  note[MP_PLAYER_HIT_BYTES];
    mp_player_hit_note_t mirror;
    uint32_t self = 0;
    uint32_t code = 0;
    uint32_t impact = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t owner = 0;
    uint32_t index = 0;
    uint32_t node = 0;
    uint32_t victim_body = 0;
    uint8_t  flags = 0;
    uint16_t who = (uint16_t)MP_PLAYER_HIT_NO_ATTACKER;
    uint8_t  slot = 0;

    if (!hit.installed || hit.is_client || bank == 0u) {
        return;
    }
    /* Addressed to the world slot the bank shows, out of the table the drain keeps. It used to
     * be the bank's own number, which is the slot on a listen host only because the seats
     * happen to be numbered alike there. */
    if (!mp_body_bank_slot(bank, &slot)) {
        ++hit.mirror_no_slot;
        return;
    }
    if (!memory_try_read_u32(hit.code_cell, &code) ||
        !memory_try_read_u32(hit.impact_cell, &impact)) {
        return;
    }

    /* The attacker travels as its PLACEMENT, which both machines have, and the far side finds
     * its own replica of it. Anything without one, an explosion or the level's own push, is
     * sent with no attacker rather than not sent: the damage is the impact code and most arms
     * of the handler read the attacker only for the direction of the shove.
     *
     * The owner is asked to name the body back before its placement index is read, because the
     * index and a projectile's lifetime share an offset and a projectile with a lifetime of zero
     * would otherwise be reported as placement zero of the level. */
    if (memory_try_read_u32(hit.self_cell, &self) && self != 0u) {
        /* A bolt the clients were told about: that player's machine fires the copy and sees
         * it hit or miss the body it shows, and a report from here would hurt twice. */
        if (hit.npc_owned != NULL && hit.npc_owned(self)) {
            return;
        }
        owner = (uint32_t)actor_owner_of(self);
        if (sent_by_an_ally(self, (uintptr_t)owner)) {
            return;
        }
        if (owner != 0u && mp_enemy_bind_index((uintptr_t)owner, &index) &&
            index < MP_WIRE_KEY_COUNT) {
            who = (uint16_t)index;
        }
    }
    if (hit.a_cell != 0 && memory_try_read_u32(hit.a_cell, &a) && a != 0u) {
        flags |= (uint8_t)MP_HIT_FLAG_A;
    }
    if (hit.b_cell != 0 && memory_try_read_u32(hit.b_cell, &b) && b != 0u) {
        flags |= (uint8_t)MP_HIT_FLAG_B;
    }
    if (self != 0u && memory_try_read_u32((uintptr_t)self + BODY_CONTACT_NODE, &node) &&
        node != 0u) {
        flags |= (uint8_t)MP_HIT_FLAG_ARMED;
    }

    mirror.slot     = slot;
    mirror.attacker = who;
    mirror.code     = (uint8_t)(code & 0xFFu);
    mirror.impact   = (uint8_t)(impact & 0xFFu);
    mirror.flags    = flags;
    /* The slot indexes the puppet's own model, which on the other machine may be another rig
     * with the same joints in another order. Hence a name and not that number. */
    (void)memory_try_read_u32(hit.other_cell, &victim_body);
    mirror.node     = mp_node_map_take(victim_body, contact_carries_a_node(flags));
    if (mp_player_hit_encode(&mirror, note, sizeof note) != MP_PLAYER_HIT_BYTES) {
        ++hit.mirror_unsent;
        return;
    }
    /* To the victim's machine alone: every other client used to receive it and throw it away as
     * meant for another slot, two copies in three with four players. */
    if (hit.send_to_slot != NULL) {
        mp_hit_relay_addressed_t sent = hit.send_to_slot(slot, note, sizeof note);

        hit.to_victim  += (sent == MP_HIT_ADDRESSED_SENT) ? 1u : 0u;
        hit.to_no_peer += (sent == MP_HIT_ADDRESSED_NO_PEER) ? 1u : 0u;
        if (sent == MP_HIT_ADDRESSED_SENT) {
            ++hit.mirrored;
            mp_enemy_sync_note_struck(who, slot);   /* ranks the attacker up for that peer */
        } else {
            ++hit.mirror_unsent;
        }
        return;
    }
    if (hit.send != NULL && hit.send(note, sizeof note)) {
        ++hit.mirrored;
        ++hit.to_everybody;
    } else {
        ++hit.mirror_unsent;
    }
}

/* The client's half of the mirror: the six globals, this player as the receiver, delivered to his
 * node the way the engine delivers a contact, so health, pain, pickups and death are the engine's
 * and a corpse's empty slot refuses it, which then counts as not performed.
 *
 * The attacker is looked up as this machine's own replica of the placement the host named,
 * and its liveness is asked of the placement rather than of a remembered pointer, for the
 * same reason the removal is. */
static bool apply_player_hit(const mp_player_hit_note_t *note)
{
    uintptr_t player_block;
    uint32_t  player_body = 0;
    uint32_t  attacker = 0;
    uint32_t  code = note->code;
    uint32_t  impact = note->impact;
    uint32_t  a = (note->flags & MP_HIT_FLAG_A) != 0u ? 1u : 0u;
    uint32_t  b = (note->flags & MP_HIT_FLAG_B) != 0u ? 1u : 0u;
    bool      performed;

    player_block = mp_cells_address(MP_CELL_HERO_BLOCK);
    if (player_block == 0 ||
        !memory_read_u32(player_block + SECOND_BODY_OBJECT, &player_body) ||
        player_body == 0u) {
        return false;
    }
    if (note->attacker != (uint16_t)MP_PLAYER_HIT_NO_ATTACKER) {
        uintptr_t actor = mp_enemy_sync_replica_for(note->attacker);

        if (actor != 0 && mp_enemy_spawn_actor_is_live(actor, note->attacker)) {
            (void)memory_read_u32(actor + ACTOR_BODY, &attacker);
        }
    }
    if (attacker == 0u) {
        ++hit.mirror_no_attacker;
    }

    mp_node_map_put(player_body, note->node);
    hit.replaying = true;
    performed = memory_try_write(hit.self_cell, &attacker, sizeof attacker) &&
                memory_try_write(hit.other_cell, &player_body, sizeof player_body) &&
                memory_try_write(hit.code_cell, &code, sizeof code) &&
                memory_try_write(hit.impact_cell, &impact, sizeof impact) &&
                (hit.a_cell == 0 || memory_try_write(hit.a_cell, &a, sizeof a)) &&
                (hit.b_cell == 0 || memory_try_write(hit.b_cell, &b, sizeof b)) &&
                mp_body_run_engine_contact();
    hit.replaying = false;
    if (performed) {
        ++hit.mirror_applied;
    }
    return performed;
}

/* ==============================================================================================
 * The three messages on this channel. The death is recognised here and taken in its own file.
 * ============================================================================================ */

bool mp_hit_relay_take_message(size_t bank, const uint8_t *note, size_t bytes)
{
    if (mp_death_is(note, bytes)) {
        mp_hit_relay_take_death(note, bytes, hit.slot);
        return true;
    }
    if (mp_player_hit_is(note, bytes)) {
        mp_player_hit_note_t mirror;

        if (!hit.installed || !hit.is_client) {
            return true;   /* ours by tag; a host has nothing to perform */
        }
        if (!mp_player_hit_decode(note, bytes, &mirror)) {
            ++hit.torn;
            return true;
        }
        if (mirror.slot != (uint8_t)hit.slot) {
            ++hit.mirror_not_mine;
            return true;
        }
        (void)apply_player_hit(&mirror);
        return true;
    }
    if (!mp_hit_note_is(note, bytes)) {
        return false;
    }
    if (!hit.installed || hit.is_client) {
        return true;   /* ours by tag; a client has nothing to perform */
    }
    {
        mp_hit_note_t          report;
        mp_trust_hit_verdict_t verdict;

        if (!mp_hit_note_decode(note, bytes, &report)) {
            ++hit.torn;
            return true;
        }
        /* A deathmatch performs no client's hit on what the arena left, because the impact code
         * is the damage and the client chose it; a copy an editor spawned is the one exception.
         * And no game performs a wave's report: the puppet fires that wave here itself. */
        verdict = mp_trust_host_performs_hit(report.victim, report.code);
        if (verdict == MP_TRUST_HIT_DEATHMATCH) {
            ++hit.refused_checked;
            return true;
        }
        if (verdict == MP_TRUST_HIT_HOST_WAVE) {
            mp_knockback_note_refused(report.victim, bank);
            return true;
        }
        (void)mp_hit_relay_perform_full(bank, report.victim, report.code, report.impact,
                                        report.flags, report.node);
    }
    return true;
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

bool mp_hit_relay_install(void)
{
    uintptr_t target;

    if (hit.installed) {
        return true;
    }
    /* The six cells come out of the operands of the entry that publishes a contact, at 00414C99,
     * which stores its five arguments and then stamps the sixth from the sender's own object:
     *
     *     00414C9C  mov eax,[ebp+0x08]; mov [g_msgSelf],eax       operand +0x07
     *     00414CA4  mov ecx,[ebp+0x0c]; mov [g_msgOther],ecx      operand +0x10
     *     00414CAD  mov edx,[ebp+0x10]; mov [g_msgCode],edx       operand +0x19
     *     00414CB6  mov eax,[ebp+0x14]; mov [g_msgA],eax          operand +0x21
     *     00414CBE  mov ecx,[ebp+0x18]; mov [g_msgB],ecx          operand +0x2A
     *     00414CC7  mov edx,[ebp+0x08]; mov eax,[edx+0x0c]; mov [g_msgImpact],eax   +0x35
     *
     * and then runs the receiver's handler task. The pattern was 46 bytes and stopped one store
     * short of the sixth; it is 57 now, and all six operands resolve on every shipped image, at
     * 00869240 through 00869254 in the retail links. */
    hit.self_cell   = mp_cells_address(MP_CELL_MSG_SELF);
    hit.other_cell  = mp_cells_address(MP_CELL_MSG_OTHER);
    hit.code_cell   = mp_cells_address(MP_CELL_MSG_CODE);
    hit.impact_cell = mp_cells_address(MP_CELL_MSG_IMPACT);
    hit.a_cell      = mp_cells_address(MP_CELL_MSG_A);
    hit.b_cell      = mp_cells_address(MP_CELL_MSG_B);
    hit.node_cell   = mp_cells_address(MP_CELL_MSG_CONTACT_NODE);
    mp_node_map_bind(hit.node_cell);
    if (hit.node_cell == 0u) {
        log_warning("the node a blade struck did not resolve, so a replayed hit leaves that cell "
                    "as the last contact on this machine set it: the impact effect and the limb "
                    "the dismemberment mod severs can then belong to another piece");
    }
    if (hit.self_cell == 0 || hit.other_cell == 0 || hit.code_cell == 0 || hit.impact_cell == 0) {
        log_warning("a contact global did not resolve, so a client's hits cannot be performed by "
                    "the host and its enemies stay invulnerable to it");
        return false;
    }
    target = mp_signatures_address(MP_SITE_ENEMY_ON_CONTACT);
    if (target == 0) {
        log_warning("the enemy contact handler did not resolve, so a client's hits go nowhere");
        return false;
    }
    if (!detour_install(&hit.detour, target, (const void *)&hook_on_contact,
                        ON_CONTACT_PROLOGUE)) {
        log_error("the enemy contact handler at %08X refused the detour", (unsigned)target);
        return false;
    }
    hit.original  = (on_contact_fn_t)hit.detour.original;
    hit.installed = true;
    log_info("the hit relay is bound at %08X; a client reports what it hits and the host performs "
             "it through the engine's own contact path", (unsigned)target);
    return true;
}

bool mp_hit_relay_installed(void)
{
    return hit.installed;
}

void mp_hit_relay_set_client(bool is_client)
{
    hit.is_client = is_client;
}

void mp_hit_relay_set_npc_owned(mp_hit_relay_npc_owned_fn_t test)
{
    hit.npc_owned = test;
}

void mp_hit_relay_set_send(mp_hit_relay_send_fn_t send)
{
    hit.send = send;
    mp_hit_relay_death_set_send(send);
}

void mp_hit_relay_set_send_to_slot(mp_hit_relay_send_to_slot_fn_t send)
{
    hit.send_to_slot = send;
}

void mp_hit_relay_report(void)
{
    uint32_t cell_cleared = 0;
    uint32_t cell_discarded = 0;
    uint32_t cell_written = 0;
    uint32_t cell_faults = 0;

    if (!hit.installed) {
        log_info("the hit relay is not bound, so a client cannot damage anything the host owns");
        return;
    }
    mp_node_map_cell_counters(&cell_cleared, &cell_discarded, &cell_written, &cell_faults);
    log_info("the hit relay (%s): %u hit(s) reported, %u dropped with nowhere to send them, "
             "%u performed here, %u refused in a deathmatch | %u with no victim, %u with no "
             "attacker, %u blade(s) armed for the call, %u replay(s) said no node was struck "
             "(%u write fault(s), %u of them threw away a node the pair pass had written, "
             "%u ran with the node the sender named)",
             hit.is_client ? "client" : "host", (unsigned)hit.reported,
             (unsigned)hit.unreported, (unsigned)hit.performed, (unsigned)hit.refused_checked,
             (unsigned)hit.no_victim, (unsigned)hit.no_attacker,
             (unsigned)hit.armed_for_the_call, (unsigned)cell_cleared,
             (unsigned)cell_faults, (unsigned)cell_discarded,
             (unsigned)cell_written);
    {
        uint32_t named = 0, no_node = 0, unnamed = 0, resolved = 0, dropped = 0;

        mp_node_map_counters(&named, &no_node, &unnamed, &resolved, &dropped);
        /* Its own sentence: `unnamed` over zero means the table wants its census run again,
         * `dropped` over zero is a swapped rig and not a fault. */
        log_info("  the struck node on the wire: %u named for sending, %u contact(s) "
                 "carried none, %u could not be named here; %u resolved on arrival, "
                 "%u dropped for a model without that node",
                 (unsigned)named, (unsigned)no_node, (unsigned)unnamed,
                 (unsigned)resolved, (unsigned)dropped);
    }
    log_info("  contacts on actors the host owns: %u from this player's armed body and %u from "
             "its shots, reported; %u unarmed touches of its body, %u from the far players' shots, "
             "%u from copies of the host's bolts and %u from anything else, not reported",
             (unsigned)hit.by_sender[MP_HIT_ATTACKER_OWN_BODY],
             (unsigned)hit.by_sender[MP_HIT_ATTACKER_OWN_SHOT],
             (unsigned)hit.by_sender[MP_HIT_ATTACKER_OWN_BUMP],
             (unsigned)hit.by_sender[MP_HIT_ATTACKER_FAR_SHOT],
             (unsigned)hit.by_sender[MP_HIT_ATTACKER_NPC_BOLT],
             (unsigned)hit.by_sender[MP_HIT_ATTACKER_OTHER]);
    log_info("  hits on a far player: %u sent from here, %u unsent, %u for a bank with no slot, "
             "%u performed on this machine's own body, %u for another slot, %u with no "
             "replica of the attacker",
             (unsigned)hit.mirrored, (unsigned)hit.mirror_unsent, (unsigned)hit.mirror_no_slot,
             (unsigned)hit.mirror_applied, (unsigned)hit.mirror_not_mine,
             (unsigned)hit.mirror_no_attacker);
    log_info("  hits on a far player by an ally, not sent: %u by its bolts, %u by its own body",
             (unsigned)hit.ally_bolt_hits, (unsigned)hit.ally_body_hits);
    log_info("  hits on a far player, how they were addressed: %u sent to the victim's own peer, "
             "%u with no peer for the slot, %u to everybody (must be 0)",
             (unsigned)hit.to_victim, (unsigned)hit.to_no_peer, (unsigned)hit.to_everybody);
    log_info("  hit messages that did not decode: %u (a key that names no enemy, or a flag this "
             "build does not know)", (unsigned)hit.torn);
    log_info("  contact owners that were not a placement: %u projectile(s), %u of no known kind; "
             "each of those would have been reported as an attacker by its own float bits",
             (unsigned)hit.owner_is_a_shot, (unsigned)hit.owner_unknown);
    mp_hit_relay_death_report();
    mp_knockback_report();
}

