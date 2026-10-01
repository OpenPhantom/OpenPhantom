/* mp_target.c: whom an NPC fights when there is more than one player. See mp_target.h. */
#include "mp_target.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_scene_rule.h"
#include "mp_signatures.h"
#include "mp_signatures_enemy.h"
#include "mp_target_rule.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The character, by the fields this file reads. Both are proven: the position is what
 * enemy_hostOnPlayer at 0x004377EE seeds from the body, and the cached target at +0x200 is the
 * field every shot, swing and parry reads back in the reconstructed enemy record. */
#define ACTOR_POS      0xD0u
#define ACTOR_TARGET   0x200u

/* The body object's position, the same three floats the pair pass measures with. */
#define BODY_POS       0x18u

/* The bank block's own body handle: the far body's object, live out of the block the bank holds. */
#define BANK_BODY      0x0Cu

/* The two kinds that mean A PLAYER. Kind 0 is the player's body; kind 1 is the nearer of the
 * player and a class-9 ally, and a far player is a better answer to both. The other three name a
 * class or a slot in the placement's own table and have nothing to do with a human. */
#define TARGET_KIND_PLAYER  0
#define TARGET_KIND_NEARER  1

/* The prologue this detour copies: push ebp / mov ebp,esp / sub esp,0x1c, which is 55 / 8B EC /
 * 83 EC 1C. The sub is three bytes, so six is an instruction boundary and five would cut it. */
#define RESOLVE_TARGET_PROLOGUE 6u

typedef int32_t(__cdecl *resolve_target_fn_t)(uintptr_t actor, float *out, int32_t kind);

/* Any far player beats this squared distance: it is how the engine's "no answer" is measured. */
#define NO_ANSWER_DISTANCE 3.4e38f

/* The callers left to the engine's own answer. One today, the menu's facing test. */
#define LEFT_ALONE_MAX 1u

/* Keyed by placement index rather than by actor pointer, because the index is the only identity
 * that survives a removal and is the same on both machines. The cost is that a respawn of the same
 * placement inherits the memory until it ages out. */
typedef struct aggro {
    uint32_t attacker;    /* the player body that last landed a hit here */
    uint32_t substep;     /* when, so the memory can age */
    bool     have;
} aggro_t;

/* What an actor last heard when a script asked for a player, kept for the scene watch. Only a
 * player's body is kept as a player: kind 1 can answer with an ally, and an ally is nobody's
 * trigger. */
typedef struct answer {
    uint32_t substep;
    uint8_t  bank;        /* 0 this machine's player, 1 and up a far one */
    bool     player;
    bool     have;
} answer_t;

typedef struct target_state {
    bool                installed;
    bool                is_host;
    detour_t            detour;
    resolve_target_fn_t original;
    uint32_t            substep;

    aggro_t  aggro[MP_TARGET_SLOTS];
    answer_t answer[MP_TARGET_SLOTS];

    uintptr_t left_alone[LEFT_ALONE_MAX];   /* return addresses, not call addresses */
    size_t    left_alone_count;
    uint32_t  facing_asked;        /* the menu's facing test, answered by the engine */
    uint32_t  facing_far_nearer;   /* of those, where a far player would have been answered */
    uint32_t  facing_nobody;       /* of those, where the engine had no body to answer with */

    const mp_npc_copies_t *copies;   /* the host's table, for whom a copy goes with */
    mp_target_owner_fn_t   owner;    /* where an owner stands */

    uint32_t calls;           /* kind 0 or 1 answered while hosting */
    uint32_t to_far;          /* answered with a far player instead of the local one */
    uint32_t by_aggro;        /* of those, chosen because that player had hurt this actor */
    uint32_t passed_dead;     /* resolutions that would have answered a far player lying dead */
    uint32_t notes;           /* hits remembered */
    uint32_t unreadable;      /* an actor or a body that could not be read */
    uint32_t following;       /* resolutions for a copy that follows or helps */
    uint32_t to_owner;        /* answered with its owner's body */
    uint32_t owner_down;      /* its owner absent or dead: the engine's answer for the host */
    uint32_t host_owned;      /* owned by the host: the engine's answer */
    uint32_t nobody;          /* of those two, the engine had no target either */
} target_state_t;

static target_state_t target;

