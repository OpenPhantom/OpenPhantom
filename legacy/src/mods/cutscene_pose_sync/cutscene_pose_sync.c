/* cutscene_pose_sync.c: keep the player's own render-interpolation state from going stale right as
 * a cutscene begins.
 *
 * ============================== What is actually broken =======================================
 *
 * Field report: right after the intro movies play going into level 6 (fedship.b3d, the Trade
 * Federation Battleship), Obi-Wan drops into the map as if he had been suspended somewhere higher,
 * and looks wrong for a few seconds before settling. Isolated live to fmv_player.dll (the retail
 * Bink path, which always forces a resolution switch around every movie, does not show it). A
 * resolution-force fix around fmv_player's own playback made it disappear too, but was rejected on
 * its own terms: it reintroduces the exact device-rebuild cost the libVLC rewrite exists to avoid,
 * for every single movie, to work around something that only actually happens once, at this one
 * transition. This is the narrow repair instead.
 *
 * Every drawn object's position is blended each frame, `previous + (current - previous) * alpha`
 * (bapobj_drawAll, 0x004112D9), between two fields on its own body: current at body+0x18 and
 * previous at body+0x54. A live probe (a temporary diagnostics build) caught the player's own body
 * at the exact moment the opening cutscene locks player control, twice, back to back:
 *
 *   current=(122.30,129.00,42.00)  previous=(0.00,0.00,0.00)     <- first read
 *   current=(122.30,129.00,42.23)  previous=(122.30,129.00,42.00) <- second read, a moment later
 *
 * `previous` is not merely stale, it is the WORLD ORIGIN - a value nothing ever wrote, on a body
 * that plainly did not exist yet the instant before (a probe taken right after the level finished
 * loading, earlier in the same run, found no body at all: `body=00000000`). This is what a freshly
 * created object's own render-interpolation pair looks like before anything has had a chance to
 * seed `previous` to match `current` - and it self-repairs almost immediately, which is exactly
 * why it is invisible in ordinary play: something reaches this body again very soon after and sets
 * it right. What retail's resolution switch was actually buying, incidentally, was time - an extra
 * frame or two for that natural repair to land before the very first real draw ever samples
 * `previous`. fmv_player's own faster transition does not leave that gap, so the first draw can
 * land before the repair does.
 *
 * ============================== What this does instead =========================================
 *
 * Dialog_EnterInputLock (0x00430ED9) is the cutscene lock's own entry point, and it is already
 * confirmed, across every capture taken chasing this report, to fire reliably at exactly this
 * transition, before the frame's own draw call: view_lead.c's own documented frame order has the
 * simulation step (where a level's own script, including this lock, runs) ahead of the camera and
 * object draw in the same frame. Hooked here, this reads the player's own body (through pPlayer,
 * the same pointer chain dev_overlay's giant/tiny player cheat already uses) and writes its
 * `current` position over its own `previous`, unconditionally, every time this lock is entered.
 *
 * That is deliberately not conditional on detecting the stale case. In the ordinary case `previous`
 * already sits close to `current` - normal per-substep tracking keeps them within one substep's
 * travel of each other - so forcing equality changes nothing the eye could ever register: a
 * cutscene lock is also the instant free player movement stops anyway, so even a genuine one-frame
 * interpolation reset lands at the one moment in the whole game built to tolerate it without being
 * noticed. Only in the stale case, which is what this exists for, does the write actually matter.
 *
 * Only ever touches the player's own body, and only the three floats at its own +0x54. No level
 * data changes, and nothing here can act on any other actor, any camera state, or any resolution.
 */
#include "cutscene_pose_sync.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUTSCENE_POSE_SYNC_SECTION "cutscene_pose_sync"

/* --- Plr_RunPhases 0x00448297, used only to derive pPlayer's own pointer slot; never detoured.
 * Byte-identical to diag_flow.c's and enhanced_input's own site for this function.
 *   +0x27 : &pPlayer [0x4B5220] */
static const uint8_t SIG_PLAYER_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
#define OFFSET_PLAYER_POINTER 0x27u

/* --- Dialog_EnterInputLock 0x00430ED9, byte-identical to diag_flow.c's own site. The cutscene
 * lock is a level, not a switch: dialogue takes 1, op 0x604 takes 5, module teardown takes 99. The
 * opening cutscene's own "LOCK Player" takes 5, which is the transition this exists for, but the
 * fix is applied on every level, described above. */
