/* mp_player_sound_rule.c: a far player's own sounds and shield, as arithmetic. See the header. */
#include "mp_player_sound_rule.h"

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The collision class of the body in the player record while it is this machine's own player.
 * Every far body and every bank body carries a class of this feature's own. */
#define LOCAL_PLAYER_CLASS 1

/* The largest length a shield's moment can carry in its one byte. */
#define SECONDS_MAX 255u

bool mp_player_sound_rule_may_note(bool session, int32_t bank_class)
{
    return session && bank_class == LOCAL_PLAYER_CLASS;
}

size_t mp_player_sound_rule_death_moments(int32_t cause, uint8_t whats[2])
{
    if (whats == NULL || cause < 0 || cause >= (int32_t)MP_PLAYER_SOUND_CAUSES) {
        return 0u;
    }
    whats[0] = (uint8_t)MP_PLAYER_SOUND_DEATH;
    if (cause != (int32_t)MP_PLAYER_SOUND_CAUSE_BURN) {
        return 1u;
    }
    whats[1] = (uint8_t)MP_PLAYER_SOUND_BURN;
    return 2u;
}

bool mp_player_sound_rule_name_index(uint32_t operand, uint32_t table, uint8_t *index)
{
    uint32_t offset;

    if (index == NULL || operand < table) {
        return false;
    }
    offset = operand - table;
    if ((offset % sizeof(uint32_t)) != 0u || offset / sizeof(uint32_t) >= MP_PLAYER_SOUND_NAMES) {
        return false;
    }
    *index = (uint8_t)(offset / sizeof(uint32_t));
    return true;
}

uint8_t mp_player_sound_rule_seconds(float timer)
{
    float whole;

    /* Written as a test for being inside, because a NaN is outside every bound. */
    if (!(timer > 0.0f)) {
        return 0u;
    }
    if (!(timer < (float)SECONDS_MAX)) {
        return (uint8_t)SECONDS_MAX;
    }
    whole = (float)(uint32_t)timer;
    return (uint8_t)(whole < timer ? whole + 1.0f : whole);
}

bool mp_player_sound_rule_too_late(uint32_t event_tick, uint32_t render_tick, bool render_known)
{
    int32_t behind;

    if (!render_known) {
        return false;
    }
    /* Read signed, so a wrapped counter and a moment ahead of the body both come out right. */
    behind = (int32_t)(render_tick - event_tick);
    return behind > (int32_t)MP_PLAYER_SOUND_LATE_SUBSTEPS;
}

static void plan_cry(mp_player_sound_table_t table, uint8_t row, uint8_t cause,
                     mp_player_sound_plan_t *out)
{
    if (row >= MP_PLAYER_SOUND_HEROES || cause >= MP_PLAYER_SOUND_CAUSES) {
        return;
    }
    /* The burning cry belongs to the fire arm of the death and to nothing else. */
    if (table == MP_PLAYER_SOUND_TABLE_BURN && cause != MP_PLAYER_SOUND_CAUSE_BURN) {
        return;
    }
    out->valid        = true;
    out->table        = table;
    out->row          = row;
    out->silent       = table == MP_PLAYER_SOUND_TABLE_DEATH &&
                        cause == MP_PLAYER_SOUND_CAUSE_SCRIPTED;
    out->engine_flags = MP_PLAYER_SOUND_PLACED_FLAGS;
    out->cue          = MP_PLAYER_SOUND_CUE_DEATH;
    out->burst        = table == MP_PLAYER_SOUND_TABLE_BURN;
}

static void plan_named(uint8_t what, uint8_t index, mp_player_sound_plan_t *out)
{
    if (index >= MP_PLAYER_SOUND_NAMES) {
        return;
    }
    out->valid        = true;
    out->table        = MP_PLAYER_SOUND_TABLE_NAMES;
    out->row          = index;
    out->engine_flags = MP_PLAYER_SOUND_PLACED_FLAGS;
    if (what == MP_PLAYER_SOUND_KEY) {
        out->engine_flags |= MP_PLAYER_SOUND_FLAG_DONT_DUP;
    }
    out->cue = MP_PLAYER_SOUND_CUE_NAMED;
}

void mp_player_sound_rule_plan(uint8_t what, uint8_t sound, uint8_t flags,
                               mp_player_sound_plan_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    switch (what) {
    case MP_PLAYER_SOUND_DEATH:
        plan_cry(MP_PLAYER_SOUND_TABLE_DEATH, sound, flags, out);
        return;
    case MP_PLAYER_SOUND_BURN:
        plan_cry(MP_PLAYER_SOUND_TABLE_BURN, sound, flags, out);
        return;
    case MP_PLAYER_SOUND_PICKUP:
    case MP_PLAYER_SOUND_KEY:
    case MP_PLAYER_SOUND_WATER:
    case MP_PLAYER_SOUND_GROUND:
        plan_named(what, sound, out);
        return;
    case MP_PLAYER_SOUND_SHIELD_ON:
        out->valid     = sound != 0u;
        out->shield_on = out->valid;
        out->seconds   = sound;
        return;
    case MP_PLAYER_SOUND_SHIELD_OFF:
        out->valid      = true;
        out->shield_off = true;
        return;
    default:
        return;
    }
}

bool mp_player_sound_rule_shield_expired(uint32_t substeps, uint8_t seconds)
{
    uint32_t length = ((uint32_t)seconds + MP_PLAYER_SOUND_SHIELD_SLACK_SECONDS) *
                      MP_PLAYER_SOUND_SUBSTEPS_PER_SECOND;

    return substeps > length;
}

bool mp_player_sound_rule_came_back(bool seen_down, bool stands)
{
    return seen_down && stands;
}
