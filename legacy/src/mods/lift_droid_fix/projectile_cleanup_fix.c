/* projectile_cleanup_fix.c: see projectile_cleanup_fix.h for the full account of what this closes
 * out and why.
 *
 * The list head DAT_00872fb8 is resolved by signature rather than hardcoded, per this project's
 * "signatures, never addresses" rule; a real, shipped fix does not get to take the shortcut
 * diag_projectiles.c documents for itself as a one-off diagnostic. FUN_004524b9 (the list's own
 * per-tick update loop) reads it as a literal `mov eax,[0x00872fb8]` at 0x004524e7, reached by a
 * `jnz` a few instructions earlier that converges two control-flow paths onto this exact point,
 * a stable anchor. The surrounding block (three reads of a second global, 0x00868724, in a short,
 * distinctive lazy-init idiom) is what makes the pattern unique; only the list-head operand itself
 * is wildcarded and read back at runtime.
 */
#include "projectile_cleanup_fix.h"
#include "known_placements.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- FUN_004524b9 0x004524b9, up to and including `mov eax,[DAT_00872fb8]` at 0x004524e7 ------- *
 *   55                      push ebp
 *   8B EC                   mov ebp,esp
 *   81 EC CC 01 00 00       sub esp,0x1CC
 *   A1 24 87 86 00          mov eax,[0x00868724]
 *   83 78 28 00             cmp dword ptr [eax+0x28],0
 *   75 1A                   jnz +0x1A                    -> converges here from two paths
 *   8B 0D 24 87 86 00       mov ecx,[0x00868724]
 *   C7 41 18 A1 4A 45 00    mov dword ptr [ecx+0x18],0x00454AA1
 *   8B 15 24 87 86 00       mov edx,[0x00868724]
 *   C7 42 28 01 00 00 00    mov dword ptr [edx+0x28],1
 *   A1 <wildcard>           mov eax,[list head]          <- the operand this resolves */
static const uint8_t SIG_PROJECTILE_LIST_TICK[] = {
    0x55,
    0x8B, 0xEC,
    0x81, 0xEC, 0xCC, 0x01, 0x00, 0x00,
    0xA1, 0x24, 0x87, 0x86, 0x00,
    0x83, 0x78, 0x28, 0x00,
    0x75, 0x1A,
    0x8B, 0x0D, 0x24, 0x87, 0x86, 0x00,
    0xC7, 0x41, 0x18, 0xA1, 0x4A, 0x45, 0x00,
    0x8B, 0x15, 0x24, 0x87, 0x86, 0x00,
    0xC7, 0x42, 0x28, 0x01, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_PROJECTILE_LIST_TICK[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_PROJECTILE_LIST_TICK) == sizeof(MSK_PROJECTILE_LIST_TICK),
               "the projectile list-tick pattern and its mask are different lengths");
#define PROJECTILE_LIST_TICK_HEAD_OFFSET 0x2Fu   /* the wildcarded operand's own offset */

#define PROJECTILE_NEXT_OFFSET       0x00u
#define PROJECTILE_POSITION_OFFSET   0x04u   /* float[3], world x/y/z */
#define PROJECTILE_AGE_OFFSET        0x14u   /* float, seconds alive, counts UP from creation */
#define PROJECTILE_FLAGS_OFFSET      0x84u   /* the word FUN_004524b9 itself tests/sets throughout */
#define PROJECTILE_REMOVE_FLAG_BIT   0x40000000u   /* set => the engine's own next pass removes it */

#define PROJECTILE_WALK_MAX 8192u

/* Field-tested at 15 and 5. 15 (a quarter second of frames) was too slow outright: the pile-up to
 * 57 entries measured live happened inside about three real seconds, and at the low frame rates
 * that pile-up itself causes, 15 frames stretches to a second and a half of wall clock, most of
 * the whole event passing between checks. 5 was faster but still coarse enough, relative to the
 * 0.1s age threshold below, to be the cause of debris being field-reported as vanishing "faster or
 * slower" rather than consistently: a five-frame gap between checks is a large fraction of a 0.1s
 * budget, so exactly when in an entry's life it gets caught could jitter by nearly as much as the
 * threshold itself. Every frame removes that jitter rather than trying to out-guess it, and the
 * walk itself is cheap (a few field reads per entry, no collision trace). */
#define CLEANUP_EVERY_FRAMES 1u

/* Field-tested at four values. 4.0s safe but too generous to touch the peak at all. 1.5s cut the
 * peak (57 -> 44 entries) and the recovery time (never -> ~8s) but barely moved the worst fps
 * (~10 -> ~12). 0.5s field-tested worse than 0.1s. 0.1s gave the best measured result but was
 * field-reported as visibly inconsistent with 5-frame polling; see CLEANUP_EVERY_FRAMES above for
 * the fix tried alongside this same value. Confirmed together: worst fps in a long session 41.9
 * (was 3-12 before either fix), peak list size 9 (was 44-57), draining to 0 between fights. */
