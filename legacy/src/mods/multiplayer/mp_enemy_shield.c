/* mp_enemy_shield.c: a droideka's shield on the running engine. See the header. */
#include "mp_enemy_shield.h"

#include "mp_enemy_shield_rule.h"
#include "mp_level_state_bind.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The body's shield word, and one slot of the pool: in use, shown, and its colour. */
#define BODY_SHIELD   0x100u
#define POOL_SLOTS    32u
#define POOL_STRIDE   0xB4u
#define SLOT_IN_USE   0x00u
#define SLOT_SHOWN    0x04u
#define SLOT_OWNER    0x10u   /* the body the slot was made for */
#define SLOT_COLOUR   0x38u   /* red, green, blue, alpha */
#define SLOT_RADIUS   0x4Cu   /* one float, 0.5 from a hang */

/* The director's commands for it. */
#define COMMAND_HANG 2
#define COMMAND_TAKE 3
#define COMMAND_SHOW 4
#define COMMAND_SIZE 5

/* How the director reaches command 4. At +0x1C it tests the command against its twenty and jumps
 * through its table, whose address is the operand at +0x2C. */
#define DIRECTOR_DISPATCH       0x1Cu
#define DIRECTOR_TABLE_OPERAND  0x2Cu
#define DIRECTOR_COMMANDS       20u

/* Command 4's arm reads the body's shield word and calls the function that shows a shield; the
 * call returns to +0x13. Command 5's arm is the same shape with the second operand, and so is the
 * function it calls, which stores that operand as the slot's radius. */
#define ARM_SHOW_RETURN 0x13u
#define ARM_SIZE_RETURN 0x13u

/* The radius a hang gives, in eighths: the host line counts the shields a script sized. */
#define HANG_RADIUS_EIGHTHS 4u

/* The pool is the operand at +0x1E of the show function, in the address arithmetic behind its
 * head. */
#define SHOW_POOL_OPERAND 0x1Eu

#define LINES_WRITTEN 8u

/* 0x00429880+0x1C: `cmp [ebp-24h], 13h; ja; mov eax, [ebp-24h]; jmp [eax*4 + table]`. */
static const uint8_t SIG_DISPATCH[16] = {
    0x83, 0x7D, 0xDC, 0x13, 0x0F, 0x87, 0x8F, 0x03, 0x00, 0x00, 0x8B, 0x45, 0xDC, 0xFF, 0x24, 0x85,
};

/* 0x004299FC, command 4: push the operand, push the body's shield word at +0x100, call. */
static const uint8_t SIG_ARM_SHOW[15] = {
    0x8B, 0x55, 0x10, 0x52, 0x8B, 0x45, 0xFC, 0x8B, 0x88, 0x00, 0x01, 0x00, 0x00, 0x51, 0xE8,
};

/* 0x0043B016 fxshield_setVisible, from behind its head: the bound of 32 slots, then
 * `imul eax, eax, 0B4h; add eax, pool`, the test of the slot's in use word, and the store of the
 * operand into the shown word at +4, which is what tells it from the other setters of the same
 * pool. Masked: the pool. Its head is the one the level state's three functions share. */
static const uint8_t SIG_SHOW_TAIL[50] = {
    0x7C, 0x06, 0x83, 0x7D, 0x08, 0x20, 0x7C, 0x04, 0x33, 0xC0, 0xEB, 0x2B,
    0x8B, 0x45, 0x08, 0x69, 0xC0, 0xB4, 0x00, 0x00, 0x00, 0x05, 0x30, 0x9A,
    0x6C, 0x00, 0x89, 0x45, 0xFC, 0x8B, 0x4D, 0xFC, 0x83, 0x39, 0x00, 0x75,
    0x04, 0x33, 0xC0, 0xEB, 0x0E, 0x8B, 0x55, 0xFC, 0x8B, 0x45, 0x0C, 0x89,
    0x42, 0x04,
};
static const uint8_t MSK_SHOW_TAIL[50] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
};

/* 0x00429A17, command 5: push the second operand, push the body's shield word, call. */
static const uint8_t SIG_ARM_RADIUS[15] = {
    0x8B, 0x55, 0x14, 0x52, 0x8B, 0x45, 0xFC, 0x8B, 0x88, 0x00, 0x01, 0x00, 0x00, 0x51, 0xE8,
};

/* 0x0043AF95 fxshield_setRadius, from behind the same head: the same bound, the same address
 * arithmetic, the same in use test, and the store of the operand into the radius at +0x4C. Masked:
 * the pool, which has to be the one command 4 named. */