void mp_target_set_host(bool is_host)
{
    target.is_host = is_host;
}

void mp_target_tick(uint32_t substep)
{
    target.substep = substep;
}

bool mp_target_installed(void)
{
    return target.installed;
}

bool mp_target_hosting(void)
{
    return target.installed && target.is_host && mp_armed_transport();
}

/* The far body of bank `index`, or 0. Read live out of the bank's block, because a respawn gives
 * the far player a new object and a handle kept from the spawn would name the old one. */
static uint32_t far_body(size_t index)
{
    uint32_t handle = 0;

    if (!mp_body_exists_at(index) ||
        !mp_bank_read_at(index, BANK_BODY, &handle, sizeof handle)) {
        return 0u;
    }
    return handle;
}

static bool body_position(uint32_t body, float out[3])
{
    return body != 0u && memory_try_read((uintptr_t)body + BODY_POS, out, 3u * sizeof(float));
}

static float distance_squared(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return dx * dx + dy * dy + dz * dz;
}

/* This machine's own player's body, out of the hero block the bank table holds for it. */
static uint32_t own_body(void)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  mine  = 0u;

    if (block == 0 || !memory_try_read_u32(block + BANK_BODY, &mine)) {
        return 0u;
    }
    return mine;
}

/* Which bank's player `body` is: 0 for the local player's own, 1 and up for a far one. False for
 * any other body, and for nought. */
static bool bank_of_player_body(uint32_t body, uint8_t *bank)
{
    size_t i;

    if (body == 0u) {
        return false;
    }
    if (own_body() == body) {
        if (bank != NULL) {
            *bank = 0u;
        }
        return true;
    }
    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        if (far_body(i) == body) {
            if (bank != NULL) {
                *bank = (uint8_t)i;
            }
            return true;
        }
    }
    return false;
}

/* Whether `body` is one of this machine's player bodies: the local player's own, or a far one. */
static bool is_a_player_body(uint32_t body)
{
    return bank_of_player_body(body, NULL);
}

void mp_target_note_attack(uintptr_t victim_actor, uint32_t attacker_object)
{
    uint32_t index = 0;

    if (!target.installed || !target.is_host || victim_actor == 0 || attacker_object == 0u) {
        return;
    }
    if (!is_a_player_body(attacker_object)) {
        return;   /* an NPC hitting an NPC decides nothing about who a human is */
    }
    if (!mp_enemy_bind_index(victim_actor, &index) || index >= MP_TARGET_SLOTS) {
        return;
    }
    target.aggro[index].attacker = attacker_object;
    target.aggro[index].substep  = target.substep;
    target.aggro[index].have     = true;
    ++target.notes;
}

void mp_target_set_copies(const mp_npc_copies_t *copies, mp_target_owner_fn_t owner)
{
    target.copies = copies;
    target.owner  = owner;
}

void mp_target_forget(uint32_t key)
{
    if (key < MP_TARGET_SLOTS) {
        target.aggro[key].attacker = 0u;
        target.aggro[key].substep  = 0u;
        target.aggro[key].have     = false;
        target.answer[key].have    = false;
    }
}

/* The remembered attacker of this actor, if the memory is fresh and still names a player's body,
 * standing or not. */
static uint32_t remembered_attacker(uintptr_t actor)
{
    uint32_t index = 0;
    aggro_t *entry;

    if (!mp_enemy_bind_index(actor, &index) || index >= MP_TARGET_SLOTS) {
        return 0u;
    }
    entry = &target.aggro[index];
    if (!entry->have) {
        return 0u;
    }
    if (target.substep - entry->substep > MP_TARGET_AGGRO_SUBSTEPS) {
        entry->have = false;
        return 0u;
    }
    if (!is_a_player_body(entry->attacker)) {
        entry->have = false;    /* the body it named is gone, which a respawn does */
        return 0u;
    }
    return entry->attacker;
}

/* The same, while that player stands. A far player lying dead is kept in the memory rather than
 * forgotten: his puppet is the same body when he stands again, and the memory ages on its own. */
static uint32_t fresh_aggro(uintptr_t actor)
{
    uint32_t attacker = remembered_attacker(actor);
    uint8_t  bank = 0u;

    if (attacker != 0u && bank_of_player_body(attacker, &bank) && bank != 0u &&
        !mp_body_far_player_stands(bank)) {
        return 0u;
    }
    return attacker;
}

