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

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>

/* The placement's own name, a null-terminated string on the record this call's own first argument
 * points to. FUN_00437250's decompile shows the byte scan seeding the new actor's name starting at
 * `param_1 + 0x2e`, but param_1 decompiles as `int *`, so that expression is already scaled by 4 -
 * the real byte offset is 0x2e * 4 = 0xB8. (First attempt at this offset, left unscaled, printed
 * floating-point bit patterns instead of names - the tell that gave the x4 away.) Temporary: logs
 * every SUCCESSFUL spawn by name, not just refusals, so a specific encounter's placements can be
 * identified live rather than guessed at. */
#define PLACEMENT_NAME_OFFSET 0xB8u
#define PLACEMENT_NAME_MAX    32u

/* The placement's own world position, a float[3] at +0xAC - the same field view_distance_fix.c's
 * own SIG_ACTIVATION_SCAN comment names as the third argument to within_range ("push rec+0xAC"),
 * pushed by address rather than by value, so it is a pointer to the triple rather than one float.
 * Logged because the name alone turned out to be a reused archetype label, not a per-placement ID -
 * "enemy092" fired about twenty times in one run, including once at the very start of the level far
 * from the lift, so position is what actually tells the lift's three droids apart from everything
 * else this hook also sees. */
#define PLACEMENT_POSITION_OFFSET 0xACu

/* TEMPORARY: two guesses at which placement is one of the field report's three lift droids, by
 * loose position matching against a list of everything the activation scan happened to create,
 * both wrong (enemy091 and enemy092 turned out not to be visible in that lift at all). Guessing
 * off a list is out; this logs the PLAYER's own position periodically instead, so the next capture
 * says directly where the lift actually is and what is actually near it, rather than inferring it.
 *
 * --- Plr_RunPhases 0x00448297, used only to derive the player pointer slot; never detoured. Byte-
 * identical to diag_flow.c's, video_overlay.c's and sfx_mute.c's own reasoning for this site.
 *   +0x27 : &pPlayer [0x4B5220] */
static const uint8_t SIG_PLAYER_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
#define OFFSET_PLAYER_POINTER     0x27u
#define PLAYER_ACTOR_OFFSET       0x0Cu   /* the same +0xC FUN_00447d18 itself reads */
#define PLAYER_CURRENT_POS_OFFSET 0x18u

#define PLAYER_POSITION_LOG_EVERY_FRAMES 10u   /* rough; small on purpose - a 90-frame throttle
                                                 * turned into one sample per ~13 real seconds
                                                 * during a 7 fps stall, which is exactly the
                                                 * window this needs to resolve precisely */

typedef struct player_position_log_state {
    bool      armed;
    uint32_t *player_pointer_slot;
    uint32_t *camera_view_pointer_slot;   /* NULL if it did not resolve: yaw/pitch just are not
                                            * logged, everything else still works */
    uint32_t  frame_count;
} player_position_log_state_t;

static player_position_log_state_t player_position_log;

static uint32_t *resolve_player_pointer_slot(void)
{
    uintptr_t   site;
    uint32_t    address = 0;
    signature_t sig = SIGNATURE_ENTRY("spawn_census_player_run_phases", SIG_PLAYER_RUN_PHASES);

    signature_resolve_table(&sig, 1);
    site = sig.address;
    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + OFFSET_PLAYER_POINTER, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        return NULL;
    }
    return (uint32_t *)(uintptr_t)address;
}

/* --- the camera object pointer, reused from enhanced_input/camera_sites.c's own SIG_CAMERA_VIEW --
 * That file resolves seven cross-checked patterns for its own free-look feature; this needs only
 * one of them, the camera object pointer, and only two READ-ONLY fields on it - the yaw offset
 * cross-check camera_sites.c performs against its OWN recentre-write site is skipped here since
 * this never writes anything. Byte-identical pattern, reasoning quoted from that file:
 *
 *   A1 <gView>                mov eax,[theCameraObject]
 *   D9 40 38                  fld  [eax+0x38]        the PREVIOUS camera yaw
 *   D9 5D E0 / D9 45 D8       stash it, load the interpolated heading
 *   D8 05 <offset>            fadd [offset]          <- the offset cell AGAIN, a third proof
 *
 * +0x34 euler.x = camera PITCH in degrees, +0x38 euler.y = camera YAW in degrees. Logged
 * alongside the player's own position and the nearby placement dump so which placement the player
 * was actually looking at can be worked out afterward from the raw numbers, rather than this DLL
 * guessing at an unverified axis/sign convention and silently pointing at the wrong thing. */
