/* mp_player_help_rule.c: the two buttons of the developer menu, pure. See the header. */
#include "mp_player_help_rule.h"

#include "mp_armed.h"

#include "common/player_help_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's eight bits fill the low byte of the answer's `released` word, and the mod's own
 * stand above them. */
_Static_assert(MP_SCENE_FREE_BITS == 8u && (MP_REPAIR_OF_THE_MOD & 0x00FFu) == 0u &&
                   MP_REPAIR_OF_THE_MOD <= 0xFFFFu,
               "what the engine gave back and what the mod left share one word of sixteen bits");

/* ==============================================================================================
 * The reader.
 * ============================================================================================ */

uint32_t mp_player_help_mark(bool found, uint32_t serial)
{
    return found ? serial : 0u;
}

bool mp_player_help_is_new(uint8_t kind, uint32_t serial, uint32_t mark)
{
    if (kind != (uint8_t)PLAYER_HELP_KIND_REPAIR && kind != (uint8_t)PLAYER_HELP_KIND_TELEPORT) {
        return false;
    }
    return serial != 0u && player_help_serial_after(serial, mark);
}

bool mp_player_help_read_due(bool found_once, bool tried, uint32_t since_ms)
{
    return found_once || !tried || since_ms >= PLAYER_HELP_NOTE_RETRY_MS;
}

uint8_t mp_player_help_ready(bool repair_bound, bool is_client, bool teleport_bound)
{
    uint8_t ready = PLAYER_HELP_READY_LISTENING;

    if (repair_bound) {
        ready |= PLAYER_HELP_READY_CAN_REPAIR;
    }
    if (is_client && teleport_bound) {
        ready |= PLAYER_HELP_READY_CAN_TELEPORT;
    }
    return ready;
}

const char *mp_player_help_reason_text(uint8_t reason)
{
    switch (reason) {
    case PLAYER_HELP_REASON_NO_LEVEL:         return "no level of a started session runs here";
    case PLAYER_HELP_REASON_DEAD:             return "this player is dead";
    case PLAYER_HELP_REASON_AT_A_GUN:         return "this player is at the tripod gun";
    case PLAYER_HELP_REASON_BUSY:             return "a re-entry or a move is under way already";
    case PLAYER_HELP_REASON_IS_HOST:          return "this machine is the host";
    case PLAYER_HELP_REASON_HOST_ELSEWHERE:   return "the host is in another level";
    case PLAYER_HELP_REASON_HOST_HAS_NO_BODY: return "the host has no standing body here yet";
    case PLAYER_HELP_REASON_HOST_DEAD:        return "the host is dead";
    case PLAYER_HELP_REASON_NEAR_ALREADY:     return "this player stands beside the host already";
    case PLAYER_HELP_REASON_NO_SEAT:          return "no free place beside the host";
    case PLAYER_HELP_REASON_OVERLAY_HOLDS:    return "the developer menu holds the player";
    case PLAYER_HELP_REASON_NOT_BOUND:        return "not bound on this executable";
    case PLAYER_HELP_REASON_GAVE_UP:          return "the wait for the body ran out";
    case PLAYER_HELP_REASON_MENU_OPEN:        return "a menu of the game is open";
    case PLAYER_HELP_REASON_CONVERSATION:     return "a conversation's answers are open";
    case PLAYER_HELP_REASON_WORLD_CHANGED:    return "the level changed";
    case PLAYER_HELP_REASON_NONE:
    default:                                  return "no reason";
    }
}

/* ==============================================================================================
 * Repair lock.
 * ============================================================================================ */

bool mp_repair_refused(bool level_running, bool bound, uint8_t *reason)
{
    uint8_t why = PLAYER_HELP_REASON_NONE;

    if (!level_running) {
        why = PLAYER_HELP_REASON_NO_LEVEL;
    } else if (!bound) {
        why = PLAYER_HELP_REASON_NOT_BOUND;
    }
    if (reason != NULL) {
        *reason = why;
    }
    return why != PLAYER_HELP_REASON_NONE;
}

