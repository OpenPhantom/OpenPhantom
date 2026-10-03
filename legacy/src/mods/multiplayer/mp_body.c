/* mp_body.c: the contact dispatcher and the far bodies.
 *
 * SIZE NOTE: over 600 lines. The spawn and everything that builds or unbuilds a body are in
 * mp_body_spawn.c, the attribution and the friendly fire gate are in mp_body_death.c, the
 * bank-aware shot hull is in mp_body_shot.c, and the record those halves share is in
 * mp_body_internal.h, and the far bodies made passable for a scene are in mp_body_passable.c. What
 * is left is two things that run every substep and never read the player's block: the dispatcher,
 * and the tick with the collision restore.
 *
 * The next seam is the dispatcher with its arming. It is the dearer one: it shares the far bodies'
 * records, the two cells the install resolves, and the counters and switches the setters, the
 * getters and the report read.
 *
 * ===================================== The contact path ========================================
 *
 * A contact is the engine publishing six globals and then running the RECEIVER's handler task
 * inline, so from inside a handler "self" is the sender and "other" is the body that was touched.
 * The dispatcher keys on the other cell to tell whose body it was and compares it against each
 * bank's own object handle to find which. Every body the spawn builds carries the player's node,
 * which is why one replaced slot sees all of them, and why the replacement has to pass the
 * original on and return exactly what it returned. A far body in a session carries a node of its
 * own with the same dispatcher in its slot, so it is still seen while the player's slot is empty.
 */
#include "mp_body.h"

#include "mp_body_asset.h"
#include "mp_body_death.h"
#include "mp_body_gate.h"
#include "mp_body_internal.h"

#include "mp_bank.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_contact_rule.h"
#include "mp_effects.h"
#include "mp_phases.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The dispatch slot inside the handler node. The offsets a body's object is read and written at
 * are shared with the build half and live in mp_body_internal.h.
 *
 * The slot offset comes from two sites that agree. The task runner at 0x00475953 calls the slot
 * with `call [eax+0x18]` and pushes no argument, and the spawn writes the engine's handler there
 * at 0x0044802D (`mov [eax+0x18], 0x00448369`, after `mov eax, [0x006CF63C]` at 0x00448028),
 * which is why every spawn puts the engine's handler back. The node cell is resolved off that
 * store, one match on all three shipped builds (retail and alternate link at 0x00448025, the
 * recompile at 0x00447FC5), the cell being the operand at +0x04; the neighbouring operand at
 * +0x0B is the handler's own address and is not used, because the dispatcher reads the live
 * slot instead. */
#define TASK_NODE_DISPATCH_SLOT 0x18u

/* The engine's own player_onContact and any handler chained before us both answer this shape:
 * cdecl, no argument, a status word in eax whose bit 1 the scheduler reads. The dispatcher must
 * return exactly what the original returned, or a body that asked to be ticked again next substep
 * would stop being ticked. The engine's handler at 0x00448369 reads only globals (no [ebp+8]
 * anywhere in it) and every path through it ends at 0x00448553 with `mov eax, 1 / ret`, so its
 * answer is 1 and that is what the dispatcher answers when it runs nothing. */
typedef uint32_t(__cdecl *contact_fn_t)(void);

typedef struct mp_body_state {
    bool         installed;
    uintptr_t    node_cell;         /* g_playerTaskNode */
    uintptr_t    other_cell;        /* the contact receiver global */
    contact_fn_t original_contact;  /* what the slot held when we last armed */
    uintptr_t    armed_over;        /* the node+slot address we wrote, to re-arm idempotently */

    bool         can_tick;          /* the phase loop installed, so a far body can be ticked */

    uint32_t     contacts_total;
    uint32_t     contacts_other;
    uint32_t     rearm_count;

    bool         route_contacts;     /* deliver far body contacts inside a bank window */
    bool         route_fault_logged;
    uint32_t     contacts_routed;
    uint32_t     route_faults;

    bool         far_is_puppet;      /* every far body's contacts are answered without the
                                      * handler */
    mp_body_puppet_hit_fn_t puppet_hit;  /* where a dropped contact is reported to */
    uint32_t     contacts_suppressed;
    uint32_t     contacts_on_the_dead;   /* of those, on a far player lying dead: not reported */

    uint32_t     dispatching;        /* contacts the dispatcher is inside of right now */

    mp_body_far_t far[MP_BANK_FAR_MAX];
} mp_body_state_t;

