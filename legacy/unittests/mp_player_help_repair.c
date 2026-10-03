/* The two buttons of the developer menu, as rules: the repair.
 *
 * "Repair lock" gives a player back what holds his controls and his camera, and does nothing
 * where nothing holds him. The rules are held against the real plan of a scene's release: what
 * a press asks on each role, what counts as nothing to do, which input hold has lost its owner,
 * when the host's scene latches its doors, what the answer says was left standing, and when the
 * two looks after a press on a host are due. The holders are read back through the real record
 * of the standing transport.
 *
 * The reader and the teleport are in mp_player_help_rule.c.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_player_help_rule.h"
#include "mp_scene_free_rule.h"
#include "mp_scene_rule.h"

#include "common/player_help_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The button counts a lock from level one. */
#define MIN_BUTTON 1

/* A player nothing holds, as the release of a scene looks at him, with the button set. */
static mp_scene_free_look_t free_player(bool client)
{
    mp_scene_free_look_t look;

    memset(&look, 0, sizeof look);
    look.client       = client;
    look.input_mode   = MP_SCENE_INPUT_MODE_PLAY;
    look.camera_bound = true;
    look.module       = 1u;
    look.has_body     = true;
    look.button       = true;
    return look;
}

/* The same player in the middle of a hero scene under a script's lock. */
static mp_scene_free_look_t in_a_hero_scene(bool client)
{
    mp_scene_free_look_t look = free_player(client);

    look.lock_level = MP_SCENE_LOCK_LEVEL;
    look.input_mode = MP_SCENE_INPUT_MODE_LOCK;
    look.bars_on    = true;
    look.module     = 0u;
    look.store      = 1u;
    look.driven     = true;
    return look;
}

static uint32_t plan_for(const mp_scene_free_look_t *look, bool is_client, uint8_t *left)
{
    return mp_scene_free_plan(look, mp_repair_asks(is_client), MIN_BUTTON, left);
}

static void check_what_a_repair_asks(void)
{
    const uint32_t       host   = mp_repair_asks(false);
    const uint32_t       client = mp_repair_asks(true);
    const uint32_t       both   = MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |
                                  MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_MODULE_ALONE;
    mp_scene_free_look_t look;
    uint8_t              left   = 0u;
    uint8_t              reason = 0xFFu;

    ut_section("the door of a repair, and what it asks of the release on each role");

    ut_check(mp_repair_refused(false, true, &reason) && reason == PLAYER_HELP_REASON_NO_LEVEL,
             "no level running: refused, there is nothing of the engine's to give back");
    ut_check(mp_repair_refused(true, false, &reason) && reason == PLAYER_HELP_REASON_NOT_BOUND,
             "the lock's release not bound: refused");
    ut_check(mp_repair_refused(false, false, &reason) && reason == PLAYER_HELP_REASON_NO_LEVEL,
             "both: the level is asked first");
    ut_check(!mp_repair_refused(true, true, &reason) && reason == PLAYER_HELP_REASON_NONE &&
                 !mp_repair_refused(true, true, NULL),
             "a running level with the release bound is let through, with no reason");

    ut_check(host == (both | MP_SCENE_FREE_ACTOR),
             "a host asks for the lock, the bars, the camera, the input mode, a module nobody "
             "drives, and the actor told to leave; never for the removal or the store");
    ut_check(client == (both | MP_SCENE_FREE_MODULE | MP_SCENE_FREE_STORE),
             "a client asks for the same, with the actor removed and the store; never for the "
             "host's way");

    look = in_a_hero_scene(false);
    ut_check(plan_for(&look, false, &left) ==
                     (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |
                      MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_ACTOR) &&
                 left == 0u,
             "a host in a hero scene: the lock, its mode, the bars, the camera and the actor");
    look = in_a_hero_scene(true);
    ut_check(plan_for(&look, true, &left) ==
                 (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |
                  MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_MODULE),
             "a client a savegame left in one: the same, with the actor removed");
    look = in_a_hero_scene(true);
    ut_check((plan_for(&look, false, &left) &
              (MP_SCENE_FREE_MODULE | MP_SCENE_FREE_ACTOR | MP_SCENE_FREE_STORE)) == 0u,
             "a client of a started session asked with the host's set: no actor is touched");
    look = free_player(false);
    look.lock_level = 1;
    look.input_mode = MP_SCENE_INPUT_MODE_LOCK;
    ut_check(plan_for(&look, false, &left) ==
                 (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_CAMERA),
             "a lock at level one with nothing behind it goes for the button");
}

