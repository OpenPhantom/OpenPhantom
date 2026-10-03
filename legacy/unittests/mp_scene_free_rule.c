/* What of a scene's hold on a player is given back: the plan out of one look.
 *
 * The case in front of it: a savegame saved in the middle of a scene restores the lock with its
 * input mode, the bars, the camera and the actor that drives the player's body, past every door.
 * A client is in no scene, so on a client all of it goes on the first substep, with no wait; a
 * look with the button set asks the same rule for the player who presses for himself, on either
 * role. What the rule must never do is as much of it as what it does: release a lock under an
 * open menu, set an input mode that is a menu's, clear the camera of a gun or of a corpse, write
 * a far body's record, or start a module the developer menu holds.
 */
#include "unittest.h"

#include "mp_scene_free_rule.h"
#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ALL_ASKS                                                                        \
    (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |                  \
     MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_MODULE | MP_SCENE_FREE_ACTOR |           \
     MP_SCENE_FREE_STORE | MP_SCENE_FREE_MODULE_ALONE)

#define CLIENT_ASKS MP_SCENE_FREE_CLIENT_ASKS

/* The lock level a client that is never held counts from, and the button's. */
#define MIN_CLIENT MP_SCENE_LOCK_LEVEL
#define MIN_BUTTON 1

/* A player nothing holds: no lock, the input mode at play, a running module with nothing in the
 * store, a body, and the camera's clearing bound. */
static mp_scene_free_look_t free_player(bool client)
{
    mp_scene_free_look_t look;

    memset(&look, 0, sizeof look);
    look.client       = client;
    look.lock_level   = 0;
    look.input_mode   = MP_SCENE_INPUT_MODE_PLAY;
    look.camera_bound = true;
    look.module       = 1u;
    look.has_body     = true;
    return look;
}

/* A client as a savegame saved in the middle of a hero scene leaves it. */
static mp_scene_free_look_t restored_client(void)
{
    mp_scene_free_look_t look = free_player(true);

    look.lock_level = MP_SCENE_LOCK_LEVEL;
    look.input_mode = MP_SCENE_INPUT_MODE_LOCK;
    look.bars_on    = true;
    look.module     = 0u;
    look.store      = 1u;
    look.driven     = true;
    return look;
}

static uint32_t plan_of(const mp_scene_free_look_t *look, uint32_t asked, int32_t min_lock)
{
    return mp_scene_free_plan(look, asked, min_lock, NULL);
}

static void check_nothing_held(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0xFFu;

    ut_section("nothing held, nothing planned");
    look = free_player(true);
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == 0u && left == 0u,
             "a client nothing holds: no plan, and nothing left for a reason");
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u,
             "and none for everything asked at the button's level either, with no button");
    look = free_player(false);
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "a host nothing holds: no plan");
    look.button = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == MP_SCENE_FREE_CAMERA,
             "the button with nothing held clears the camera and nothing else");
    look = restored_client();
    ut_check(plan_of(&look, 0u, MIN_CLIENT) == 0u, "nothing asked: nothing planned");
    ut_check(mp_scene_free_plan(NULL, ALL_ASKS, MIN_BUTTON, &left) == 0u && left == 0u,
             "no look: no plan");

    ut_section("a client a savegame left in the middle of a hero scene");
    look = restored_client();
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) ==
                     (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |
                      MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_MODULE) &&
                 left == 0u,
             "the lock, its input mode, the bars, the camera and the actor go in one plan");
    look.module = 1u;
    look.driven = false;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) ==
                 (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |
                  MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_STORE),
             "with the module running again the store it left is cleared in the next");
}