static const uint8_t SIG_CAMERA_VIEW[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x40, 0x38,
    0xD9, 0x5D, 0xE0,
    0xD9, 0x45, 0xD8,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CAMERA_VIEW[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_VIEW_OBJECT         0x01u
#define BAPVIEW_EULER_PITCH_OFFSET 0x34u
#define BAPVIEW_EULER_YAW_OFFSET   0x38u

static uint32_t *resolve_camera_view_pointer_slot(void)
{
    uintptr_t   site;
    uint32_t    address = 0;
    signature_t sig = SIGNATURE_ENTRY_MASKED("spawn_census_camera_view", SIG_CAMERA_VIEW,
                                             MSK_CAMERA_VIEW);

    signature_resolve_table(&sig, 1);
    site = sig.address;
    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + OFFSET_VIEW_OBJECT, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        return NULL;
    }
    return (uint32_t *)(uintptr_t)address;
}

/* TEMPORARY: dumps every LEVEL PLACEMENT within range of the player, not just ones that happened
 * to fire a "created" log during this capture. Matching by "recently created nearby" turned out to
 * miss the actual droids twice in a row - they can be actors that were already active before this
 * session's own capture window started, which never emit a fresh "created" line at all. This walks
 * the SAME placement table FUN_00437161 (the activation scan) itself iterates: world = *(0x8A0060),
 * count = *(world+0x204), array = *(world+0x20C), each entry the same placement record everything
 * else in this file already reads by the same offsets. 0x008A0060 is used directly rather than
 * resolved by signature - it is a fixed global read as a literal absolute operand at several
 * already-confirmed sites this session (FUN_0040be00, FUN_0040c2be among them), not a relocatable
 * target, and this is a one-off diagnostic rather than something meant to ship. */
#define WORLD_POINTER_ADDRESS          0x008A0060u
#define WORLD_PLACEMENT_COUNT_OFFSET   0x204u
#define WORLD_PLACEMENT_ARRAY_OFFSET   0x20Cu
#define PLACEMENT_ACTIVE_FLAG_BIT      0x1u
#define PLACEMENT_CREATED_FLAG_OFFSET  0xC8u
#define NEARBY_PLACEMENT_DUMP_RADIUS   15.0f
#define NEARBY_PLACEMENT_DUMP_EVERY    5u    /* ticks of THIS function, itself already throttled
                                               * to once per PLAYER_POSITION_LOG_EVERY_FRAMES */

static void dump_nearby_placements(const float *player_position)
{
    uint32_t world;
    int32_t  count;
    uint32_t array;
    int32_t  index;

    if (!memory_read(WORLD_POINTER_ADDRESS, &world, sizeof(world)) || world == 0) {
        return;
    }
    if (!memory_read((uintptr_t)world + WORLD_PLACEMENT_COUNT_OFFSET, &count, sizeof(count)) ||
        count <= 0 || count > 4096) {
        return;
    }
    if (!memory_read((uintptr_t)world + WORLD_PLACEMENT_ARRAY_OFFSET, &array, sizeof(array)) ||
        array == 0) {
        return;
    }

    for (index = 0; index < count; ++index) {
        uint32_t placement;
        uint32_t flags;
        float    position[3];
        float    dx, dy, dz;

        if (!memory_read((uintptr_t)array + (uint32_t)index * 4u, &placement,
                         sizeof(placement)) || placement == 0) {
            continue;
        }
        if (!memory_read((uintptr_t)placement, &flags, sizeof(flags))) {
            continue;
        }
        if (!memory_read((uintptr_t)placement + PLACEMENT_POSITION_OFFSET, position,
                         sizeof(position))) {
            continue;
        }

        dx = position[0] - player_position[0];
        dy = position[1] - player_position[1];
        dz = position[2] - player_position[2];
        if ((dx * dx + dy * dy + dz * dz) >=
            (NEARBY_PLACEMENT_DUMP_RADIUS * NEARBY_PLACEMENT_DUMP_RADIUS)) {
            continue;
        }

        {
            uint32_t created = 0;

            (void)memory_read((uintptr_t)placement + PLACEMENT_CREATED_FLAG_OFFSET, &created,
                              sizeof(created));
            log_info("spawn census: NEARBY \"%.*s\" at (%.1f, %.1f, %.1f), active=%d created=%u",
                     (int)PLACEMENT_NAME_MAX, (const char *)(uintptr_t)placement + PLACEMENT_NAME_OFFSET,
                     (double)position[0], (double)position[1], (double)position[2],
                     (flags & PLACEMENT_ACTIVE_FLAG_BIT) != 0, (unsigned)created);
        }
    }
}

static void player_position_log_tick(void)
{
    uint32_t player_record;
    uint32_t player_actor;
    float    position[3];
    static uint32_t dump_count;

    if (player_position_log.player_pointer_slot == NULL) {
        return;
    }
    ++player_position_log.frame_count;
    if (player_position_log.frame_count < PLAYER_POSITION_LOG_EVERY_FRAMES) {
        return;
    }
    player_position_log.frame_count = 0;

    if (!memory_read((uintptr_t)player_position_log.player_pointer_slot, &player_record,
                     sizeof(player_record)) || player_record == 0) {
        return;
    }
    if (!memory_read((uintptr_t)player_record + PLAYER_ACTOR_OFFSET, &player_actor,
                     sizeof(player_actor)) || player_actor == 0) {
        return;
    }
    if (!memory_read((uintptr_t)player_actor + PLAYER_CURRENT_POS_OFFSET, position,
                     sizeof(position))) {
        return;
    }

    if (player_position_log.camera_view_pointer_slot != NULL) {
        uint32_t camera_view;
        float    pitch, yaw;

        if (memory_read((uintptr_t)player_position_log.camera_view_pointer_slot, &camera_view,
                        sizeof(camera_view)) && camera_view != 0 &&
            memory_read((uintptr_t)camera_view + BAPVIEW_EULER_PITCH_OFFSET, &pitch,
                        sizeof(pitch)) &&
            memory_read((uintptr_t)camera_view + BAPVIEW_EULER_YAW_OFFSET, &yaw, sizeof(yaw))) {
            log_info("spawn census: player at (%.1f, %.1f, %.1f), camera yaw %.1f pitch %.1f",
                     (double)position[0], (double)position[1], (double)position[2], (double)yaw,
                     (double)pitch);
        } else {
            log_info("spawn census: player at (%.1f, %.1f, %.1f)", (double)position[0],
                     (double)position[1], (double)position[2]);
        }
    } else {
        log_info("spawn census: player at (%.1f, %.1f, %.1f)", (double)position[0],
                 (double)position[1], (double)position[2]);
    }

    ++dump_count;
    if (dump_count >= NEARBY_PLACEMENT_DUMP_EVERY) {
        dump_count = 0;
        dump_nearby_placements(position);
    }
}

void spawn_census_log_player_position(bool enabled)
{
    if (!enabled || player_position_log.armed) {
        return;
    }
    player_position_log.armed = true;

    player_position_log.player_pointer_slot = resolve_player_pointer_slot();
    if (player_position_log.player_pointer_slot == NULL) {
        log_warning("spawn census: the player pointer chain did not resolve, player position is "
                    "not logged");
        return;
    }
    player_position_log.camera_view_pointer_slot = resolve_camera_view_pointer_slot();
    if (player_position_log.camera_view_pointer_slot == NULL) {
        log_warning("spawn census: the camera view pointer did not resolve, camera yaw/pitch "
                    "will not be logged alongside player position");
    }
    if (!frame_hook_add(player_position_log_tick)) {
        log_warning("spawn census: the per-frame hook could not be installed, player position is "
                    "not logged");
    }
}

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
        const float *position = (const float *)((const char *)a + PLACEMENT_POSITION_OFFSET);

        log_info("spawn census: created \"%.*s\" at (%.1f, %.1f, %.1f)", (int)PLACEMENT_NAME_MAX,
                 (const char *)a + PLACEMENT_NAME_OFFSET,
                 (double)position[0], (double)position[1], (double)position[2]);
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

/* ============================================================================================
 * The destroy side. FUN_00437850(actor, reason): tears an actor down and, for every reason except
 * 3, writes back to its own placement (actor+0x10, the same pointer FUN_00437250 stored there at
 * creation) - reason 1 or 14 sets placement+0xC8 to 2 (blocks re-creation for good), anything else
 * sets it to 0 UNLESS the actor's own state field (actor+0x20) reads 14, in which case that too is
 * immediately overwritten back to 2. Zero is also activation_scan's own "not yet created" gate, so
 * a reason that lands on the zero path is a placement that will be recreated on the very next scan
 * tick - which is exactly the shape of what the field report described. */
static const uint8_t SIG_ACTOR_DESTROY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x14,
    0x81, 0xE1, 0x00, 0x20, 0x00, 0x00, 0x85, 0xC9
};
#define ACTOR_DESTROY_PROLOGUE 6u