static void check_nothing_to_do(void)
{
    mp_scene_free_look_t look;
    uint8_t              left = 0u;
    uint32_t             plan;
    uint32_t             bit;
    unsigned             held = 0u;

    ut_section("nothing to do: nothing planned but the camera, and nothing of the mod's left");

    look = free_player(false);
    plan = plan_for(&look, false, &left);
    ut_check(plan == MP_SCENE_FREE_CAMERA && mp_repair_found_nothing(plan, 0u),
             "a host nothing holds: the camera is cleared all the same, and that is nothing");
    look = free_player(true);
    plan = plan_for(&look, true, &left);
    ut_check(plan == MP_SCENE_FREE_CAMERA && mp_repair_found_nothing(plan, 0u),
             "a client nothing holds: the same");
    look.at_gun = true;
    plan = plan_for(&look, true, &left);
    ut_check(plan == 0u && mp_repair_found_nothing(plan, 0u) &&
                 mp_repair_reason(left, false) == PLAYER_HELP_REASON_AT_A_GUN,
             "at the tripod gun not even the camera goes, and the answer says why");
    look        = free_player(false);
    look.dead   = true;
    plan = plan_for(&look, false, &left);
    ut_check(plan == 0u && mp_repair_found_nothing(plan, 0u) &&
                 mp_repair_reason(left, false) == PLAYER_HELP_REASON_DEAD,
             "nor for a dead player");

    for (bit = 1u; bit < (1u << MP_SCENE_FREE_BITS); bit <<= 1) {
        if (bit != MP_SCENE_FREE_CAMERA) {
            held += !mp_repair_found_nothing(bit, 0u) ? 1u : 0u;
            held += !mp_repair_found_nothing(bit | MP_SCENE_FREE_CAMERA, 0u) ? 1u : 0u;
        }
    }
    ut_checkf(held == 2u * (MP_SCENE_FREE_BITS - 1u),
              "each of the other %u things planned is something that held, with the camera or "
              "without", (unsigned)(MP_SCENE_FREE_BITS - 1u));
    ut_check(!mp_repair_found_nothing(0u, MP_REPAIR_LEFT_SCENE) &&
                 !mp_repair_found_nothing(MP_SCENE_FREE_CAMERA, MP_REPAIR_LEFT_TELEPORT) &&
                 !mp_repair_found_nothing(0u, MP_REPAIR_CLOSED_CHAT) &&
                 !mp_repair_found_nothing(0u, MP_REPAIR_DROPPED_PAUSE) &&
                 !mp_repair_found_nothing(0u, MP_REPAIR_DROPPED_CHAT),
             "a state of the mod that was left, or a hold that fell, is something that held");
    ut_check(mp_repair_found_nothing(0u, 0x0001u) && mp_repair_found_nothing(0u, 0xE000u),
             "a bit that is none of the mod's five counts for nothing");
}