uint32_t mp_repair_asks(bool is_client)
{
    const uint32_t both = MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |
                          MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_MODULE_ALONE;

    return is_client ? both | MP_SCENE_FREE_MODULE | MP_SCENE_FREE_STORE
                     : both | MP_SCENE_FREE_ACTOR;
}

uint32_t mp_repair_orphans(uint32_t holders, bool menu_open, bool chat_typing)
{
    uint32_t orphans = 0u;

    if ((holders & (uint32_t)MP_ARMED_HOLDER_PAUSE) != 0u && !menu_open) {
        orphans |= (uint32_t)MP_ARMED_HOLDER_PAUSE;
    }
    if ((holders & (uint32_t)MP_ARMED_HOLDER_CHAT) != 0u && !chat_typing) {
        orphans |= (uint32_t)MP_ARMED_HOLDER_CHAT;
    }
    return orphans;
}

bool mp_repair_found_nothing(uint32_t freed, uint32_t of_the_mod)
{
    return (freed & ~MP_SCENE_FREE_CAMERA) == 0u && (of_the_mod & MP_REPAIR_OF_THE_MOD) == 0u;
}

bool mp_repair_latches(bool is_client, uint32_t given)
{
    return !is_client &&
           (given & (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_ACTOR)) != 0u;
}

bool mp_repair_only_left_the_mod(uint32_t of_the_mod, uint32_t given)
{
    return (of_the_mod & MP_REPAIR_LEFT_SCENE) != 0u && !mp_repair_latches(false, given);
}

uint8_t mp_repair_reason(uint8_t left_because, bool menu_holds)
{
    if ((left_because & MP_SCENE_FREE_LEFT_MENU) != 0u || menu_holds) {
        return PLAYER_HELP_REASON_MENU_OPEN;
    }
    if ((left_because & MP_SCENE_FREE_LEFT_CONVERSATION) != 0u) {
        return PLAYER_HELP_REASON_CONVERSATION;
    }
    if ((left_because & MP_SCENE_FREE_LEFT_OVERLAY) != 0u) {
        return PLAYER_HELP_REASON_OVERLAY_HOLDS;
    }
    if ((left_because & MP_SCENE_FREE_LEFT_GUN) != 0u) {
        return PLAYER_HELP_REASON_AT_A_GUN;
    }
    if ((left_because & MP_SCENE_FREE_LEFT_DEAD) != 0u) {
        return PLAYER_HELP_REASON_DEAD;
    }
    if ((left_because & MP_SCENE_FREE_LEFT_UNBOUND) != 0u) {
        return PLAYER_HELP_REASON_NOT_BOUND;
    }
    return PLAYER_HELP_REASON_NONE;
}

mp_player_help_verdict_t mp_repair_verdict(uint32_t plan, uint32_t given, uint32_t of_the_mod,
                                           uint8_t left_because, bool menu_holds)
{
    mp_player_help_verdict_t verdict;

    verdict.reason = mp_repair_reason(left_because, menu_holds);
    if (!mp_repair_found_nothing(given, of_the_mod)) {
        verdict.outcome = PLAYER_HELP_OUTCOME_DONE;
    } else if (mp_repair_not_carried_out(plan, given) != 0u) {
        verdict.outcome = PLAYER_HELP_OUTCOME_REFUSED;
        verdict.reason  = PLAYER_HELP_REASON_NOT_BOUND;
    } else {
        verdict.outcome = PLAYER_HELP_OUTCOME_NOTHING;
    }
    return verdict;
}

uint16_t mp_repair_released(uint32_t given, uint32_t of_the_mod)
{
    return (uint16_t)((given & 0x00FFu) | (of_the_mod & MP_REPAIR_OF_THE_MOD));
}

uint32_t mp_repair_not_carried_out(uint32_t plan, uint32_t given)
{
    uint32_t missed = plan & ~given;

    if ((given & MP_SCENE_FREE_LOCK) != 0u) {
        missed &= ~MP_SCENE_FREE_INPUT_MODE;
    }
    return missed;
}