/* A copy that follows or helps goes with its owner: the owner's body while the owner stands, and
 * the engine's own answer for the host otherwise, which the caller already holds. Answers whether
 * the rule decided, so nothing after it looks at this actor. */
static bool goes_with_its_owner(uintptr_t actor, float *out, int32_t *ok)
{
    uint32_t key   = 0;
    uint8_t  owner = 0;
    uint32_t body  = 0;
    float    pos[3];
    bool     stands = false;

    if (target.copies == NULL || !mp_enemy_bind_index(actor, &key) || !mp_wire_key_is_copy(key) ||
        !mp_npc_copies_follows(target.copies, key - MP_WIRE_KEY_COPY_BASE, &owner)) {
        return false;
    }
    ++target.following;
    if (owner != 0u && target.owner != NULL) {
        stands = target.owner(owner, &body) && body_position(body, pos);
    }
    if (mp_npc_copies_goes_with(owner, stands) == 0u) {
        if (owner != 0u) {
            ++target.owner_down;
        } else {
            ++target.host_owned;
        }
        target.nobody += *ok == 0 ? 1u : 0u;
        return true;   /* the engine's answer stands, for the host */
    }
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2];
    (void)memory_try_write(actor + ACTOR_TARGET, &body, sizeof body);
    *ok = 1;
    ++target.to_owner;
    return true;
}

/* The far body that beats `engine_distance`, squared from `here`, or 0 when none does, by the rule
 * in mp_target_rule: the one that hurt the actor outright, otherwise the nearest, and never a far
 * player lying dead. Writes nothing into the engine: the caller decides what the answer is used
 * for. */
static uint32_t far_answer(uintptr_t actor, const float here[3], float engine_distance,
                           float best_pos[3], bool *by_aggro, bool *passed_dead)
{
    mp_target_far_t far[MP_BANK_FAR_MAX];
    uint32_t        bodies[MP_BANK_FAR_MAX];
    float           positions[MP_BANK_FAR_MAX][3];
    uint32_t        wanted = remembered_attacker(actor);
    size_t          i;
    int             chosen;

    for (i = 0; i < MP_BANK_FAR_MAX; ++i) {
        bodies[i]          = far_body(i + 1u);
        far[i].readable    = body_position(bodies[i], positions[i]);
        far[i].stands      = mp_body_far_player_stands(i + 1u);
        far[i].attacker    = wanted != 0u && bodies[i] == wanted;
        far[i].distance    = far[i].readable ? distance_squared(here, positions[i]) : 0.0f;
    }
    chosen = mp_target_rule_far_pick(far, MP_BANK_FAR_MAX, engine_distance, by_aggro,
                                     passed_dead);
    if (chosen < 0) {
        return 0u;
    }
    best_pos[0] = positions[chosen][0];
    best_pos[1] = positions[chosen][1];
    best_pos[2] = positions[chosen][2];
    return bodies[chosen];
}

/* What the actor was answered, whoever answered it, read back from the cache the engine and this
 * hull both write. Kept only while hosting, and only a player's body counts as a player. */
static void remember_the_answer(uintptr_t actor)
{
    uint32_t index  = 0u;
    uint32_t winner = 0u;
    uint8_t  bank   = 0u;
    answer_t entry;

    if (!mp_enemy_bind_index(actor, &index) || index >= MP_TARGET_SLOTS) {
        return;
    }
    (void)memory_try_read_u32(actor + ACTOR_TARGET, &winner);
    entry.player  = bank_of_player_body(winner, &bank);
    entry.bank    = entry.player ? bank : 0u;
    entry.substep = target.substep;
    entry.have    = true;
    target.answer[index] = entry;
}

/* The menu's facing test is the engine's to answer, for this machine's own player. What the
 * extension would have said is still worked out, only to be counted: it is the number of menus a
 * far player would have opened here, or a dead host would have crashed on. */
static void note_the_facing_test(uintptr_t actor, const float here[3], const float *out,
                                 int32_t ok)
{
    float best_pos[3];
    bool  by_aggro = false;
    bool  passed_dead = false;
    float engine_distance = ok != 0 ? distance_squared(here, out) : NO_ANSWER_DISTANCE;

    ++target.facing_asked;
    if (ok == 0) {
        ++target.facing_nobody;
    }
    if (far_answer(actor, here, engine_distance, best_pos, &by_aggro, &passed_dead) != 0u) {
        ++target.facing_far_nearer;
    }
}

