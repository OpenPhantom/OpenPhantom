/* mp_signatures_player_sound.c: the places a player's own body is made heard, as byte patterns.
 * See the header.
 *
 * Every window ends on its call's E8 and the four operand bytes behind it, all masked, and runs
 * back over the instructions that push the call's arguments until it is unique in every shipped
 * image, the editor's recompile included. Every absolute operand is masked, the ones that are read
 * as much as the loads of the player pointer, and so is every jump distance: a pattern standing on
 * a distance is one a recompile moves. What is required is the shape that makes each one the call
 * it is: the drop timer of 0.2 s stored before the burning ground, the lock flag 0x200 pushed for
 * the key, the pickup's taken bit, the shield's slot at +0x100 and its timer at +0x94, the cause 4
 * test before the death cry and the place +0x118 both cries are played at.
 */
#include "mp_signatures_player_sound.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Plr_TickCarryAndGroundProbe on a damaging floor: the drop timer set to 0.2 s, then the burning
 * ground's name, entry 52, pushed and played with no flags. */
static const uint8_t SIG_MP_PLAYER_SOUND_GROUND[29] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x82, 0xA8, 0x02, 0x00, 0x00, 0xCD, 0xCC,
    0x4C, 0x3E, 0x6A, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0xE8, 0x00, 0x00, 0x00,
    0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_GROUND[29] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00
};
#define GROUND_NAME 19u
#define GROUND_CALL 24u

/* Plr_UseTestButton spending a key: the key bit tested, then the key's name, entry 58, played with
 * the engine's lock flag 0x200. */