static void check_the_lock(void)
{
    static const int32_t LEVELS[] = { -1, 0, 1, 4, 5, 6, 99 };
    mp_scene_free_look_t look;
    uint8_t              left = 0u;
    size_t               i;

    ut_section("the lock: at the level that counts or above, and never under an open menu");
    for (i = 0u; i < sizeof LEVELS / sizeof LEVELS[0]; ++i) {
        bool goes;

        look            = free_player(true);
        look.lock_level = LEVELS[i];
        goes = (plan_of(&look, MP_SCENE_FREE_LOCK, MIN_CLIENT) & MP_SCENE_FREE_LOCK) != 0u;
        ut_checkf(goes == (LEVELS[i] >= MP_SCENE_LOCK_LEVEL),
                  "a client, the lock at %d, counted from the script's level: %s",
                  (int)LEVELS[i], goes ? "released" : "left");
        goes = (plan_of(&look, MP_SCENE_FREE_LOCK, MIN_BUTTON) & MP_SCENE_FREE_LOCK) != 0u;
        ut_checkf(goes == (LEVELS[i] >= 1), "the lock at %d, counted from one: %s",
                  (int)LEVELS[i], goes ? "released" : "left");
        goes = (plan_of(&look, MP_SCENE_FREE_LOCK, 0) & MP_SCENE_FREE_LOCK) != 0u ||
               (plan_of(&look, MP_SCENE_FREE_LOCK, -3) & MP_SCENE_FREE_LOCK) != 0u;
        ut_checkf(goes == (LEVELS[i] >= 1),
                  "the lock at %d, counted from nought or below: a lock at nought or one that "
                  "did not read is never released", (int)LEVELS[i]);
    }
    look            = free_player(true);
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check(plan_of(&look, CLIENT_ASKS & ~MP_SCENE_FREE_LOCK, MIN_CLIENT) == 0u,
             "a lock that is not asked for stays, and nothing goes along with it");
    look.menu_open = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == 0u &&
                 left == MP_SCENE_FREE_LEFT_MENU,
             "a menu of the engine open: the lock stays, and the reason is said");
    look.menu_open = false;
    ut_check((mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) & MP_SCENE_FREE_LOCK) !=
                     0u && left == 0u,
             "and it goes with the menu closed");
}

static void check_the_input_mode(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;
    int32_t              mode;

    ut_section("the input mode: the lock's alone, over a lock of nought or one that goes");
    for (mode = -1; mode <= 6; ++mode) {
        bool goes;

        look            = free_player(true);
        look.input_mode = mode;
        goes = (plan_of(&look, CLIENT_ASKS, MIN_CLIENT) & MP_SCENE_FREE_INPUT_MODE) != 0u;
        ut_checkf(goes == (mode == MP_SCENE_INPUT_MODE_LOCK),
                  "a client, the lock at nought, the mode %d: %s", (int)mode,
                  goes ? "set to play" : "left");
    }
    look            = free_player(true);
    look.input_mode = MP_SCENE_INPUT_MODE_LOCK;
    look.menu_open  = true;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u,
             "a menu open: the mode stays, whatever it reads");
    look            = free_player(true);
    look.input_mode = MP_SCENE_INPUT_MODE_LOCK;
    look.lock_level = 1;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u,
             "a lock of a dialogue standing at one, not counted: the mode is the lock's");
    look.lock_level = -1;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u,
             "a lock that did not read: the mode stays");
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check(plan_of(&look, MP_SCENE_FREE_INPUT_MODE, MIN_CLIENT) == 0u,
             "a script's lock standing and not asked for: the mode stays");
    ut_check((plan_of(&look, CLIENT_ASKS, MIN_CLIENT) & MP_SCENE_FREE_INPUT_MODE) != 0u,
             "the lock going in this plan: the mode goes with it");

    ut_section("the input mode on a host, and under a conversation");
    look            = free_player(false);
    look.input_mode = MP_SCENE_INPUT_MODE_LOCK;
    ut_check(plan_of(&look, ALL_ASKS, MIN_CLIENT) == 0u,
             "a host, the lock at nought and the lock's mode, no button: left to the menu's "
             "own look");
    look.button = true;
    ut_check((plan_of(&look, ALL_ASKS, MIN_BUTTON) & MP_SCENE_FREE_INPUT_MODE) != 0u,
             "the button on a host: set to play");
    look.button     = false;
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check((plan_of(&look, ALL_ASKS, MIN_CLIENT) & MP_SCENE_FREE_INPUT_MODE) != 0u,
             "a host whose lock goes in this plan: the mode with it");
    look              = free_player(true);
    look.input_mode   = MP_SCENE_INPUT_MODE_LOCK;
    look.conversation = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == 0u &&
                 left == MP_SCENE_FREE_LEFT_CONVERSATION,
             "answers of a conversation open before their lock is raised: the mode is theirs");
    look.button = true;
    ut_check((mp_scene_free_plan(&look, ALL_ASKS, MIN_BUTTON, &left) &
              MP_SCENE_FREE_INPUT_MODE) == 0u,
             "for the button as well");
}

