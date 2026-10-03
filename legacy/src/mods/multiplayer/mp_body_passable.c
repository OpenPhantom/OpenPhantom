/* mp_body_passable.c: the far bodies made passable while a scene plays on the host. See the
 * section of mp_body.h on a far body in the way of a scene, and mp_contact_rule for the rule.
 *
 * One owner of each far body's class word while a scene plays, asked every substep and writing only
 * on a change. The word's writers are the spawn, which asks this owner for the class, a death clip,
 * which zeroes it, and this owner, through make_passable and the body module's restore. Its readers
 * are the engine's collision tests: the cylinder push an actor and the host's own body make every
 * step, the pair pass, a blast, the move and crush tests and a search by class.
 *
 * By address: the push is bapobj_cylinderPush at 0x004131EB, which tests only the OTHER body's
 * class against the band 1 to 9, called from an actor's post tick at 0x004362C8 and from the
 * player's collision phase; the pair pass, the blast, the move test, the crush test and the search
 * by class each skip a class of 0. A death clip's zeroing is bapobj_disableCollision at
 * 0x0041401A, which the draw pass calls on the clip's parameter event and which also zeroes both
 * cylinder words.
 *
 * In this feature the attribution reads the class of a contact's sender, and a body of class 0
 * sends none; whether a far player stands is his own machine's pose, never his class.
 *
 * One contact reaches a body of class 0 all the same, and it is no collision test: a script's
 * message, opcode 0x10D. op_message at 0x0042E9C2 resolves its target, tests the height and the
 * radius and runs the target's handler, with no look at the class. A far player who plays on
 * through a scene of the host's walks into the traps and the shoves that send one, so such a
 * contact is counted apart, and only a contact with no script running is one a collision test
 * should not have made.
 *
 * Its own file because the body module was at its size limit, and because nothing here touches the
 * dispatcher's state: the far body records are reached through mp_body_far_at like every other
 * half of the module.
 */
#include "mp_body.h"

#include "mp_body_internal.h"
#include "mp_body_passable.h"

#include "mp_bank.h"
#include "mp_contact_rule.h"

#include "common/logging.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A stretch's two lines this many times, and after that the report's counts alone. */
#define PASSABLE_LINES_MAX 64u

typedef struct passable_state {
    bool     scene_passable;       /* the scene's wish, as last said */
    uint32_t times;                /* stretches the scene wanted the bodies passable */
    uint32_t substeps;
    uint32_t stretch_substeps;     /* of the stretch that runs, or ran last */
    uint32_t made_passable;
    uint32_t restored;
    uint32_t held_back_dead;
    uint32_t revivals_kept;        /* revivals during a scene, kept passable */
    uint32_t faults;
    uint32_t contacts;             /* a collision test's on a passable body: must stay 0 */
    uint32_t script_contacts;      /* contacts a script's message delivered to one */
    uint32_t lines;

    mp_body_passable_script_fn_t script_runs;
} passable_state_t;

static passable_state_t pass;

void mp_body_passable_set_script_probe(mp_body_passable_script_fn_t probe)
{
    pass.script_runs = probe;
}

/* Class 0, the one word a scene takes. The side and the cylinder stay, so a shot the far player
 * fires still carries his side, and the restore puts back what a death clip would also take. */
static void make_passable(mp_body_far_t *far, uint32_t object)
{
    if (patch_write_u32(object + BAPOBJ_OBJ_CLASS, 0u) != PATCH_RESULT_OK) {
        ++pass.faults;
        return;
    }
    far->passable_object = object;
    ++pass.made_passable;
}

/* Everything a death clip takes, put back. A body whose cylinder never read gets its class back
 * alone, which is all a scene took from a living one. */
static void give_back(size_t index, uint32_t object)
{
    if (mp_body_collision_restore_at(index, object) ||
        patch_write_u32(object + BAPOBJ_OBJ_CLASS, (uint32_t)mp_bank_class_of(index)) ==
            PATCH_RESULT_OK) {
        ++pass.restored;
        return;
    }
    ++pass.faults;
}

/* One body `object` asked about its collision and the answer carried out. The mark is the object
 * made passable, so a body built since is a body that never was. */
static mp_contact_collision_t step_collision_at(size_t index, uint32_t object, bool revived)
{
    mp_body_far_t         *far = mp_body_far_at(index);
    mp_contact_collision_t step;

    if (far == NULL || !mp_body_module_ready() || !far->spawned || object == 0u) {
        return MP_COLLISION_KEEP;
    }
    step = mp_contact_rule_collision(pass.scene_passable, !far->lies_dead, revived,
                                     far->passable_object, object);
    switch (step) {
    case MP_COLLISION_PASSABLE:
        make_passable(far, object);
        break;
    case MP_COLLISION_RESTORE:
        far->passable_object = 0u;
        if (revived) {
            (void)mp_body_collision_restore_at(index, object);   /* says so itself, once */
        } else {
            give_back(index, object);
        }
        break;
    case MP_COLLISION_LEFT_DEAD:
        far->passable_object = 0u;
        ++pass.held_back_dead;
        break;
    case MP_COLLISION_UNMARK:
        far->passable_object = 0u;
        break;
    case MP_COLLISION_KEEP:
    default:
        break;
    }
    return step;
}

