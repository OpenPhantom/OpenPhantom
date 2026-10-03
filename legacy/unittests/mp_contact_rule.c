/* mp_contact_rule.c: what each delivery path does with the verdict on a contact between players. */
#include "unittest.h"

#include "mp_contact_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* The puppet branch reports a hit to the victim's machine and shows the hurt here. Anything but an
 * allowed contact sends nothing: a refused one was refused on both machines by the same rule, and
 * one between two far players is judged on the victim's own machine, where the attacker's shot or
 * swing is replayed on its own body. Reported from here as well, it hurt twice with friendly fire
 * on and once with it off. */
static void test_the_puppet_branch(void)
{
    ut_section("a puppet's contact");
    ut_check(mp_contact_rule_puppet_reports(MP_CONTACT_ALLOWED),
             "an allowed contact is reported to the victim's machine");
    ut_check(!mp_contact_rule_puppet_reports(MP_CONTACT_REFUSED),
             "a refused one is not");
    ut_check(!mp_contact_rule_puppet_reports(MP_CONTACT_NOT_OURS),
             "and one between two far players is not either: the victim's machine judges it");
}

/* A routed delivery and one to this machine's own player run the engine's handler here and report
 * nothing to another machine, so only a refusal stops them. */
static void test_the_paths_that_carry_out(void)
{
    ut_section("a delivery carried out on this machine");
    ut_check(mp_contact_rule_carries_out(MP_CONTACT_ALLOWED), "an allowed contact is carried out");
    ut_check(!mp_contact_rule_carries_out(MP_CONTACT_REFUSED),
             "a refused one is answered and nothing of it is carried out");
    ut_check(mp_contact_rule_carries_out(MP_CONTACT_NOT_OURS),
             "and one between two far players is carried out as it always was");
}

/* ==============================================================================================
 * The collision of a far body in the way of a scene.
 * ============================================================================================ */

#define OBJECT     0x100u
#define NEW_OBJECT 0x200u

static mp_contact_collision_t ask(bool passable, bool alive, bool applied)
{
    return mp_contact_rule_collision(passable, alive, false, applied ? OBJECT : 0u, OBJECT);
}

static void test_alive_by_scene_by_applied(void)
{
    ut_section("a far body's collision: alive, the scene, made passable already");
    ut_check(ask(true, true, false) == MP_COLLISION_PASSABLE,
             "a living body in a scene is made passable");
    ut_check(ask(true, true, true) == MP_COLLISION_KEEP,
             "once: a body made passable already is not written again");
    ut_check(ask(true, false, false) == MP_COLLISION_KEEP,
             "a body lying dead in a scene is left as its death clip left it");
    ut_check(ask(true, false, true) == MP_COLLISION_KEEP,
             "and one that died while passable keeps its mark for its revival or the end");
    ut_check(ask(false, true, true) == MP_COLLISION_RESTORE,
             "a living body made passable is given back when the scene no longer wants it");
    ut_check(ask(false, false, true) == MP_COLLISION_LEFT_DEAD,
             "one lying dead then is only unmarked: its revival puts the collision back");
    ut_check(ask(false, true, false) == MP_COLLISION_KEEP,
             "outside a scene a living body nobody marked is not written");
    ut_check(ask(false, false, false) == MP_COLLISION_KEEP, "and neither is a dead one");
}

static void test_a_revival(void)
{
    ut_section("a far player who stands again");
    ut_check(mp_contact_rule_collision(false, true, true, 0u, OBJECT) == MP_COLLISION_RESTORE,
             "outside a scene a revival puts the collision back, as it always did");
    ut_check(mp_contact_rule_collision(true, true, true, 0u, OBJECT) == MP_COLLISION_PASSABLE,
             "while a scene wants the bodies passable it is made passable instead");
    ut_check(mp_contact_rule_collision(true, true, true, OBJECT, OBJECT) == MP_COLLISION_KEEP,
             "and one still marked from before its death is left as it is");
}

static void test_a_new_object(void)
{
    ut_section("a body built again is another object");
    ut_check(mp_contact_rule_collision(true, true, false, OBJECT, NEW_OBJECT) ==
                 MP_COLLISION_PASSABLE,
             "a body built again during a scene was never made passable, so it is");
    ut_check(mp_contact_rule_collision(false, true, false, OBJECT, NEW_OBJECT) ==
                 MP_COLLISION_UNMARK,
             "after the scene the old object's mark is dropped and the new body is not written");
    ut_check(mp_contact_rule_collision(true, false, false, OBJECT, NEW_OBJECT) ==
                 MP_COLLISION_UNMARK,
             "and a new body lying dead in a scene drops it as well");
}

/* The mark as the owner keeps it: written on a pass, cleared on everything that unmarks. */
static mp_contact_collision_t walk(bool passable, bool alive, bool revived, uint32_t *marked)
{
    mp_contact_collision_t step = mp_contact_rule_collision(passable, alive, revived, *marked,
                                                            OBJECT);

    if (step == MP_COLLISION_PASSABLE) {
        *marked = OBJECT;
    } else if (step != MP_COLLISION_KEEP) {
        *marked = 0u;
    }
    return step;
}

static void test_scenes_walked_through(void)
{
    uint32_t marked = 0u;

    ut_section("three scenes walked through substep by substep");
    ut_check(walk(true, true, false, &marked) == MP_COLLISION_PASSABLE &&
                 walk(true, true, false, &marked) == MP_COLLISION_KEEP,
             "a living body is made passable as the scene begins, and only then");
    ut_check(walk(true, false, false, &marked) == MP_COLLISION_KEEP && marked == OBJECT,
             "it dies in the scene: nothing written, the mark kept");
    ut_check(walk(true, true, true, &marked) == MP_COLLISION_KEEP && marked == OBJECT,
             "it stands again in the scene: still passable, nothing written");
    ut_check(walk(false, true, false, &marked) == MP_COLLISION_RESTORE && marked == 0u,
             "and the end of the scene gives it back once");

    ut_check(walk(true, false, false, &marked) == MP_COLLISION_KEEP &&
                 walk(false, false, false, &marked) == MP_COLLISION_KEEP && marked == 0u,
             "a body dead before the scene is never written for it");
    ut_check(walk(false, true, true, &marked) == MP_COLLISION_RESTORE,
             "and its revival after it puts the death clip's collision back");

    ut_check(walk(true, true, false, &marked) == MP_COLLISION_PASSABLE &&
                 walk(true, false, false, &marked) == MP_COLLISION_KEEP,
             "a body made passable dies in the scene");
    ut_check(walk(false, false, false, &marked) == MP_COLLISION_LEFT_DEAD && marked == 0u,
             "the scene ends while it lies dead: held back, unmarked, nothing written");
    ut_check(walk(false, true, true, &marked) == MP_COLLISION_RESTORE,
             "and its revival gives the collision back");
}

int main(void)
{
    test_the_puppet_branch();
    test_the_paths_that_carry_out();
    test_alive_by_scene_by_applied();
    test_a_revival();
    test_a_new_object();
    test_scenes_walked_through();

    return ut_summary("mp_contact_rule");
}