mp_repair_after_t mp_repair_after(bool world_reads, bool same_world, float then, float now,
                                  float wait_seconds)
{
    if (!world_reads || !same_world || !isfinite(then) || !isfinite(now) || now < then) {
        return MP_REPAIR_AFTER_DROP;
    }
    return now - then >= wait_seconds ? MP_REPAIR_AFTER_DUE : MP_REPAIR_AFTER_WAIT;
}

mp_repair_actor_t mp_repair_actor_after(const mp_scene_free_look_t *look)
{
    if (look == NULL || look->module != 0u) {
        return MP_REPAIR_ACTOR_LEFT;
    }
    return look->driven ? MP_REPAIR_ACTOR_STILL_DRIVES : MP_REPAIR_ACTOR_LEFT_STOPPED;
}

/* ==============================================================================================
 * Teleport to host.
 * ============================================================================================ */

static mp_player_help_verdict_t verdict_of(uint8_t outcome, uint8_t reason)
{
    mp_player_help_verdict_t verdict;

    verdict.outcome = outcome;
    verdict.reason  = reason;
    return verdict;
}

mp_player_help_verdict_t mp_teleport_door(const mp_teleport_look_t *look)
{
    if (look == NULL) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NOT_BOUND);
    }
    if (!look->is_client) {
        return verdict_of(PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_IS_HOST);
    }
    if (!look->level_of_session) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NO_LEVEL);
    }
    if (!look->bound) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NOT_BOUND);
    }
    if (look->dead) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_DEAD);
    }
    if (look->busy || look->teleporting) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_BUSY);
    }
    if (look->at_gun) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_AT_A_GUN);
    }
    if (look->overlay_holds) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_OVERLAY_HOLDS);
    }
    if (look->host_elsewhere) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_ELSEWHERE);
    }
    if (!look->host_pose) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_HAS_NO_BODY);
    }
    if (look->host_dead) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_DEAD);
    }
    /* Neither dead nor alive yet, in the host's own words: a body that has not stood up. */
    if (!look->host_stands) {
        return verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_HAS_NO_BODY);
    }
    if (look->distance_known && isfinite(look->distance) && look->distance < MP_TELEPORT_NEAR) {
        return verdict_of(PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NEAR_ALREADY);
    }
    return verdict_of(PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
}

mp_teleport_search_t mp_teleport_search(bool found, bool beside_the_anchor, uint32_t substeps)
{
    if (!beside_the_anchor) {
        return MP_TELEPORT_SEARCH_NO_SEAT;
    }
    if (found) {
        return MP_TELEPORT_SEARCH_SEATED;
    }
    return substeps >= MP_TELEPORT_SEARCH_SUBSTEPS ? MP_TELEPORT_SEARCH_NO_SEAT
                                                   : MP_TELEPORT_SEARCH_GOES_ON;
}

bool mp_teleport_moved(mp_scene_seat_stage_t stage, mp_player_help_verdict_t *verdict)
{
    mp_player_help_verdict_t ended;

    switch (stage) {
    case MP_SCENE_SEAT_DONE:
        ended = verdict_of(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
        break;
    case MP_SCENE_SEAT_GIVEN_UP:
    case MP_SCENE_SEAT_IDLE:
        ended = verdict_of(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_GAVE_UP);
        break;
    case MP_SCENE_SEAT_WAITING:
    case MP_SCENE_SEAT_FADING:
    case MP_SCENE_SEAT_PLACED:
    case MP_SCENE_SEAT_RESPAWNING:
    default:
        return false;
    }
    if (verdict != NULL) {
        *verdict = ended;
    }
    return true;
}

uint8_t mp_teleport_left_reason(bool world_changed)
{
    return world_changed ? PLAYER_HELP_REASON_WORLD_CHANGED : PLAYER_HELP_REASON_GAVE_UP;
}