static const uint8_t SIG_RADIUS_TAIL[50] = {
    0x7C, 0x06, 0x83, 0x7D, 0x08, 0x20, 0x7C, 0x04, 0x33, 0xC0, 0xEB, 0x26,
    0x8B, 0x45, 0x08, 0x69, 0xC0, 0xB4, 0x00, 0x00, 0x00, 0x05, 0x30, 0x9A,
    0x6C, 0x00, 0x89, 0x45, 0xFC, 0x8B, 0x4D, 0xFC, 0x83, 0x39, 0x00, 0x75,
    0x04, 0x33, 0xC0, 0xEB, 0x09, 0x8B, 0x55, 0xFC, 0x8B, 0x45, 0x0C, 0x89,
    0x42, 0x4C,
};
static const uint8_t MSK_RADIUS_TAIL[50] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
};

typedef struct shield_state {
    bool     attempted;
    bool     bound;
    bool     sizes;     /* command 5 was proved as well */
    uint32_t pool;

    /* the host */
    uint32_t hanging;
    uint32_t hidden;
    uint32_t in_colour;
    uint32_t other_colour;
    uint32_t sized;     /* a radius other than the one a hang gives */
    uint32_t unreadable;
    uint32_t foreign;   /* a shield word that named another body's slot */
    /* a client */
    uint32_t named;
    uint32_t hung;
    uint32_t taken_off;
    uint32_t shown;
    uint32_t hid;
    uint32_t recoloured;
    uint32_t resized;
    uint32_t resized_after_hang;
    uint32_t size_unbound;   /* a size the host has, and command 5 was not proved here */
    uint32_t not_taken;
    uint32_t replica_unreadable;
    uint32_t unbound;
    uint32_t written;
} shield_state_t;

static shield_state_t shield;

static bool refuse(const char *why)
{
    log_warning("the droideka's shield is NOT bound, so a client does not see the shields the "
                "host's scripts hang: %s", why);
    return false;
}

/* Command 5 of the director's table: its arm, the function the arm calls, and the pool that
 * function names, which has to be the one command 4 named. */
static bool sizes_bound(uint32_t table)
{
    uint8_t   code[sizeof SIG_ARM_RADIUS];
    uint32_t  arm  = 0;
    uint32_t  pool = 0;
    uintptr_t size;

    if (!memory_read_u32((uintptr_t)table + (uintptr_t)COMMAND_SIZE * 4u, &arm) ||
        !memory_is_inside_image((uintptr_t)arm, sizeof code) ||
        !memory_try_read((uintptr_t)arm, code, sizeof code) ||
        memcmp(code, SIG_ARM_RADIUS, sizeof code) != 0) {
        return false;
    }
    size = mp_level_state_bind_callee((uintptr_t)arm + ARM_SIZE_RETURN);
    return size != 0u &&
           mp_level_state_bind_proved(size, SIG_RADIUS_TAIL, MSK_RADIUS_TAIL,
                                      sizeof SIG_RADIUS_TAIL) != 0u &&
           memory_read_u32(size + SHOW_POOL_OPERAND, &pool) && pool == shield.pool;
}

/* Once, as soon as the director is hulled: a side whose director is never hulled asks every time
 * and finds nothing, and says so once. */
