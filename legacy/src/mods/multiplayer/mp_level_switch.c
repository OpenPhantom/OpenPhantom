/* mp_level_switch.c: the three script arms that switch the level, hulled. See the header. */
#include "mp_level_switch.h"

#include "mp_level_state.h"
#include "mp_session_now.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"

#include <stddef.h>
#include <stdint.h>

/* `void <arm>(character *actor, int mode, i16 slot)`. All three are the same shape, which is why
 * one hull body serves them; the slot arrives in a four byte stack slot and the engine reads the
 * low word of it with a movsx, so it is taken here as the int the caller pushed and never widened
 * or narrowed on the way through. */
typedef void(__cdecl *switch_arm_fn_t)(uint32_t actor, int32_t mode, int32_t slot);

typedef struct arm_row {
    detour_t        detour;
    switch_arm_fn_t original;
    bool            installed;
} arm_row_t;

typedef struct level_switch_state {
    bool      host;
    arm_row_t rows[MP_LEVEL_KIND_COUNT];
} level_switch_state_t;

static level_switch_state_t level_switch;

/* One body for all three. The session is read here, once per entry, from the one reading every
 * gate of a client shares; the level state counts the entry and answers whether it is refused. A
 * refused entry never reaches the engine, and the host's note says what the level has instead. */
static void decide_and_chain(mp_level_kind_t kind, uint32_t actor, int32_t mode, int32_t slot)
{
    arm_row_t *row = &level_switch.rows[kind];

    if (mp_level_state_switch(kind, (uintptr_t)actor, mode,
                              mp_session_now_client_of_a_started_session(NULL, NULL))) {
        return;
    }
    if (row->original != NULL) {
        row->original(actor, mode, slot);
    }
}

/* engine: void emitter_set(character *actor, int mode, i16 slot) */
static void __cdecl hook_emitter_set(uint32_t actor, int32_t mode, int32_t slot)
{
    decide_and_chain(MP_LEVEL_KIND_EMITTER, actor, mode, slot);
}

/* engine: void light_set(character *actor, int mode, i16 slot) */
static void __cdecl hook_light_set(uint32_t actor, int32_t mode, int32_t slot)
{
    decide_and_chain(MP_LEVEL_KIND_LIGHT, actor, mode, slot);
}

/* engine: void sound_set(character *actor, int mode, i16 slot) */
static void __cdecl hook_sound_set(uint32_t actor, int32_t mode, int32_t slot)
{
    decide_and_chain(MP_LEVEL_KIND_SOUND, actor, mode, slot);
}

static bool install_one(mp_level_kind_t kind, mp_site_t site, const void *hook)
{
    arm_row_t *row    = &level_switch.rows[kind];
    uintptr_t  target = mp_signatures_address(site);

    if (row->installed) {
        return true;
    }
    if (target == 0u) {
        return false;
    }
    if (!detour_install(&row->detour, target, hook, mp_signatures_prologue(site))) {
        return false;
    }
    row->original  = (switch_arm_fn_t)row->detour.original;
    row->installed = true;
    return true;
}

bool mp_level_switch_install(void)
{
    bool all;

    /* All three or none are refused. The addresses are asked for first, so an arm that does not
     * resolve leaves the other two unhulled; a detour that is refused after another was taken
     * leaves that one counting and chaining, and the level state then refuses nothing. */
    if (mp_signatures_address(MP_SITE_EMITTER_SET) == 0u ||
        mp_signatures_address(MP_SITE_LIGHT_SET) == 0u ||
        mp_signatures_address(MP_SITE_SOUND_SET) == 0u) {
        log_warning("what a script switches on the level is NOT held on this machine: one of the "
                    "three arms did not resolve, so a client goes on switching its own level");
        (void)mp_level_state_install(false);
        return false;
    }
    all = install_one(MP_LEVEL_KIND_EMITTER, MP_SITE_EMITTER_SET,
                      (const void *)&hook_emitter_set) &&
          install_one(MP_LEVEL_KIND_LIGHT, MP_SITE_LIGHT_SET, (const void *)&hook_light_set) &&
          install_one(MP_LEVEL_KIND_SOUND, MP_SITE_SOUND_SET, (const void *)&hook_sound_set);
    if (!all) {
        log_warning("what a script switches on the level is NOT held on this machine: a detour on "
                    "one of the three arms was refused, so the ones that stand only count");
        (void)mp_level_state_install(false);
        return false;
    }
    log_info("what a script switches on the level is hulled at emitter_set, light_set and "
             "sound_set: counted on both sides, and refused to a client's own scripts in a started "
             "session whenever this side can match the host's level");
    (void)mp_level_state_install(true);
    return true;
}

void mp_level_switch_set_host(bool host)
{
    level_switch.host = host;
}

void mp_level_switch_report(void)
{
    mp_level_state_report(level_switch.host);
}
