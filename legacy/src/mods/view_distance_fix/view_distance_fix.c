/* view_distance_fix.c: how far the world is drawn, how far the fog reaches, and when NPCs come
 * into being.
 *
 * ==============================================================================================
 * What has to be understood before any number here is turned
 *
 * Draw distance and fog are two numbers no engine code connects.
 *     draw distance: world+0x014 (B3D hdr+0x854), overridable per cell by bapCell+0x03
 *     fog:           world+0x218 / +0x21C (B3D hdr+0x90 / +0x94)
 * And graphics_clearFrame 0x46C0F5 clears the picture to the FOG COLOUR. Geometry beyond fogEnd
 * is therefore exactly background-coloured, i.e. invisible. Raising the draw distance alone costs
 * fill rate without a single additional pixel. The two have to move together, and connecting them
 * is what fog_regime.c does.
 *
 * Authored per level (draw distance / fog end):
 *   GUNGA 16/14, GARDEN 22/26, MAUL 28/32, FINAL 24/30, SWAMP 22/30, ESPA 22/32,
 *   RACE 22/32, QUEEN 26/38, ASSAULT 23/38, BIGCITY 20/50, FEDSHIP 18/56
 * Only GUNGA is fully fog-covered. In BIGCITY and FEDSHIP the fog ends far BEHIND the geometry,
 * there the cut edge is a visible wall even in the shipped state.
 *
 * Field of view and range multiply. bapdraw_drawWorld collects over a rectangle that grows with
 * tan(fovH/2) and caps it with a circle test at `range`. The cell count goes like
 * (hFOV/360)*pi*r^2, from 63 to 106 degrees that is x1.68 from the field of view alone.
 *
 * And the fog has to reach the screen at all, and it has to follow the cut edge when the cut edge
 * moves. Both of those live in fog_regime.c now: which of the engine's two fog regimes is in
 * force, and how far the band reaches once the radius cap in view_range.c has shortened the
 * picture. This DLL feeds it the two numbers only it has, the field of view it observes and the
 * cut edge it computes, and never reaches into the fog itself.
 *
 * NPCs ARE CREATED, not merely drawn. 1127 of 1315 scannable placements (85.7 %) have an
 * activation radius SMALLER than their level's draw distance, the enemy only comes into being
 * once it is well inside the visible picture. That is the pop-in, and it exists at 60 degrees
 * too. This is the ONE change here that touches GAME BEHAVIOUR: an actor created earlier thinks
 * earlier. It therefore ships at 1.0, the engine's own value, and installs nothing.
 *
 * Creating them earlier is not free, and that is why the default came back down. The engine draws
 * actors from two pools fixed at start-up, 128 actors and 255 things, and the activation test is
 * a SPHERE, so a radius multiplied by k multiplies the activated volume by k cubed: 1.25 was very
 * nearly twice as many actors alive at once. A full pool makes the spawn return zero silently,
 * the placement is skipped, and an enemy that should be standing in front of the player is not
 * there at all. spawn_census.c counts exactly that.
 *
 * THE SEAMS TAKEN. This file was well past the hard limit, and three whole responsibilities came
 * out of it, each carrying the byte evidence that explains it:
 *
 *   view_settings.c    the ini: every key, its default and its clamp, and the handful that are
 *                      re-read while the game runs. It touches no engine memory and resolves no
 *                      signature, which is what made it the first cut and why it took the
 *                      configuration record with it.
 *   view_range.c       the draw distance actually in force: the field of view observer, the
 *                      radius cap, the cut edge, the bapmat_viewDistance detour, and the per
 *                      frame tick that arbitrates between the frame governor, the level opening
 *                      window, a scripted camera and the cell watchdog.
 *   two_sided_faces.c  the software backface cull word and the rdThing_Draw detour that clears it
 *                      for a body with a hole in it. It shares nothing with the draw distance but
 *                      the DLL it ships in.
 *
 * What stays here is the site table, which is the byte evidence for all five patched sites, the
 * NPC activation radius, and the install sequence that resolves every site and hands the
 * addresses out in the order the rest of them depend on.
 */
#include "view_distance_fix.h"

#include "cell_watchdog.h"
#include "frame_governor.h"
#include "draw_table.h"
#include "fog_regime.h"

#include "poly_bias.h"
#include "device_dither.h"
#include "scene_fade.h"
#include "translucent_fog.h"

#include "common/cinematic_gate.h"
#include "fog_trace.h"
#include "spawn_census.h"
#include "vertex_table.h"

