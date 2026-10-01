/* mp_effects.c: the spark, the flash and the impact voice a replicated blade owes its own body.
 *
 * The decision and the performance are separate on purpose, and the reason is not tidiness. The
 * contact arrives at bank 0, where the engine's player record names the LOCAL player. The impact
 * voice reads and writes a cooldown inside that record, so calling it there would take the local
 * player's voice away and leave the puppet silent, which is a worse bug than the one this module
 * exists to fix. So the contact is judged where it arrives and played where the puppet's own
 * record is installed, one substep later at the most. The impact voice opens with
 * `A1 20 52 4B 00  mov eax,[pr]` and `D9 40 34  fld dword [eax+0x34]` and writes 0x3E4CCCCD,
 * 0.2 s, back into that field; at bank 0 the field is the local player's, who would go quiet for
 * a fifth of a second every time he was struck. The hold costs one substep at the most, 31 ms.
 *
 * The arm this module stands in for is the third of the armed contact handler at 0044855C. With
 * the sabre attack descriptor hung on the record or midair attack phase 1, and the melee
 * cooldown at pr+0x2BC run out, it reseeds that cooldown to 0.3 s, plays voice group 2 for codes
 * 2 and 3 or 3 for 0x21 and 0x25, reads the sabre node's sphere, flashes and sparks at its
 * centre and knocks the actor back by 1.0. This module makes the three calls and the sphere
 * read, and neither of the two writes: not the cooldown, which is a field of a record fed from
 * the wire, and not the knockback, which would move a body the far machine places.
 *
 * Two departures from the engine's own arm, both deliberate:
 *
 * The engine reseeds a melee cooldown inside the player record. A puppet's record is fed from the
 * wire and is not this module's to write, so the same span is counted here in substeps instead.
 * Nothing of the engine's is written by this file at all.
 *
 * The engine flashes and sparks for every contact code and only picks the voice by code. This
 * module answers nothing for a code it cannot name, because the codes it can name cover every
 * case a blade can produce and a flash at a point nobody chose is worse than no flash.
 *
 * SIZE NOTE: between 600 and 700 lines, and it holds TWO reasons to play rather than one,
 * which is the seam if it grows again. The blade half answers the message a puppet's own
 * weapon sends; the hurt half answers the one its body receives. They share the peer table,
 * the resolved engine entries and the window, and nothing else: two pending slots, two
 * nodes, two cooldowns and two sets of counters, because the engine rations them by two
 * different numbers and places them at two different joints. Splitting them would copy the
 * peer table and the resolve; keeping them makes the file long. A third reason to play is
 * the line where that trade stops holding.
 */
#include "mp_effects.h"

#include "mp_bank.h"
#include "mp_body_wear.h"
#include "mp_cells.h"
#include "mp_node_map.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The player record's sabre node, the node the blade hangs off and the point every impact effect
 * of an armed contact is placed at. The same offset the puppet's sabre module reads, and the one
 * the engine's armed contact arm hands to the node sphere call. */
#define RECORD_SABRE_NODE 0x4Cu

/* The body object's blade contact code. Zero means the contact sphere is off, so a non-zero value
 * is the engine's own answer to "is this body's weapon armed right now". It stands in for the
 * mode descriptor test the engine's arm makes, which a puppet by design cannot pass. The offset
 * is the one the engine's own active sphere test switches on: zero there means no sphere. */
#define OBJECT_CONTACT_CODE 0xACu

/* The object's world position, and the record cell the swept blade test carries its previous
 * sample in. The engine refreshes that cell itself at the end of every call, hit or miss; what
 * this module does is decide when it may not be trusted. */
#define OBJECT_POSITION        0x18u
#define RECORD_SWING_CONTACT_NODE 0x58u
#define RECORD_SWING_NODE_PREV    0x1ACu