static mp_body_state_t body;

static mp_body_far_t *far_of(size_t index)
{
    return mp_bank_index_ok(index) ? &body.far[index - 1u] : NULL;
}

mp_body_far_t *mp_body_far_at(size_t index)
{
    return far_of(index);
}

bool mp_body_module_ready(void)
{
    return body.installed;
}

void mp_body_note_far_player_at(size_t index, bool present)
{
    mp_body_far_t *far = far_of(index);

    if (far != NULL) {
        far->occupied = present;
    }
}

/* What a rebuilt body must not inherit. The cylinder belongs to the model that was bound, so
 * keeping it would let a death clip's restore put the departed model's collision back on the new
 * one; the death report and the health are statements about a body that no longer exists. The
 * lifetime counters are deliberately left alone: they are evidence about the session, not about
 * the body, and zeroing them would hide how much work a rebuild costs. */
void mp_body_forget_body_state(mp_body_far_t *far)
{
    if (far == NULL) {
        return;
    }
    far->cylinder_saved       = false;
    far->cylinder_radius_bits = 0u;
    far->cylinder_height_bits = 0u;
    far->death_reported       = false;
    far->health_seen          = false;
    far->health_known         = 0;
    far->passable_object      = 0u;   /* the object the scene made passable has left */
}

uint32_t mp_body_contacts_total(void)      { return body.contacts_total; }
uint32_t mp_body_contacts_other(void)      { return body.contacts_other; }
uint32_t mp_body_contacts_suppressed(void) { return body.contacts_suppressed; }
bool     mp_body_installed(void)           { return body.installed; }

int32_t mp_body_hero_at(size_t index)
{
    const mp_body_far_t *far = far_of(index);

    return (far != NULL && far->spawned) ? far->hero : -1;
}

int32_t mp_body_slot_at(size_t index)
{
    const mp_body_far_t *far = far_of(index);

    return (far != NULL && far->spawned) ? far->slot : -1;
}

const char *mp_body_asset_at(size_t index)
{
    const mp_body_far_t *far = far_of(index);

    return (far != NULL && far->spawned && far->wearing_asset) ? far->asset : "";
}

void mp_body_note_revived_at(size_t index)
{
    mp_body_far_t *far = far_of(index);

    if (far != NULL) {
        far->death_reported = false;
    }
}

void mp_body_enable_contact_routing(void)
{
    body.route_contacts = true;
}

void mp_body_set_puppet_hit_listener(mp_body_puppet_hit_fn_t listener)
{
    body.puppet_hit = listener;
}

void mp_body_set_second_is_puppet(bool is_puppet)
{
    body.far_is_puppet = is_puppet;
    /* One switch: a far body that is a puppet is also the one that needs a contact node of its
     * own, and the hits its machine performs here need the engine's own delivery. */
    mp_body_gate_set_own_nodes(is_puppet);
}

/* The same reading the re-entry's anchors and the copies' owners are fed from: the pose the
 * interpolator resolved for that bank this substep, and the far machine's own word about its
 * player in it. A bank with no pose has said neither, and is nobody to fight or to hurt. */
bool mp_body_far_player_stands(size_t index)
{
    mp_bridge_far_pose_t pose;

    return mp_bridge_far_pose(index, MP_FAR_READER_OTHER, &pose) &&
           mp_bridge_far_pose_stands(&pose);
}

bool mp_body_exists_at(size_t index)
{
    const mp_body_far_t *far = far_of(index);

    return far != NULL && far->spawned;
}

/* ==============================================================================================
 * The count dispatcher.
 * ============================================================================================ */

/* Which far bank's body the receiver is, or 0 for none: each bank's own object handle sits at
 * +0x0C of its block, read live because a respawn allocates a new body. The handle is a raw
 * object pointer and not an index, stored right after the pool allocation in the spawn
 * (0x00447ECC: `mov [edx+0x0C], eax`) and read back the same way by the engine's own alive test
 * at 0x00447D18. */
static size_t bank_of_body(uint32_t object)
{
    size_t index;

    for (index = 1u; index <= MP_BANK_FAR_MAX; ++index) {
        uint32_t handle = 0;

        if (body.far[index - 1u].spawned &&
            mp_bank_read_at(index, HERO_BLOCK_HACTOR, &handle, sizeof handle) &&
            handle == object) {
            return index;
        }
    }
    return 0u;
}