/* The engine's answer, then ours. The original runs first and unconditionally: it fills the
 * caller's vector, caches its own answer in the actor, and its return value is what a failure has
 * to look like. Only then is a far body allowed to win, and when it does, both the vector and the
 * cache are rewritten, because the callers read one or the other and never the same one twice.
 *
 * One caller is never extended, and it is recognised by where it returns to: the menu's facing
 * test reads the heading of this machine's own player, so its position has to be that player's
 * too. It is still counted in the resolutions for a player, so that number stays comparable, and
 * its answer is not kept as the actor's last (mp_target_answer_is_kept).
 *
 * The engine's own answer is the one to beat, and its distance is measured to the vector it just
 * wrote rather than to the body it chose: kind 1 can answer with an ally, and an ally is a
 * perfectly good answer that a far player only beats by being closer.
 *
 * engine: int resolve_target(character *actor, vec3 *out, i16 kind) */
static int32_t __cdecl hook_resolve_target(uintptr_t actor, float *out, int32_t kind)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();
    int32_t   ok     = target.original(actor, out, kind);
    int16_t   which  = (int16_t)(kind & 0xFFFF);
    float     here[3];
    float     best_pos[3];
    uint32_t  best;
    bool      by_aggro = false;
    bool      passed_dead = false;
    bool      engine_only;

    if (!mp_target_hosting() || actor == 0 || out == NULL) {
        return ok;
    }
    if (which != TARGET_KIND_PLAYER && which != TARGET_KIND_NEARER) {
        return ok;
    }
    if (!memory_try_read(actor + ACTOR_POS, here, sizeof here)) {
        ++target.unreadable;
        return ok;
    }
    ++target.calls;
    engine_only = mp_target_left_to_the_engine(target.left_alone, target.left_alone_count, caller);
    if (engine_only) {
        note_the_facing_test(actor, here, out, ok);
        if (mp_target_answer_is_kept(engine_only)) {
            remember_the_answer(actor);
        }
        return ok;
    }
    if (goes_with_its_owner(actor, out, &ok)) {
        remember_the_answer(actor);
        return ok;
    }
    best = far_answer(actor, here, ok != 0 ? distance_squared(here, out) : NO_ANSWER_DISTANCE,
                      best_pos, &by_aggro, &passed_dead);
    target.passed_dead += passed_dead ? 1u : 0u;
    if (best != 0u) {
        out[0] = best_pos[0];
        out[1] = best_pos[1];
        out[2] = best_pos[2];
        (void)memory_try_write(actor + ACTOR_TARGET, &best, sizeof best);
        ++target.to_far;
        if (by_aggro) {
            ++target.by_aggro;
        }
        ok = 1;
    }
    remember_the_answer(actor);
    return ok;
}

/* The one caller left to the engine, found by its own bytes and proved by its own call: the five
 * bytes in front of the address it returns to have to be a call to the resolver this file hulls. A
 * site that is not found, or calls something else, leaves the list empty, and the test is then
 * extended like every other call, which is how it behaved before. */
static void find_the_facing_test(uintptr_t resolver)
{
    mp_scene_call_site_t call   = { 0u, { 0u } };
    uintptr_t            callee = 0u;
    uintptr_t            site;

    target.left_alone_count = 0u;
    site = signature_find_unique(SIG_MP_FACING_TEST_RESOLVE, MSK_MP_FACING_TEST_RESOLVE,
                                 sizeof SIG_MP_FACING_TEST_RESOLVE);
    if (site == 0u) {
        log_warning("the menu's facing test did not resolve, so the target resolver answers it "
                    "like any other call: a far player can pass a menu on this machine, and with "
                    "this player dead the test goes on to read the heading of no body");
        return;
    }
    call.return_address = site + FACING_TEST_RESOLVE_RETURN;
    (void)memory_read(call.return_address - MP_SCENE_CALL_BYTES, call.call, MP_SCENE_CALL_BYTES);
    if (mp_scene_camera_callee(&call, 1u, &callee) != MP_SCENE_CALLEE_AGREED ||
        callee != resolver) {
        log_warning("the menu's facing test at %08X does not call the target resolver at %08X, so "
                    "it is not left to the engine and a far player can still pass a menu here",
                    (unsigned)site, (unsigned)resolver);
        return;
    }
    target.left_alone[0]    = call.return_address;
    target.left_alone_count = 1u;
    log_info("the menu's facing test asks the target resolver from %08X and is left to the "
             "engine's own answer for this machine's player, so a far player never passes a menu "
             "here and a dead player is answered with no target",
             (unsigned)(call.return_address - MP_SCENE_CALL_BYTES));
}

