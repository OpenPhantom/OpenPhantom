/* mp_level_state_bind.c: the level's state on the running engine. See the header. */
#include "mp_level_state_bind.h"

#include "mp_cells.h"
#include "mp_scene_rule.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The world's two tables. */
#define WORLD_LIGHT_COUNT   0x1C4u   /* i32, the first light pool */
#define WORLD_LIGHTS        0x1CCu   /* records of 0x80 */
#define WORLD_EMITTER_COUNT 0xCCCu   /* i32, the emitter placements */
#define WORLD_EMITTERS      0xCD0u   /* records of 0x50 */

#define LIGHT_STRIDE 0x80u
#define LIGHT_ACTIVE 0x74u

/* One emitter placement: where, under which template, and the pool slot it spawned into, or -1.
 * The engine writes the slot when it spawns and never clears it when that emitter ends. */
#define PLACE_STRIDE    0x50u
#define PLACE_POSITION  0x00u
#define PLACE_TEMPLATE  0x10u
#define PLACE_LIVE      0x4Cu

/* One slot of the emitter pool: its name, its type (0 is a free slot, written last by the
 * allocator), its flags and its position. */
#define POOL_SLOTS     64u
#define POOL_STRIDE    0x124u
#define SLOT_NAME      0x00u
#define SLOT_TYPE      0x10u
#define SLOT_FLAGS     0x14u
#define SLOT_POSITION  0xC0u
#define SLOT_DISABLED  0x00080000u

/* The two most any shipped world holds are 20 placements and 403 lights; a count far past both is a
 * pointer that is not a world. */
#define COUNT_CEILING 4096u

/* Where each script arm calls, as the distance from the arm's start to the address its call
 * returns to: emitter_set at +0x29, light_set's on at +0x2C and its off at +0x41. */
#define EMITTER_ARM_RETURN   0x2Eu
#define LIGHT_ARM_ON_RETURN  0x31u
#define LIGHT_ARM_OFF_RETURN 0x46u

/* The head all three functions share, `push ebp; mov ebp, esp; push ecx; cmp [ebp+8], 0`. Another
 * module may overwrite it with a branch: the diagnostics hull the emitter switch with eight bytes.
 * The patterns below start behind it, and the head is only asked to be this or a branch. */
#define CALLED_HEAD_BYTES 8u
static const uint8_t SHIPPED_HEAD[CALLED_HEAD_BYTES] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00
};

#define BRANCH_OPCODE 0xE9u

/* Where the pool is named inside the emitter switch: `mov eax, [edx + pool + 0x14]` at +0x57, so
 * the operand at +0x59 is the flags word of slot 0. */
#define SET_ACTIVE_POOL_OPERAND 0x59u

/* engine: void emitter_setPlacementActive(i32 placeIndex, i32 bOn) */
typedef void(__cdecl *set_active_fn_t)(int32_t index, int32_t on);
/* engine: void baplight_enableLevelLight(bapWorld *level, i32 index)
 * The world travels as the value it is read as, never dereferenced here. */
typedef void(__cdecl *level_light_fn_t)(uint32_t level, int32_t index);

/* 0x0041FE24 emitter_setPlacementActive, from behind its head to the flag it sets. Masked: the two
 * reads of the world pointer and the pool operand. */
static const uint8_t SIG_SET_ACTIVE_TAIL[90] = {
    0x7C, 0x10, 0xA1, 0x60, 0x00, 0x8A, 0x00, 0x8B, 0x4D, 0x08, 0x3B, 0x88,
    0xCC, 0x0C, 0x00, 0x00, 0x7C, 0x05, 0xE9, 0x95, 0x00, 0x00, 0x00, 0x8B,
    0x15, 0x60, 0x00, 0x8A, 0x00, 0x8B, 0x82, 0xD0, 0x0C, 0x00, 0x00, 0x89,
    0x45, 0xFC, 0x8B, 0x4D, 0x08, 0x6B, 0xC9, 0x50, 0x8B, 0x55, 0xFC, 0x03,
    0xD1, 0x89, 0x55, 0xFC, 0x8B, 0x45, 0xFC, 0x83, 0x78, 0x4C, 0x00, 0x7C,
    0x5D, 0x83, 0x7D, 0x0C, 0x00, 0x75, 0x2B, 0x8B, 0x4D, 0xFC, 0x8B, 0x51,
    0x4C, 0x69, 0xD2, 0x24, 0x01, 0x00, 0x00, 0x8B, 0x82, 0x64, 0xFA, 0x6B,
    0x00, 0x0D, 0x00, 0x00, 0x08, 0x00,
};
static const uint8_t MSK_SET_ACTIVE_TAIL[90] = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* 0x00406E90 baplight_enableLevelLight, from behind its head to the store of the active word. No
 * absolute operand. */
