/* mp_scene_free_rule.c: what of a scene's hold on a player is given back, pure. See the header. */
#include "mp_scene_free_rule.h"

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The lock. A conversation's answers hold it at level one on every frame the list is drawn, so
 * for the button a lock below a script's level is left to the conversation; a script's lock
 * goes even then, and the list takes its own level again on its next frame. */
static uint32_t the_lock(const mp_scene_free_look_t *look, uint32_t asked, int32_t min_lock,
                         uint8_t *left)
{
    if ((asked & MP_SCENE_FREE_LOCK) == 0u || look->lock_level <= 0 ||
        look->lock_level < min_lock) {
        return 0u;
    }
    if (look->button && look->conversation && look->lock_level < MP_SCENE_LOCK_LEVEL) {
        *left |= MP_SCENE_FREE_LEFT_CONVERSATION;
        return 0u;
    }
    if (look->menu_open) {
        *left |= MP_SCENE_FREE_LEFT_MENU;
        return 0u;
    }
    return MP_SCENE_FREE_LOCK;
}

/* The input mode. `plan` is what the lock above answered: with the lock going in this plan the
 * mode goes with it, whatever the level reads now. */
static uint32_t the_input_mode(const mp_scene_free_look_t *look, uint32_t asked, uint32_t plan,
                               uint8_t *left)
{
    if ((asked & MP_SCENE_FREE_INPUT_MODE) == 0u ||
        look->input_mode != MP_SCENE_INPUT_MODE_LOCK || look->menu_open) {
        return 0u;
    }
    if ((plan & MP_SCENE_FREE_LOCK) != 0u) {
        return MP_SCENE_FREE_INPUT_MODE;
    }
    if (look->lock_level != 0) {
        return 0u;   /* a lock stands, or did not read: the mode is the lock's */
    }
    if (look->conversation) {
        *left |= MP_SCENE_FREE_LEFT_CONVERSATION;
        return 0u;
    }
    if (!look->client && !look->button) {
        return 0u;
    }
    return MP_SCENE_FREE_INPUT_MODE;
}

/* The camera's override. `plan` is the lock and the bars as planned: without the button the
 * override is cleared only along with one of them. */
static uint32_t the_camera(const mp_scene_free_look_t *look, uint32_t asked, uint32_t plan,
                           uint8_t *left)
{
    bool along = (plan & (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS)) != 0u;

    if ((asked & MP_SCENE_FREE_CAMERA) == 0u || (!look->button && !along)) {
        return 0u;
    }
    /* For the button under an open menu of the engine nothing but the bars goes. The lock stays
     * for the menu's sake, and the load and save menu holds the camera's override itself. */
    if (look->button && look->menu_open) {
        *left |= MP_SCENE_FREE_LEFT_MENU;
        return 0u;
    }
    if (!look->camera_bound) {
        *left |= MP_SCENE_FREE_LEFT_UNBOUND;
        return 0u;
    }
    if (look->bank_window) {
        *left |= MP_SCENE_FREE_LEFT_BANK;
        return 0u;
    }
    if (look->at_gun) {
        *left |= MP_SCENE_FREE_LEFT_GUN;
        return 0u;
    }
    if (look->dead) {
        *left |= MP_SCENE_FREE_LEFT_DEAD;
        return 0u;
    }
    if (look->button && look->conversation) {
        *left |= MP_SCENE_FREE_LEFT_CONVERSATION;
        return 0u;
    }
    return MP_SCENE_FREE_CAMERA;
}

/* Everything that writes the player's record: the module under its actor, the store, and a
 * module nobody drives. */