size_t mp_body_bank_of_object(uint32_t object)
{
    return bank_of_body(object);
}

bool mp_body_handler_known(void)
{
    return body.original_contact != NULL;
}

bool mp_body_call_the_handler(void)
{
    if (body.original_contact == NULL) {
        return false;
    }
    (void)body.original_contact();
    return true;
}

bool mp_body_dispatching(void)
{
    return body.dispatching != 0u;
}

/* A contact routed into a bank may have changed that body's health; the first few transitions
 * are worth a line each, because they are the direct evidence damage is landing on the right
 * body, and after that the report's number carries it. */
#define HEALTH_CHANGE_LOG_CAP 8u

static void note_routed_health(size_t index)
{
    mp_body_far_t *far = far_of(index);
    int32_t        health;

    if (far == NULL) {
        return;
    }
    health = mp_bank_health_at(index);
    if (!far->health_seen) {
        far->health_seen  = true;
        far->health_known = health;
        return;
    }
    if (health == far->health_known) {
        return;
    }
    ++far->health_changes;
    if (far->health_changes <= HEALTH_CHANGE_LOG_CAP) {
        log_info("bank %u's body's health moved from %d to %d through a routed contact",
                 (unsigned)index, (int)far->health_known, (int)health);
    }
    far->health_known = health;
}

/* The contact goes to the local player's own body, which is where the engine's own handler hurts
 * and kills him. The flag is read on both sides of that call, because this is the only moment at
 * which the death and its cause are both in front of one reader: a substep later the message
 * globals hold whatever happened next. */
static uint32_t deliver_to_player(void)
{
    uint32_t result;

    mp_body_death_watch_begin();
    result = body.original_contact();
    mp_body_death_watch_end();
    return result;
}

/* The count dispatcher. It runs in place of player_onContact, on the scheduler's thread, with
 * nothing of ours swapped in. It reads who was touched, counts, and passes the original handler
 * its exact contract. With routing enabled, a contact whose receiver is a far body is delivered
 * inside a persistent window of THAT body's bank, so the engine's own handler hurts, shoves and
 * kills that body instead of the player. A delivery that arrives while a bank is already active
 * (a push from inside a far body's own tick) is passed through as it is: the block and the status
 * already hold that body's content there, so the handler is aimed right without a second window,
 * and the codes those pushes carry match no arm of the handler anyway: its arms are 2, 3, 9,
 * 0x1E, 0x22, 0x20 to 0x27, 0x0A to 0x1B and 0x1F, and a player body's class is 1 or 5 to 7.
 *
 * The pair filter that posts these runs at engine message 0x0E, after the task list, so it is
 * outside this feature's swap window and the player pointer is bank 0's at every delivery. With
 * the co-op classes the delivered code reaches no arm of the engine's handler, so "two bodies
 * exchange contact messages" is invisible in the game; that is why the slot was first replaced
 * by a plain counter that passed the original on.
 *
 * What the engine's handler would have done for a puppet's contact on this machine, all of it
 * wrong here: the banked health falls, the HUD's own target flashes through the absolute ease
 * cells from 0x006CFAF8, the hits taken statistic at 0x00872EF0 counts the attacker's own hit,
 * the pain voice plays as if it were the attacker's, the drop timer is set to a quarter second
 * in the bank block and never ticked down (a lasting local immunity), a burn code enters the
 * death path and a pickup code lands in the shared inventory. The shot's own task at 0x00454AA1
 * still ends the bolt with its sparks and its kill flag, delivered separately.
 *
 * One blade contact is posted as two messages with two receivers and only one carries damage:
 * the pair pass posts (o2, o1, o2's class, a = 0, b = 1) to the actor and (o1, o2, o1's contact
 * code, a = 1, b = 0) to the victim, and the handler forks on the b cell at 0x00869250. With b
 * clear it runs the body path where 0x20 to 0x27 becomes damage; with b set it runs the armed
 * contact at 0x0044855C, which deals no damage and instead sparks, flashes and plays an impact
 * voice at the actor's own sabre node. So the actor's message is the feeling of having
 * connected, and the actor of a replicated swing is the puppet; that one is offered to the
 * effect layer. Most of the suppressed count is neither: a cylinder overlap posts both
 * directions with a and b clear, once per body per substep for as long as two bodies touch. */