static bool bound(void)
{
    uintptr_t director = mp_level_state_bind_director_entry();
    uint8_t   dispatch[sizeof SIG_DISPATCH];
    uint8_t   code[sizeof SIG_ARM_SHOW];
    uint32_t  table    = 0;
    uint32_t  arm      = 0;
    uintptr_t show;
    uint32_t  pool     = 0;

    if (shield.attempted || director == 0u) {
        return shield.bound;
    }
    shield.attempted = true;
    /* Past its head the director is the engine's own, and this feature's hull on the head is the
     * only one: the dispatch is compared whole. */
    if (!memory_try_read(director + DIRECTOR_DISPATCH, dispatch, sizeof dispatch) ||
        memcmp(dispatch, SIG_DISPATCH, sizeof dispatch) != 0) {
        return refuse("the director's dispatch is not where the engine shipped it");
    }
    if (!memory_read_u32(director + DIRECTOR_TABLE_OPERAND, &table) ||
        !memory_is_inside_image((uintptr_t)table, DIRECTOR_COMMANDS * 4u) ||
        !memory_read_u32((uintptr_t)table + (uintptr_t)COMMAND_SHOW * 4u, &arm) ||
        !memory_is_inside_image((uintptr_t)arm, sizeof code) ||
        !memory_try_read((uintptr_t)arm, code, sizeof code) ||
        memcmp(code, SIG_ARM_SHOW, sizeof code) != 0) {
        return refuse("command 4 of the director is not the arm that shows a shield");
    }
    show = mp_level_state_bind_callee((uintptr_t)arm + ARM_SHOW_RETURN);
    if (show == 0u ||
        mp_level_state_bind_proved(show, SIG_SHOW_TAIL, MSK_SHOW_TAIL, sizeof SIG_SHOW_TAIL) ==
            0u ||
        !memory_read_u32(show + SHOW_POOL_OPERAND, &pool) ||
        !memory_is_inside_image((uintptr_t)pool, (size_t)POOL_SLOTS * POOL_STRIDE)) {
        return refuse("the function command 4 calls does not name the shield pool");
    }
    shield.pool  = pool;
    shield.bound = true;
    shield.sizes = sizes_bound(table);
    log_info("the droideka's shield is bound: its pool at %08X, read out of the director's command "
             "4 at %08X, %u slot(s) of %u byte(s); command 5, the size, %s", (unsigned)pool,
             (unsigned)arm, (unsigned)POOL_SLOTS, (unsigned)POOL_STRIDE,
             shield.sizes ? "proved on the same pool"
                          : "NOT proved, so a client shows every shield at the size a hang gives");
    return true;
}

/* A body's shield as the director sees it. False when the body did not read. */
static bool shield_here(uint32_t body, mp_enemy_shield_here_t *here)
{
    uint8_t slot[SLOT_RADIUS + 4u];
    float   radius = 0.0f;
    int32_t id = -1;
    int32_t  in_use = 0;
    int32_t  shown = 0;
    uint32_t owner = 0;

    memset(here, 0, sizeof *here);
    here->id = -1;
    if (!memory_try_read((uintptr_t)body + BODY_SHIELD, &id, sizeof id)) {
        return false;
    }
    here->id = id;
    if (id < 0 || (uint32_t)id >= POOL_SLOTS) {
        return true;
    }
    if (!memory_try_read((uintptr_t)shield.pool + (uintptr_t)id * POOL_STRIDE, slot,
                         sizeof slot)) {
        return false;
    }
    memcpy(&in_use, slot + SLOT_IN_USE, sizeof in_use);
    memcpy(&shown, slot + SLOT_SHOWN, sizeof shown);
    memcpy(&owner, slot + SLOT_OWNER, sizeof owner);
    memcpy(&radius, slot + SLOT_RADIUS, sizeof radius);
    /* A word left standing after the pool was cleared, or relinked by a savegame, can name a
     * slot another body holds now; that shield is not this body's to describe or take off. */
    if (in_use != 0 && owner != body) {
        ++shield.foreign;
        in_use = 0;
    }
    here->in_use = in_use != 0;
    here->shown  = shown != 0;
    here->colour = mp_enemy_shield_colour(slot[SLOT_COLOUR], slot[SLOT_COLOUR + 1u],
                                          slot[SLOT_COLOUR + 2u]);
    here->radius = mp_enemy_shield_eighths(radius);
    return true;
}

void mp_enemy_shield_read(uint32_t body, mp_enemy_record_t *record)
{
    mp_enemy_shield_here_t here;
    mp_enemy_shield_t      said;

    if (record == NULL || body == 0u || !bound()) {
        return;
    }
    if (!shield_here(body, &here)) {
        ++shield.unreadable;
        return;   /* the field stays 0, which says nothing */
    }
    memset(&said, 0, sizeof said);
    said.read  = true;
    said.hangs = here.id >= 0 && here.in_use;
    if (said.hangs) {
        said.shown  = here.shown;
        said.colour = here.colour;
        said.radius = here.radius;
        shield.sized += here.radius != HANG_RADIUS_EIGHTHS ? 1u : 0u;
        ++shield.hanging;
        shield.hidden += said.shown ? 0u : 1u;
        if (said.colour != 0u) {
            ++shield.in_colour;
        } else {
            ++shield.other_colour;
        }
    }
    record->value[MP_ENEMY_F_SHIELD] = mp_enemy_shield_pack(&said);
}

static const char *state_said(const mp_enemy_shield_t *want)
{
    if (!want->hangs) {
        return "hangs none";
    }
    return want->shown ? "hangs one, shown" : "hangs one, hidden";
}