static void say_the_passability(bool rose, uint32_t made, uint32_t dead, uint32_t restored,
                                uint32_t held)
{
    if (pass.lines >= PASSABLE_LINES_MAX) {
        return;
    }
    ++pass.lines;
    if (rose) {
        log_info("the far bodies are passable for a scene: %u bank(s) made class 0, %u lying "
                 "dead left as their death clip left them", (unsigned)made, (unsigned)dead);
        return;
    }
    log_info("the far bodies are solid again after %u substep(s) passable for a scene: %u bank(s) "
             "restored, %u held back while dead", (unsigned)pass.stretch_substeps,
             (unsigned)restored, (unsigned)held);
}

void mp_body_set_scene_passable(bool passable)
{
    bool     rose     = passable && !pass.scene_passable;
    bool     fell     = !passable && pass.scene_passable;
    uint32_t made     = pass.made_passable;
    uint32_t restored = pass.restored;
    uint32_t held     = pass.held_back_dead;
    uint32_t dead     = 0u;
    size_t   index;

    pass.scene_passable = passable;
    if (!mp_body_module_ready()) {
        return;
    }
    if (rose) {
        ++pass.times;
        pass.stretch_substeps = 0u;
    }
    if (passable) {
        ++pass.substeps;
        ++pass.stretch_substeps;
    }
    for (index = 1u; index <= MP_BANK_FAR_MAX; ++index) {
        const mp_body_far_t *far = mp_body_far_at(index);
        uint32_t             object = 0u;

        if (!mp_bank_read_at(index, HERO_BLOCK_HACTOR, &object, sizeof object)) {
            object = 0u;   /* no body to write this substep; the next one asks again */
        }
        (void)step_collision_at(index, object, false);
        dead += far != NULL && far->spawned && far->lies_dead ? 1u : 0u;
    }
    if (rose || fell) {
        say_the_passability(rose, pass.made_passable - made, dead, pass.restored - restored,
                            pass.held_back_dead - held);
    }
}

bool mp_body_collision_note_life_at(size_t index, bool alive, uint32_t object)
{
    mp_body_far_t         *far = mp_body_far_at(index);
    mp_contact_collision_t step;

    if (far == NULL) {
        return false;
    }
    far->lies_dead = !alive;
    if (!alive || !mp_body_module_ready()) {
        return false;
    }
    step = step_collision_at(index, object, true);
    if (pass.scene_passable && far->spawned) {
        ++pass.revivals_kept;
    }
    return step == MP_COLLISION_PASSABLE || step == MP_COLLISION_RESTORE;
}

int32_t mp_body_collision_class_at(size_t index, uint32_t object)
{
    mp_body_far_t *far = mp_body_far_at(index);

    if (far == NULL || object == 0u || !mp_body_module_ready() ||
        mp_contact_rule_collision(pass.scene_passable, !far->lies_dead, false, 0u, object) !=
            MP_COLLISION_PASSABLE) {
        return mp_bank_class_of(index);
    }
    far->passable_object = object;
    ++pass.made_passable;
    return 0;
}

/* Compared with the marked objects, which costs no read inside a contact delivery. A contact
 * delivered while a script runs is that script's message; any other came from a collision test,
 * which should have skipped the body. */
void mp_body_passable_note_contact(uint32_t object)
{
    size_t index;

    for (index = 1u; index <= MP_BANK_FAR_MAX; ++index) {
        const mp_body_far_t *far = mp_body_far_at(index);

        if (far != NULL && far->passable_object != 0u && far->passable_object == object) {
            if (pass.script_runs != NULL && pass.script_runs()) {
                ++pass.script_contacts;
            } else {
                ++pass.contacts;
            }
            return;
        }
    }
}

void mp_body_passable_report(void)
{
    log_info("  the far bodies passable for scenes: %u time(s) over %u substep(s), %u made class "
             "0, %u restore(s), %u held back while dead, %u revival(s) kept passable for a scene, "
             "%u contact(s) of a collision test on a passable far body (must be 0), %u "
             "delivered to one by a script's message, which asks no class, %u write(s) refused",
             (unsigned)pass.times, (unsigned)pass.substeps, (unsigned)pass.made_passable,
             (unsigned)pass.restored, (unsigned)pass.held_back_dead,
             (unsigned)pass.revivals_kept, (unsigned)pass.contacts,
             (unsigned)pass.script_contacts, (unsigned)pass.faults);
}