#include "two_sided_faces.h"
#include "view_range.h"
#include "view_settings.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0040E42A  bapmat_viewDistance: THE DRAW DISTANCE --------------------------------------- *
 *   55 / 8B EC / 51 / 8B 45 08        prologue, 7 bytes, clean boundary
 *   8B 48 14                          range = world->viewRange (+0x14, from B3D hdr+0x854)
 *
 * The [2,64] clamp is NOT here; it lives only in the world-walk caller 0x404F33. The other two
 * callers (0x4048F3 an oldcode cheat, 0x4221FA the emitter cull radius) do NOT clamp, which is
 * why our detour clamps itself. */
static const uint8_t SIG_VIEW_DISTANCE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x14, 0x89, 0x4D, 0xFC, 0x8B, 0x55, 0x0C
};

/* --- 0x004371E4  enemy_activationScan: THE ACTIVATION RADIUS ---------------------------------- *
 *   8B 55 F8 / 8B 42 28 / 50          push rec+0x28 = ACTIVE RANGE  -> arg3
 *   8B 4D FC / 83 C1 18 / 51          push playerBody+0x18
 *   8B 55 F8 / 81 C2 AC000000 / 52    push rec+0xAC
 *   E8 <rel32>                        call within_range 0x428EB3    <- at +0x18
 *
 * Only THIS call site is redirected. within_range has a second caller (0x4332F2, an AI query)
 * that must stay untouched. radius == 0 means "always active" and must not become finite. */
static const uint8_t SIG_ACTIVATION_SCAN[] = {
    0x8B, 0x55, 0xF8, 0x8B, 0x42, 0x28, 0x50,
    0x8B, 0x4D, 0xFC, 0x83, 0xC1, 0x18, 0x51,
    0x8B, 0x55, 0xF8, 0x81, 0xC2, 0xAC, 0x00, 0x00, 0x00, 0x52
};
#define OFFSET_ACTIVATION_SCAN_CALL 0x18u

/* --- 0x0040F3F7  rdMesh_draw: THE BACKFACE CULL ---------------------------------------------- *
 *   (before) 8A 1D A0868600   mov bl,[0x8686A0]      <- the cull address, at anchor-4
 *   B8 01000000              mov eax,1
 *   84 D8                    test al,bl
 *   75 32                    jne  -> face DROPPED
 *
 * The DEVICE does not cull at all: 0x489D02 sets D3DRENDERSTATE_CULLMODE to D3DCULL_NONE. The
 * engine culls in SOFTWARE, per face, at this one place. The anchor is DELIBERATELY address-free;
 * the cull address is read from anchor-4 and checked against the image. */
static const uint8_t SIG_MESH_CULL_WORD[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00, 0x84, 0xD8, 0x75, 0x32
};

/* --- 0x0040FE70  rdThing_Draw --------------------------------------------------------------- *
 *   83 EC 48 / B9 0C000000            prologue, 8 bytes, clean boundary
 * NO frame pointer: the two cdecl arguments are at [esp+4] / [esp+8] on entry.
 * rdMesh_draw has two callers (0x4100E5 from here, 0x456E17 from shot_drawAll), which is why
 * the detour must RESET the cull word at the end, not merely set it at the start.
 *
 * The pattern reaches eight bytes past the prologue on purpose. The prologue itself is what another
 * DLL's detour overwrites, and dev_overlay does exactly that here, so the site is declared in the
 * DETOUR form and the tail is what identifies it. */
static const uint8_t SIG_THING_DRAW[] = {
    0x83, 0xEC, 0x48, 0xB9, 0x0C, 0x00, 0x00, 0x00, 0x55, 0x8B, 0x6C, 0x24, 0x50, 0x56, 0x8B, 0x74
};

/* --- 0x00475FFA  rdCamera_BuildProjection --------------------------------------------------- *
 * We hook it ONLY to observe cam+0x38 after the engine has rebuilt the projection. The radius
 * cap in view_range.c couples range to the field of view, and asking another DLL for that number
 * would make this DLL depend on it. common/detour.c chains us with whoever else is on this
 * function, so the value we read is always the one that is in force. */
static const uint8_t SIG_RDCAMERA_BUILD_PROJECTION[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x04,
    0x89, 0x4D, 0xFC, 0x83, 0x7D, 0xFC, 0x00
};