static const uint8_t SIG_LIGHT_ON_TAIL[63] = {
    0x74, 0x14, 0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45, 0x08, 0x8B,
    0x4D, 0x0C, 0x3B, 0x88, 0xC4, 0x01, 0x00, 0x00, 0x7C, 0x02, 0xEB, 0x39,
    0x8B, 0x55, 0x0C, 0xC1, 0xE2, 0x07, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0xCC,
    0x01, 0x00, 0x00, 0x03, 0xCA, 0x89, 0x4D, 0xFC, 0x8B, 0x55, 0xFC, 0x83,
    0x7A, 0x74, 0x00, 0x75, 0x1C, 0x8B, 0x45, 0xFC, 0xC7, 0x40, 0x74, 0x01,
    0x00, 0x00, 0x00,
};

/* 0x00406EED baplight_disableLevelLight, from behind its head to its test of the active word. It
 * differs from the one above in two branch displacements and the sense of that test. */
static const uint8_t SIG_LIGHT_OFF_TAIL[53] = {
    0x74, 0x14, 0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45, 0x08, 0x8B,
    0x4D, 0x0C, 0x3B, 0x88, 0xC4, 0x01, 0x00, 0x00, 0x7C, 0x02, 0xEB, 0x2F,
    0x8B, 0x55, 0x0C, 0xC1, 0xE2, 0x07, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0xCC,
    0x01, 0x00, 0x00, 0x03, 0xCA, 0x89, 0x4D, 0xFC, 0x8B, 0x55, 0xFC, 0x83,
    0x7A, 0x74, 0x00, 0x74, 0x12,
};

/* How the director reaches an arm. At +0x1C it tests the command against its twenty and jumps
 * through its table, whose address is the operand at +0x2C. */
#define DIRECTOR_DISPATCH      0x1Cu
#define DIRECTOR_TABLE_OPERAND 0x2Cu
#define DIRECTOR_COMMANDS      20u

/* 0x00429880+0x1C: `cmp [ebp-24h], 13h; ja; mov eax, [ebp-24h]; jmp [eax*4 + table]`. */
static const uint8_t SIG_DIRECTOR_DISPATCH[16] = {
    0x83, 0x7D, 0xDC, 0x13, 0x0F, 0x87, 0x8F, 0x03, 0x00, 0x00, 0x8B, 0x45, 0xDC, 0xFF, 0x24, 0x85,
};

/* Command 15, the escort: the actor's placement and health, health times 100 over the
 * placement's hit points, and the call that sets the bar, which returns to +0x25. */
#define COMMAND_ESCORT    15
#define ARM_ESCORT_RETURN 0x25u
static const uint8_t SIG_ARM_ESCORT[33] = {
    0x8B, 0x4D, 0x08, 0x8B, 0x51, 0x10, 0x89, 0x55, 0xF0, 0x8B, 0x45, 0x08,
    0x8B, 0x40, 0x38, 0x6B, 0xC0, 0x64, 0x8B, 0x4D, 0xF0, 0x99, 0xF7, 0x79,
    0x0C, 0x89, 0x45, 0xF4, 0x8B, 0x55, 0xF4, 0x52, 0xE8,
};

/* 0x0045A0A3, the escort's setter, from behind its head: the clamp to 0..100, the store of the
 * health and the store of the bar's fade target, 3.0, for a health above 0. Masked: the two words.
 * Its head is the one the level state's three functions share. */
#define ESCORT_HEALTH_OPERAND 0x24u
#define ESCORT_TARGET_OPERAND 0x30u
static const uint8_t SIG_ESCORT_TAIL[48] = {
    0x7D, 0x09, 0xC7, 0x45, 0x08, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0D, 0x83,
    0x7D, 0x08, 0x64, 0x7E, 0x07, 0xC7, 0x45, 0x08, 0x64, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x08, 0xA3, 0x00, 0x00, 0x00, 0x00, 0x83, 0x7D, 0x08, 0x00,
    0x74, 0x0C, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40,
};
static const uint8_t MSK_ESCORT_TAIL[48] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* The actor a replayed command is handed, as the director's head reads it. */
#define STAND_IN_BYTES 0x204u

/* engine: void status_setEscortHp(int hp) */
typedef void(__cdecl *escort_set_fn_t)(int32_t health);