static void check_the_bars_and_the_camera(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;

    ut_section("the bars, and the camera along with a lock or with the bars");
    look         = free_player(true);
    look.bars_on = true;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) ==
                 (MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA),
             "the bars alone: down, and the camera's override cleared along with them");
    ut_check(plan_of(&look, MP_SCENE_FREE_CAMERA, MIN_CLIENT) == 0u,
             "the bars not asked for: the camera has nothing to go along with");
    look            = free_player(true);
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) ==
                 (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_CAMERA),
             "a lock alone: released, and the camera along with it");
    look.menu_open = true;
    ut_check((plan_of(&look, CLIENT_ASKS, MIN_CLIENT) & MP_SCENE_FREE_CAMERA) == 0u,
             "a lock that waits for a menu takes no camera along");
    look.bars_on = true;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) ==
                 (MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA),
             "but the bars go under the menu, and the camera with them");

    ut_section("the camera is never cleared where the override is the engine's own");
    look         = free_player(true);
    look.bars_on = true;
    look.at_gun  = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == MP_SCENE_FREE_BARS &&
                 left == MP_SCENE_FREE_LEFT_GUN,
             "at the tripod gun: the bars go, the camera stays, and the reason is said");
    look.at_gun = false;
    look.dead   = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == MP_SCENE_FREE_BARS &&
                 left == MP_SCENE_FREE_LEFT_DEAD,
             "a dead player: the same");
    look.dead         = false;
    look.camera_bound = false;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == MP_SCENE_FREE_BARS &&
                 left == MP_SCENE_FREE_LEFT_UNBOUND,
             "the clearing not resolved: the same");
    look.camera_bound = true;
    look.bank_window  = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == MP_SCENE_FREE_BARS &&
                 left == MP_SCENE_FREE_LEFT_BANK,
             "a look from inside a bank window cannot tell a gun or a corpse: the same");

    ut_section("the camera for the button: always, but for a gun, a corpse or no binding");
    look        = free_player(false);
    look.button = true;
    ut_check(plan_of(&look, MP_SCENE_FREE_CAMERA, MIN_BUTTON) == MP_SCENE_FREE_CAMERA,
             "the button with no lock and no bars: cleared");
    look.at_gun = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "the button at the gun: nothing");
    look.at_gun = false;
    look.dead   = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "the button for a corpse: nothing");
    look.dead         = false;
    look.camera_bound = false;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "the button with no binding: nothing");
}

static void check_the_actor(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;
    uint32_t             module;

    ut_section("a client's module goes with the actor that drives its body");
    look              = free_player(true);
    look.module       = 0u;
    look.store        = 1u;
    look.driven       = true;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == MP_SCENE_FREE_MODULE,
             "stopped, a state in the store, a body and a driver: the actor is removed");
    ut_check(plan_of(&look, ALL_ASKS, MIN_CLIENT) == MP_SCENE_FREE_MODULE,
             "and never as a host's actor, whatever is asked");
    look.driven = false;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u,
             "nobody drives, the developer menu or the free camera: nothing");
    look.driven   = true;
    look.has_body = false;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u, "no body: nothing");
    look.has_body = true;
    look.store    = 0u;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u,
             "nothing in the store: the engine's put-back would write nought, so nothing");
    look.store = 1u;
    for (module = 1u; module <= 4u; ++module) {
        look.module = module;
        ut_checkf((plan_of(&look, CLIENT_ASKS, MIN_CLIENT) & MP_SCENE_FREE_MODULE) == 0u,
                  "the module at %u, not stopped: no actor is removed", (unsigned)module);
    }
    look.module      = 0u;
    look.bank_window = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == 0u &&
                 left == MP_SCENE_FREE_LEFT_BANK,
             "inside a bank window the record is a far body's: nothing, and the reason is said");

    ut_section("the same picture on a host is the actor, and only when asked");
    look        = free_player(false);
    look.module = 0u;
    look.store  = 1u;
    look.driven = true;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u,
             "what a client asks names no host's actor: nothing");
    ut_check(plan_of(&look, ALL_ASKS, MIN_CLIENT) == MP_SCENE_FREE_ACTOR,
             "asked: the actor is told to leave, and never removed as a client's is");
    look.button = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) ==
                 (MP_SCENE_FREE_ACTOR | MP_SCENE_FREE_CAMERA),
             "the button on a host in a hero scene: the actor and the camera");
    look.bank_window = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "inside a bank window: nothing");
}