void mp_enemy_shield_apply(uintptr_t actor, uint32_t body, uint32_t index,
                           const mp_enemy_record_t *record)
{
    mp_enemy_shield_t      want;
    mp_enemy_shield_here_t here;
    mp_enemy_shield_plan_t plan;
    bool                   called = true;

    if (record == NULL || actor == 0u || body == 0u) {
        return;
    }
    want = mp_enemy_shield_unpack(record->value[MP_ENEMY_F_SHIELD]);
    if (!want.read) {
        return;
    }
    shield.named += want.hangs ? 1u : 0u;
    if (!bound()) {
        ++shield.unbound;
        return;
    }
    if (!shield_here(body, &here)) {
        ++shield.replica_unreadable;
        return;
    }
    plan = mp_enemy_shield_plan(&want, &here);
    if (plan.size && !shield.sizes) {
        /* The size is left at the hang's and counted; the rest still follows. */
        ++shield.size_unbound;
        plan.size = false;
        want.radius = 0u;
    }
    if (!plan.release && !plan.create && !plan.show && !plan.size) {
        return;
    }
    if (plan.release) {
        called = called && mp_level_state_bind_call_director(actor, COMMAND_TAKE, 0, 0);
    }
    if (plan.create) {
        called = called &&
                 mp_level_state_bind_call_director(actor, COMMAND_HANG, (int32_t)plan.colour, 0);
    }
    if (plan.show) {
        called = called &&
                 mp_level_state_bind_call_director(actor, COMMAND_SHOW, plan.shown ? 1 : 0, 0);
    }
    if (plan.size) {
        float    radius = mp_enemy_shield_radius_of(plan.radius);
        uint32_t bits   = 0;

        /* The director takes its second operand as a float. */
        memcpy(&bits, &radius, sizeof bits);
        called = called &&
                 mp_level_state_bind_call_director(actor, COMMAND_SIZE, 0, (int32_t)bits);
    }
    if (called && shield_here(body, &here) && mp_enemy_shield_agrees(&want, &here)) {
        shield.hung += (plan.create && !plan.recolour) ? 1u : 0u;
        shield.recoloured += plan.recolour ? 1u : 0u;
        shield.taken_off += (plan.release && !plan.create) ? 1u : 0u;
        shield.shown += (plan.show && plan.shown) ? 1u : 0u;
        shield.hid += (plan.show && !plan.shown) ? 1u : 0u;
        shield.resized += plan.size ? 1u : 0u;
        shield.resized_after_hang += (plan.size && plan.create) ? 1u : 0u;
        if (shield.written < LINES_WRITTEN) {
            ++shield.written;
            log_info("a droideka's shield changed here: placement %u %s, as the host has it",
                     (unsigned)index, state_said(&want));
        }
        return;
    }
    ++shield.not_taken;
    if (shield.written < LINES_WRITTEN) {
        ++shield.written;
        log_info("a droideka's shield could NOT change here: placement %u, the director was asked "
                 "for a body that %s and the body %s", (unsigned)index, state_said(&want),
                 called ? "did not take it" : "was never handed over, the director is not hulled");
    }
}

void mp_enemy_shield_report(void)
{
    log_info("  a droideka's shield (host): %u record(s) with one hanging, %u of them hidden, %u "
             "in a colour of the director's five, %u in another, %u with a size other than a "
             "hang's, %u unreadable; %u shield word(s) on either side that named another body's "
             "slot and were read as none",
             (unsigned)shield.hanging, (unsigned)shield.hidden, (unsigned)shield.in_colour,
             (unsigned)shield.other_colour, (unsigned)shield.sized, (unsigned)shield.unreadable,
             (unsigned)shield.foreign);
    log_info("  a droideka's shield (client): %u record(s) with one hanging, %u hung here, %u "
             "taken off here, %u shown and %u hidden to match, %u hung again for another colour, "
             "%u sized to match (%u of them after a hang), %u the director did not take, %u "
             "unreadable, %u not bound, %u size(s) left at a hang's for want of command 5",
             (unsigned)shield.named, (unsigned)shield.hung, (unsigned)shield.taken_off,
             (unsigned)shield.shown, (unsigned)shield.hid, (unsigned)shield.recoloured,
             (unsigned)shield.resized, (unsigned)shield.resized_after_hang,
             (unsigned)shield.not_taken, (unsigned)shield.replica_unreadable,
             (unsigned)shield.unbound, (unsigned)shield.size_unbound);
}