static void check_the_holders(void)
{
    const uint32_t pause = (uint32_t)MP_ARMED_HOLDER_PAUSE;
    const uint32_t chat  = (uint32_t)MP_ARMED_HOLDER_CHAT;
    const uint32_t scene = (uint32_t)MP_ARMED_HOLDER_SCENE;

    ut_section("an input hold nobody owns falls, and one with its owner stays");

    ut_check(mp_repair_orphans(0u, false, false) == 0u, "no hold: nothing to drop");
    ut_check(mp_repair_orphans(pause, false, false) == pause,
             "the pause's hold with no menu open is dropped");
    ut_check(mp_repair_orphans(pause, true, false) == 0u,
             "under an open menu it stays: the player would walk under the menu");
    ut_check(mp_repair_orphans(chat, false, false) == chat,
             "the chat's hold with no line open is dropped");
    ut_check(mp_repair_orphans(chat, false, true) == 0u, "with a line open it stays");
    ut_check(mp_repair_orphans(pause | chat, true, false) == chat &&
                 mp_repair_orphans(pause | chat, false, true) == pause &&
                 mp_repair_orphans(pause | chat, false, false) == (pause | chat) &&
                 mp_repair_orphans(pause | chat, true, true) == 0u,
             "each of the two is judged by its own owner");
    ut_check(mp_repair_orphans(scene, false, false) == 0u &&
                 mp_repair_orphans(scene | pause | chat, false, false) == (pause | chat),
             "the hold of a host's scene is never dropped here: the scene decides it each "
             "substep");

    ut_section("the holders as mp_armed reads them back");
    mp_armed_set_transport(false, false);
    ut_check(mp_armed_holders() == 0u, "no transport: no holder");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    ut_check(mp_armed_holders() == 0u, "a hold asked for with no transport is not taken");
    mp_armed_set_transport(true, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_SCENE, true);
    ut_check(mp_armed_holders() == (pause | scene) && mp_armed_input_held(),
             "two holders read back as their two bits");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, false);
    ut_check(mp_armed_holders() == scene && mp_armed_input_held(),
             "one dropped: the other's bit alone, and the input still held");
    mp_armed_set_transport(false, false);
    ut_check(mp_armed_holders() == 0u && !mp_armed_input_held(),
             "the transport down: every hold gone with it");
}

