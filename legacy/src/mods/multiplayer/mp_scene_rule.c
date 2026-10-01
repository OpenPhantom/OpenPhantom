/* mp_scene_rule.c: the scene decisions. See the header. */
#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_scene_putback_allowed(uint32_t saved_module_state)
{
    return saved_module_state != 0u;
}

bool mp_scene_hero_is_gated_here(bool client_holds, bool suppressed, bool gather_holds)
{
    return (client_holds || gather_holds) && !suppressed;
}

bool mp_scene_running(int32_t lock_level, uint32_t module_state, uint32_t saved_module_state,
                      bool has_body)
{
    return lock_level >= MP_SCENE_LOCK_LEVEL ||
           (module_state == 0u && saved_module_state != 0u && has_body);
}

mp_scene_move_t mp_scene_may_move(bool has_body, bool module_running, bool stands,
                                  mp_scene_mode_t mode)
{
    if (!has_body || !module_running) {
        return MP_SCENE_MOVE_NO_BODY;
    }
    if (!stands || mode == MP_SCENE_MODE_DEATH) {
        return MP_SCENE_MOVE_DEAD;
    }
    return mode == MP_SCENE_MODE_PARKABLE ? MP_SCENE_MOVE_YES : MP_SCENE_MOVE_MODE;
}

mp_scene_respawn_caller_t mp_scene_respawn_caller(uintptr_t caller, uintptr_t warp_return,
                                                  uintptr_t swap_return, bool inside_image)
{
    if (!inside_image) {
        return MP_SCENE_RESPAWN_BY_DLL;
    }
    if (caller != 0u && caller == warp_return) {
        return MP_SCENE_RESPAWN_BY_WARP;
    }
    if (caller != 0u && caller == swap_return) {
        return MP_SCENE_RESPAWN_BY_SWAP;
    }
    return MP_SCENE_RESPAWN_BY_IMAGE;
}

mp_scene_camera_owner_t mp_scene_camera_owner(int32_t lock_level, bool for_all)
{
    if (lock_level >= MP_SCENE_LOCK_LEVEL) {
        return MP_SCENE_CAMERA_OF_THE_LOCK;
    }
    return for_all ? MP_SCENE_CAMERA_OF_ALL : MP_SCENE_CAMERA_OF_ITS_OWN;
}

mp_scene_hero_origin_t mp_scene_hero_origin(bool inside_image, bool script_running)
{
    if (!inside_image) {
        return MP_SCENE_HERO_BY_DLL;
    }
    return script_running ? MP_SCENE_HERO_BY_SCRIPT : MP_SCENE_HERO_BY_ENGINE;
}

uint32_t mp_scene_bits_changed(const uint8_t *before, const uint8_t *after, size_t bytes,
                               uint32_t window_first_bit, uint32_t band_first,
                               uint32_t band_last, uint32_t *in_band)
{
    uint32_t changed = 0u;
    uint32_t band    = 0u;
    size_t   index;

    if (in_band != NULL) {
        *in_band = 0u;
    }
    if (before == NULL || after == NULL) {
        return 0u;
    }
    for (index = 0u; index < bytes * 8u; ++index) {
        uint8_t  mask = (uint8_t)(1u << (index & 7u));
        uint32_t bit  = window_first_bit + (uint32_t)index;

        if ((before[index >> 3] & mask) == (after[index >> 3] & mask)) {
            continue;
        }
        ++changed;
        if (bit >= band_first && bit <= band_last) {
            ++band;
        }
    }
    if (in_band != NULL) {
        *in_band = band;
    }
    return changed;
}

bool mp_scene_camera_is_a_script(const uintptr_t *script_returns, size_t count, uintptr_t caller)
{
    size_t index;

    if (script_returns == NULL || caller == 0u) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (script_returns[index] != 0u && script_returns[index] == caller) {
            return true;
        }
    }
    return false;
}

mp_scene_callee_t mp_scene_camera_callee(const mp_scene_call_site_t *sites, size_t count,
                                         uintptr_t *entry)
{
    uintptr_t agreed = 0u;
    size_t    index;

    if (entry == NULL) {
        return MP_SCENE_CALLEE_NO_SITES;
    }
    *entry = 0u;
    if (sites == NULL || count == 0u) {
        return MP_SCENE_CALLEE_NO_SITES;
    }
    for (index = 0; index < count; ++index) {
        const mp_scene_call_site_t *site = &sites[index];
        uint32_t                    displacement;
        uintptr_t                   target;

        if (site->call[0] != MP_SCENE_CALL_OPCODE) {
            return MP_SCENE_CALLEE_NOT_A_CALL;
        }
        displacement = (uint32_t)site->call[1] | ((uint32_t)site->call[2] << 8) |
                       ((uint32_t)site->call[3] << 16) | ((uint32_t)site->call[4] << 24);
        /* The displacement counts from the instruction after the call, which is the return
         * address itself, and it wraps the way the processor's own addition does. */
        target = (uintptr_t)((uint32_t)site->return_address + displacement);
        if (target == 0u) {
            return MP_SCENE_CALLEE_NOT_A_CALL;
        }
        if (index != 0u && target != agreed) {
            return MP_SCENE_CALLEE_DISAGREE;
        }
        agreed = target;
    }
    *entry = agreed;
    return MP_SCENE_CALLEE_AGREED;
}

bool mp_scene_session_runs(bool started, bool ended)
{
    return started && !ended;
}

bool mp_scene_client_of_a_started_session(bool started, bool ended, bool is_client)
{
    return is_client && mp_scene_session_runs(started, ended);
}

mp_scene_answer_t mp_scene_answer_of(bool on_record, bool was_a_player, uint32_t age)
{
    if (!on_record) {
        return MP_SCENE_ANSWER_NONE;
    }
    if (age > MP_SCENE_OWN_ANSWER_SUBSTEPS) {
        return MP_SCENE_ANSWER_STALE;
    }
    return was_a_player ? MP_SCENE_ANSWER_PLAYER : MP_SCENE_ANSWER_NOT_A_PLAYER;
}

mp_scene_trigger_rule_t mp_scene_trigger(const mp_scene_evidence_t *evidence, uint8_t *bank)
{
    mp_scene_trigger_rule_t rule   = MP_SCENE_BY_HOST_ANCHOR;
    uint8_t                 chosen = 0u;

    if (evidence != NULL && evidence->own == MP_SCENE_ANSWER_PLAYER) {
        rule   = MP_SCENE_BY_OWN_ANSWER;
        chosen = evidence->own_bank;
    } else if (evidence != NULL && evidence->died && evidence->attacker_known) {
        rule   = MP_SCENE_BY_LAST_ATTACKER;
        chosen = evidence->attacker_bank;
    }
    if (bank != NULL) {
        *bank = chosen;
    }
    return rule;
}