static uint32_t dispatch_one(void)
{
    uint32_t other = 0;
    bool     to_far = false;

    ++body.contacts_total;
    if (body.other_cell != 0 && memory_try_read_u32(body.other_cell, &other) && other != 0) {
        uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
        uint32_t  player_body = 0;

        /* The receiver is a body object; the player's own body is the handle at the hero block's
         * +0x0C, not the block itself. A receiver that is not the player's body is a far body
         * being touched, which is the whole point of counting. Reading the handle live rather than
         * once keeps it right across a respawn, which allocates a new body. */
        if (block != 0 && memory_try_read_u32(block + HERO_BLOCK_HACTOR, &player_body) &&
            player_body != 0 && other != player_body) {
            ++body.contacts_other;
            to_far = true;
            mp_body_gate_note_far_contact();
            mp_body_passable_note_contact(other);
        }
    }

    /* A puppet's contacts are the far machine's to judge. Answered here with the engine's own
     * answer and nothing run: the shot that touched it still ends through its own task, and
     * everything the handler would have done (health, HUD, statistics, pain, pickups, the drop
     * timer) belongs to the machine the far player is hurt on. Only at bank 0, the same condition
     * as the routed path: inside a window the hero block names a puppet's own body, and a touch
     * of the local player would read as the puppet's. */
    if (to_far && body.far_is_puppet && mp_bank_active_class() == 1) {
        size_t which = bank_of_body(other);

        ++body.contacts_suppressed;
        /* Dropped here, performed there. What a far player is worth in health, pain and
         * death belongs to that player's own machine, and until this line was here it
         * belonged to nobody: the enemies could not kill a client at all. The gate is asked
         * first: a contact the rule set refuses is not reported, so nothing happens on the
         * other machine either. Nor is one between two far players, which that machine
         * judges itself on its own body.
         *
         * Nor is one on a far player lying dead. His machine emptied his contact slot at the
         * death, so the engine there would refuse it; reported, it reached his corpse around
         * that refusal and his next body as soon as it stood. */
        if (which != 0u && !mp_body_far_player_stands(which)) {
            ++body.contacts_on_the_dead;
        } else if (mp_contact_rule_puppet_reports(mp_body_death_contact_verdict(which))) {
            if (body.puppet_hit != NULL) {
                body.puppet_hit(which);
            }
            /* Inside the gate on purpose. The offer next door stands outside it because it
             * judges the puppet's own weapon connecting, which hurts nobody; this one is the
             * victim's message, and blood for damage the host does not pass on would be a
             * lie in the picture. */
            mp_effects_note_hurt(which);
        }
        /* One of these contacts is not the puppet being touched but the puppet's own weapon
         * connecting, and the engine answers that one with a spark, a flash and a voice rather
         * than with damage. Offered to the effect layer before it is dropped; it judges the
         * message here and plays nothing here, because at bank 0 the player record is the local
         * player's. */
        mp_effects_note_contact(which, other);
        return 1;
    }

    if (body.original_contact == NULL) {
        return 1;   /* the engine's own handler answers 1; matching it keeps the body scheduled */
    }

    if (to_far && body.route_contacts && mp_bank_active_class() == 1) {
        uint32_t result;
        size_t   which = bank_of_body(other);
        bool     routed;

        if (!mp_contact_rule_carries_out(mp_body_death_contact_verdict(which))) {
            return 1;   /* the engine's own answer, with nothing of the contact carried out */
        }
        routed = which != 0u && mp_bank_swap_in_persistent_at(which);
        if (!routed && !body.route_fault_logged) {
            body.route_fault_logged = true;
            log_warning("a contact for a far body could not open its bank window and was "
                        "delivered to bank 0; later cases are counted, not logged");
        }
        if (routed) {
            ++body.contacts_routed;
        } else {
            ++body.route_faults;
        }
        result = body.original_contact();
        if (routed) {
            mp_bank_swap_out();
            note_routed_health(which);
        }
        return result;
    }

    /* Everything left is delivered as it always was. Only one of those cases can be watched for a
     * death: the receiver being the local player's own body with no bank active. Inside a window
     * the player pointer names a far body, so the flag read there would be that body's life, and a
     * contact whose receiver is a far body did not touch this player at all. */
    if (to_far || mp_bank_active() != 0u) {
        return body.original_contact();
    }
    /* The receiver is this machine's own player, and a sender out of a far player's bank is the
     * one way another player hurts him: his bolt, his blade, his push, his blast. It is the same
     * question the two arms above ask, put from the other side, and the rule is asked once. */
    if (!mp_contact_rule_carries_out(mp_body_death_contact_verdict(0u))) {
        return 1;   /* the engine's own answer, with nothing of the contact carried out */
    }
    return deliver_to_player();
}