bool mp_target_last_answer(uintptr_t actor, bool *player, uint8_t *bank, uint32_t *age)
{
    uint32_t        index = 0u;
    const answer_t *entry;

    if (!mp_enemy_bind_index(actor, &index) || index >= MP_TARGET_SLOTS) {
        return false;
    }
    entry = &target.answer[index];
    if (!entry->have) {
        return false;
    }
    *player = entry->player;
    *bank   = entry->bank;
    *age    = target.substep - entry->substep;
    return true;
}

bool mp_target_last_attacker(uintptr_t actor, uint8_t *bank)
{
    return bank_of_player_body(fresh_aggro(actor), bank);
}

bool mp_target_player_position(uint8_t bank, float out[3])
{
    if (bank > MP_BANK_FAR_MAX) {
        return false;
    }
    return body_position(bank == 0u ? own_body() : far_body(bank), out);
}

bool mp_target_install(void)
{
    uintptr_t site;

    if (target.installed) {
        return true;
    }
    site = mp_signatures_address(MP_SITE_RESOLVE_TARGET);
    if (site == 0) {
        log_warning("the target resolver did not resolve, so an NPC on this host answers only the "
                    "local player and a far player walks through the level unnoticed");
        return false;
    }
    /* Before the detour, because the hook reads the list from the moment the branch is written. */
    find_the_facing_test(site);
    if (!detour_install(&target.detour, site, (const void *)&hook_resolve_target,
                        RESOLVE_TARGET_PROLOGUE)) {
        log_error("the target resolver at %08X refused the detour", (unsigned)site);
        return false;
    }
    target.original  = (resolve_target_fn_t)target.detour.original;
    target.installed = true;
    log_info("the target resolver is bound at %08X: an NPC answers whoever hurt it, and otherwise "
             "the nearest player", (unsigned)site);
    return true;
}

void mp_target_report(void)
{
    if (!target.installed) {
        log_info("the target resolver is not bound: every NPC answers the local player only");
        return;
    }
    if (!target.is_host) {
        log_info("the target resolver is bound and idle: this side is a client and its NPCs are "
                 "parked replicas that resolve nothing");
        return;
    }
    log_info("whom the NPCs fight: %u resolution(s) for a player, %u answered with a far player "
             "(%u of them because that player had hurt them); %u hit(s) remembered, %u actor(s) "
             "unreadable, %u passed over a far player lying dead",
             (unsigned)target.calls, (unsigned)target.to_far, (unsigned)target.by_aggro,
             (unsigned)target.notes, (unsigned)target.unreadable, (unsigned)target.passed_dead);
    log_info("whom the copies go with: %u resolution(s) for a copy that follows or helps, %u "
             "answered with the copy's owner, %u owner absent or dead and %u owned by the host, "
             "both answered as the engine answers for the host, %u of them with no target",
             (unsigned)target.following, (unsigned)target.to_owner,
             (unsigned)target.owner_down, (unsigned)target.host_owned, (unsigned)target.nobody);
    if (target.left_alone_count == 0u) {
        log_info("the facing test of the conversation menu: NOT left to the engine, because its "
                 "site did not resolve or did not call the resolver, so it is answered like every "
                 "other call and is among the %u resolution(s) for a player above",
                 (unsigned)target.calls);
        return;
    }
    log_info("the facing test of the conversation menu: %u asked and every one left to the engine "
             "for the player of this machine; in %u of them a far player would have been answered "
             "before, and in %u this player had no body to answer with (the engine answers 0; "
             "before, any far player with a readable body replaced it)",
             (unsigned)target.facing_asked, (unsigned)target.facing_far_nearer,
             (unsigned)target.facing_nobody);
}