typedef struct bind_state {
    bool             attempted;
    bool             ready;
    set_active_fn_t  set_active;
    level_light_fn_t light_on;
    level_light_fn_t light_off;
    uint32_t         pool;          /* slot 0 of the emitter pool */

    mp_level_director_fn_t director;
    uintptr_t              director_entry;

    bool            escort_attempted;
    escort_set_fn_t escort_set;
    uint32_t        escort_health;   /* i32, 0..100 */
    uint32_t        escort_target;   /* f32, the bar's fade target, 0 when it is down */
} bind_state_t;

static bind_state_t bind;
static uint8_t      stand_in[STAND_IN_BYTES];

uintptr_t mp_level_state_bind_callee(uintptr_t return_address)
{
    mp_scene_call_site_t site;
    uintptr_t            entry = 0u;

    if (return_address < MP_SCENE_CALL_BYTES) {
        return 0u;
    }
    site.return_address = return_address;
    if (!memory_try_read(return_address - MP_SCENE_CALL_BYTES, site.call, sizeof site.call)) {
        return 0u;
    }
    return mp_scene_camera_callee(&site, 1u, &entry) == MP_SCENE_CALLEE_AGREED ? entry : 0u;
}

uintptr_t mp_level_state_bind_proved(uintptr_t address, const uint8_t *tail, const uint8_t *mask,
                                     size_t tail_size)
{
    uint8_t seen[CALLED_HEAD_BYTES + 128u];
    size_t  i;

    if (address == 0u || tail == NULL || tail_size > sizeof seen - CALLED_HEAD_BYTES ||
        !memory_try_read(address, seen, CALLED_HEAD_BYTES + tail_size)) {
        return 0u;
    }
    for (i = 0; i < tail_size; ++i) {
        if ((mask == NULL || mask[i] != 0u) && seen[CALLED_HEAD_BYTES + i] != tail[i]) {
            return 0u;
        }
    }
    if (seen[0] != BRANCH_OPCODE && memcmp(seen, SHIPPED_HEAD, CALLED_HEAD_BYTES) != 0) {
        return 0u;
    }
    return address;
}

static bool refuse(const char *why)
{
    log_warning("the level's state is NOT bound, so a client goes on switching its own level and "
                "nothing it switches is matched to the host: %s", why);
    return false;
}

bool mp_level_state_bind_install(void)
{
    uintptr_t emitter_arm;
    uintptr_t light_arm;
    uintptr_t set_active;
    uintptr_t light_on;
    uintptr_t light_off;
    uint32_t  flags_cell = 0;

    if (bind.attempted) {
        return bind.ready;
    }
    bind.attempted = true;
    emitter_arm = mp_signatures_address(MP_SITE_EMITTER_SET);
    light_arm   = mp_signatures_address(MP_SITE_LIGHT_SET);
    if (emitter_arm == 0u || light_arm == 0u) {
        return refuse(emitter_arm == 0u ? "the emitter arm did not resolve"
                                        : "the light arm did not resolve");
    }
    set_active = mp_level_state_bind_callee(emitter_arm + EMITTER_ARM_RETURN);
    light_on   = mp_level_state_bind_callee(light_arm + LIGHT_ARM_ON_RETURN);
    light_off  = mp_level_state_bind_callee(light_arm + LIGHT_ARM_OFF_RETURN);
    if (set_active == 0u || light_on == 0u || light_off == 0u) {
        return refuse("a script arm's call does not name a function");
    }
    if (mp_level_state_bind_proved(set_active, SIG_SET_ACTIVE_TAIL, MSK_SET_ACTIVE_TAIL,
                                   sizeof SIG_SET_ACTIVE_TAIL) == 0u) {
        return refuse("the function the emitter arm calls is not the emitter switch past its "
                      "head");
    }
    if (mp_level_state_bind_proved(light_on, SIG_LIGHT_ON_TAIL, NULL, sizeof SIG_LIGHT_ON_TAIL) ==
            0u ||
        mp_level_state_bind_proved(light_off, SIG_LIGHT_OFF_TAIL, NULL,
                                   sizeof SIG_LIGHT_OFF_TAIL) == 0u) {
        return refuse("a function the light arm calls is not the light switch past its head");
    }
    if (!memory_read_u32(set_active + SET_ACTIVE_POOL_OPERAND, &flags_cell) ||
        flags_cell < SLOT_FLAGS ||
        !memory_is_inside_image((uintptr_t)(flags_cell - SLOT_FLAGS),
                                (size_t)POOL_SLOTS * POOL_STRIDE)) {
        return refuse("the emitter pool the switch names lies outside the image");
    }
    bind.set_active = (set_active_fn_t)set_active;
    bind.light_on   = (level_light_fn_t)light_on;
    bind.light_off  = (level_light_fn_t)light_off;
    bind.pool       = flags_cell - SLOT_FLAGS;
    bind.ready      = true;
    log_info("the level's state is bound: emitter placements through %08X (the pool at %08X), "
             "level lights on through %08X and off through %08X, each read out of its script "
             "arm's call and checked past its head, so another module's hull on a head does not "
             "stop it", (unsigned)set_active, (unsigned)bind.pool, (unsigned)light_on,
             (unsigned)light_off);
    return true;
}