enum {
    SITE_VIEW_DISTANCE,
    SITE_ACTIVATION_SCAN,
    SITE_MESH_CULL_WORD,
    SITE_THING_DRAW,
    SITE_RDCAMERA_BUILD_PROJECTION,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("view_distance",             SIG_VIEW_DISTANCE),
    SIGNATURE_ENTRY("activation_scan",           SIG_ACTIVATION_SCAN),
    SIGNATURE_ENTRY("mesh_cull_word",            SIG_MESH_CULL_WORD),
    /* DETOUR form, because another DLL gets here first. dev_overlay hooks this same function
       for giant and tiny player, and it loads before this one, so by the time this pattern is
       searched the first eight bytes are a jump into its thunk. The plain form found zero
       matches and switched two-sided drawing off in every session the overlay was installed,
       on both platforms, reported only as a warning line nobody was reading. */
    SIGNATURE_ENTRY_DETOUR("thing_draw",         SIG_THING_DRAW, THING_DRAW_PROLOGUE_SIZE),
    SIGNATURE_ENTRY_DETOUR("rdcamera_build_projection", SIG_RDCAMERA_BUILD_PROJECTION,
                           BUILD_PROJECTION_PROLOGUE_SIZE)
};

typedef int32_t (__cdecl *within_range_fn_t)(const float *a, const float *b, float radius);

typedef struct view_distance_state {
    bool                   installed;
    view_distance_config_t config;

    within_range_fn_t      engine_within_range;
} view_distance_state_t;

static view_distance_state_t view_state;

/* ============================================================================================
 * The NPC activation radius
 * ============================================================================================ */
static int32_t __cdecl hook_within_range(const float *a, const float *b, float radius)
{
    /* radius == 0 means "always active" in the engine (0x4371D7 compares against 0.0f), the
     * scaling must not turn that into a finite radius. */
    if (radius > 0.0f) {
        radius *= view_state.config.npc_range_scale;
    }
    return view_state.engine_within_range(a, b, radius);
}

/* ============================================================================================ */
static void install_npc_range(void)
{
    uintptr_t site = sites[SITE_ACTIVATION_SCAN].address;
    uintptr_t call_site;
    uintptr_t target;

    if (view_state.config.npc_range_scale <= 1.0f) {
        log_info("NpcRangeScale=1, NPCs appear as in the original");
        return;
    }
    if (site == 0) {
        log_warning("activation_scan did not resolve, NPCs keep appearing inside the clear "
                    "picture");
        return;
    }

    call_site = site + OFFSET_ACTIVATION_SCAN_CALL;
    if (!patch_read_call_target(call_site, &target)) {
        log_warning("no usable E8 at %08X, refused", (unsigned)call_site);
        return;
    }
    view_state.engine_within_range = (within_range_fn_t)target;

    if (patch_redirect_call(call_site, (const void *)hook_within_range) == PATCH_RESULT_OK) {
        log_info("NPC activation radius x%.2f (only the call site %08X; the second caller of "
                 "%08X is untouched). This is the only change here that touches GAME BEHAVIOUR.",
                 (double)view_state.config.npc_range_scale, (unsigned)call_site,
                 (unsigned)target);
    } else {
        log_error("the call site %08X could not be written", (unsigned)call_site);
        view_state.engine_within_range = NULL;
    }
}

/* The fog is a responsibility of its own and lives in fog_regime.c. All this does is hand it the
 * settings; the two live numbers reach it from the hooks in view_range.c. */
static void install_fog_regime(void)
{
    fog_regime_config_t fog_config;

    /* 0 leaves the engine as the device asks for it, which on modern hardware is no fog at all.
     * 1 and 2 both arm the ramp first, so a device that turns out not to support per-pixel fog
     * degrades to the ramp rather than to nothing. */
    fog_config.vertex_fog     = view_state.config.fog_implementation >= 1;
    fog_config.pixel_fog      = view_state.config.fog_implementation == 2;
    fog_config.authored_band  = view_state.config.authored_fog;
    fog_config.min_end_fraction = view_settings_clamp(view_state.config.fog_min_end, 0.0f, 1.0f);
    fog_config.band_scale       = view_settings_clamp(view_state.config.fog_band_scale,
                                                      0.25f, 1.0f);
    fog_config.open_seconds     = view_state.config.level_open_seconds;
    fog_config.follow_fov     = view_state.config.fog_follow_fov;
    fog_config.inside_cut     = view_state.config.fog_inside_cut;
    fog_config.fog_scale      = view_state.config.fog_scale;
    fog_config.settle_seconds = view_state.config.fog_settle_seconds;
    fog_config.log_band       = view_state.config.log_fog_band;
    fog_config.open_fog_start = view_state.config.level_open_fog_start;
    fog_config.open_fog_end   = view_state.config.level_open_fog_end;

    fog_regime_install(&fog_config);
}