static const uint8_t SIG_MP_PLAYER_SOUND_KEY[22] = {
    0x23, 0xC2, 0x85, 0xC0, 0x74, 0x00, 0x68, 0x00, 0x02, 0x00, 0x00, 0xA1, 0x00, 0x00,
    0x00, 0x00, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_KEY[22] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define KEY_NAME 12u
#define KEY_CALL 17u

/* Plr_EnterSwim: the other arm's clip call cleaned up and jumped over, then the water's name,
 * entry 59, played with no flags. */
static const uint8_t SIG_MP_PLAYER_SOUND_WATER[19] = {
    0x83, 0xC4, 0x0C, 0xEB, 0x00, 0x6A, 0x00, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_WATER[19] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
#define WATER_NAME 9u
#define WATER_CALL 14u

/* Plr_TickTimers when the shield's time is up: the timer at +0x94 set to nought, then the shield
 * named by the body's slot at +0x100 released. */
static const uint8_t SIG_MP_PLAYER_SOUND_SHIELD_DOWN[37] = {
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x81, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x42, 0x0C, 0x8B, 0x88, 0x00,
    0x01, 0x00, 0x00, 0x51, 0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_SHIELD_DOWN[37] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define SHIELD_DOWN_CALL 32u

/* Plr_PickUp once a pickup reports that it was taken: the pickup's name, entry 53, played, then the
 * taken bit 8 raised on the item. */
static const uint8_t SIG_MP_PLAYER_SOUND_PICKUP[30] = {
    0x83, 0x7D, 0xFC, 0x01, 0x75, 0x00, 0x6A, 0x00, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x08, 0x8B, 0x55, 0x0C, 0x8B, 0x02,
    0x0C, 0x08
};
static const uint8_t MSK_MP_PLAYER_SOUND_PICKUP[30] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
#define PICKUP_NAME 10u
#define PICKUP_CALL 15u

/* Plr_PickUp's shield arm: kind 0x0A, a shield created on the player's body and stored in its slot
 * at +0x100, and the timer at +0x94 set to the pickup's length. */
static const uint8_t SIG_MP_PLAYER_SOUND_SHIELD_UP[54] = {
    0x83, 0x7D, 0x08, 0x0A, 0x75, 0x00, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x51,
    0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x8B, 0x0D, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x51, 0x0C, 0x89, 0x82, 0x00, 0x01, 0x00, 0x00, 0xA1, 0x00, 0x00,
    0x00, 0x00, 0xC7, 0x80, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_SHIELD_UP[54] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define SHIELD_UP_CALL  16u
#define SHIELD_UP_TIMER 50u

/* Plr_EnterDeath's cry: no voice for cause 4, then the hero's row of the death voices, looked up in
 * the sound names, played at the record's place +0x118 with the flags 0x24. */
static const uint8_t SIG_MP_PLAYER_SOUND_DEATH[57] = {
    0x83, 0xB8, 0x64, 0x03, 0x00, 0x00, 0x04, 0x74, 0x00, 0x6A, 0x24, 0x8B, 0x0D, 0x00,
    0x00, 0x00, 0x00, 0x81, 0xC1, 0x18, 0x01, 0x00, 0x00, 0x51, 0x6A, 0x00, 0x8B, 0x15,
    0x00, 0x00, 0x00, 0x00, 0x8B, 0x42, 0x6C, 0x8B, 0x0C, 0x85, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x14, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x52, 0x6A, 0x00, 0xE8, 0x00, 0x00, 0x00,
    0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_DEATH[57] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00
};
#define DEATH_VOICES 38u
#define DEATH_NAMES  45u
#define DEATH_CALL   52u

/* Plr_EnterDeath's fire arm: the body burst at a pushed speed, the two second burn timer at +0x368,
 * then the hero's row of the burning voices played exactly as the death cry is. */
static const uint8_t SIG_MP_PLAYER_SOUND_BURN[88] = {
    0x6A, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B,
    0x42, 0x0C, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C, 0x8B, 0x0D, 0x00,
    0x00, 0x00, 0x00, 0xC7, 0x81, 0x68, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6A,
    0x24, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x81, 0xC2, 0x18, 0x01, 0x00, 0x00, 0x52,
    0x6A, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x48, 0x6C, 0x8B, 0x14, 0x8D, 0x00,
    0x00, 0x00, 0x00, 0x8B, 0x04, 0x95, 0x00, 0x00, 0x00, 0x00, 0x50, 0x6A, 0x00, 0xE8,
    0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MP_PLAYER_SOUND_BURN[88] = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00
};
#define BURN_SPEED  3u
#define BURN_BURST  17u
#define BURN_VOICES 69u
#define BURN_NAMES  76u
#define BURN_CALL   83u

/* The four calls a session repoints. */
SIGNATURE_REDIRECTED_CALL(SIG_MP_PLAYER_SOUND_GROUND, GROUND_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_MP_PLAYER_SOUND_KEY, KEY_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_MP_PLAYER_SOUND_WATER, WATER_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_MP_PLAYER_SOUND_SHIELD_DOWN, SHIELD_DOWN_CALL);

_Static_assert(sizeof(SIG_MP_PLAYER_SOUND_GROUND) == sizeof(MSK_MP_PLAYER_SOUND_GROUND) &&
                   sizeof(SIG_MP_PLAYER_SOUND_KEY) == sizeof(MSK_MP_PLAYER_SOUND_KEY) &&
                   sizeof(SIG_MP_PLAYER_SOUND_WATER) == sizeof(MSK_MP_PLAYER_SOUND_WATER) &&
                   sizeof(SIG_MP_PLAYER_SOUND_SHIELD_DOWN) ==
                       sizeof(MSK_MP_PLAYER_SOUND_SHIELD_DOWN) &&
                   sizeof(SIG_MP_PLAYER_SOUND_PICKUP) == sizeof(MSK_MP_PLAYER_SOUND_PICKUP) &&
                   sizeof(SIG_MP_PLAYER_SOUND_SHIELD_UP) ==
                       sizeof(MSK_MP_PLAYER_SOUND_SHIELD_UP) &&
                   sizeof(SIG_MP_PLAYER_SOUND_DEATH) == sizeof(MSK_MP_PLAYER_SOUND_DEATH) &&
                   sizeof(SIG_MP_PLAYER_SOUND_BURN) == sizeof(MSK_MP_PLAYER_SOUND_BURN),
               "a player sound pattern and its mask differ in length");
_Static_assert(GROUND_CALL + 5u == sizeof(SIG_MP_PLAYER_SOUND_GROUND) &&
                   KEY_CALL + 5u == sizeof(SIG_MP_PLAYER_SOUND_KEY) &&
                   WATER_CALL + 5u == sizeof(SIG_MP_PLAYER_SOUND_WATER) &&
                   SHIELD_DOWN_CALL + 5u == sizeof(SIG_MP_PLAYER_SOUND_SHIELD_DOWN) &&
                   DEATH_CALL + 5u == sizeof(SIG_MP_PLAYER_SOUND_DEATH) &&
                   BURN_CALL + 5u == sizeof(SIG_MP_PLAYER_SOUND_BURN),
               "every pattern with a call it is named for ends on that call and its operand");
_Static_assert(SHIELD_UP_TIMER + 4u == sizeof(SIG_MP_PLAYER_SOUND_SHIELD_UP),
               "the shield pickup's pattern ends on the timer's value");

static signature_t sites[MP_PLAYER_SOUND_SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("player_sound_ground", SIG_MP_PLAYER_SOUND_GROUND,
                           MSK_MP_PLAYER_SOUND_GROUND),
    SIGNATURE_ENTRY_MASKED("player_sound_key", SIG_MP_PLAYER_SOUND_KEY, MSK_MP_PLAYER_SOUND_KEY),
    SIGNATURE_ENTRY_MASKED("player_sound_water", SIG_MP_PLAYER_SOUND_WATER,
                           MSK_MP_PLAYER_SOUND_WATER),
    SIGNATURE_ENTRY_MASKED("player_sound_shield_down", SIG_MP_PLAYER_SOUND_SHIELD_DOWN,
                           MSK_MP_PLAYER_SOUND_SHIELD_DOWN),
    SIGNATURE_ENTRY_MASKED("player_sound_pickup", SIG_MP_PLAYER_SOUND_PICKUP,
                           MSK_MP_PLAYER_SOUND_PICKUP),
    SIGNATURE_ENTRY_MASKED("player_sound_shield_up", SIG_MP_PLAYER_SOUND_SHIELD_UP,
                           MSK_MP_PLAYER_SOUND_SHIELD_UP),
    SIGNATURE_ENTRY_MASKED("player_sound_death", SIG_MP_PLAYER_SOUND_DEATH,
                           MSK_MP_PLAYER_SOUND_DEATH),
    SIGNATURE_ENTRY_MASKED("player_sound_burn", SIG_MP_PLAYER_SOUND_BURN,
                           MSK_MP_PLAYER_SOUND_BURN)
};

static bool                     read_once;
static mp_player_sound_engine_t engine;

/* The function a call at `offset` into a resolved site reaches, 0 when the site did not resolve or
 * the byte there is no call into the image. */
static uintptr_t call_at(mp_player_sound_site_t site, size_t offset, uintptr_t *call)
{
    uintptr_t target = 0u;

    if (sites[site].address == 0u ||
        !patch_read_call_target(sites[site].address + offset, &target)) {
        return 0u;
    }
    if (call != NULL) {
        *call = sites[site].address + offset;
    }
    return target;
}

/* An absolute operand, read through the shared reader so that bytes somebody else has written
 * over are taken from the executable on disk instead. */
static uint32_t operand_at(mp_player_sound_site_t site, size_t offset)
{
    uintptr_t value = 0u;

    if (sites[site].address == 0u ||
        !signature_read_address_operand(&sites[site], offset, &value)) {
        return 0u;
    }
    return (uint32_t)value;
}

/* A float pushed or stored as an immediate. Nothing writes these bytes, and an immediate is not an
 * address the loader relocates, so it is read where it stands. */
static float immediate_at(mp_player_sound_site_t site, size_t offset)
{
    float value = 0.0f;

    if (sites[site].address == 0u ||
        !memory_try_read(sites[site].address + offset, &value, sizeof value)) {
        return 0.0f;
    }
    return value;
}

/* Two readings of one thing agree when both are there and equal, or when one of them is missing,
 * which the caller finds as a nought on its own. */
static bool agree(uintptr_t a, uintptr_t b)
{
    return a == 0u || b == 0u || a == b;
}

static void read_everything(void)
{
    uintptr_t pickup_play_name;
    uintptr_t key_play_name;
    uintptr_t water_play_name;
    uint32_t  burn_names;
    uintptr_t burn_play_by_name;

    engine.play_name        = call_at(MP_PLAYER_SOUND_SITE_GROUND, GROUND_CALL,
                                      &engine.ground_call);
    key_play_name           = call_at(MP_PLAYER_SOUND_SITE_KEY, KEY_CALL, &engine.key_call);
    water_play_name         = call_at(MP_PLAYER_SOUND_SITE_WATER, WATER_CALL, &engine.water_call);
    pickup_play_name        = call_at(MP_PLAYER_SOUND_SITE_PICKUP, PICKUP_CALL, NULL);
    engine.shield_release   = call_at(MP_PLAYER_SOUND_SITE_SHIELD_DOWN, SHIELD_DOWN_CALL,
                                      &engine.shield_down_call);
    engine.ground_name      = operand_at(MP_PLAYER_SOUND_SITE_GROUND, GROUND_NAME);
    engine.key_name         = operand_at(MP_PLAYER_SOUND_SITE_KEY, KEY_NAME);
    engine.water_name       = operand_at(MP_PLAYER_SOUND_SITE_WATER, WATER_NAME);
    engine.pickup_name      = operand_at(MP_PLAYER_SOUND_SITE_PICKUP, PICKUP_NAME);

    engine.death_voices     = operand_at(MP_PLAYER_SOUND_SITE_DEATH, DEATH_VOICES);
    engine.names            = operand_at(MP_PLAYER_SOUND_SITE_DEATH, DEATH_NAMES);
    engine.play_by_name     = call_at(MP_PLAYER_SOUND_SITE_DEATH, DEATH_CALL, NULL);
    engine.burn_voices      = operand_at(MP_PLAYER_SOUND_SITE_BURN, BURN_VOICES);
    burn_names              = operand_at(MP_PLAYER_SOUND_SITE_BURN, BURN_NAMES);
    burn_play_by_name       = call_at(MP_PLAYER_SOUND_SITE_BURN, BURN_CALL, NULL);
    engine.burst            = call_at(MP_PLAYER_SOUND_SITE_BURN, BURN_BURST, NULL);
    engine.burst_speed      = immediate_at(MP_PLAYER_SOUND_SITE_BURN, BURN_SPEED);
    engine.shield_create    = call_at(MP_PLAYER_SOUND_SITE_SHIELD_UP, SHIELD_UP_CALL, NULL);
    engine.shield_timer     = immediate_at(MP_PLAYER_SOUND_SITE_SHIELD_UP, SHIELD_UP_TIMER);

    /* The three repointed sounds and the pickup's reach one entry, and both cries read one table of
     * names through one entry. A pair that disagrees means one of the four patterns landed in
     * somebody else's code, and nothing of it is used. */
    engine.agree = agree(engine.play_name, key_play_name) &&
                   agree(engine.play_name, water_play_name) &&
                   agree(engine.play_name, pickup_play_name) &&
                   agree(key_play_name, water_play_name) &&
                   agree(engine.names, burn_names) &&
                   agree(engine.play_by_name, burn_play_by_name);
    if (engine.play_name == 0u) {
        engine.play_name = key_play_name != 0u ? key_play_name : water_play_name;
    }
    if (engine.names == 0u) {
        engine.names = burn_names;
    }
    if (engine.play_by_name == 0u) {
        engine.play_by_name = burn_play_by_name;
    }
}

const mp_player_sound_engine_t *mp_signatures_player_sound_engine(void)
{
    if (read_once) {
        return &engine;
    }
    read_once = true;
    memset(&engine, 0, sizeof engine);
    engine.resolved = signature_resolve_table(sites, (size_t)MP_PLAYER_SOUND_SITE_COUNT);
    read_everything();
    log_info("the player sound sites: %u of %u resolved; the sound entry %08X, the voice entry "
             "%08X, the names at %08X, the death and burning voices at %08X and %08X, the burst "
             "%08X at %.2f, the shield %08X for %.1f s and its release %08X; %s",
             (unsigned)engine.resolved, (unsigned)MP_PLAYER_SOUND_SITE_COUNT,
             (unsigned)engine.play_name, (unsigned)engine.play_by_name, (unsigned)engine.names,
             (unsigned)engine.death_voices, (unsigned)engine.burn_voices,
             (unsigned)engine.burst, (double)engine.burst_speed,
             (unsigned)engine.shield_create, (double)engine.shield_timer,
             (unsigned)engine.shield_release,
             engine.agree ? "the pairs that must agree do"
                          : "two sites that must name one thing do not, so none of it is used");
    return &engine;
}