bool mp_level_state_bind_ready(void)
{
    return bind.ready;
}

void mp_level_state_bind_set_director(mp_level_director_fn_t trampoline, uintptr_t entry)
{
    bind.director       = trampoline;
    bind.director_entry = entry;
}

bool mp_level_state_bind_has_director(void)
{
    return bind.director != NULL;
}

uintptr_t mp_level_state_bind_director_entry(void)
{
    return bind.director_entry;
}

bool mp_level_state_bind_call_director(uintptr_t actor, int32_t command, int32_t a1, int32_t a2)
{
    if (bind.director == NULL || actor == 0u) {
        return false;
    }
    (void)bind.director((void *)actor, command, a1, a2);
    return true;
}

uintptr_t mp_level_state_bind_stand_in(void)
{
    return (uintptr_t)stand_in;
}

uintptr_t mp_level_state_bind_director_arm(int32_t command)
{
    uint8_t  dispatch[sizeof SIG_DIRECTOR_DISPATCH];
    uint32_t table = 0;
    uint32_t arm   = 0;

    /* Past its head the director is the engine's own, and this feature's hull on the head is the
     * only one: the dispatch is compared whole. */
    if (bind.director_entry == 0u || command < 0 || command >= (int32_t)DIRECTOR_COMMANDS ||
        !memory_try_read(bind.director_entry + DIRECTOR_DISPATCH, dispatch, sizeof dispatch) ||
        memcmp(dispatch, SIG_DIRECTOR_DISPATCH, sizeof dispatch) != 0 ||
        !memory_read_u32(bind.director_entry + DIRECTOR_TABLE_OPERAND, &table) ||
        !memory_is_inside_image((uintptr_t)table, DIRECTOR_COMMANDS * 4u) ||
        !memory_read_u32((uintptr_t)table + (uintptr_t)command * 4u, &arm) ||
        !memory_is_inside_image((uintptr_t)arm, 1u)) {
        return 0u;
    }
    return (uintptr_t)arm;
}

static bool refuse_escort(const char *why)
{
    log_warning("the escort's bar is NOT bound, so a client does not see the bar the host's "
                "scripts raise, and its own scripts go on raising theirs: %s", why);
    return false;
}

bool mp_level_state_bind_escort(void)
{
    uintptr_t arm;
    uintptr_t setter;
    uint8_t   code[sizeof SIG_ARM_ESCORT];
    uint32_t  health = 0;
    uint32_t  target = 0;

    if (bind.escort_attempted || bind.director_entry == 0u) {
        return bind.escort_set != NULL;
    }
    bind.escort_attempted = true;
    arm = mp_level_state_bind_director_arm(COMMAND_ESCORT);
    if (arm == 0u || !memory_try_read(arm, code, sizeof code) ||
        memcmp(code, SIG_ARM_ESCORT, sizeof code) != 0) {
        return refuse_escort("command 15 of the director is not the arm that sets the bar");
    }
    setter = mp_level_state_bind_callee(arm + ARM_ESCORT_RETURN);
    if (setter == 0u ||
        mp_level_state_bind_proved(setter, SIG_ESCORT_TAIL, MSK_ESCORT_TAIL,
                                   sizeof SIG_ESCORT_TAIL) == 0u ||
        !memory_read_u32(setter + ESCORT_HEALTH_OPERAND, &health) ||
        !memory_read_u32(setter + ESCORT_TARGET_OPERAND, &target) ||
        !memory_is_inside_image((uintptr_t)health, sizeof(int32_t)) ||
        !memory_is_inside_image((uintptr_t)target, sizeof(float))) {
        return refuse_escort("the function command 15 calls is not the bar's setter past its head");
    }
    bind.escort_health = health;
    bind.escort_target = target;
    bind.escort_set    = (escort_set_fn_t)setter;
    log_info("the escort's bar is bound: the setter at %08X, its health at %08X and its fade "
             "target at %08X, read out of the director's command 15 at %08X",
             (unsigned)setter, (unsigned)health, (unsigned)target, (unsigned)arm);
    return true;
}