/* ============================================================================================ */
void view_distance_fix_install(void)
{
    bool watchdog_ok;

    log_init("view_distance_fix", false);

    if (view_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }

    view_settings_load(&view_state.config);
    if (!view_state.config.enabled) {
        log_info("Enabled=0, draw distance, fog and NPC activation stay as they shipped");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    view_state.installed = true;
    view_range_configure(&view_state.config, view_state.config.view_range_scale);

    /* The frame hook belongs here, not only in the frame-rate DLL. The tick carries the cell
     * watchdog AND the two-sided reset; without it the watchdog is mute and two-sidedness is off
     * for the whole process after eight things, while the log a few lines further down would
     * cheerfully say "watchdog active". Exactly the kind of silent failure this project has paid
     * for three times. */
    if (!frame_hook_add(view_range_on_frame)) {
        log_warning("no per-frame hook, so NO cell watchdog. The draw distance is held at "
                    "1.0, because an overflow would silently overwrite the bucket list heads.");
        view_state.config.view_range_scale = 1.0f;
        view_range_set_scale(1.0f);
    }

    /* AFTER the frame hook is settled, so that the scale it is told about is the one that survived
     * the branch above: with no hook the range is pinned to 1.0 and there is nothing for a
     * governor to give back. */
    frame_governor_configure(view_state.config.frame_backoff, view_state.config.backoff_fps,
                             view_state.config.view_range_scale);

    view_range_install_fov_observer(sites[SITE_RDCAMERA_BUILD_PROJECTION].address);

    /* ORDER: the watchdog resolves its counters against the very operands the relocation
     * rewrites, so it must run FIRST. See draw_table.h. */
    watchdog_ok = cell_watchdog_install(view_state.config.lower_cell_limit,
                                        view_state.config.relocate_draw_table);
    if (!watchdog_ok) {
        view_state.config.view_range_scale = 1.0f;
        view_range_set_scale(1.0f);
    }

    if (view_state.config.relocate_draw_table) {
        draw_table_relocate();
        if (draw_table_is_active()) {
            cell_watchdog_set_limit(draw_table_limit());
        }
    }

    /* Same ordering rule as the draw table just above, and for the identical reason: the watchdog
     * has already resolved the vertex counter by this point, and this relocation touches none of
     * the operands that resolution reads. */
    if (view_state.config.relocate_vertex_table) {
        vertex_table_relocate();
        if (vertex_table_is_active()) {
            cell_watchdog_set_vertex_limit(vertex_table_limit());
        }
    }

    /* ORDER: the fog reads the cut edge the draw-distance detour reports, so that detour has to be
     * standing before the first frame the fog ticks on. */
    view_range_install_draw_distance(sites[SITE_VIEW_DISTANCE].address);
    install_fog_regime();
    install_npc_range();
    if (view_state.config.cutscene_range > 0.0f) {
        (void)cinematic_gate_install();
    }
    poly_bias_install(view_state.config.poly_depth_bias);
    translucent_fog_install(view_state.config.translucent_fog);
    device_dither_configure(view_state.config.dither);
    scene_fade_install(view_state.config.level_fade_seconds);
    two_sided_faces_install(sites[SITE_MESH_CULL_WORD].address, sites[SITE_THING_DRAW].address,
                            view_state.config.two_sided_severed, view_state.config.two_sided_max);

    /* Last, and on the same resolved site the range test uses. It is the only observer of a
     * failure the engine reports nowhere: a spawn the pools were too full to satisfy. */
    (void)spawn_census_install(sites[SITE_ACTIVATION_SCAN].address,
                               view_state.config.spawn_census);

    /* The destroy side of the same investigation. Self-resolves its own site, independent of
     * activation_scan. Nothing else in this project detours that function any more; see
     * spawn_census.h's own comment. */
    (void)spawn_census_install_destroy_observer(view_state.config.spawn_census);

    spawn_census_log_player_position(view_state.config.log_player_position);
}

void view_distance_fix_shutdown(void)
{
    /* The capture is a rolling window now, so this is the only place it can be written
     * out: whatever the player was doing last is what it holds. */
    fog_trace_flush("the game is closing");

    draw_table_restore();
    vertex_table_restore();
}