static uint32_t the_record(const mp_scene_free_look_t *look, uint32_t asked, uint8_t *left)
{
    bool     stopped = look->module == 0u && look->has_body;
    uint32_t plan    = 0u;

    if (stopped && look->store != 0u && look->driven) {
        plan |= asked & (look->client ? MP_SCENE_FREE_MODULE : MP_SCENE_FREE_ACTOR);
    }
    if (look->client && look->store != 0u && look->module != 0u) {
        plan |= asked & MP_SCENE_FREE_STORE;
    }
    if (look->button && stopped && !look->driven &&
        (asked & MP_SCENE_FREE_MODULE_ALONE) != 0u) {
        if (look->overlay_holds) {
            *left |= MP_SCENE_FREE_LEFT_OVERLAY;
        } else {
            plan |= MP_SCENE_FREE_MODULE_ALONE;
        }
    }
    /* The button under an open menu of the engine leaves the record alone. The lock stays under
     * that menu, and an actor removed now would leave that lock with no script to let go of it:
     * the player closes the menu and presses again. */
    if (plan != 0u && look->button && look->menu_open) {
        *left |= MP_SCENE_FREE_LEFT_MENU;
        return 0u;
    }
    if (plan != 0u && look->bank_window) {
        *left |= MP_SCENE_FREE_LEFT_BANK;
        return 0u;
    }
    return plan;
}

uint32_t mp_scene_free_plan(const mp_scene_free_look_t *look, uint32_t asked, int32_t min_lock,
                            uint8_t *left_because)
{
    uint32_t plan = 0u;
    uint8_t  left = 0u;

    if (left_because != NULL) {
        *left_because = 0u;
    }
    if (look == NULL) {
        return 0u;
    }
    plan |= the_lock(look, asked, min_lock, &left);
    plan |= the_input_mode(look, asked, plan, &left);
    if ((asked & MP_SCENE_FREE_BARS) != 0u && look->bars_on) {
        plan |= MP_SCENE_FREE_BARS;
    }
    plan |= the_camera(look, asked, plan, &left);
    plan |= the_record(look, asked, &left);
    if (left_because != NULL) {
        *left_because = left;
    }
    return plan;
}

/* ==============================================================================================
 * Where the two releases live.
 * ============================================================================================ */

/* The twelve bytes of an end: the call of the camera's clearing, the push of the level, the call
 * of the lock's release. */
#define END_CAMERA_CALL  0u
#define END_PUSH         5u
#define END_RELEASE_CALL 7u
#define PUSH_IMM8_OPCODE 0x6Au

/* The address a near call names: its displacement is signed and counted from `behind`, the
 * address of the instruction after it. False where the five bytes are no call. */
static bool call_names(const uint8_t *call, uintptr_t behind, uintptr_t *named)
{
    int32_t displacement = 0;

    if (call[0] != MP_SCENE_CALL_OPCODE) {
        return false;
    }
    memcpy(&displacement, call + 1u, sizeof displacement);
    *named = behind + (uintptr_t)(intptr_t)displacement;
    return true;
}

void mp_scene_free_read_ends(uintptr_t release, const mp_scene_free_end_t ends[MP_SCENE_FREE_ENDS],
                             mp_scene_free_ends_t *out)
{
    uintptr_t camera[MP_SCENE_FREE_ENDS] = { 0u, 0u };
    size_t    end;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (release == 0u || ends == NULL) {
        return;
    }
    out->release_holds = true;
    for (end = 0u; end < MP_SCENE_FREE_ENDS; ++end) {
        uintptr_t behind = ends[end].return_address;
        uintptr_t named  = 0u;

        if (!ends[end].read || behind <= MP_SCENE_FREE_END_BYTES ||
            !call_names(ends[end].bytes + END_RELEASE_CALL, behind, &named)) {
            continue;   /* no end of the release that can be read: it proves nothing */
        }
        if (named != release) {
            out->release_holds = false;
            out->disagreeing   = named;
            continue;
        }
        ++out->witnesses;
        /* The camera's call ends where the push begins, seven bytes in front of the return. */
        if (ends[end].bytes[END_PUSH] != PUSH_IMM8_OPCODE ||
            !call_names(ends[end].bytes + END_CAMERA_CALL,
                        behind - (MP_SCENE_FREE_END_BYTES - MP_SCENE_CALL_BYTES), &camera[end])) {
            camera[end] = 0u;
        }
    }
    if (out->release_holds && camera[0] != 0u && camera[0] == camera[1] && camera[0] != release) {
        out->camera_off = camera[0];
    }
}