bool mp_level_state_bind_escort_read(uint8_t *health, bool *shown)
{
    int32_t hp     = 0;
    float   target = 0.0f;

    if (bind.escort_set == NULL || health == NULL || shown == NULL ||
        !memory_try_read((uintptr_t)bind.escort_health, &hp, sizeof hp) ||
        !memory_try_read((uintptr_t)bind.escort_target, &target, sizeof target)) {
        return false;
    }
    *health = (uint8_t)(hp < 0 ? 0 : (hp > 100 ? 100 : hp));
    *shown  = target > 0.0f;
    return true;
}

bool mp_level_state_bind_escort_set(uint8_t health)
{
    if (bind.escort_set == NULL) {
        return false;
    }
    bind.escort_set((int32_t)health);
    return true;
}

uint32_t mp_level_state_bind_world(void)
{
    uintptr_t cell  = mp_cells_address(MP_CELL_LEVEL);
    uint32_t  world = 0;

    if (cell == 0u || !memory_try_read(cell, &world, sizeof world)) {
        return 0u;
    }
    return world;
}

bool mp_level_state_bind_counts(uint32_t world, uint32_t *emitters, uint32_t *lights)
{
    int32_t e = 0;
    int32_t l = 0;

    if (world == 0u || emitters == NULL || lights == NULL ||
        !memory_try_read((uintptr_t)world + WORLD_EMITTER_COUNT, &e, sizeof e) ||
        !memory_try_read((uintptr_t)world + WORLD_LIGHT_COUNT, &l, sizeof l) || e < 0 || l < 0 ||
        (uint32_t)e > COUNT_CEILING || (uint32_t)l > COUNT_CEILING) {
        return false;
    }
    *emitters = (uint32_t)e;
    *lights   = (uint32_t)l;
    return true;
}

mp_level_emitter_t mp_level_state_bind_emitter(uint32_t world, uint32_t index)
{
    uint32_t places = 0;
    uint8_t  place[PLACE_STRIDE];
    uint8_t  slot[SLOT_FLAGS + 4u];
    float    at[3];
    int32_t  live = -1;
    uint32_t type = 0;
    uint32_t flags = 0;
    bool     owned = false;

    if (world == 0u || !bind.ready ||
        !memory_try_read((uintptr_t)world + WORLD_EMITTERS, &places, sizeof places) ||
        places == 0u ||
        !memory_try_read((uintptr_t)places + (uintptr_t)index * PLACE_STRIDE, place,
                         sizeof place)) {
        return MP_LEVEL_EMITTER_GONE;
    }
    memcpy(&live, place + PLACE_LIVE, sizeof live);
    if (live >= 0 && (uint32_t)live < POOL_SLOTS &&
        memory_try_read((uintptr_t)bind.pool + (uintptr_t)live * POOL_STRIDE, slot,
                        sizeof slot) &&
        memory_try_read((uintptr_t)bind.pool + (uintptr_t)live * POOL_STRIDE + SLOT_POSITION, at,
                        sizeof at)) {
        memcpy(&type, slot + SLOT_TYPE, sizeof type);
        memcpy(&flags, slot + SLOT_FLAGS, sizeof flags);
        owned = mp_level_state_emitter_owned(type, (const char *)slot + SLOT_NAME,
                                             (const char *)place + PLACE_TEMPLATE, at,
                                             (const float *)(place + PLACE_POSITION));
    }
    return mp_level_state_emitter_seen(live, owned, (flags & SLOT_DISABLED) != 0u);
}

bool mp_level_state_bind_light(uint32_t world, uint32_t index, bool *on)
{
    uint32_t lights = 0;
    uint32_t active = 0;

    if (world == 0u || on == NULL ||
        !memory_try_read((uintptr_t)world + WORLD_LIGHTS, &lights, sizeof lights) ||
        lights == 0u ||
        !memory_try_read((uintptr_t)lights + (uintptr_t)index * LIGHT_STRIDE + LIGHT_ACTIVE,
                         &active, sizeof active)) {
        return false;
    }
    *on = active != 0u;
    return true;
}

bool mp_level_state_bind_set_emitter(uint32_t index, bool on)
{
    if (!bind.ready) {
        return false;
    }
    bind.set_active((int32_t)index, on ? 1 : 0);
    return true;
}

bool mp_level_state_bind_set_light(uint32_t world, uint32_t index, bool on)
{
    if (!bind.ready || world == 0u) {
        return false;
    }
    if (on) {
        bind.light_on(world, (int32_t)index);
    } else {
        bind.light_off(world, (int32_t)index);
    }
    return true;
}