static void check_the_store_and_a_module_alone(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;
    uint32_t             module;

    ut_section("a client's store that nothing here wrote");
    for (module = 0u; module <= 4u; ++module) {
        bool goes;

        look        = free_player(true);
        look.module = module;
        look.store  = 1u;
        goes = (plan_of(&look, CLIENT_ASKS, MIN_CLIENT) & MP_SCENE_FREE_STORE) != 0u;
        ut_checkf(goes == (module != 0u),
                  "a state in the store with the module at %u: %s", (unsigned)module,
                  goes ? "cleared" : "left, it is a parked module's");
    }
    look       = free_player(true);
    look.store = 0u;
    ut_check(plan_of(&look, CLIENT_ASKS, MIN_CLIENT) == 0u, "an empty store: nothing");
    look       = free_player(false);
    look.store = 1u;
    ut_check(plan_of(&look, ALL_ASKS, MIN_CLIENT) == 0u,
             "a host's store is its own scene's: never cleared");
    look             = free_player(true);
    look.store       = 1u;
    look.bank_window = true;
    ut_check(mp_scene_free_plan(&look, CLIENT_ASKS, MIN_CLIENT, &left) == 0u &&
                 left == MP_SCENE_FREE_LEFT_BANK,
             "inside a bank window: not written");

    ut_section("a stopped module nobody drives goes for the button alone");
    look        = free_player(true);
    look.module = 0u;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "no button: never");
    look.button = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) ==
                 (MP_SCENE_FREE_MODULE_ALONE | MP_SCENE_FREE_CAMERA),
             "the button, nothing in the store: set running");
    look.store = 1u;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) ==
                 (MP_SCENE_FREE_MODULE_ALONE | MP_SCENE_FREE_CAMERA),
             "the button, a state in the store: put back");
    look.client = false;
    ut_check((plan_of(&look, ALL_ASKS, MIN_BUTTON) & MP_SCENE_FREE_MODULE_ALONE) != 0u,
             "on a host as well");
    look.overlay_holds = true;
    ut_check((mp_scene_free_plan(&look, ALL_ASKS, MIN_BUTTON, &left) &
              MP_SCENE_FREE_MODULE_ALONE) == 0u &&
                 (left & MP_SCENE_FREE_LEFT_OVERLAY) != 0u,
             "the developer menu holds the module itself: left, and the reason is said");
    look.overlay_holds = false;
    look.driven        = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) ==
                 (MP_SCENE_FREE_ACTOR | MP_SCENE_FREE_CAMERA),
             "an actor drives it: that is the actor's way out, not this one");
    look.driven   = false;
    look.has_body = false;
    ut_check((plan_of(&look, ALL_ASKS, MIN_BUTTON) & MP_SCENE_FREE_MODULE_ALONE) == 0u,
             "no body, the time between a despawn and a spawn: never");
    look.has_body = true;
    for (module = 1u; module <= 4u; ++module) {
        look.module = module;
        ut_checkf((plan_of(&look, ALL_ASKS, MIN_BUTTON) & MP_SCENE_FREE_MODULE_ALONE) == 0u,
                  "the module at %u, running, quitting, dying or coming back: never touched",
                  (unsigned)module);
    }
    look.module      = 0u;
    look.bank_window = true;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u, "inside a bank window: nothing");
}