static void check_the_latch_and_the_answer(void)
{
    static const uint8_t ORDER[] = {
        MP_SCENE_FREE_LEFT_MENU, MP_SCENE_FREE_LEFT_CONVERSATION, MP_SCENE_FREE_LEFT_OVERLAY,
        MP_SCENE_FREE_LEFT_GUN,  MP_SCENE_FREE_LEFT_DEAD,         MP_SCENE_FREE_LEFT_UNBOUND,
    };
    static const uint8_t REASONS[] = {
        PLAYER_HELP_REASON_MENU_OPEN, PLAYER_HELP_REASON_CONVERSATION,
        PLAYER_HELP_REASON_OVERLAY_HOLDS, PLAYER_HELP_REASON_AT_A_GUN, PLAYER_HELP_REASON_DEAD,
        PLAYER_HELP_REASON_NOT_BOUND,
    };
    mp_player_help_verdict_t verdict;
    uint32_t                 given;
    unsigned                 latched = 0u;
    unsigned                 right   = 0u;
    size_t                   first;
    size_t                   later;

    ut_section("the latch: only on a host, and only when the engine gave something back");

    ut_check(mp_repair_latches(false, MP_SCENE_FREE_LOCK) &&
                 mp_repair_latches(false, MP_SCENE_FREE_BARS) &&
                 mp_repair_latches(false, MP_SCENE_FREE_ACTOR),
             "the lock, the bars or the actor taken back each arm it");
    ut_check(!mp_repair_latches(false, 0u) && !mp_repair_latches(false, MP_SCENE_FREE_CAMERA) &&
                 !mp_repair_latches(false, MP_SCENE_FREE_INPUT_MODE) &&
                 !mp_repair_latches(false, MP_SCENE_FREE_MODULE_ALONE) &&
                 !mp_repair_latches(false, MP_SCENE_FREE_CAMERA | MP_SCENE_FREE_INPUT_MODE |
                                               MP_SCENE_FREE_MODULE_ALONE),
             "the camera, which goes on every press, the input mode and a module nobody drove "
             "do not");
    for (given = 0u; given < (1u << MP_SCENE_FREE_BITS); ++given) {
        latched += mp_repair_latches(true, given) ? 1u : 0u;
    }
    ut_check(latched == 0u, "a client never latches, whatever was given back");
    ut_check(mp_repair_only_left_the_mod(MP_REPAIR_LEFT_SCENE, 0u) &&
                 mp_repair_only_left_the_mod(MP_REPAIR_LEFT_SCENE, MP_SCENE_FREE_CAMERA),
             "a host still on his way to a scene: the mod's hold left, nothing of the engine's "
             "taken, so the scene plays on and no latch is armed");
    ut_check(!mp_repair_only_left_the_mod(MP_REPAIR_LEFT_SCENE, MP_SCENE_FREE_ACTOR) &&
                 !mp_repair_only_left_the_mod(MP_REPAIR_LEFT_SCENE, MP_SCENE_FREE_LOCK) &&
                 !mp_repair_only_left_the_mod(0u, 0u) &&
                 !mp_repair_only_left_the_mod(MP_REPAIR_CLOSED_CHAT, 0u),
             "a scene the engine ran is not that case, and neither is a press that left no "
             "scene");

    ut_section("the answer: done or nothing, with the one reason for what was left");
    ut_check(mp_repair_reason(0u, false) == PLAYER_HELP_REASON_NONE &&
                 mp_repair_reason(MP_SCENE_FREE_LEFT_BANK, false) == PLAYER_HELP_REASON_NONE,
             "nothing left standing: no reason");
    for (first = 0u; first < sizeof ORDER / sizeof ORDER[0]; ++first) {
        uint8_t all_later = 0u;

        for (later = first; later < sizeof ORDER / sizeof ORDER[0]; ++later) {
            all_later |= ORDER[later];
        }
        right += mp_repair_reason(ORDER[first], false) == REASONS[first] &&
                 mp_repair_reason(all_later, false) == REASONS[first] ? 1u : 0u;
    }
    ut_checkf(right == sizeof ORDER / sizeof ORDER[0],
              "%u of the 6 reasons answer alone and ahead of every one after them", right);
    ut_check(mp_repair_reason(0u, true) == PLAYER_HELP_REASON_MENU_OPEN &&
                 mp_repair_reason(MP_SCENE_FREE_LEFT_GUN, true) == PLAYER_HELP_REASON_MENU_OPEN,
             "a pause hold standing under its open menu is an open menu, ahead of the rest");

    verdict = mp_repair_verdict(MP_SCENE_FREE_CAMERA, MP_SCENE_FREE_CAMERA, 0u, 0u, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_NOTHING &&
                 verdict.reason == PLAYER_HELP_REASON_NONE,
             "only the camera cleared: nothing to do, no reason");
    verdict = mp_repair_verdict(0u, 0u, 0u, MP_SCENE_FREE_LEFT_MENU, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_NOTHING &&
                 verdict.reason == PLAYER_HELP_REASON_MENU_OPEN,
             "a lock left under an open menu and nothing else: nothing done, a menu is open");
    verdict = mp_repair_verdict(MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA,
                                MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA, 0u,
                                MP_SCENE_FREE_LEFT_CONVERSATION, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_DONE &&
                 verdict.reason == PLAYER_HELP_REASON_CONVERSATION,
             "the bars taken down beside an open conversation: done, and the reason for the "
             "rest");
    verdict = mp_repair_verdict(0u, 0u, MP_REPAIR_LEFT_TELEPORT, 0u, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_DONE &&
                 verdict.reason == PLAYER_HELP_REASON_NONE,
             "a teleport left and nothing of the engine's: done");
    verdict = mp_repair_verdict(MP_SCENE_FREE_LOCK | MP_SCENE_FREE_CAMERA, MP_SCENE_FREE_CAMERA,
                                0u, MP_SCENE_FREE_LEFT_GUN, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_REFUSED &&
                 verdict.reason == PLAYER_HELP_REASON_NOT_BOUND,
             "a lock planned that the engine did not release, and nothing else let go: refused "
             "as not bound, not done");
    verdict = mp_repair_verdict(MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS, MP_SCENE_FREE_BARS, 0u,
                                0u, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_DONE,
             "the bars went and the lock did not: done, the line says what was missed");
    verdict = mp_repair_verdict(MP_SCENE_FREE_LOCK, 0u, MP_REPAIR_CLOSED_CHAT, 0u, false);
    ut_check(verdict.outcome == PLAYER_HELP_OUTCOME_DONE,
             "the chat's line closed beside a plan the engine did not carry out: done");

    ut_section("the released word, and what of a plan was not carried out");
    ut_check(mp_repair_released(0xFFu, MP_REPAIR_OF_THE_MOD) == 0x1FFFu &&
                 mp_repair_released(0u, 0u) == 0u,
             "what the engine gave back fills the low byte, the mod's five bits stand above it");
    ut_check(mp_repair_released(0xFFFFFF00u, 0xFFFFE0FFu) == 0u,
             "and nothing outside either set gets in");
    ut_check(mp_repair_not_carried_out(MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS,
                                       MP_SCENE_FREE_BARS) == MP_SCENE_FREE_LOCK,
             "a lock the engine would not release is missed");
    ut_check(mp_repair_not_carried_out(MP_SCENE_FREE_LOCK | MP_SCENE_FREE_INPUT_MODE,
                                       MP_SCENE_FREE_LOCK) == 0u,
             "the input mode is not missed beside a released lock: the release sets it");
    ut_check(mp_repair_not_carried_out(MP_SCENE_FREE_INPUT_MODE, 0u) ==
                     MP_SCENE_FREE_INPUT_MODE &&
                 mp_repair_not_carried_out(MP_SCENE_FREE_LOCK | MP_SCENE_FREE_INPUT_MODE, 0u) ==
                     (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_INPUT_MODE),
             "it is missed where it was planned alone, or the lock did not go either");
    ut_check(mp_repair_not_carried_out(0x3Fu, 0x3Fu) == 0u, "a plan carried out whole: nothing");
}

