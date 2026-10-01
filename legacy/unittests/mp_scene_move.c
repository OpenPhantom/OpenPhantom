/* The single questions a scene gathering asks: whether a scene runs, whether a player may be moved,
 * who asked for a respawn, where a hero's spawn came from and what a warp did to the quest bits.
 *
 * Each is a rule a host and a client ask in several places, which is why each is one function
 * driven here over every value it can be given. */
#include "unittest.h"

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A scene runs by the lock at a script's level or by a park with a body under it, and by nothing
 * else: a table over the lock's levels, the module's five states, a store of nought or not, and
 * the body. */
static void check_when_a_scene_runs(void)
{
    static const int32_t LOCKS[4] = { 0, 1, 5, 99 };
    size_t               lock;
    uint32_t             module;
    unsigned             bits;

    ut_section("a scene runs by the lock at a script's level, or a park with a body under it");
    for (lock = 0u; lock < 4u; ++lock) {
        for (module = 0u; module < 5u; ++module) {
            for (bits = 0u; bits < 4u; ++bits) {
                uint32_t saved  = (bits & 1u) != 0u ? 1u : 0u;
                bool     body   = (bits & 2u) != 0u;
                bool     runs   = mp_scene_running(LOCKS[lock], module, saved, body);
                bool     wanted = LOCKS[lock] >= 5 || (module == 0u && saved != 0u && body);

                ut_checkf(runs == wanted, "lock %d, module %u, stored %u, body %u: %s",
                          (int)LOCKS[lock], (unsigned)module, (unsigned)saved, (unsigned)body,
                          runs ? "a scene" : "no scene");
            }
        }
    }
    ut_check(!mp_scene_running(1, 1u, 0u, true), "a menu's lock of one is never a scene");
    ut_check(!mp_scene_running(0, 3u, 1u, true) && !mp_scene_running(0, 4u, 1u, true),
             "dying and the respawn write the module to other values and are no scene");
    ut_check(!mp_scene_running(0, 0u, 1u, false),
             "a level end despawns the body and leaves the module at nought, with a store the "
             "last grab of the level left: no scene");
}

/* The teleport writes a position and clears the ground contact, and nothing else. */
static void check_who_may_be_moved(void)
{
    static const mp_scene_mode_t MODES[4] = {
        MP_SCENE_MODE_UNREAD, MP_SCENE_MODE_PARKABLE, MP_SCENE_MODE_DEATH, MP_SCENE_MODE_OTHER
    };
    unsigned bits;
    size_t   mode;

    ut_section("a player is moved only standing, alive, and out of a mode the engine parks from");
    ut_check(mp_scene_may_move(true, true, true, MP_SCENE_MODE_PARKABLE) == MP_SCENE_MOVE_YES,
             "standing, a sabre attack or Panaka, alive, with a body: yes");
    ut_check(mp_scene_may_move(true, true, false, MP_SCENE_MODE_DEATH) == MP_SCENE_MOVE_DEAD,
             "a corpse is not moved, where the placement's own two gates would have let it");
    ut_check(mp_scene_may_move(true, true, true, MP_SCENE_MODE_OTHER) == MP_SCENE_MOVE_MODE,
             "hanging, swimming, pushing, on a gun or in the air: not moved");
    ut_check(mp_scene_may_move(true, true, true, MP_SCENE_MODE_UNREAD) == MP_SCENE_MOVE_MODE,
             "a mode that did not read is not a mode anybody may be moved from");
    ut_check(mp_scene_may_move(false, true, true, MP_SCENE_MODE_PARKABLE) ==
                 MP_SCENE_MOVE_NO_BODY,
             "no body: nothing to move");
    ut_check(mp_scene_may_move(true, false, true, MP_SCENE_MODE_PARKABLE) ==
                 MP_SCENE_MOVE_NO_BODY,
             "a module not running, a load, a respawn or a park: the placement would refuse it");
    for (bits = 0u; bits < 8u; ++bits) {
        for (mode = 0u; mode < 4u; ++mode) {
            bool            body    = (bits & 1u) != 0u;
            bool            running = (bits & 2u) != 0u;
            bool            stands  = (bits & 4u) != 0u;
            mp_scene_move_t move    = mp_scene_may_move(body, running, stands, MODES[mode]);
            bool            yes     = body && running && stands &&
                               MODES[mode] == MP_SCENE_MODE_PARKABLE;

            ut_checkf((move == MP_SCENE_MOVE_YES) == yes,
                      "body %u, running %u, standing %u, mode %u: %s", (unsigned)body,
                      (unsigned)running, (unsigned)stands, (unsigned)mode,
                      move == MP_SCENE_MOVE_YES ? "moved" : "not moved");
        }
    }
}

static void check_who_asked_for_a_respawn(void)
{
    const uintptr_t warp = (uintptr_t)0x00435126u;
    const uintptr_t swap = (uintptr_t)0x004302E9u;

    ut_section("a respawn is told by the address its call returns to");
    ut_check(mp_scene_respawn_caller(warp, warp, swap, true) == MP_SCENE_RESPAWN_BY_WARP,
             "the script's warp, opcode 0x607");
    ut_check(mp_scene_respawn_caller(swap, warp, swap, true) == MP_SCENE_RESPAWN_BY_SWAP,
             "the cheats' hero swap, which the lobby's own swap goes through");
    ut_check(mp_scene_respawn_caller((uintptr_t)0x00447000u, warp, swap, true) ==
                 MP_SCENE_RESPAWN_BY_IMAGE,
             "any other caller in the executable");
    ut_check(mp_scene_respawn_caller((uintptr_t)0x10001234u, warp, swap, false) ==
                 MP_SCENE_RESPAWN_BY_DLL,
             "a caller outside it, which is this feature's own re-entry");
    ut_check(mp_scene_respawn_caller(warp, 0u, swap, true) == MP_SCENE_RESPAWN_BY_IMAGE,
             "a warp whose site did not resolve is nobody's warp, never every caller's");
    ut_check(mp_scene_respawn_caller(0u, 0u, 0u, true) == MP_SCENE_RESPAWN_BY_IMAGE,
             "and a nought caller matches no nought site");
}

