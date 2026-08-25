/* activation_race_fix.c: see activation_race_fix.h and droid_fix.h for the field investigation
 * this closes out.
 *
 * Independent, chained detour on FUN_00437850 (0x00437850, the actor-teardown function). See
 * common/detour.h's own chaining contract. view_distance_fix.dll's spawn_census.c also detours this
 * same function, for an unrelated "destroying X, reason N" observation log; whichever DLL happens
 * to load second becomes the outer wrapper and calls the other's hook as its own "original", which
 * is exactly what common/detour.c is designed to do and needs no coordination between the two.
 *
 * FOUR EARLIER DESIGNS, kept as history:
 *
 * 1. Re-check "is the player still within the placement's own activation radius" from inside this
 *    hook, right before forwarding a reason-0 destroy. Field-tested at zero suppressions: this
 *    hook's own read of the player's position happens a handful of instructions after the engine's
 *    own read that just produced the deactivation verdict, with nothing moving in between, so
 *    re-running the identical test on the identical position can never disagree with an answer
 *    already computed.
 *
 * 2. A time-based grace period, applied to every placement: record every successful creation with
 *    the tick it happened on, suppress a reason-0 destroy within a few ticks of that. Worked
 *    exactly as designed and was field-tested as making no difference to the frame rate, but
 *    measured against two placements a later, more careful capture proved were never actually part
 *    of either stalling encounter at all. That verdict was measured on the wrong target.
 *
 * 3. Full suppression (never letting the five spawn at all) was tried next, one placement and then
 *    all five, and measured to fully account for both field-reported stalls: zero of the five
 *    present, zero frame drop, at either lift. That is a bigger change than the bug calls for,
 *    though; it removes the encounter rather than fixing it.
 *
 * 4. A general fix: a deeper investigation found that the actor structure carries a persistent,
 *    per-tick-refreshed field (actor+0x108) that appeared, from decompiled evidence, to hold a
 *    pointer to whichever mover the actor is currently riding, refreshed by FUN_00435c67/
 *    FUN_0040be00 ahead of the deactivation check every tick. Refusing the reason-0 destroy
 *    whenever that field was non-null, instead of matching the actor's placement against five
 *    known positions, would have covered any actor on any mover, anywhere in the game, not just
 *    the two known lifts. It shipped, built and reviewed clean, but FIELD-TESTED AT ZERO
 *    SUPPRESSIONS: a played session that reached the known encounters logged 2,014 reason-0
 *    destroys for the five known placements (view_distance_fix.dll's own observer confirms this)
 *    and this fix refused none of them. Whatever actor+0x108 actually is, or whatever ordering
 *    assumption was wrong, it did not hold for these actors in practice. Reverted in favour of the
 *    fix below, which IS field-confirmed; the mover-field question is left for a future,
 *    dedicated investigation rather than blocking a working build on it.
 *
 * THE FIX ACTUALLY HERE: matches the actor's own placement (actor+0x10, the pointer FUN_00437250
 * stores there at creation) against the five known positions, and refuses the reason-0 destroy
 * only for those five, unconditionally. This is design 4's own immediate predecessor; it is the
 * version that was field-tested and confirmed working in-game, both on its own and again after
 * this DLL's extraction from view_distance_fix.dll. */
#include "activation_race_fix.h"
#include "known_placements.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdint.h>

/* The placement's own name (a reused archetype label, logged for context only) and world position,
 * the same offsets view_distance_fix's spawn_census.c reads for the same reasons. See that file's
 * own header for how 0xB8 and 0xAC were derived and cross-checked. */
#define PLACEMENT_NAME_OFFSET     0xB8u
#define PLACEMENT_NAME_MAX        32u
#define PLACEMENT_POSITION_OFFSET 0xACu
#define KNOWN_RACE_TOLERANCE      0.5f   /* exact-placement identity; projectile_cleanup_fix.c used
                                           * to match this same table at a wider, platform-area
                                           * tolerance, before its own fix dropped the position gate
                                           * entirely in favour of covering the whole ballistic list */

/* FUN_00437850(actor, reason): tears an actor down and, for every reason except 3, writes back to
 * its own placement (actor+0x10, the pointer FUN_00437250 stores there at creation). Reason 1 or
 * 14 sets placement+0xC8 to 2 (blocks re-creation for good), anything else sets it to 0 unless the
 * actor's own state field (actor+0x20) reads 14, in which case that too is immediately overwritten
 * back to 2. Zero is also the activation scan's own "not yet created" gate, so a reason-0 destroy
 * is recreated on the very next scan tick, the mechanism this fix is for. Byte-identical to
 * view_distance_fix's own spawn_census.c copy of this pattern: two independent DLLs detouring the
 * same function each carry their own copy of the site they resolve, per this project's "feature
 * DLLs never depend on each other" rule. */
static const uint8_t SIG_ACTOR_DESTROY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x14,
    0x81, 0xE1, 0x00, 0x20, 0x00, 0x00, 0x85, 0xC9
};
#define ACTOR_DESTROY_PROLOGUE 6u

typedef void (__cdecl *destroy_fn_t)(void *actor, int32_t reason);

typedef struct activation_race_fix_state {
    bool     installed;
    detour_t detour;
    uint32_t refused;
} activation_race_fix_state_t;

static activation_race_fix_state_t fix_state;

static void __cdecl hook_actor_destroy(void *actor, int32_t reason)
{
    destroy_fn_t original = (destroy_fn_t)fix_state.detour.original;
    void        *placement;

    if (reason == 0 &&
        memory_read((uintptr_t)actor + 0x10u, &placement, sizeof(placement)) &&
        placement != NULL) {
        const float *position = (const float *)((const char *)placement +
                                                 PLACEMENT_POSITION_OFFSET);

        if (known_placement_is_near(position, KNOWN_RACE_TOLERANCE)) {
            ++fix_state.refused;
            log_info("activation race fix: refused a reason-0 destroy of \"%.*s\" at (%.1f, %.1f, "
                     "%.1f) (%u refused this session)",
                     (int)PLACEMENT_NAME_MAX, (const char *)placement + PLACEMENT_NAME_OFFSET,
                     (double)position[0], (double)position[1], (double)position[2],
                     (unsigned)fix_state.refused);
            return;
        }
    }

    original(actor, reason);
}

void activation_race_fix_install(bool enabled)
{
    signature_t site = SIGNATURE_ENTRY_DETOUR("activation_race_actor_destroy", SIG_ACTOR_DESTROY,
                                              ACTOR_DESTROY_PROLOGUE);

    if (fix_state.installed) {
        return;
    }
    fix_state.installed = true;

    if (!enabled) {
        log_info("Enabled=0, the five known lift droids keep the activation/deactivation race");
        return;
    }

    signature_resolve_table(&site, 1);
    if (site.address == 0) {
        log_warning("the actor-destroy function did not resolve, the activation-race fix is not "
                    "installed");
        return;
    }
    if (!detour_install(&fix_state.detour, site.address, (const void *)hook_actor_destroy,
                        ACTOR_DESTROY_PROLOGUE)) {
        log_warning("the actor-destroy detour at %08X failed, the activation-race fix is not "
                    "installed",
                    (unsigned)site.address);
        return;
    }

    log_info("activation-race fix armed at %08X for five known placements: all five still spawn "
             "and fight normally, only the specific reason-0 destroy the field investigation found "
             "at them is refused",
             (unsigned)site.address);
}