static const uint8_t SIG_LOCK_ENTER[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83,
    0x3D, 0x8C, 0x4D, 0x6C, 0x00, 0x00, 0x75, 0x11
};
#define LOCK_ENTER_PROLOGUE 11u

enum {
    SITE_PLAYER_RUN_PHASES,
    SITE_LOCK_ENTER,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("player_run_phases", SIG_PLAYER_RUN_PHASES),
    SIGNATURE_ENTRY_DETOUR("lock_enter", SIG_LOCK_ENTER, LOCK_ENTER_PROLOGUE)
};

#define PLAYER_ACTOR_OFFSET      0x0Cu  /* pPlayer -> bapObj*, confirmed by dev_overlay's own
                                         * giant/tiny player cheat via Plr_AutoAim */
#define OBJ_CURRENT_POS_OFFSET   0x18u  /* three floats; what bapobj_drawAll calls "current" */
#define OBJ_PREVIOUS_POS_OFFSET  0x54u  /* three floats; what it blends FROM every frame */

typedef int32_t (__cdecl *lock_enter_fn_t)(int32_t level);

typedef struct cutscene_pose_sync_state {
    bool       installed;
    bool       enabled;
    detour_t   lock_enter;
    uint32_t  *player_pointer_slot;
    uint32_t   syncs_performed;
} cutscene_pose_sync_state_t;

static cutscene_pose_sync_state_t fix_state;

static void load_config(void)
{
    fix_state.enabled = ini_read_bool(CUTSCENE_POSE_SYNC_SECTION, "Enabled", true);
}

/* Reads the operand behind Plr_RunPhases' own `mov ecx,[pPlayer]`, the same technique
 * diag_flow.c's diag_derive_address uses, inlined here since that helper is diagnostics-only. */
static uint32_t *resolve_player_pointer_slot(void)
{
    uintptr_t site = sites[SITE_PLAYER_RUN_PHASES].address;
    uint32_t  address = 0;

    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + OFFSET_PLAYER_POINTER, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        return NULL;
    }
    return (uint32_t *)(uintptr_t)address;
}

static int32_t __cdecl hook_lock_enter(int32_t level)
{
    lock_enter_fn_t original = (lock_enter_fn_t)fix_state.lock_enter.original;
    int32_t          result = original(level);
    uint32_t         player = 0;
    uint32_t         body = 0;
    float            current[3];

    if (fix_state.player_pointer_slot != NULL &&
        memory_read((uintptr_t)fix_state.player_pointer_slot, &player, sizeof(player)) &&
        player != 0 &&
        memory_read((uintptr_t)player + PLAYER_ACTOR_OFFSET, &body, sizeof(body)) &&
        body != 0 &&
        memory_read((uintptr_t)body + OBJ_CURRENT_POS_OFFSET, current, sizeof(current))) {
        *(float *)((uintptr_t)body + OBJ_PREVIOUS_POS_OFFSET + 0u) = current[0];
        *(float *)((uintptr_t)body + OBJ_PREVIOUS_POS_OFFSET + 4u) = current[1];
        *(float *)((uintptr_t)body + OBJ_PREVIOUS_POS_OFFSET + 8u) = current[2];
        ++fix_state.syncs_performed;
    }

    return result;
}

void cutscene_pose_sync_install(void)
{
    if (fix_state.installed) {
        return;
    }
    fix_state.installed = true;

    log_init("cutscene_pose_sync", false);

    if (!host_image_resolve()) {
        log_error("no 32-bit host image, this fix stays off");
        return;
    }

    load_config();
    if (!fix_state.enabled) {
        log_info("disabled");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);

    fix_state.player_pointer_slot = resolve_player_pointer_slot();
    if (fix_state.player_pointer_slot == NULL) {
        log_warning("pPlayer's own pointer slot did not resolve, this fix cannot find the player's "
                    "body and stays off");
        return;
    }

    if (sites[SITE_LOCK_ENTER].address == 0) {
        log_warning("Dialog_EnterInputLock did not resolve, this fix stays off");
        return;
    }
    if (!detour_install(&fix_state.lock_enter, sites[SITE_LOCK_ENTER].address,
                        (const void *)hook_lock_enter, LOCK_ENTER_PROLOGUE)) {
        log_error("the detour on Dialog_EnterInputLock failed, this fix stays off");
        return;
    }

    log_info("the player's own render-interpolation state is now synced to their real position "
             "every time a cutscene lock is entered, so a freshly spawned body can never be drawn "
             "blending from the world origin");
}