typedef void (__cdecl *destroy_fn_t)(void *actor, int32_t reason);

typedef struct destroy_census_state {
    bool     installed;
    detour_t detour;
    bool     log_enabled;   /* the general "destroying X, reason N" line, every actor */
} destroy_census_state_t;

static destroy_census_state_t destroy_census;

/* FUN_00432bf2's own per-tick tail destroys with reason 0 when the LIVE actor position (actor+0xD0)
 * falls outside a DEACTIVATION radius read from the placement at +0x2C - a different field from the
 * ACTIVATION radius at +0x28 the scan itself uses against the placement's own STATIC position at
 * +0xAC. Measured live: for the placements that got caught in this, the two radii and the two
 * positions all agreed (deactivation radius wider than activation, proper hysteresis, position
 * identical to the placement's own), so the level data is not at fault, and reason 0 is the only
 * reason this mechanism ever produces.
 *
 * TWO SUPPRESSION ATTEMPTS AND THE FIX THAT REPLACED THEM were built and field-tested against this
 * mechanism from this hook, before being extracted into droid_fix.dll's own activation_race_
 * fix.c as an independent, chained detour on this same function; see that file's own header for
 * the full account, including the two earlier designs that were tried and rejected. This hook now
 * only observes; it no longer refuses anything itself. */
