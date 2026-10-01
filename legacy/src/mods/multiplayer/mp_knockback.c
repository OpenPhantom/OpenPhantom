/* mp_knockback.c: the host's count of what the contacts with code 0x22 did. The model is in the
 * header and in mp_knockback_rule.h.
 *
 * Only a first touch is read in full. The handler is called for every substep a wave and an actor
 * overlap, and every read here costs on the path every contact takes, so a contact with another
 * code stops at the code, and a touch that goes on an overlap stops at the pair.
 */
#include "mp_knockback.h"

#include "mp_bank.h"
#include "mp_enemy_sync.h"
#include "mp_knockback_rule.h"
#include "mp_task.h"
#include "mp_trust.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The three fields of the actor the handler's arms read: its flags, its state and its health,
 * which is a signed integer. */
#define ACTOR_STATE_FLAGS 0x14u
#define ACTOR_STATE       0x20u
#define ACTOR_HEALTH      0x38u

/* The side of the object that sent the contact: the class of whoever fired it. A wave of this
 * machine's own player carries 1, a far player's puppet stamps its bank's class on its shots. */
#define OBJECT_SIDE 0x08u
#define OWN_SIDE    1

typedef struct knockback_state {
    mp_knockback_memory_t memory;
    uint32_t              far_waves[MP_KNOCKBACK_TOUCH_COUNT];   /* first touches, by answer */
    uint32_t              far_otherwise;
    uint32_t              own_waves[MP_KNOCKBACK_TOUCH_COUNT];
    uint32_t              own_otherwise;
    uint32_t              later;          /* touches that went on an overlap */
    uint32_t              other_sender;   /* a 0x22 contact whose side names no player */
    uint32_t              resets;         /* the enemy table's resets the memory belongs to */
} knockback_state_t;

static knockback_state_t knock;

/* The memory names actors and wave objects by address, and a new level reuses the addresses. The
 * enemy table's reset is the one exit every way out of a level or a session takes, and it counts
 * itself, so a count that moved means the memory belongs to a world that is gone. */
static void follow_the_level(void)
{
    uint32_t resets = mp_enemy_sync_resets();

    if (resets != knock.resets) {
        mp_knockback_forget(&knock.memory);
        knock.resets = resets;
    }
}

/* Whose wave sent the contact, as a bank: 0 for this machine's own player, the far bank whose
 * class the side carries, or false for a side that is neither. */
static bool bank_of_side(int32_t side, uint8_t *bank)
{
    size_t i;

    if (side == OWN_SIDE) {
        *bank = 0u;
        return true;
    }
    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        if (side == mp_bank_class_of(i)) {
            *bank = (uint8_t)i;
            return true;
        }
    }
    return false;
}

void mp_knockback_before(mp_knockback_watch_t *watch, uintptr_t victim, uint32_t sender,
                         uintptr_t code_cell)
{
    uint32_t code   = 0u;
    int32_t  side   = 0;
    int32_t  health = 0;
    uint32_t flags  = 0u;
    uint8_t  bank   = 0u;

    memset(watch, 0, sizeof *watch);
    if (victim == 0 || sender == 0u || code_cell == 0 ||
        !memory_try_read(code_cell, &code, sizeof code) || code != MP_TRUST_WAVE_CODE) {
        return;
    }
    follow_the_level();
    if (!memory_try_read((uintptr_t)sender + OBJECT_SIDE, &side, sizeof side) ||
        !bank_of_side(side, &bank)) {
        ++knock.other_sender;
        return;
    }
    if (!mp_knockback_touch(&knock.memory, sender, (uint32_t)victim, bank, mp_task_ticks())) {
        ++knock.later;
        return;
    }
    watch->counted = true;
    watch->actor   = victim;
    watch->bank    = bank;
    if (!memory_try_read(victim + ACTOR_HEALTH, &health, sizeof health) ||
        !memory_try_read(victim + ACTOR_STATE, &watch->state_before,
                         sizeof watch->state_before) ||
        !memory_try_read(victim + ACTOR_STATE_FLAGS, &flags, sizeof flags)) {
        watch->unread = true;
        return;
    }
    watch->touch = mp_knockback_touch_of(health, watch->state_before, flags);
}

/* A first touch whose actor could not be read is counted with the ones where the engine did
 * otherwise: in neither case does the line know what happened. */
void mp_knockback_after(const mp_knockback_watch_t *watch)
{
    int32_t   state = 0;
    uint32_t *counts;
    uint32_t *otherwise;

    if (!watch->counted) {
        return;
    }
    counts    = watch->bank == 0u ? knock.own_waves : knock.far_waves;
    otherwise = watch->bank == 0u ? &knock.own_otherwise : &knock.far_otherwise;
    if (watch->unread || !memory_try_read(watch->actor + ACTOR_STATE, &state, sizeof state) ||
        !mp_knockback_touch_agrees(watch->touch, watch->state_before, state)) {
        ++*otherwise;
        return;
    }
    ++counts[watch->touch];
}

void mp_knockback_note_refused(uint32_t key, size_t bank)
{
    bool      far_bank = mp_bank_index_ok(bank);
    uintptr_t actor    = far_bank ? mp_enemy_sync_actor_for(key) : 0;

    follow_the_level();
    mp_knockback_refused(&knock.memory, (uint32_t)actor, far_bank ? (uint8_t)bank : (uint8_t)0u,
                         mp_task_ticks());
}

void mp_knockback_report(void)
{
    uint32_t waiting;

    follow_the_level();
    waiting = mp_knockback_waiting_count(&knock.memory, mp_task_ticks());
    log_info("  the contacts with code 0x22 (a force wave or an energy ball) a client reported: "
             "%u refused because this machine's copy of the same wave performs it; %u of them "
             "met by a wave of that player's puppet on the same actor within %u substeps either "
             "side, %u by none, %u cut off by the end of a level or a session, %u still inside "
             "that window, %u not watched",
             (unsigned)knock.memory.refused, (unsigned)knock.memory.met,
             (unsigned)MP_KNOCKBACK_WINDOW, (unsigned)knock.memory.unmet,
             (unsigned)knock.memory.cut, (unsigned)waiting, (unsigned)knock.memory.unwatched);
    log_info("  the contacts with code 0x22 (a force wave or an energy ball) made here, first "
             "touch of each wave on each actor: by the far players' waves %u thrown into the "
             "knockback, %u refused by the actor's no-throw flag, %u already in a throw, %u dead, "
             "a corpse or standing by, %u where the engine did otherwise; by this machine's own "
             "%u thrown, %u refused by the no-throw flag, %u already in a throw, %u dead, a "
             "corpse or standing by, %u otherwise | %u later touch(es) of the same wave, %u from "
             "another sender",
             (unsigned)knock.far_waves[MP_KNOCKBACK_THROWN],
             (unsigned)knock.far_waves[MP_KNOCKBACK_NO_THROW],
             (unsigned)knock.far_waves[MP_KNOCKBACK_ALREADY],
             (unsigned)knock.far_waves[MP_KNOCKBACK_DOWN], (unsigned)knock.far_otherwise,
             (unsigned)knock.own_waves[MP_KNOCKBACK_THROWN],
             (unsigned)knock.own_waves[MP_KNOCKBACK_NO_THROW],
             (unsigned)knock.own_waves[MP_KNOCKBACK_ALREADY],
             (unsigned)knock.own_waves[MP_KNOCKBACK_DOWN], (unsigned)knock.own_otherwise,
             (unsigned)knock.later, (unsigned)knock.other_sender);
}