/* What the engine calls. The depth is what lets a death entry say that a contact delivered here
 * was behind it; a delivery can nest when a handler's own work touches another body. */
static uint32_t __cdecl contact_dispatch(void)
{
    uint32_t result;

    ++body.dispatching;
    result = dispatch_one();
    --body.dispatching;
    return result;
}

uintptr_t mp_body_dispatcher_address(void)
{
    return (uintptr_t)&contact_dispatch;
}

void mp_body_arm_dispatcher(void)
{
    uintptr_t slot;
    uint32_t  node = 0;
    uint32_t  current = 0;

    if (!body.installed || body.node_cell == 0) {
        return;
    }
    if (!memory_try_read_u32(body.node_cell, &node) || node == 0) {
        return;
    }
    slot = (uintptr_t)node + TASK_NODE_DISPATCH_SLOT;
    if (!memory_try_read_u32(slot, &current)) {
        return;
    }
    if ((uintptr_t)current == (uintptr_t)&contact_dispatch) {
        return;     /* already ours, and the original is already kept */
    }
    if (current == 0) {
        /* The engine nulls this slot deliberately, on a player between death and respawn, so that
         * nothing can be delivered to a body on its way back. Adopting a null here would make the
         * dispatcher pass contacts on to nowhere, which swallows the engine's own handling. So the
         * slot is left null until a spawn puts a real handler back, and only then is it taken.
         * The first field run did adopt it: once before any spawn had put a handler there, and
         * again on every death to respawn cycle. */
        return;
    }

    body.original_contact = (contact_fn_t)current;
    body.armed_over       = slot;
    if (patch_write_pointer32(slot, (const void *)&contact_dispatch) != PATCH_RESULT_OK) {
        log_error("the contact dispatcher could not be armed on the slot at %08X", (unsigned)slot);
        body.original_contact = NULL;
        return;
    }
    ++body.rearm_count;
    log_info("the contact dispatcher is armed, passing on to %08X (arming %u)",
             (unsigned)current, (unsigned)body.rearm_count);
}

/* ==============================================================================================
 * The tick and the restore.
 * ============================================================================================ */

/* Tick a bank's body through the player pipeline for one substep. It runs after the mod task's
 * own tick client that spawned the body, and only once a body exists. The phases read the player
 * through the pointer cell, so with the bank's block installed at the hero block the body moves,
 * collides against the level and other bodies, and commits its own position, all from the one
 * walk. The walk is this feature's own loop over the pristine phase table, not the engine's
 * runner, so no other fix's thunk is reached and the body neither dies of the player's health nor
 * fires nor feeds the shared camera. One effect is known and left for a later step: a contact
 * struck during the tick is charged to the ticking bank rather than the touched body, which is a
 * correctness detail of knockback, not of the body moving.
 *
 * A body that died inside its walk is never ticked again: the next walk would run the death
 * table, and its one function writes the level outcome for every player. A pit, a drowning or a
 * crush kills from inside a mode's own phase with the death check withheld, and the death entry
 * hangs the death descriptor on the record, so the dead flag at +0x394 of the block is read
 * after every walk and the first walk that leaves it set is the last. */
void mp_body_tick_at(size_t index)
{
    mp_body_far_t *far = far_of(index);

    if (far == NULL || !body.installed || !far->spawned || !body.can_tick) {
        return;
    }
    if (mp_phases_dead_at(index)) {
        if (!far->death_reported) {
            far->death_reported = true;
            log_info("bank %u's body is no longer ticked after %u substep(s): it died",
                     (unsigned)index, (unsigned)far->ticks);
        }
        return;
    }
    if (mp_bank_run_at(index, &mp_phases_run_second)) {
        ++far->ticks;
    } else {
        ++far->tick_faults;
    }
}