/* The scene watch sorted a hero's spawn inline before the rule existed. That form is the
 * reference the rule is held to, over every input. */
static mp_scene_hero_origin_t the_inline_origin(bool inside_image, bool script_running)
{
    if (!inside_image) {
        return MP_SCENE_HERO_BY_DLL;
    }
    if (!script_running) {
        return MP_SCENE_HERO_BY_ENGINE;
    }
    return MP_SCENE_HERO_BY_SCRIPT;
}

static void check_where_a_hero_came_from(void)
{
    unsigned bits;

    ut_section("a hero's spawn is a scene only from a script inside the executable");
    for (bits = 0u; bits < 4u; ++bits) {
        bool inside  = (bits & 1u) != 0u;
        bool running = (bits & 2u) != 0u;

        ut_checkf(mp_scene_hero_origin(inside, running) == the_inline_origin(inside, running),
                  "inside %u, a script running %u: as the watch sorted it before",
                  (unsigned)inside, (unsigned)running);
    }
}

/* The hero's window of the campaign bank is bytes 6 to 11, bits 48 to 95; the shared story is
 * bits 51 to 84. */
static void check_what_a_warp_did_to_the_quest_bits(void)
{
    uint8_t  before[6];
    uint8_t  after[6];
    uint32_t in_band = 99u;

    ut_section("a warp's spawn reloads the hero's quest bits, and the count says which changed");
    memset(before, 0, sizeof before);
    memcpy(after, before, sizeof after);
    ut_check(mp_scene_bits_changed(before, after, 6u, 48u, 51u, 84u, &in_band) == 0u &&
                 in_band == 0u,
             "two equal windows change nothing");

    after[0] = 0x07u;   /* bits 48, 49, 50: the hero's, outside the story */
    ut_check(mp_scene_bits_changed(before, after, 6u, 48u, 51u, 84u, &in_band) == 3u &&
                 in_band == 0u,
             "the three bits below the story are counted and none in it");

    after[0] = 0x08u;   /* bit 51, the story's first */
    after[4] = 0x10u;   /* bit 84, its last */
    after[4] |= 0x20u;  /* bit 85, the hero's again */
    ut_check(mp_scene_bits_changed(before, after, 6u, 48u, 51u, 84u, &in_band) == 3u &&
                 in_band == 2u,
             "bits 51 and 84 are the story's, 85 is not: both ends of the band are inside it");

    memset(after, 0xFF, sizeof after);
    ut_check(mp_scene_bits_changed(before, after, 6u, 48u, 51u, 84u, &in_band) == 48u &&
                 in_band == 34u,
             "a whole window of change is forty eight bits, thirty four of them the story");
    ut_check(mp_scene_bits_changed(before, after, 6u, 48u, 51u, 84u, NULL) == 48u,
             "and the band may go uncounted");
    ut_check(mp_scene_bits_changed(NULL, after, 6u, 48u, 51u, 84u, &in_band) == 0u &&
                 in_band == 0u,
             "with nothing to compare, nothing changed");
}

/* The dolly's camera on the host: a take inside the lock or inside a scene that runs for
 * everybody belongs to that scene, and only a take outside both is weighed as a scene of its own.
 * The lock goes first, because it is the older answer and has its own counter. */
static void check_whose_camera_a_take_is(void)
{
    static const int32_t LOCKS[4] = { 0, 1, MP_SCENE_LOCK_LEVEL, 99 };
    size_t               lock;
    unsigned             for_all;
    unsigned             wrong = 0u;

    ut_section("a camera take inside a scene for everybody is that scene's, as one inside a lock");
    ut_check(mp_scene_camera_owner(0, true) == MP_SCENE_CAMERA_OF_ALL,
             "a gathered hero scene that never raised the lock keeps its camera on the host");
    ut_check(mp_scene_camera_owner(1, true) == MP_SCENE_CAMERA_OF_ALL,
             "and so does one with a menu's lock of one");
    ut_check(mp_scene_camera_owner(0, false) == MP_SCENE_CAMERA_OF_ITS_OWN,
             "with no scene running a take is weighed as a scene of its own");
    for (lock = 0u; lock < 4u; ++lock) {
        for (for_all = 0u; for_all < 2u; ++for_all) {
            mp_scene_camera_owner_t owner = mp_scene_camera_owner(LOCKS[lock], for_all != 0u);
            mp_scene_camera_owner_t want  = LOCKS[lock] >= MP_SCENE_LOCK_LEVEL
                                                ? MP_SCENE_CAMERA_OF_THE_LOCK
                                                : (for_all != 0u ? MP_SCENE_CAMERA_OF_ALL
                                                                 : MP_SCENE_CAMERA_OF_ITS_OWN);

            wrong += owner == want ? 0u : 1u;
        }
    }
    ut_checkf(wrong == 0u, "over every lock level and both answers of the scene for all, the lock "
              "first: %u wrong", wrong);
}

int main(void)
{
    check_when_a_scene_runs();
    check_who_may_be_moved();
    check_who_asked_for_a_respawn();
    check_where_a_hero_came_from();
    check_what_a_warp_did_to_the_quest_bits();
    check_whose_camera_a_take_is();
    return ut_summary("the scene's single questions");
}
