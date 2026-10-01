/* diag_push_block.c: why a push block does or does not move for this machine's player.
 *
 * Five observers, each calling the original and handing back its answer unchanged:
 *
 *   the USE test for a block face        whether the probe ahead of the player found one
 *   the mode entry                       whether the player held a direction as it began
 *   the mode's own tick                  whether a direction was still held, and whether the tick
 *                                        reached the block's push at all (the player's own wall
 *                                        probe comes first and can stop it)
 *   the block's push                     what it answered, and which of its own gates it met
 *   the body test inside the push        whether an actor stood in the way
 *
 * The push's gates are read before the call from the same fields the push tests first: the index
 * against the level's mover count, the falling flag, the radius and the step along the floor. A
 * refusal with none of those, no body and no fall is the push's wall probe, a block riding it or
 * the floor under the target; the head of the push cannot tell those three apart.
 *
 * Only a push reached from the player's own tick is counted, so a host pushing its clients' blocks
 * does not mix into the single player question.
 */
#include "diag_push_block.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- Plr_UseTestPushBlock 0x0044CBA7 --------------------------------------------------------- *
 * push ebp; mov ebp,esp; sub esp,0x10; mov eax,[pr]; mov ecx,[eax+0x2A0] (the heading); push ecx;
 * push 0.3f (how far ahead); mov edx,[pr]; add edx,0x118 (the position); push edx;
 * lea eax,[ebp-0xC]; push eax; call Plr_PointAhead. The player pointer is read out of +7, behind
 * the prologue. */