static void __cdecl hook_actor_destroy(void *actor, int32_t reason)
{
    destroy_fn_t original = (destroy_fn_t)destroy_census.detour.original;
    void        *placement;
    bool         have_placement;

    have_placement = memory_read((uintptr_t)actor + 0x10u, &placement, sizeof(placement)) &&
                     placement != NULL;

    if (destroy_census.log_enabled) {
        if (have_placement) {
            const float *position = (const float *)((const char *)placement +
                                                     PLACEMENT_POSITION_OFFSET);

            log_info("spawn census: destroying \"%.*s\" at (%.1f, %.1f, %.1f), reason %d",
                     (int)PLACEMENT_NAME_MAX, (const char *)placement + PLACEMENT_NAME_OFFSET,
                     (double)position[0], (double)position[1], (double)position[2], (int)reason);
        } else {
            log_info("spawn census: destroying actor %08X (no placement back-pointer), reason %d",
                     (unsigned)(uintptr_t)actor, (int)reason);
        }
    }

    original(actor, reason);
}

bool spawn_census_install_destroy_observer(bool log_enabled)
{
    signature_t site = SIGNATURE_ENTRY_DETOUR("actor_destroy", SIG_ACTOR_DESTROY,
                                              ACTOR_DESTROY_PROLOGUE);

    if (destroy_census.installed) {
        destroy_census.log_enabled = destroy_census.log_enabled || log_enabled;
        return destroy_census.detour.installed;
    }
    destroy_census.installed = true;

    if (!log_enabled) {
        return false;
    }
    destroy_census.log_enabled = log_enabled;

    signature_resolve_table(&site, 1);
    if (site.address == 0) {
        log_warning("the actor-destroy function did not resolve, the teardown log is not "
                    "available");
        return false;
    }
    if (!detour_install(&destroy_census.detour, site.address, (const void *)hook_actor_destroy,
                        ACTOR_DESTROY_PROLOGUE)) {
        log_warning("the actor-destroy detour at %08X failed, the teardown log is not available",
                    (unsigned)site.address);
        return false;
    }

    log_info("actor-destroy observer armed at %08X: teardown log on", (unsigned)site.address);
    return true;
}