static void check_a_conversation(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;

    ut_section("the button under a conversation's open answers");
    look              = free_player(false);
    look.button       = true;
    look.conversation = true;
    look.lock_level   = 1;
    look.input_mode   = MP_SCENE_INPUT_MODE_LOCK;
    ut_check(mp_scene_free_plan(&look, ALL_ASKS, MIN_BUTTON, &left) == 0u &&
                 left == MP_SCENE_FREE_LEFT_CONVERSATION,
             "the list's own lock at one and the camera stay, and the reason is said");
    look.lock_level = MP_SCENE_LOCK_LEVEL - 1;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) == 0u,
             "a lock one below a script's level is still the list's");
    look.lock_level = MP_SCENE_LOCK_LEVEL;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) ==
                 (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_INPUT_MODE),
             "a script's lock over it goes, with its mode, and the camera still stays");
    look.conversation = false;
    look.lock_level   = 1;
    ut_check(plan_of(&look, ALL_ASKS, MIN_BUTTON) ==
                 (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_CAMERA),
             "with no answers open a lock at one goes for the button, and the camera");
}

/* Every look the fields can make, against what a plan may never contain. */
static void check_every_look(void)
{
    static const int32_t  LEVELS[] = { -1, 0, 1, MP_SCENE_LOCK_LEVEL };
    static const int32_t  MODES[]  = { -1, MP_SCENE_INPUT_MODE_PLAY, 2, MP_SCENE_INPUT_MODE_LOCK };
    static const uint32_t ASKS[]   = { CLIENT_ASKS, ALL_ASKS, MP_SCENE_FREE_LOCK,
                                       MP_SCENE_FREE_CAMERA, MP_SCENE_FREE_INPUT_MODE };
    static const int32_t  MINS[]   = { MIN_BUTTON, MIN_CLIENT };
    const uint32_t        record   = MP_SCENE_FREE_MODULE | MP_SCENE_FREE_ACTOR |
                                     MP_SCENE_FREE_STORE | MP_SCENE_FREE_MODULE_ALONE;
    uint32_t              looks    = 0u;
    uint32_t              beyond   = 0u;   /* more than was asked */
    uint32_t              in_bank  = 0u;   /* a record or a camera planned in a bank window */
    uint32_t              bad_lock = 0u;
    uint32_t              bad_mode = 0u;
    uint32_t              bad_cam  = 0u;
    uint32_t              bad_rec  = 0u;
    uint32_t              bad_menu = 0u;   /* the button under an open menu took more than bars */
    uint32_t              differs  = 0u;   /* the plan depends on whether a reason is taken */
    uint32_t              bits;
    size_t                l, m, a, n;
    uint32_t              module;

    ut_section("every look, against what a plan may never contain");
    for (bits = 0u; bits < (1u << 13); ++bits) {
        for (l = 0u; l < 4u; ++l) {
            for (m = 0u; m < 4u; ++m) {
                for (module = 0u; module <= 4u; ++module) {
                    mp_scene_free_look_t look;

                    memset(&look, 0, sizeof look);
                    look.client        = (bits & 0x0001u) != 0u;
                    look.button        = (bits & 0x0002u) != 0u;
                    look.menu_open     = (bits & 0x0004u) != 0u;
                    look.bars_on       = (bits & 0x0008u) != 0u;
                    look.camera_bound  = (bits & 0x0010u) != 0u;
                    look.dead          = (bits & 0x0020u) != 0u;
                    look.at_gun        = (bits & 0x0040u) != 0u;
                    look.store         = (bits & 0x0080u) != 0u ? 1u : 0u;
                    look.has_body      = (bits & 0x0100u) != 0u;
                    look.driven        = (bits & 0x0200u) != 0u;
                    look.bank_window   = (bits & 0x0400u) != 0u;
                    look.conversation  = (bits & 0x0800u) != 0u;
                    look.overlay_holds = (bits & 0x1000u) != 0u;
                    look.lock_level    = LEVELS[l];
                    look.input_mode    = MODES[m];
                    look.module        = module;
                    for (a = 0u; a < sizeof ASKS / sizeof ASKS[0]; ++a) {
                        for (n = 0u; n < 2u; ++n) {
                            uint8_t  left = 0u;
                            uint32_t plan = mp_scene_free_plan(&look, ASKS[a], MINS[n], &left);
                            bool     park = module == 0u && look.has_body;

                            ++looks;
                            beyond  += (plan & ~ASKS[a]) != 0u ? 1u : 0u;
                            differs += plan != plan_of(&look, ASKS[a], MINS[n]) ? 1u : 0u;
                            in_bank += look.bank_window &&
                                               (plan & (record | MP_SCENE_FREE_CAMERA)) != 0u
                                           ? 1u : 0u;
                            bad_lock += (plan & MP_SCENE_FREE_LOCK) != 0u &&
                                                (look.menu_open || look.lock_level < 1 ||
                                                 look.lock_level < MINS[n])
                                            ? 1u : 0u;
                            bad_mode += (plan & MP_SCENE_FREE_INPUT_MODE) != 0u &&
                                                (look.input_mode != MP_SCENE_INPUT_MODE_LOCK ||
                                                 look.menu_open ||
                                                 (look.lock_level != 0 &&
                                                  (plan & MP_SCENE_FREE_LOCK) == 0u))
                                            ? 1u : 0u;
                            bad_cam += (plan & MP_SCENE_FREE_CAMERA) != 0u &&
                                               (!look.camera_bound || look.dead || look.at_gun ||
                                                (!look.button &&
                                                 (plan & (MP_SCENE_FREE_LOCK |
                                                          MP_SCENE_FREE_BARS)) == 0u))
                                           ? 1u : 0u;
                            bad_rec += ((plan & MP_SCENE_FREE_MODULE) != 0u &&
                                        !(look.client && park && look.store != 0u &&
                                          look.driven)) ||
                                               ((plan & MP_SCENE_FREE_ACTOR) != 0u &&
                                                !(!look.client && park && look.store != 0u &&
                                                  look.driven)) ||
                                               ((plan & MP_SCENE_FREE_STORE) != 0u &&
                                                !(look.client && look.store != 0u &&
                                                  module != 0u)) ||
                                               ((plan & MP_SCENE_FREE_MODULE_ALONE) != 0u &&
                                                !(look.button && park && !look.driven &&
                                                  !look.overlay_holds))
                                           ? 1u : 0u;
                            bad_menu += look.button && look.menu_open &&
                                                (plan & ~MP_SCENE_FREE_BARS) != 0u
                                            ? 1u : 0u;
                        }
                    }
                }
            }
        }
    }
    ut_checkf(beyond == 0u, "%u plan(s) over %u look(s): none holds what was not asked for",
              (unsigned)beyond, (unsigned)looks);
    ut_checkf(differs == 0u, "%u plan(s) change with whether a reason is taken",
              (unsigned)differs);
    ut_checkf(in_bank == 0u,
              "%u plan(s) inside a bank window write the record or clear the camera",
              (unsigned)in_bank);
    ut_checkf(bad_lock == 0u,
              "%u plan(s) release a lock under a menu, at nought, unread or below its level",
              (unsigned)bad_lock);
    ut_checkf(bad_mode == 0u,
              "%u plan(s) set a mode that is not the lock's, under a menu, or under a lock "
              "that stays", (unsigned)bad_mode);
    ut_checkf(bad_cam == 0u,
              "%u plan(s) clear a camera unbound, of a corpse, of a gun, or alone with no button",
              (unsigned)bad_cam);
    ut_checkf(bad_rec == 0u,
              "%u plan(s) remove an actor, clear a store or start a module without its evidence",
              (unsigned)bad_rec);
    ut_checkf(bad_menu == 0u,
              "%u plan(s) of the button under an open menu take more than the bars",
              (unsigned)bad_menu);
}

int main(void)
{
    check_nothing_held();
    check_the_lock();
    check_the_input_mode();
    check_the_bars_and_the_camera();
    check_the_actor();
    check_the_store_and_a_module_alone();
    check_a_conversation();
    check_every_look();
    return ut_summary("what of a scene's hold is given back");
}