/* How far the puppet may be placed in one substep and still be walking rather than teleporting.
 * A player on foot covers well under half a unit in a thirty-second of a second, so two units is
 * far past anything a swing can cross while it runs, and well under the distance a resync or a
 * respawn moves. Past it the swept blade would draw a line through the level and strike whatever
 * it crossed. */
#define SWEEP_JUMP_UNITS 2.0f

typedef void(__cdecl *play_voice_fn_t)(int32_t group);
typedef void(__cdecl *engine_fn_t)(void);
typedef void(__cdecl *play_name_fn_t)(const char *wav, uint32_t flags);
typedef void(__cdecl *spawn_at_fn_t)(const char *name, const float at[3], int32_t builtin);
typedef void(__cdecl *effect_at_fn_t)(const float at[3]);
typedef float(__cdecl *node_sphere_fn_t)(void *object, uint32_t node, float out_centre[3]);

/* One far body's blade against the world: the verdict its own contacts are waiting to have
 * played, its cooldown, and where its sweep stood a substep ago. Each far bank shows its own
 * player; one record for all of them would play one player's spark at another's blade and
 * sweep a line from one body to the next. */
typedef struct effects_peer {
    bool     hurt_pending;   /* a damage contact is waiting for this bank's window */
    uint32_t hurt_code;      /* the contact code it arrived with */
    uint32_t hurt_node;      /* the struck node, as a raw slot on this machine's own body */
    uint32_t since_hurt;     /* substeps since the last pain voice, for the engine's 0.25 s */
    bool     sweeping;       /* the blade was armed in the previous substep as well */
    bool     have_last_pos;
    float    last_pos[3];    /* where the puppet stood then, to catch a placement jump */
    bool     pending;        /* a verdict waiting for the next window of this bank */
    int32_t  pending_group;
    uint32_t since_effect;   /* substeps since the last set was played on this body */
} effects_peer_t;