/* Four checked writes onto the body's object, which the caller read live, out of the bank's own
 * block or out of the puppet's window: the object does not change for the life of the body, but a
 * stored pointer would outlive a level.
 *
 * What is being undone: the engine's collision disable at 0x0041401A writes a radius of 0 at
 * obj+0xB8, a height of 0 at obj+0xBC and a class of 0 at obj+0x04, and the draw pass calls it
 * when the base clip carries the parameter event mode and its latch has fired, which the death
 * clips do. The pair pass skips any object whose class is 0, so after a death clip the puppet is
 * invisible to it: no blade contact, no reflect, no cylinder. Both cylinder words are copied
 * unscaled from the actor at the bind (asset+0xEC and asset+0xF4), which is why they are read
 * off the object right after the spawn and written back as bits. */
bool mp_body_collision_restore_at(size_t index, uint32_t object)
{
    mp_body_far_t *far = far_of(index);
    int32_t        class_word;
    bool           ok;

    if (far == NULL || !body.installed || !far->spawned || !far->cylinder_saved || object == 0) {
        return false;
    }
    class_word = mp_bank_class_of(index);

    ok = patch_write_u32(object + BAPOBJ_OBJ_CLASS, (uint32_t)class_word) == PATCH_RESULT_OK;
    ok = patch_write_u32(object + BAPOBJ_SHOOTER_CLASS, (uint32_t)class_word)
             == PATCH_RESULT_OK && ok;
    ok = patch_write_u32(object + BAPOBJ_CYLINDER_RADIUS, far->cylinder_radius_bits)
             == PATCH_RESULT_OK && ok;
    ok = patch_write_u32(object + BAPOBJ_CYLINDER_HEIGHT, far->cylinder_height_bits)
             == PATCH_RESULT_OK && ok;
    if (!ok) {
        ++far->restore_faults;
        if (!far->restore_fault_logged) {
            far->restore_fault_logged = true;
            log_error("bank %u's body's collision could not be restored on object %08X: a write "
                      "refused; later faults are counted, not logged", (unsigned)index,
                      (unsigned)object);
        }
        return false;
    }

    ++far->collision_restores;
    if (!far->restore_logged) {
        far->restore_logged = true;
        log_info("bank %u's body's collision is restored on object %08X: class %d, side %d, the "
                 "cylinder the spawn bound; later restores are counted, not logged",
                 (unsigned)index, (unsigned)object, (int)class_word, (int)class_word);
    }
    return true;
}

/* ==============================================================================================
 * The names that mean bank 1.
 * ============================================================================================ */

void mp_body_spawn_tick(void)
{
    mp_body_spawn_at(1u);
}

bool mp_body_second_exists(void)
{
    return mp_body_exists_at(1u);
}

void mp_body_tick_second(void)
{
    mp_body_tick_at(1u);
}

