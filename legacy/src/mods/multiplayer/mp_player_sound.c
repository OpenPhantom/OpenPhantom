/* mp_player_sound.c: this machine's player's own sounds and shield, caught for the far side. See
 * the header.
 *
 * Each repointed call keeps its engine behaviour exactly: the hook calls the function the call
 * reached before, with the same two arguments, hands back its answer, and only then decides
 * whether there is something to tell. A sound the engine refused to start is told all the same,
 * because the far side has a start gate of its own and its listener stands somewhere else.
 */
#include "mp_player_sound.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_player_sound_rule.h"
#include "mp_signatures_player_sound.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The player record's hero row, the one both voice tables are indexed by, and the shield's timer,
 * which the shield pickup sets and the timers phase counts down. */
#define RECORD_HERO         0x6Cu
#define RECORD_SHIELD_TIMER 0x94u

/* The pickup kind whose arm in the engine's pickup puts a shield round the body. */
#define SHIELD_PICKUP_KIND 0x0A

typedef int32_t(__cdecl *play_name_fn_t)(const char *name, uint32_t flags);
typedef int32_t(__cdecl *shield_release_fn_t)(int32_t id);

/* A named sound whose index could not be read out of its site. */
#define NO_NAME 0xFFu

typedef struct sender_state {
    bool                       installed;
    bool                       complete;
    mp_player_sound_queue_fn_t queue;
    play_name_fn_t             play_name;
    shield_release_fn_t        shield_release;

    /* The name each caught sound plays, as an index into the sound names, NO_NAME when its site
     * did not read. */
    uint8_t ground_name;
    uint8_t key_name;
    uint8_t water_name;
    uint8_t pickup_name;

    mp_player_sound_sent_t counts;
} sender_state_t;

static sender_state_t sender = {
    false, false, NULL, NULL, NULL, NO_NAME, NO_NAME, NO_NAME, NO_NAME, { { 0u } }
};

/* Whether the body the engine just acted for is this machine's player in a session. */
static bool this_players(void)
{
    if (mp_player_sound_rule_may_note(mp_armed_transport(), mp_bank_active_class())) {
        return true;
    }
    ++sender.counts.left;
    return false;
}

static void queue_moment(uint8_t what, uint8_t sound, uint8_t flags)
{
    mp_event_t moment;

    if (sender.queue == NULL) {
        ++sender.counts.unqueued;
        return;
    }
    memset(&moment, 0, sizeof moment);
    moment.kind        = MP_EVENT_PLAYER_SOUND;
    moment.sound_what  = what;
    moment.sound_index = sound;
    moment.sound_flags = flags;
    sender.queue(&moment);
    ++sender.counts.sent[what];
}

/* One of the four named sounds, told when its name is known. */
static void note_named(uint8_t what, uint8_t name)
{
    if (!this_players()) {
        return;
    }
    if (name == NO_NAME) {
        ++sender.counts.unsited;
        return;
    }
    queue_moment(what, name, 0u);
}

/* The three named sounds, each at its own call of the one entry.
 *
 * engine: int bapsound_playName(const char *wavename, u32 flags) */
static int32_t __cdecl hook_ground(const char *name, uint32_t flags)
{
    int32_t channel = sender.play_name(name, flags);

    note_named((uint8_t)MP_PLAYER_SOUND_GROUND, sender.ground_name);
    return channel;
}

/* engine: int bapsound_playName(const char *wavename, u32 flags) */
static int32_t __cdecl hook_key(const char *name, uint32_t flags)
{
    int32_t channel = sender.play_name(name, flags);

    note_named((uint8_t)MP_PLAYER_SOUND_KEY, sender.key_name);
    return channel;
}

/* engine: int bapsound_playName(const char *wavename, u32 flags) */
static int32_t __cdecl hook_water(const char *name, uint32_t flags)
{
    int32_t channel = sender.play_name(name, flags);

    note_named((uint8_t)MP_PLAYER_SOUND_WATER, sender.water_name);
    return channel;
}

/* The shield released at the timers phase's one call, when its time is up.
 *
 * engine: int fxshield_release(int id) */
static int32_t __cdecl hook_shield_down(int32_t id)
{
    int32_t released = sender.shield_release(id);

    if (this_players()) {
        queue_moment((uint8_t)MP_PLAYER_SOUND_SHIELD_OFF, 0u, 0u);
    }
    return released;
}