typedef struct mp_effects_state {
    play_voice_fn_t  play_voice;
    play_name_fn_t   play_name;   /* the pain voice: one named wav, no place of its own */
    spawn_at_fn_t    spawn_at;    /* a builtin particle template at a point */
    uintptr_t        name_cell;   /* the cell that names the wav a hurt player plays */
    uintptr_t        code_cell;   /* the contact code of the message being published */
    effect_at_fn_t   flash_at;
    effect_at_fn_t   spark_at;
    node_sphere_fn_t node_sphere;
    engine_fn_t      test_swing_world;

    effects_peer_t   peer[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */
    uint32_t         sweeps;
    uint32_t         sweeps_worn;     /* of them, on a body in a borrowed model */
    uint32_t         sweeps_unprobed; /* withheld: the contact node answered no sphere */
    uint32_t         sweeps_seeded;

    uint32_t contacts;
    uint32_t blade_contacts;
    uint32_t played;
    uint32_t suppressed;
    uint32_t without_voice;   /* played, but the code named no voice group */
    uint32_t body_messages;   /* offered with the b flag clear: the victim's message, not ours */
    uint32_t not_armed;       /* offered with b set while the blade carried no contact code */

    uint32_t hurt_offered;    /* damage contacts on a far body, all codes */
    uint32_t hurt_in_band;    /* of them, the ones the engine's receiver would have taken */
    uint32_t hurt_voices;
    uint32_t hurt_swallowed;  /* inside the engine's own 0.25 s drop timer */
    uint32_t hurt_blood;
    uint32_t hurt_no_node;    /* melee, but the cell named no node */
    uint32_t hurt_no_sphere;  /* the node has no mesh, so it has no place */
    uint32_t hurt_refused;    /* a site or the cell did not resolve */
    uint32_t refused;

    bool missing_logged;
    bool refusal_logged;
} mp_effects_state_t;

static mp_effects_state_t effects;

/* One far body's blade effects by the bank that shows it. An index that is no far bank lands on the
 * spare at 0, which is what a caller outside every window, a unit test, gets. */
static effects_peer_t *peer_of(size_t bank)
{
    return &effects.peer[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

/* ==============================================================================================
 * The decision. Nothing above this banner's end touches the engine.
 * ============================================================================================ */

static bool code_is_blade(uint32_t contact_code)
{
    return contact_code == MP_EFFECTS_CODE_BLADE_PLAIN ||
           contact_code == MP_EFFECTS_CODE_BLADE_JEDI;
}

static bool code_is_body(uint32_t contact_code)
{
    return contact_code >= MP_EFFECTS_CODE_BODY_FIRST &&
           contact_code <= MP_EFFECTS_CODE_BODY_LAST;
}

mp_effects_verdict_t mp_effects_decide(uint32_t contact_code, uint32_t message_b, bool armed,
                                       uint32_t substeps_since_effect, int32_t *voice_group)
{
    int32_t group;

    /* The b flag is the whole fork. With it clear this is the message the victim's handler
     * answers with damage, and a body standing next to another body produces a stream of them
     * that has nothing to do with a blade. */
    if (message_b == 0u || !armed) {
        return MP_EFFECTS_NOTHING;
    }

    /* Every code that gets this far is played. The engine's own arm lights the flash and the
     * spark before it looks at the code at all, and looks at it only to pick a voice; a code that
     * names none leaves the hit silent rather than unlit. */
    if (code_is_blade(contact_code)) {
        group = MP_EFFECTS_VOICE_BLADE;
    } else if (code_is_body(contact_code)) {
        group = MP_EFFECTS_VOICE_BODY;
    } else {
        group = MP_EFFECTS_VOICE_NONE;
    }

    if (substeps_since_effect < MP_EFFECTS_COOLDOWN_SUBSTEPS) {
        return MP_EFFECTS_LOCKED;
    }

    if (voice_group != NULL) {
        *voice_group = group;
    }
    return MP_EFFECTS_DUE;
}

mp_effects_sweep_t mp_effects_sweep_verdict(bool worn, bool weapon)
{
    if (!worn) {
        return MP_EFFECTS_SWEEP_RUN;
    }
    return weapon ? MP_EFFECTS_SWEEP_PROBE : MP_EFFECTS_SWEEP_NONE;
}

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

static void note_refusal(const char *why)
{
    ++effects.refused;
    if (!effects.refusal_logged) {
        effects.refusal_logged = true;
        log_warning("an impact effect for the puppet was refused: %s; later ones are counted, "
                    "not logged", why);
    }
}

/* The four effect sites are called and none is detoured here, but each is registered as a detour
 * target so that it survives another module's branch through the resolver's second stage. For
 * the node sphere that is load bearing today: the dev overlay's borrowed weapon draw detours it
 * with a prologue of 6, the same boundary declared here, so with that DLL loaded first the site
 * resolves on its tail. Every pattern matches exactly once, whole and tail only, on all six
 * shipped images; the recompile moves three of them (impact voice 00450E98 to 00450E38, flash
 * 00450EED to 00450E8D, spark 00450F00 to 00450EA0) and leaves the node sphere at 00414231.
 *
 * The flash, nineteen bytes whole, is the shortest function in either table: a pattern past its
 * `C3` would carry the head of the spark as a required byte, so it stops there, with a prologue
 * of six and a thirteen byte tail that is unique on all six images too. Two byte windows inside
 * these patterns read as data band addresses without being operands, `08 50 6A 00` at flash
 * +0x05 and `11 52 6A 00` at spark +0x17, and in each the zero of the last `push 0` is masked
 * rather than argued about. The node sphere holds no operand and carries no mask; its pattern
 * ends on the assert line push `68 79 0A 00 00`, in front of the two string addresses behind
 * it. */
void mp_effects_resolve(void)
{
    size_t bank;

    effects.play_voice  = (play_voice_fn_t)mp_signatures_address(MP_SITE_PLR_PLAY_IMPACT_VOICE);
    effects.flash_at    = (effect_at_fn_t)mp_signatures_address(MP_SITE_PLR_FLASH_AT);
    effects.spark_at    = (effect_at_fn_t)mp_signatures_address(MP_SITE_PLR_SPARK_AT);
    effects.node_sphere = (node_sphere_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_NODE_SPHERE);
    effects.test_swing_world =
        (engine_fn_t)mp_signatures_address(MP_SITE_PLR_TEST_SWING_WORLD);
    effects.play_name = (play_name_fn_t)mp_signatures_address(MP_SITE_BAPSOUND_PLAY_NAME);
    effects.spawn_at  = (spawn_at_fn_t)mp_signatures_address(MP_SITE_EMITTER_SPAWN_AT);
    effects.name_cell = mp_cells_address(MP_CELL_HURT_VOICE_NAME);
    effects.code_cell = mp_cells_address(MP_CELL_MSG_CODE);
    if (effects.play_name == NULL || effects.spawn_at == NULL || effects.name_cell == 0u) {
        log_warning("a hurt far body will make no sound and draw no blood: named sound %s, "
                    "particle at a point %s, the wav's own cell %s",
                    effects.play_name != NULL ? "ok" : "MISSING",
                    effects.spawn_at != NULL ? "ok" : "MISSING",
                    effects.name_cell != 0u ? "ok" : "MISSING");
    }
    for (bank = 0u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_effects_reset(bank);
    }

    if (effects.play_voice == NULL || effects.flash_at == NULL || effects.spark_at == NULL ||
        effects.node_sphere == NULL || effects.test_swing_world == NULL) {
        if (!effects.missing_logged) {
            effects.missing_logged = true;
            log_warning("the impact effects of a replicated blade are off: impact voice %s, "
                        "flash %s, spark %s, node sphere %s, swept blade %s",
                        effects.play_voice != NULL ? "ok" : "MISSING",
                        effects.flash_at != NULL ? "ok" : "MISSING",
                        effects.spark_at != NULL ? "ok" : "MISSING",
                        effects.node_sphere != NULL ? "ok" : "MISSING",
                        effects.test_swing_world != NULL ? "ok" : "MISSING");
        }
    }
}

void mp_effects_reset(size_t bank)
{
    effects_peer_t *p = peer_of(bank);

    p->pending       = false;
    p->pending_group = MP_EFFECTS_VOICE_BODY;

    /* The cooldown starts run out, so the first contact after an arrival is answered at once. */
    p->since_effect = MP_EFFECTS_COOLDOWN_SUBSTEPS;

    p->hurt_pending = false;
    p->hurt_code    = 0u;
    p->hurt_node    = 0u;
    p->since_hurt   = MP_EFFECTS_HURT_COOLDOWN_SUBSTEPS;

    /* The swept blade starts over as well: the body it swept from is gone, and its previous point
     * would sweep a line from wherever that body stood to wherever the new one is placed. */
    p->sweeping      = false;
    p->have_last_pos = false;
}

/* The receiving body's own contact sphere, which is the engine's answer to whether its weapon is
 * armed. A body object that cannot be read is not armed, so the contact is dropped rather than
 * guessed at. */
static bool receiver_is_armed(uint32_t receiver_object)
{
    uint32_t contact_code = 0;

    if (receiver_object == 0u) {
        return false;
    }
    if (!memory_try_read_u32((uintptr_t)receiver_object + OBJECT_CONTACT_CODE, &contact_code)) {
        return false;
    }
    return contact_code != 0u;
}

void mp_effects_note_contact(size_t bank, uint32_t receiver_object)
{
    effects_peer_t      *p         = peer_of(bank);
    uintptr_t            code_cell = mp_cells_address(MP_CELL_MSG_CODE);
    uintptr_t            a_cell    = mp_cells_address(MP_CELL_MSG_A);
    uintptr_t            b_cell    = mp_cells_address(MP_CELL_MSG_B);
    uint32_t             code      = 0;
    uint32_t             message_a = 0;
    uint32_t             message_b = 0;
    int32_t              group     = MP_EFFECTS_VOICE_BODY;
    bool                 armed;
    mp_effects_verdict_t verdict;

    if (code_cell == 0u || b_cell == 0u) {
        return;     /* the cells said so at resolve time; a count per contact would only be noise */
    }
    if (!memory_try_read_u32(code_cell, &code) || !memory_try_read_u32(b_cell, &message_b)) {
        note_refusal("a contact message cell did not read");
        return;
    }

    /* The two reasons a contact is not this module's are counted apart, because in the field they
     * are different answers to "why did nothing spark": a stream of body messages means the blade
     * simply never connected, while armed messages arriving on an unarmed blade would mean the
     * arming is not where this module looks for it. The run right after the layer was built
     * offered 0 armed contacts on both sides while the body module suppressed 434 contacts and 18
     * swings were performed on the puppet, and the report could not say which of the two reasons
     * applied; these two counters are what that run earned.
     *
     * The b flag is the fifth of five stores in one straight run at the head of the post contact
     * entry (self, other, code, a, b), and the pair pass posts one contact twice: with b set to
     * the ACTOR and clear to the VICTIM. Both directions of a plain cylinder overlap go out with a
     * and b both clear, one per body per substep while two bodies touch, which is where the bulk
     * of the suppressed contacts came from. */
    armed = receiver_is_armed(receiver_object);
    if (message_b == 0u) {
        ++effects.body_messages;
    } else if (!armed) {
        ++effects.not_armed;
    }

    verdict = mp_effects_decide(code, message_b, armed, p->since_effect, &group);
    if (verdict == MP_EFFECTS_NOTHING) {
        return;     /* the ordinary case: two bodies touching, counted by the dispatcher already */
    }

    ++effects.contacts;
    if (a_cell != 0u && memory_try_read_u32(a_cell, &message_a) && message_a != 0u) {
        ++effects.blade_contacts;
    }

    if (verdict == MP_EFFECTS_LOCKED) {
        ++effects.suppressed;
        return;
    }
    /* A second contact inside one window keeps the first verdict's group. The engine would have
     * answered only the first one as well: its own cooldown is reseeded by the first. */
    if (!p->pending) {
        p->pending       = true;
        p->pending_group = group;
    }
}

/* Called last in the puppet window, after the placement, the weapon, the plan, the events, the
 * sabre tick and the vitals, because the node sphere rebuilds the joint matrices lazily, at most
 * once per substep, keyed on the tick counter against thing+0x1C: the first caller in a substep
 * fixes the pose every later caller sees, and the effects want the pose the player will see. */
void mp_effects_run_in_window(size_t bank, uint32_t record, uint32_t object)
{
    effects_peer_t *p = peer_of(bank);
    uint32_t        node = 0;
    float           centre[3];
    float           radius;

    if (p->since_effect < MP_EFFECTS_COOLDOWN_SUBSTEPS) {
        ++p->since_effect;
    }
    if (!p->pending) {
        return;
    }
    p->pending = false;

    if (effects.play_voice == NULL || effects.flash_at == NULL || effects.spark_at == NULL ||
        effects.node_sphere == NULL) {
        note_refusal("a site the effects stand on did not resolve");
        return;
    }
    if (record == 0u || object == 0u) {
        note_refusal("the window had no record or no body object");
        return;
    }
    /* A body carrying its player's own weapon is asked of the engine, which names the hand that
     * weapon is drawn at and whose sphere is the drawn weapon's; every other body answers 0 there
     * and keeps the node its own spawn left in the record. */
    node = mp_body_wear_worn_blade_node(bank, object);
    if (node == 0u && !memory_try_read_u32((uintptr_t)record + RECORD_SABRE_NODE, &node)) {
        note_refusal("the record's sabre node did not read");
        return;
    }

    /* A node with no mesh has no sphere, and the engine's own reply to that is a radius of zero
     * with the centre left untouched: it returns 0 when the mesh index at node+0x4C is negative
     * or the node index is out of range. The engine's arm ignores that return, which it can
     * afford because the player always has a sabre node. Playing the effects then would put them
     * at whatever the caller's stack held, so the zero is a refusal here rather than something to
     * ignore. */
    memset(centre, 0, sizeof centre);
    radius = effects.node_sphere((void *)(uintptr_t)object, node, centre);
    if (!(radius > 0.0f)) {
        note_refusal("the sabre node has no sphere on this body");
        return;
    }

    effects.flash_at(centre);
    effects.spark_at(centre);
    if (p->pending_group == MP_EFFECTS_VOICE_NONE) {
        ++effects.without_voice;
    } else {
        effects.play_voice(p->pending_group);
    }

    p->since_effect = 0u;
    ++effects.played;
}


/* Whether the engine would find a sphere at the contact node the swing starter parked in the
 * record. The engine's own test asks for that sphere inside itself and ignores the answer, which
 * it can afford for a player who always has one; a body whose weapon is drawn by another DLL can
 * answer no for a substep, and the query leaves the centre untouched when it does, so the test
 * would sweep from whatever its own stack held. */
static bool contact_sphere_answers(uint32_t record, uint32_t object)
{
    float    centre[3];
    uint32_t node = 0;

    if (!memory_try_read_u32((uintptr_t)record + RECORD_SWING_CONTACT_NODE, &node) || node == 0u) {
        return false;
    }
    memset(centre, 0, sizeof centre);
    return effects.node_sphere((void *)(uintptr_t)object, node, centre) > 0.0f;
}

/* The blade against the level, once per armed substep. The engine's own test reads the player
 * record for the node, the row radius, the heading and the previous sample, so it needs nothing
 * from here but the window it is called in; what this function owns is the decision whether the
 * previous sample may be swept from at all.
 *
 * Seeding means writing the node's position into the record's previous cell and NOT sweeping this
 * substep, which is what the engine's own call would have left behind had it run. A swing that has
 * just been armed has a previous sample from the last swing, minutes old; a puppet that has just
 * been placed somewhere else has one from where it used to stand. Both would sweep a line through
 * the level and strike whatever it crossed, and the local player can produce neither.
 *
 * The engine's test at 0044E6E2 sweeps a wall probe from where the contact node stood last
 * substep to where it stands now, at the swing row's radius, against mask 0x13, the two steep
 * wall bits plus slide. A Jedi that catches a wall gets impact voice group 1, a flash, a spark
 * emitter and a scorch decal 0.2 wide oriented along the swing, and is pushed off the wall at one
 * unit a second; anyone else gets a single sound. Its three callers are all mode ticks, the sabre
 * attack tick, Panaka's update and phase one of the midair attack, which is why a puppet never
 * ran it. The knockback it adds to the record has no reader in the puppet's plan and is ignored,
 * as the knockback a contact gives the puppet is today. */
void mp_effects_sweep_blade(size_t bank, uint32_t record, uint32_t object)
{
    effects_peer_t     *p = peer_of(bank);
    mp_effects_sweep_t  may;
    float               centre[3];
    float               position[3];
    float               radius;
    bool                seed;
    int                 axis;

    if (effects.test_swing_world == NULL || effects.node_sphere == NULL) {
        return;     /* said once at the resolve; a count per substep would only be noise */
    }
    /* A body in a borrowed rig is swept only while it carries its player's own weapon. The node
     * the swing starter found by the hero's blade name is missing on most borrowed rigs, carried
     * without a mesh on one and hidden under the rig's own weapon on the rest; where the overlay
     * hangs that player's weapon it answers the hand instead, and the sphere there is the drawn
     * weapon's. Where it does not, the node sphere writes no point or one near the world's
     * origin, the swing's own contact node is put back to 0, and a sweep would test a blade
     * nothing else believes in. The next sweep seeds afresh. */
    may = mp_effects_sweep_verdict(mp_body_wear_worn(bank), mp_body_wear_weapon(bank));
    if (may == MP_EFFECTS_SWEEP_NONE) {
        p->sweeping = false;
        return;
    }
    if (record == 0u || object == 0u) {
        note_refusal("the window had no record or no body object for the swept blade");
        return;
    }
    if (!memory_try_read((uintptr_t)object + OBJECT_POSITION, position, sizeof position)) {
        note_refusal("the puppet's position did not read for the swept blade");
        return;
    }

    seed = !p->sweeping;
    if (!seed && p->have_last_pos) {
        for (axis = 0; axis < 3; ++axis) {
            float delta = position[axis] - p->last_pos[axis];

            if (delta > SWEEP_JUMP_UNITS || delta < -SWEEP_JUMP_UNITS) {
                seed = true;
            }
        }
    }
    p->sweeping = true;
    p->have_last_pos = true;
    memcpy(p->last_pos, position, sizeof position);

    if (!seed) {
        /* The sphere is asked for before the sweep on a worn body, because the answer for such a
         * body comes from the DLL that draws its weapon and can be no. A refusal takes the sweep
         * away for this substep and makes the next one seed, so the previous point never grows
         * older than one substep and the test never sweeps a line across the level. */
        if (may == MP_EFFECTS_SWEEP_PROBE && !contact_sphere_answers(record, object)) {
            p->sweeping = false;
            ++effects.sweeps_unprobed;
            return;
        }
        effects.test_swing_world();
        ++effects.sweeps;
        if (may == MP_EFFECTS_SWEEP_PROBE) {
            ++effects.sweeps_worn;
        }
        return;
    }

    /* The node the engine's test uses is the swing's contact node, which the swing starter parked
     * in the record; the sabre node this module uses elsewhere is a different one. Reading the
     * contact node here would duplicate the starter's work, so the seed goes through the same
     * function the test opens with and writes what it would have written. */
    {
        uint32_t node = 0;

        if (!memory_try_read_u32((uintptr_t)record + RECORD_SWING_CONTACT_NODE, &node)) {
            note_refusal("the record's swing contact node did not read");
            return;
        }
        memset(centre, 0, sizeof centre);
        radius = effects.node_sphere((void *)(uintptr_t)object, node, centre);
        if (!(radius > 0.0f)) {
            note_refusal("the swing contact node has no sphere on this body");
            return;
        }
        for (axis = 0; axis < 3; ++axis) {
            if (patch_write_f32((uintptr_t)record + RECORD_SWING_NODE_PREV +
                                    (unsigned)axis * 4u, centre[axis]) != PATCH_RESULT_OK) {
                note_refusal("the swept blade's previous point would not take a seed");
                return;
            }
        }
    }
    ++effects.sweeps_seeded;
}

/* ==============================================================================================
 * The second reason: a far body was hurt. See the header for why it keeps its own pending slot,
 * its own node and its own cooldown.
 * ============================================================================================ */

void mp_effects_note_hurt(size_t bank)
{
    effects_peer_t *p = peer_of(bank);
    uint32_t        code = 0;

    ++effects.hurt_offered;
    if (effects.code_cell == 0u || !memory_try_read_u32(effects.code_cell, &code)) {
        ++effects.hurt_refused;
        return;
    }
    if (code < MP_EFFECTS_HURT_CODE_FIRST || code > MP_EFFECTS_HURT_CODE_LAST) {
        return;   /* a push, a pickup, a death: the engine's receiver never sees these */
    }
    ++effects.hurt_in_band;
    p->hurt_pending = true;
    p->hurt_code    = code;
    /* Read here rather than in the window, because the cell holds THIS contact only until the
     * next one is published, and the window runs a substep later at the latest. */
    p->hurt_node    = mp_node_map_cell_slot();
}

void mp_effects_run_hurt(size_t bank, uint32_t object)
{
    effects_peer_t *p = peer_of(bank);
    float           centre[3];
    float           radius;

    if (p->since_hurt < MP_EFFECTS_HURT_COOLDOWN_SUBSTEPS) {
        ++p->since_hurt;
    }
    if (!p->hurt_pending) {
        return;
    }
    p->hurt_pending = false;

    if (effects.play_name == NULL || effects.name_cell == 0u) {
        ++effects.hurt_refused;
        return;
    }
    if (p->since_hurt < MP_EFFECTS_HURT_COOLDOWN_SUBSTEPS) {
        ++effects.hurt_swallowed;
        return;
    }

    /* The voice first, and with no place of its own: it falls through the engine's own funnel
     * with a null position, and the anchor the puppet window holds open gives it the body's. */
    {
        uint32_t wav = 0;

        if (memory_try_read_u32(effects.name_cell, &wav) && wav != 0u) {
            effects.play_name((const char *)(uintptr_t)wav, 0u);
            ++effects.hurt_voices;
            p->since_hurt = 0u;
        } else {
            ++effects.hurt_refused;
            return;
        }
    }

    if (p->hurt_code != MP_EFFECTS_HURT_CODE_BLOOD) {
        return;
    }
    if (p->hurt_node == 0u) {
        ++effects.hurt_no_node;
        return;
    }
    if (effects.spawn_at == NULL || effects.node_sphere == NULL || object == 0u) {
        ++effects.hurt_refused;
        return;
    }
    /* The engine throws this radius away and spawns anyway, which puts blood on an uninitialised
     * stack for a node with no mesh. That is a defect and not a thing to copy. */
    memset(centre, 0, sizeof centre);
    radius = effects.node_sphere((void *)(uintptr_t)object, p->hurt_node, centre);
    if (!(radius > 0.0f)) {
        ++effects.hurt_no_sphere;
        return;
    }
    effects.spawn_at(NULL, centre, MP_EFFECTS_BLOOD_TEMPLATE);
    ++effects.hurt_blood;
}

void mp_effects_hurt_counters(uint32_t *offered, uint32_t *in_band, uint32_t *voices,
                              uint32_t *swallowed, uint32_t *blood, uint32_t *no_node,
                              uint32_t *no_sphere, uint32_t *refused)
{
    if (offered != NULL)   { *offered   = effects.hurt_offered; }
    if (in_band != NULL)   { *in_band   = effects.hurt_in_band; }
    if (voices != NULL)    { *voices    = effects.hurt_voices; }
    if (swallowed != NULL) { *swallowed = effects.hurt_swallowed; }
    if (blood != NULL)     { *blood     = effects.hurt_blood; }
    if (no_node != NULL)   { *no_node   = effects.hurt_no_node; }
    if (no_sphere != NULL) { *no_sphere = effects.hurt_no_sphere; }
    if (refused != NULL)   { *refused   = effects.hurt_refused; }
}

uint32_t mp_effects_contacts(void)       { return effects.contacts; }
uint32_t mp_effects_blade_contacts(void) { return effects.blade_contacts; }
uint32_t mp_effects_played(void)         { return effects.played; }
uint32_t mp_effects_suppressed(void)     { return effects.suppressed; }
uint32_t mp_effects_without_voice(void)  { return effects.without_voice; }
uint32_t mp_effects_body_messages(void)  { return effects.body_messages; }
uint32_t mp_effects_not_armed(void)      { return effects.not_armed; }
uint32_t mp_effects_sweeps(void)         { return effects.sweeps; }
uint32_t mp_effects_sweeps_worn(void)    { return effects.sweeps_worn; }
uint32_t mp_effects_sweeps_unprobed(void){ return effects.sweeps_unprobed; }
uint32_t mp_effects_sweeps_seeded(void)  { return effects.sweeps_seeded; }
uint32_t mp_effects_refused(void)        { return effects.refused; }