void mp_body_note_second_revived(void)
{
    mp_body_note_revived_at(1u);
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

bool mp_body_install(void)
{
    uintptr_t shot_site;
    size_t    shot_prologue;
    size_t    index;

    if (body.installed) {
        return true;
    }

    /* The other cell is the dispatcher's discriminator. The contact poster at 0x00414C99 publishes
     * six globals and then runs the receiver's handler inline: its head stores the first argument
     * to 0x869240 (self) at 0x414C9F, the second to 0x869244 (other) at 0x414CA7 and the third to
     * 0x869248 (code) at 0x414CB0, with the remaining three at +0x4C, +0x50 and +0x54. The pair
     * filter calls it as (o2, o1, ...) and then the reverse, so "other" is the receiver. The
     * anchor on that head matches once on all three shipped builds. The recompile's data shift is
     * not uniform, which is why both cells come from masked operands: the node cell moves by
     * 0x50 (0x006CF63C to 0x006CF5EC) while the message globals move by 0x60 (0x00869240 to
     * 0x008691E0). */
    body.node_cell  = mp_cells_address(MP_CELL_PLAYER_TASK_NODE);
    body.other_cell = mp_cells_address(MP_CELL_MSG_OTHER);
    if (body.node_cell == 0 || body.other_cell == 0) {
        log_warning("the body module cannot install: the task node or the contact global did not "
                    "resolve");
        return false;
    }

    shot_site     = mp_signatures_address(MP_SITE_SHOT_SPAWN);
    shot_prologue = mp_signatures_prologue(MP_SITE_SHOT_SPAWN);
    if (shot_site == 0) {
        log_warning("the body module cannot install: shot_spawn did not resolve");
        return false;
    }
    if (!mp_body_shot_install(shot_site, shot_prologue)) {
        log_warning("the body module cannot install: the shot hull did not take at %08X",
                    (unsigned)shot_site);
        return false;
    }

    /* The phase loop is not required to install: the shot hull and the dispatcher are the core,
     * and a far body's tick is optional and gated. A build where the table cannot be read back
     * from disk simply cannot tick a far body, which is logged there and here. */
    body.can_tick = mp_phases_install();
    if (!body.can_tick) {
        log_warning("the body module installs without the phase loop: a far body can be spawned "
                    "but not ticked on this build");
    }

    /* The asset layer is optional in exactly the same sense. Without it no far body can wear an
     * actor of its own, because nothing can prove a name is loadable and an unproved name handed
     * to the spawn ends the process. The bodies then keep the local hero, which is what they did
     * before this layer existed. */
    if (!mp_body_asset_install()) {
        log_warning("the body module installs without the asset layer: every far body wears the "
                    "hero this machine's own player wears, whatever the far side reports");
    }

    for (index = 1u; index <= MP_BANK_FAR_MAX; ++index) {
        body.far[index - 1u].slot     = -1;
        body.far[index - 1u].hero     = -1;
        body.far[index - 1u].occupied = true;
    }
    body.installed = true;

    /* The death watch is optional in the same sense the phase loop is: without its two cells the
     * dispatcher does everything it did before and reports no death at all, which would otherwise
     * show up as a scoreboard that never moves and name nothing as the reason. */
    if (!mp_body_death_install()) {
        log_warning("the body module installs without the death watch: the player record pointer "
                    "or the contact sender did not resolve, so a death of this player cannot be "
                    "seen here and none will be reported");
    }

    log_info("the body module is installed: shot hull at %08X, task node cell %08X, phase loop %s, "
             "asset layer %s, death watch %s, %u far bank(s)", (unsigned)shot_site,
             (unsigned)body.node_cell, body.can_tick ? "ready" : "absent",
             mp_body_asset_ready() ? "ready" : "absent",
             mp_body_death_ready() ? "ready" : "absent",
             (unsigned)MP_BANK_FAR_MAX);
    return true;
}

void mp_body_report(const char *why)
{
    size_t index;

    if (!body.installed) {
        return;
    }
    log_info("the body module at %s: %u contacts counted, %u of them to a body that is not bank 0, "
             "dispatcher armed %u time(s), %u loop fault(s)", why, (unsigned)body.contacts_total,
             (unsigned)body.contacts_other, (unsigned)body.rearm_count,
             (unsigned)mp_phases_second_faults());
    for (index = 1u; index <= MP_BANK_FAR_MAX; ++index) {
        const mp_body_far_t *far = &body.far[index - 1u];

        if (!far->spawned) {
            continue;
        }
        log_info("  bank %u's body: the far player's hero %d riding slot %d as %s, ticked %u "
                 "substep(s), %u tick fault(s), %s; collision restored %u time(s) with %u fault(s)",
                 (unsigned)index, (int)far->hero, (int)far->slot,
                 far->asset[0] != 0 ? far->asset : "that slot's own asset",
                 (unsigned)far->ticks, (unsigned)far->tick_faults,
                 mp_phases_dead_at(index) ? "dead" : "alive",
                 (unsigned)far->collision_restores, (unsigned)far->restore_faults);
        if (far->rebuilds != 0u || far->rebuild_refusals != 0u || far->wish) {
            log_info("    %u appearance(s) applied, %u refusal(s), %s",
                     (unsigned)far->rebuilds, (unsigned)far->rebuild_refusals,
                     far->wish ? "an appearance is still waiting to be carried out"
                               : "nothing waiting");
        }
        if (body.route_contacts) {
            log_info("    its health stands at %d after %u change(s) through routed contacts",
                     (int)mp_bank_health_at(index), (unsigned)far->health_changes);
        }
    }
    if (body.route_contacts) {
        log_info("  %u contact(s) delivered inside a bank window, %u window fault(s)",
                 (unsigned)body.contacts_routed, (unsigned)body.route_faults);
    }
    if (body.far_is_puppet) {
        log_info("  the far bodies are puppets: %u contact(s) answered without the engine's "
                 "handler, %u shot(s) left without their side, %u contact(s) on a far player "
                 "lying dead not reported",
                 (unsigned)body.contacts_suppressed, (unsigned)mp_body_shot_side_faults(),
                 (unsigned)body.contacts_on_the_dead);
    }
    mp_body_passable_report();
    mp_body_gate_report();
    mp_body_shot_report();
    mp_body_death_report();
    mp_body_asset_report();
}