void mp_player_sound_note_death(int32_t cause, uintptr_t record)
{
    uint8_t  whats[2] = { 0u, 0u };
    size_t   count = mp_player_sound_rule_death_moments(cause, whats);
    uint32_t hero = 0u;
    size_t   i;

    if (!this_players()) {
        return;
    }
    if (count == 0u || record == 0u || !memory_try_read(record + RECORD_HERO, &hero, sizeof hero) ||
        hero >= MP_PLAYER_SOUND_HEROES) {
        ++sender.counts.unread;
        return;
    }
    for (i = 0; i < count; ++i) {
        queue_moment(whats[i], (uint8_t)hero, (uint8_t)cause);
    }
}

void mp_player_sound_note_pickup(int32_t kind)
{
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t  record = 0u;
    float     timer = 0.0f;
    uint8_t   seconds;

    if (!this_players()) {
        return;
    }
    if (sender.pickup_name == NO_NAME) {
        ++sender.counts.unsited;
    } else {
        queue_moment((uint8_t)MP_PLAYER_SOUND_PICKUP, sender.pickup_name, 0u);
    }
    if (kind != SHIELD_PICKUP_KIND) {
        return;
    }
    /* The length is the timer the pickup has just set, read rather than assumed. */
    if (pr_cell == 0u || !memory_read_u32(pr_cell, &record) || record == 0u ||
        !memory_try_read((uintptr_t)record + RECORD_SHIELD_TIMER, &timer, sizeof timer)) {
        ++sender.counts.unread;
        return;
    }
    seconds = mp_player_sound_rule_seconds(timer);
    if (seconds == 0u) {
        ++sender.counts.unread;
        return;
    }
    queue_moment((uint8_t)MP_PLAYER_SOUND_SHIELD_ON, seconds, 0u);
}

void mp_player_sound_sent_counts(mp_player_sound_sent_t *out)
{
    if (out != NULL) {
        *out = sender.counts;
    }
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

static uint8_t name_of(uint32_t operand, uint32_t names)
{
    uint8_t index = NO_NAME;

    if (operand == 0u || names == 0u || !mp_player_sound_rule_name_index(operand, names, &index)) {
        return NO_NAME;
    }
    return index;
}

/* One call repointed at its hook, only when the function it reached is known, because the hook
 * calls that function and nothing else. */
static bool repoint(uintptr_t call, uintptr_t original, const void *hook, const char *what)
{
    patch_result_t result;

    if (call == 0u || original == 0u) {
        log_warning("the %s is not caught for the far side: its site did not resolve", what);
        return false;
    }
    result = patch_redirect_call(call, hook);
    if (result != PATCH_RESULT_OK) {
        log_warning("the %s is not caught for the far side: its call at %08X did not move (%s)",
                    what, (unsigned)call, patch_result_text(result));
        return false;
    }
    return true;
}

bool mp_player_sound_install(mp_player_sound_queue_fn_t queue)
{
    const mp_player_sound_engine_t *engine;
    uint32_t                        redirected = 0u;

    if (sender.installed) {
        return sender.complete;
    }
    sender.installed = true;
    sender.queue     = queue;
    engine = mp_signatures_player_sound_engine();
    if (!engine->agree) {
        log_warning("this player's own sounds are not told to the far side: two of their sites "
                    "name different functions or tables, so none of them is trusted");
        return false;
    }
    sender.play_name      = (play_name_fn_t)engine->play_name;
    sender.shield_release = (shield_release_fn_t)engine->shield_release;
    sender.ground_name    = name_of(engine->ground_name, engine->names);
    sender.key_name       = name_of(engine->key_name, engine->names);
    sender.water_name     = name_of(engine->water_name, engine->names);
    sender.pickup_name    = name_of(engine->pickup_name, engine->names);

    redirected += repoint(engine->ground_call, engine->play_name, (const void *)&hook_ground,
                          "burning ground") ? 1u : 0u;
    redirected += repoint(engine->key_call, engine->play_name, (const void *)&hook_key, "key")
                      ? 1u : 0u;
    redirected += repoint(engine->water_call, engine->play_name, (const void *)&hook_water,
                          "plunge into water") ? 1u : 0u;
    redirected += repoint(engine->shield_down_call, engine->shield_release,
                          (const void *)&hook_shield_down, "shield's end") ? 1u : 0u;
    sender.counts.redirected = redirected;
    sender.complete          = redirected == 4u;
    log_info("this player's own sounds go to the far side: %u of 4 calls caught (the burning "
             "ground plays name %u, the key %u, the water %u, a pickup %u; 255 is a name that did "
             "not read), the death cries from the death hull and the pickup from the pickup hull",
             (unsigned)redirected, (unsigned)sender.ground_name, (unsigned)sender.key_name,
             (unsigned)sender.water_name, (unsigned)sender.pickup_name);
    return sender.complete;
}