static void check_the_looks_after(void)
{
    const float          wait = MP_REPAIR_LOOK_AGAIN_SECONDS;
    mp_scene_free_look_t look;

    ut_section("the looks after a host's repair, timed on the world's own clock");

    ut_check(mp_repair_after(true, true, 10.0f, 10.0f, wait) == MP_REPAIR_AFTER_WAIT &&
                 mp_repair_after(true, true, 10.0f, 10.0625f, wait) == MP_REPAIR_AFTER_WAIT,
             "two substeps after the press the look is not due yet");
    ut_check(mp_repair_after(true, true, 10.0f, 10.125f, wait) == MP_REPAIR_AFTER_DUE &&
                 mp_repair_after(true, true, 10.0f, 500.0f, wait) == MP_REPAIR_AFTER_DUE,
             "four substeps after it is, and so is any later moment");
    ut_check(mp_repair_after(true, true, 10.0f, 12.0f, MP_REPAIR_LATCH_SECONDS) ==
                     MP_REPAIR_AFTER_WAIT &&
                 mp_repair_after(true, true, 10.0f, 12.25f, MP_REPAIR_LATCH_SECONDS) ==
                     MP_REPAIR_AFTER_DUE,
             "the latch's catch is said once its sixty four substeps are surely over");
    ut_check(mp_repair_after(false, true, 10.0f, 11.0f, wait) == MP_REPAIR_AFTER_DROP,
             "a world clock that does not read drops the look");
    ut_check(mp_repair_after(true, false, 10.0f, 11.0f, wait) == MP_REPAIR_AFTER_DROP,
             "so does another world");
    ut_check(mp_repair_after(true, true, 10.0f, 9.0f, wait) == MP_REPAIR_AFTER_DROP,
             "and a clock that reads less than at the press, which is another level's");
    ut_check(mp_repair_after(true, true, (float)NAN, 11.0f, wait) == MP_REPAIR_AFTER_DROP &&
                 mp_repair_after(true, true, 10.0f, (float)NAN, wait) == MP_REPAIR_AFTER_DROP &&
                 mp_repair_after(true, true, 10.0f, (float)INFINITY, wait) == MP_REPAIR_AFTER_DROP,
             "a time that is not a finite number is no time to wait for");

    look = free_player(false);
    ut_check(mp_repair_actor_after(&look) == MP_REPAIR_ACTOR_LEFT &&
                 mp_repair_actor_after(NULL) == MP_REPAIR_ACTOR_LEFT,
             "the module runs: the engine removed the actor and put the player back");
    look = in_a_hero_scene(false);
    ut_check(mp_repair_actor_after(&look) == MP_REPAIR_ACTOR_STILL_DRIVES,
             "stopped and still driven: the actors do not tick");
    look.driven = false;
    ut_check(mp_repair_actor_after(&look) == MP_REPAIR_ACTOR_LEFT_STOPPED,
             "stopped with nobody driving: the actor is gone and the module was not put back");
}


int main(void)
{
    check_what_a_repair_asks();
    check_nothing_to_do();
    check_the_holders();
    check_the_latch_and_the_answer();
    check_the_looks_after();

    return ut_summary("mp_player_help_repair");
}