#define STALE_AGE_SECONDS 0.1f

/* Same five placements activation_race_fix.c matches, wider on purpose: this is not matching one
 * placement's exact spawn point, it is covering the platform area debris from a fight there can
 * actually land on. */
#define NEAR_PLACEMENT_RADIUS 12.0f

typedef struct cleanup_state {
    bool      armed;
    uintptr_t list_head_address;
    uint32_t  frame_count;
    uint32_t  total_forced;
} cleanup_state_t;

static cleanup_state_t cleanup_state;

static bool resolve_projectile_list_head(uintptr_t *out_address)
{
    signature_t site = SIGNATURE_ENTRY_MASKED("projectile_list_tick", SIG_PROJECTILE_LIST_TICK,
                                              MSK_PROJECTILE_LIST_TICK);
    uint32_t     address = 0;

    *out_address = 0;
    signature_resolve_table(&site, 1);
    if (site.address == 0) {
        return false;
    }
    if (!memory_read_u32(site.address + PROJECTILE_LIST_TICK_HEAD_OFFSET, &address) ||
        address == 0) {
        log_warning("the projectile list head operand at %08X could not be read",
                    (unsigned)(site.address + PROJECTILE_LIST_TICK_HEAD_OFFSET));
        return false;
    }
    if (!memory_is_inside_image((uintptr_t)address, sizeof(uint32_t))) {
        log_warning("the projectile list head operand reads back as %08X, outside the host image, "
                    "refused",
                    (unsigned)address);
        return false;
    }
    *out_address = (uintptr_t)address;
    return true;
}

static void cleanup_tick(void)
{
    uint32_t entry;
    uint32_t walked;

    if (!cleanup_state.armed) {
        return;
    }
    ++cleanup_state.frame_count;
    if (cleanup_state.frame_count < CLEANUP_EVERY_FRAMES) {
        return;
    }
    cleanup_state.frame_count = 0;

    if (!memory_read(cleanup_state.list_head_address, &entry, sizeof(entry))) {
        return;
    }

    walked = 0;
    while (entry != 0 && walked < PROJECTILE_WALK_MAX) {
        uint32_t next = 0;
        float    position[3];
        float    age = 0.0f;
        uint32_t flags = 0;

        if (!memory_read((uintptr_t)entry + PROJECTILE_NEXT_OFFSET, &next, sizeof(next))) {
            break;
        }

        if (memory_read((uintptr_t)entry + PROJECTILE_POSITION_OFFSET, position,
                        sizeof(position)) &&
            memory_read((uintptr_t)entry + PROJECTILE_AGE_OFFSET, &age, sizeof(age)) &&
            age > STALE_AGE_SECONDS && known_placement_is_near(position, NEAR_PLACEMENT_RADIUS) &&
            memory_read((uintptr_t)entry + PROJECTILE_FLAGS_OFFSET, &flags, sizeof(flags)) &&
            (flags & PROJECTILE_REMOVE_FLAG_BIT) == 0) {
            *(uint32_t *)((uintptr_t)entry + PROJECTILE_FLAGS_OFFSET) =
                flags | PROJECTILE_REMOVE_FLAG_BIT;
            ++cleanup_state.total_forced;
            log_info("projectile cleanup fix: forced removal of a %.1fs-old entry at (%.1f, "
                     "%.1f, %.1f) near a known placement (%u total this session)", (double)age,
                     (double)position[0], (double)position[1], (double)position[2],
                     (unsigned)cleanup_state.total_forced);
        }

        entry = next;
        ++walked;
    }
}

void projectile_cleanup_fix_install(bool enabled)
{
    if (!enabled || cleanup_state.armed) {
        return;
    }

    if (!resolve_projectile_list_head(&cleanup_state.list_head_address)) {
        log_warning("the projectile list head did not resolve, projectile cleanup fix is not "
                    "installed");
        return;
    }

    if (!frame_hook_add(cleanup_tick)) {
        log_warning("projectile cleanup fix: the per-frame hook could not be installed, so it "
                    "will never run");
        return;
    }
    cleanup_state.armed = true;

    log_info("projectile cleanup fix armed at %08X: entries older than %.1fs within %.1f units of "
             "one of five known placements are force-removed on the engine's own next pass",
             (unsigned)cleanup_state.list_head_address, (double)STALE_AGE_SECONDS,
             (double)NEAR_PLACEMENT_RADIUS);
}
