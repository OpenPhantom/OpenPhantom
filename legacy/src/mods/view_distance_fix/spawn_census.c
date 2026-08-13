/* spawn_census.c: count the spawns the engine refuses, because nothing else does.
 *
 * ==============================================================================================
 * THE SILENT FAILURE, walked from the report to the bytes
 *
 * enemy_activationScan decides per placement whether the actor should exist. Once the distance
 * test, the difficulty band and the type test have all passed, it calls the spawn:
 *
 *   0043722E  E8 1D 00 00 00        call 0x00437250          <- the site redirected here
 *   00437233  83 C4 0C              add esp,0xC              ; cdecl, three arguments
 *   00437236  85 C0                 test eax,eax
 *   00437238  74 0D                 jz  <skip this placement>
 *   0043723D  mov [rec+0xC8],1                               ; only now is it marked as created
 *
 * Inside, the spawn takes a record out of two pools that are allocated once and never grow:
 *
 *   00431F4D  push 128 / push 0x204 / call pool_create       ; actors, 128 slots  -> [0x6C4DA4]
 *   004108A0  push 255 / push 0x110 / call pool_create       ; things, 255 slots  -> [0x8A01DC]
 *
 * and it gives up in two places, both without a word:
 *
 *   004372A1  call pool_alloc                                ; actor record
 *   004372B2  call <reap>                                    ; free the ones marked reusable
 *   004372BD  call pool_alloc                                ; one retry
 *   004372CE  33 C0                 xor eax,eax              ; refused
 *   0043741C  <thing == NULL>  ->  free the actor again  ->  xor eax,eax
 *
 * A refusal is therefore invisible from outside: the placement is skipped, `rec+0xC8` stays zero,
 * and the log of a session in which an enemy never appeared is identical to one in which
 * everything worked. This file is the only thing that can tell those two apart.
 *
 * ==============================================================================================
 * WHY IT MATTERS EVEN THOUGH THE ENGINE RETRIES
 *
 * The scan runs every AI tick, so a refused placement is tried again and usually succeeds once a
 * slot frees. That makes the ordinary case "appears late" rather than "never appears". What turns
 * it into "never" is a placement that is only scanned while some other condition holds, a script
 * step or a cutscene handover among them: miss that window and there is no second chance.
 *
 * The pools are sized for the load the levels were authored with. Anything that activates more
 * actors at once eats into that margin, and because the activation test is a sphere, a radius
 * multiplied by k multiplies the activated VOLUME by k cubed. A 1.25 radius is very nearly twice
 * the actors alive at one time, against 128 slots.
 *
 * ==============================================================================================
 * WHAT THIS DOES NOT SAY
 *
 * The two refusal paths are inside the spawn and look identical from the call site, so this
 * cannot tell an exhausted actor pool from an exhausted thing pool. It reports that the engine
 * refused, how often, and the capacities the refusal was measured against. Distinguishing the two
 * would mean hooking pool_alloc, which is shared by everything in the game and is a far larger
 * claim on the process than a census is worth.
 */
#include "spawn_census.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stddef.h>

/* Measured from the activation scan's own entry, and checked before it is used: the opcode must
 * be a call and the three-argument cdecl cleanup must follow it. Verified against every shipped
 * image, where the scan itself moves but this distance does not. */
#define OFFSET_SPAWN_CALL     0x4Au
#define OFFSET_SPAWN_CLEANUP  0x4Fu
static const uint8_t SPAWN_CLEANUP[] = { 0x83, 0xC4, 0x0C };   /* add esp,0xC */

/* The two pools the spawn draws from, quoted in the log so a refusal arrives with the number it
 * should be read against rather than as a bare count. */
#define ACTOR_POOL_SLOTS 128
#define THING_POOL_SLOTS 255

/* The first refusal is worth a full line. After that they arrive in bursts, because a full pool
 * stays full for a while, so the rest are counted and reported every so often. */
#define REFUSAL_REPORT_EVERY 64

typedef int32_t (__cdecl *spawn_fn_t)(void *a, void *b, void *c);

typedef struct spawn_census_state {
    bool       installed;
    spawn_fn_t original;
    uint32_t   refusals;
    uint32_t   next_report;
} spawn_census_state_t;

static spawn_census_state_t census;

static int32_t __cdecl hook_spawn(void *a, void *b, void *c)
{
    int32_t created;

    if (census.original == NULL) {
        /* Not the window detour.c has: here the target is stored BEFORE the call is redirected,
         * so by the time this hook can run it is set. The guard stands because a null call would
         * take the process down, and it must not be mistaken for a refusal, which is why nothing
         * is counted on this path. */
        return 0;
    }

    created = census.original(a, b, c);
    if (created != 0) {
        return created;
    }

    ++census.refusals;
    if (census.refusals == 1) {
        log_warning("the engine REFUSED to create an NPC the activation scan asked for. Its two "
                    "pools hold %d actors and %d things, both fixed at start-up, and a full pool "
                    "makes the spawn return zero with no message of its own. The placement is "
                    "skipped and retried on later ticks, so this shows up as an enemy that "
                    "appears late, or never, if it is only scanned during one script step. If a "
                    "level is missing an enemy, this line is why.",
                    ACTOR_POOL_SLOTS, THING_POOL_SLOTS);
        census.next_report = REFUSAL_REPORT_EVERY;
        return created;
    }
    if (census.refusals >= census.next_report) {
        census.next_report += REFUSAL_REPORT_EVERY;
        log_warning("%u refused NPC spawns so far this session", (unsigned)census.refusals);
    }
    return created;
}

bool spawn_census_install(uintptr_t activation_scan, bool enabled)
{
    uintptr_t call_site;
    uintptr_t target;
    uint8_t   cleanup[sizeof(SPAWN_CLEANUP)];

    if (census.installed) {
        return census.original != NULL;
    }
    census.installed = true;

    if (!enabled) {
        log_info("[diagnostics] Spawns=0, refused NPC spawns are not counted. The engine's own "
                 "spawn says nothing when its pools are full, so an enemy that never appears "
                 "leaves no trace at all; switch it on before asking anybody to reproduce one.");
        return false;
    }
    if (activation_scan == 0) {
        log_warning("the activation scan did not resolve, refused NPC spawns cannot be counted");
        return false;
    }

    call_site = activation_scan + OFFSET_SPAWN_CALL;
    if (!patch_read_call_target(call_site, &target)) {
        log_warning("no usable call at %08X, refused NPC spawns cannot be counted",
                    (unsigned)call_site);
        return false;
    }
    if (!memory_read(activation_scan + OFFSET_SPAWN_CLEANUP, cleanup, sizeof cleanup) ||
        cleanup[0] != SPAWN_CLEANUP[0] || cleanup[1] != SPAWN_CLEANUP[1] ||
        cleanup[2] != SPAWN_CLEANUP[2]) {
        log_warning("the call at %08X is not followed by the three-argument cleanup this census "
                    "was measured against, refused rather than counting the wrong function",
                    (unsigned)call_site);
        return false;
    }

    census.original = (spawn_fn_t)target;
    if (patch_redirect_call(call_site, (const void *)hook_spawn) != PATCH_RESULT_OK) {
        log_warning("the call at %08X could not be redirected, refused NPC spawns are not counted",
                    (unsigned)call_site);
        census.original = NULL;
        return false;
    }

    log_info("refused NPC spawns are counted (call site %08X, engine spawn %08X). Observation "
             "only: the engine's own function runs and its answer is returned unchanged.",
             (unsigned)call_site, (unsigned)target);
    return true;
}