static const uint8_t SIG_DIAG_PUSH_USE_TEST[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x88, 0xA0, 0x02,
    0x00, 0x00, 0x51, 0x68, 0x9A, 0x99, 0x99, 0x3E, 0x8B, 0x15, 0x20, 0x52, 0x4B, 0x00, 0x81,
    0xC2, 0x18, 0x01, 0x00, 0x00, 0x52, 0x8D, 0x45, 0xF4, 0x50, 0xE8
};
static const uint8_t MSK_DIAG_PUSH_USE_TEST[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PUSH_USE_TEST_PROLOGUE 6u
#define PUSH_USE_TEST_PLAYER   0x07u

/* --- Plr_EnterPushBlock 0x0044E990 ----------------------------------------------------------- *
 * push ebp; mov ebp,esp; mov eax,[pr]; mov ecx,[eax+0x154] (the move bits); and ecx,1; ... push 4;
 * push 0x2F (the push clip). Eight bytes to the first boundary past five, and the player pointer
 * inside them is masked. */
static const uint8_t SIG_DIAG_PUSH_ENTER[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x88, 0x54, 0x01, 0x00, 0x00, 0x83,
    0xE1, 0x01, 0x85, 0xC9, 0x74, 0x18, 0x6A, 0x04, 0x6A, 0x2F
};
static const uint8_t MSK_DIAG_PUSH_ENTER[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PUSH_ENTER_PROLOGUE 8u

/* --- Plr_UpdatePushBlock 0x0044EA0F ----------------------------------------------------------- *
 * push ebp; mov ebp,esp; sub esp,0x18; then the sine and cosine of the heading. */
static const uint8_t SIG_DIAG_PUSH_UPDATE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x8D, 0x45, 0xF8, 0x50, 0x8D, 0x4D, 0xFC, 0x51, 0x8B,
    0x15, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x82, 0xA0, 0x02, 0x00, 0x00, 0x50, 0xE8
};
static const uint8_t MSK_DIAG_PUSH_UPDATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PUSH_UPDATE_PROLOGUE 6u

/* --- bapmap_pushBlock 0x0040B2FC -------------------------------------------------------------- *
 * push ebp; mov ebp,esp; sub esp,0xC0 (nine bytes, one instruction boundary); mov eax,[g_level];
 * mov ecx,[ebp+0xC]; cmp ecx,[eax+0x620]; jle; xor eax,eax. The level pointer is read out of +10,
 * behind the prologue. */
static const uint8_t SIG_DIAG_PUSH_BLOCK[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0xA1, 0x60, 0x00, 0x8A, 0x00, 0x8B,
    0x4D, 0x0C, 0x3B, 0x88, 0x20, 0x06, 0x00, 0x00, 0x7E, 0x07, 0x33, 0xC0
};
static const uint8_t MSK_DIAG_PUSH_BLOCK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PUSH_BLOCK_PROLOGUE 9u
#define PUSH_BLOCK_LEVEL    0x0Au

/* --- bapobj_isMoveBlockedByActor 0x00413CDD --------------------------------------------------- *
 * push ebp; mov ebp,esp; sub esp,0x28; then the body it skips copied twice into the frame. */
static const uint8_t SIG_DIAG_PUSH_BODY_TEST[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x8B, 0x45, 0x18, 0x89, 0x45, 0xE4, 0x8B, 0x4D, 0xE4,
    0x89, 0x4D, 0xEC, 0xC7, 0x45, 0xE4, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x55, 0x14
};
#define PUSH_BODY_TEST_PROLOGUE 6u

enum {
    SITE_USE_TEST,
    SITE_ENTER,
    SITE_UPDATE,
    SITE_PUSH,
    SITE_BODY_TEST,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_use_test_push_block", SIG_DIAG_PUSH_USE_TEST,
                                  MSK_DIAG_PUSH_USE_TEST, PUSH_USE_TEST_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_enter_push_block", SIG_DIAG_PUSH_ENTER,
                                  MSK_DIAG_PUSH_ENTER, PUSH_ENTER_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("plr_update_push_block", SIG_DIAG_PUSH_UPDATE,
                                  MSK_DIAG_PUSH_UPDATE, PUSH_UPDATE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapmap_push_block", SIG_DIAG_PUSH_BLOCK, MSK_DIAG_PUSH_BLOCK,
                                  PUSH_BLOCK_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_move_blocked", SIG_DIAG_PUSH_BODY_TEST,
                           PUSH_BODY_TEST_PROLOGUE)
};

/* The player record: the move bits at +0x154, bit 1 forward and bit 2 back. The level: the mover
 * count at +0x620 and the table at +0x624. A block: its flags byte at +0x50, bit 2 falling, and
 * the radius of its first part at +0xE4. */
#define PLAYER_MOVE_INPUT 0x154u
#define MOVE_BITS         0x3u
#define LEVEL_MOVERS      0x620u
#define LEVEL_TABLE       0x624u
#define BLOCK_FLAGS       0x50u
#define BLOCK_FALLING     0x02u
#define BLOCK_RADIUS      0xE4u

#define REPORT_MS 1000u

typedef int32_t(__cdecl *use_test_fn_t)(void);
typedef void(__cdecl *enter_fn_t)(void);
typedef void(__cdecl *update_fn_t)(void);
typedef int32_t(__cdecl *push_fn_t)(void *actor, int32_t index, const float *pusher,
                                    float *delta);
typedef int32_t(__cdecl *body_test_fn_t)(float height, float radius, const float *from,
                                         const float *to, void *self);

typedef enum refusal {
    REFUSAL_NONE = 0,
    REFUSAL_NO_BLOCK,
    REFUSAL_FALLING,
    REFUSAL_RADIUS,
    REFUSAL_NO_STEP,
    REFUSAL_BODY,
    REFUSAL_WALL,
    REFUSAL_DROPPING,
    REFUSAL_COUNT
} refusal_t;

typedef struct push_counts {
    uint32_t asked;
    uint32_t found;
    uint32_t entries;
    uint32_t entries_moving;
    uint32_t entries_still;
    uint32_t updates;
    uint32_t no_move_bit;
    uint32_t refused[REFUSAL_COUNT];
    uint32_t moved;
    uint32_t own_wall;
} push_counts_t;

typedef struct push_state {
    bool          installed;
    uintptr_t     player_cell;
    uintptr_t     level_cell;
    detour_t      use_test;
    detour_t      enter;
    detour_t      update;
    detour_t      push;
    detour_t      body_test;
    bool          in_update;
    bool          push_reached;
    bool          in_push;
    bool          body_in_way;
    push_counts_t counts;
    push_counts_t said;
    DWORD         said_at;
} push_state_t;

static push_state_t push;

static bool read_u32(uintptr_t address, uint32_t *out)
{
    return address != 0u && memory_try_read(address, out, sizeof *out);
}

static uint32_t move_bits(void)
{
    uint32_t player = 0u;
    uint32_t bits = 0u;

    if (!read_u32(push.player_cell, &player) || !read_u32(player + PLAYER_MOVE_INPUT, &bits)) {
        return 0u;
    }
    return bits & MOVE_BITS;
}

/* engine: i32 Plr_UseTestPushBlock(void) */
static int32_t __cdecl hook_use_test(void)
{
    int32_t found = ((use_test_fn_t)push.use_test.original)();

    ++push.counts.asked;
    push.counts.found += found != 0 ? 1u : 0u;
    return found;
}

/* engine: void Plr_EnterPushBlock(void) */
static void __cdecl hook_enter(void)
{
    ++push.counts.entries;
    if (move_bits() != 0u) {
        ++push.counts.entries_moving;
    } else {
        ++push.counts.entries_still;
    }
    ((enter_fn_t)push.enter.original)();
}

/* engine: void Plr_UpdatePushBlock(void) */
static void __cdecl hook_update(void)
{
    bool moving = move_bits() != 0u;

    ++push.counts.updates;
    push.counts.no_move_bit += moving ? 0u : 1u;
    push.in_update    = true;
    push.push_reached = false;
    ((update_fn_t)push.update.original)();
    push.in_update = false;
    push.counts.own_wall += moving && !push.push_reached ? 1u : 0u;
}

/* The gates the push meets before its body test, from the same fields, in the same order. */
static refusal_t first_gate(int32_t index, const float *delta, uint32_t *block)
{
    uint32_t level = 0u;
    int32_t  movers = 0;
    uint8_t  flags = 0u;
    float    radius = 0.0f;

    *block = 0u;
    if (!read_u32(push.level_cell, &level) || level == 0u ||
        !memory_try_read(level + LEVEL_MOVERS, &movers, sizeof movers) || index < 0 ||
        index >= movers || !read_u32(level + LEVEL_TABLE + (uint32_t)index * 4u, block) ||
        *block == 0u) {
        return REFUSAL_NO_BLOCK;
    }
    if (!memory_try_read(*block + BLOCK_FLAGS, &flags, sizeof flags) ||
        (flags & BLOCK_FALLING) != 0u) {
        return REFUSAL_FALLING;
    }
    if (!memory_try_read(*block + BLOCK_RADIUS, &radius, sizeof radius) || !(radius > 0.0f)) {
        return REFUSAL_RADIUS;
    }
    if (delta == NULL || (delta[0] == 0.0f && delta[1] == 0.0f)) {
        return REFUSAL_NO_STEP;
    }
    return REFUSAL_NONE;
}

/* engine: i32 bapmap_pushBlock(void *hActor, i32 moverIndex, const vec3 *pusherPos, vec3 *delta) */
static int32_t __cdecl hook_push(void *actor, int32_t index, const float *pusher, float *delta)
{
    bool      counted = push.in_update;
    uint32_t  block = 0u;
    refusal_t gate = counted ? first_gate(index, delta, &block) : REFUSAL_NONE;
    uint8_t   flags = 0u;
    int32_t   answer;

    push.push_reached = push.push_reached || counted;
    push.in_push      = counted;
    push.body_in_way  = false;
    answer = ((push_fn_t)push.push.original)(actor, index, pusher, delta);
    push.in_push = false;
    if (!counted) {
        return answer;
    }
    if (answer != 0) {
        ++push.counts.moved;
    } else if (gate != REFUSAL_NONE) {
        ++push.counts.refused[gate];
    } else if (push.body_in_way) {
        ++push.counts.refused[REFUSAL_BODY];
    } else if (memory_try_read(block + BLOCK_FLAGS, &flags, sizeof flags) &&
               (flags & BLOCK_FALLING) != 0u) {
        ++push.counts.refused[REFUSAL_DROPPING];
    } else {
        ++push.counts.refused[REFUSAL_WALL];
    }
    return answer;
}

/* engine: i32 bapobj_isMoveBlockedByActor(f32 h, f32 r, vec3 *from, vec3 *to, bapObj *me) */
static int32_t __cdecl hook_body_test(float height, float radius, const float *from,
                                      const float *to, void *self)
{
    int32_t blocked = ((body_test_fn_t)push.body_test.original)(height, radius, from, to, self);

    if (push.in_push && blocked != 0) {
        push.body_in_way = true;
    }
    return blocked;
}

static uint32_t refused_total(const push_counts_t *c)
{
    uint32_t total = 0u;
    size_t   i;

    for (i = 1; i < REFUSAL_COUNT; ++i) {
        total += c->refused[i];
    }
    return total;
}

/* Once a second at most, and only when something changed. */
static void report(void)
{
    const push_counts_t *c = &push.counts;
    DWORD                now = GetTickCount();

    if (memcmp(&push.counts, &push.said, sizeof push.counts) == 0 ||
        (DWORD)(now - push.said_at) < REPORT_MS) {
        return;
    }
    push.said    = push.counts;
    push.said_at = now;
    diag_log_write("the push blocks: %u use test(s) found a block face (of %u asked), %u mode "
                   "entries (%u with a move bit, %u without), %u update substep(s): %u left for "
                   "want of a move bit, %u refused by the block (no block: %u, flags 2: %u, "
                   "radius: %u, no step: %u, a body in the way: %u, its wall, a rider on it or "
                   "the floor under it: %u, dropping: %u), %u moved; %u left by the player's own "
                   "wall probe",
                   (unsigned)c->found, (unsigned)c->asked, (unsigned)c->entries,
                   (unsigned)c->entries_moving, (unsigned)c->entries_still, (unsigned)c->updates,
                   (unsigned)c->no_move_bit, (unsigned)refused_total(c),
                   (unsigned)c->refused[REFUSAL_NO_BLOCK], (unsigned)c->refused[REFUSAL_FALLING],
                   (unsigned)c->refused[REFUSAL_RADIUS], (unsigned)c->refused[REFUSAL_NO_STEP],
                   (unsigned)c->refused[REFUSAL_BODY], (unsigned)c->refused[REFUSAL_WALL],
                   (unsigned)c->refused[REFUSAL_DROPPING], (unsigned)c->moved,
                   (unsigned)c->own_wall);
}

int diag_push_block_install(int push_block_level)
{
    int   installed = 0;
    void *player_cell;
    void *level_cell;

    if (push_block_level <= 0 || push.installed) {
        return 0;
    }
    push.installed = true;
    signature_resolve_table(sites, SITE_COUNT);

    /* Both cells read before any hook of this file stands, and each out of an operand behind its
     * own site's prologue, where no other module's head detour writes. */
    player_cell = diag_derive_address(sites, SITE_USE_TEST, PUSH_USE_TEST_PLAYER, "the player");
    level_cell  = diag_derive_address(sites, SITE_PUSH, PUSH_BLOCK_LEVEL, "the level");
    if (player_cell == NULL || level_cell == NULL) {
        log_warning("the push block observer is off: the player or the level cell did not read");
        return 0;
    }
    push.player_cell = (uintptr_t)player_cell;
    push.level_cell  = (uintptr_t)level_cell;

    installed += diag_install_observer(sites, SITE_BODY_TEST, &push.body_test,
                                       (const void *)hook_body_test, PUSH_BODY_TEST_PROLOGUE,
                                       "an actor in the way of a pushed block") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_PUSH, &push.push, (const void *)hook_push,
                                       PUSH_BLOCK_PROLOGUE,
                                       "a block's push and which gate refused it") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_UPDATE, &push.update,
                                       (const void *)hook_update, PUSH_UPDATE_PROLOGUE,
                                       "the push mode's tick and its move bits") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_ENTER, &push.enter, (const void *)hook_enter,
                                       PUSH_ENTER_PROLOGUE,
                                       "the push mode's entry and its move bits") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_USE_TEST, &push.use_test,
                                       (const void *)hook_use_test, PUSH_USE_TEST_PROLOGUE,
                                       "the USE key's test for a block face") ? 1 : 0;
    if (installed != 0 && !frame_hook_add(&report)) {
        log_warning("the push block observer counts, but the frame hook is unavailable, so its "
                    "line is never written");
    }
    return installed;
}
