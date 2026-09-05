/* fog_regime_install.c: resolving the fog regime's sites and patching them, once.
 *
 * Split out of fog_regime.c, which was past the hard limit, and this is the seam its own section
 * banner already drew: everything here ran under the heading "D, installation".
 *
 * The seam is time, not state. Everything in this file runs once, while the game starts: it asks
 * the device what it can do, decides which of the engine's two fog regimes to use, resolves every
 * signature and writes every byte. Nothing here runs again afterwards. What stayed behind is the
 * work that runs on every frame and on every level load.
 *
 * It writes the same record the rest of the module reads, through fog_regime_internal.h, and that
 * is deliberate rather than a leak: the fog regime is one machine and pretending otherwise would
 * mean inventing accessors for ten functions to reach the same fields.
 *
 * SIZE NOTE: past the 600 line mark, and the byte evidence is the reason. Five signatures sit at
 * top, each with the disassembly that proves it and the reasoning for the mask it carries, and
 * that evidence belongs beside the pattern rather than in a document nobody has open. The code
 * underneath it is well inside the normal band. The next seam, if this grows, is the device
 * capability query and the choice between the two fog regimes, which is a decision rather than
 * a patch and answers a question the rest of the pass only acts on.
 */
#include "fog_regime_internal.h"

#include "fog_band.h"
#include "fog_trace.h"
#include "view_distance_fix.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- 0x0041F14A  baplight_applyLevelFog: THE FOG BAND ---------------------------------------- *
 *   55 / 8B EC / 83 EC 0C             prologue, 6 bytes, clean boundary
 *
 * It reads world+0x214 (the packed fog colour) into std3D_setFogColor 0x00487A30, then pushes
 * world+0x21C and world+0x218 into std3D_setFogRange 0x00487AC0 (0x0041F1B3 / 0x0041F1BD), then
 * turns the device fog state on or off from world+0x210 bit 0.
 *
 * TWO CALLERS, and both matter here: 0x0041CAA7 in the level-load path and 0x00438F77 at the tail
 * of the effects fog restore. Without a remembered load value the scale would SQUARE itself on the
 * second run, which is why nothing in this file ever computes from the value currently in the
 * field.
 *
 * The band it hands to the device is not what draws the fog in this regime, see the capability
 * query below, but it is the one place the AUTHORED numbers can be caught, which is what this
 * detour is for. */
static const uint8_t SIG_APPLY_LEVEL_FOG[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0x14, 0x02, 0x00, 0x00, 0xC1
};
#define APPLY_LEVEL_FOG_PROLOGUE_SIZE 6u

/* --- 0x00487B30  THE FOG REGIME: the engine has two, and the wrong one is in force ------------ *
 *   A1 6C 59 85 00      mov eax,[0x85596C]     the chosen device record
 *   8B 80 A4 01 00 00   mov eax,[eax+0x1A4]    the record carries a 0xFC-byte D3DDEVICEDESC copy
 *                                              at +0x138, so +0x1A4 = dpcTriCaps.dwRasterCaps
 *   25 00 01 00 00      and eax,0x100          D3DPRASTERCAPS_FOGTABLE
 *   C3
 * Its only two callers (0x00401E31 world frame setup, 0x00419894 vertex-cache setup) treat a
 * ZERO as "this device cannot do table fog, compute it myself" and arm the engine's own per-vertex
 * ramp, which walks the authored band in WORLD units and writes the D3D fog factor into the
 * vertex's SPECULAR ALPHA (0xFF clear, 0x00 fully fogged).
 * Non-zero means the engine leaves fog to the device: FOGTABLEMODE = D3DFOG_LINEAR with
 * FOGSTART/FOGEND handed over unconverted in world units (4..20 / 14..56 across the levels), and
 * every polygon here is PRE-TRANSFORMED (vertex format 0x1C4 = XYZRHW|DIFFUSE|SPECULAR|TEX1), so
 * the device evaluates that band against a device-space depth inside [0,1] and never fogs
 * anything, while issuing all five fog states exactly as asked.
 *
 * All three sites or none, and that is not tidiness: with the ramp disarmed the world pass writes
 * a CONSTANT ZERO into every world vertex's specular (0x00402459), and zero means FULLY FOGGED.
 * Clearing FOGTABLEMODE without arming the ramp paints the world in the fog colour.
 * The 2-D layer is unaffected, sprites and lines carry render-state words without the fog bit
 * 0x40, so FOGENABLE is 0 for them and their specular alpha is never read.
 *
 * Anchored five bytes in, so the three bytes the patch overwrites are not part of the pattern. */
static const uint8_t SIG_FOG_TABLE_CAP[] = {
    0x8B, 0x80, 0xA4, 0x01, 0x00, 0x00,        /* mov eax,[eax+0x1A4]  dpcTriCaps.dwRasterCaps */
    0x25, 0x00, 0x01, 0x00, 0x00,              /* and eax,0x100        D3DPRASTERCAPS_FOGTABLE */
    0xC3
};
#define FOG_CAP_QUERY_HEAD_OFFSET (-5)         /* back over `mov eax,[abs32]` to the entry point */

/* The two writers of FOGTABLEMODE, both `6A 03 6A 23` = push D3DFOG_LINEAR, push 0x23. The first
 * is the per-primitive state machine at 0x004884B8, which re-issues the five fog states whenever
 * the fog bit moves; the second is the whole-state commit at 0x00489B5B, which re-issues all
 * thirty-four states on every level load and after every display-mode change. Patching only one of
 * them leaves the other to put the table back. Both anchors start AFTER the `push 3`, and both
 * wildcard the device-pointer operands so the pattern carries no absolute address. */
static const uint8_t SIG_FOG_TABLE_MODE_DELTA[] = {
    0x6A, 0x23, 0x50, 0x8B, 0x08, 0xFF, 0x51, 0x58,   /* push 0x23; push dev; call [vtbl+0x58] */
    0xA1, 0x00, 0x00, 0x00, 0x00,                     /* mov eax,[std3D_pDevice] */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,               /* mov ecx,[std3D_fogStart] */
    0x51, 0x6A, 0x24                                  /* push it; push 0x24 = FOGSTART */
};
static const uint8_t MSK_FOG_TABLE_MODE_DELTA[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_FOG_TABLE_MODE_DELTA == sizeof MSK_FOG_TABLE_MODE_DELTA,
               "the fog table mode delta pattern and its mask are different lengths");

static const uint8_t SIG_FOG_TABLE_MODE_COMMIT[] = {
    0x6A, 0x23, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,   /* push 0x23; mov ecx,[std3D_pDevice] */
    0x51, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,         /* push it; mov edx,[std3D_pDevice] */
    0x8B, 0x02, 0xFF, 0x50, 0x58                      /* mov eax,[edx]; call [eax+0x58] */
};
static const uint8_t MSK_FOG_TABLE_MODE_COMMIT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_FOG_TABLE_MODE_COMMIT == sizeof MSK_FOG_TABLE_MODE_COMMIT,
               "the fog table mode commit pattern and its mask are different lengths");
#define FOG_TABLE_MODE_OFFSET (-2)             /* back over the `push 3` the anchor sits behind */

/* --- 0x00401DFE  bapdraw_setFrameState: where the live world pointer lives -------------------- *
 *   83 3D <g_level> 00      cmp dword ptr [g_level],0
 *   0F 84 <rel32>           je   -> no world this frame
 *   8B 15 <g_level>         mov edx,[g_level]
 *   8B 82 10 02 00 00       mov eax,[edx+0x210]      the world's render-flag word
 *   83 E0 01                and eax,1                bit 0 = "this world has fog"
 *
 * Why this DLL needs it at all. The per-frame tick writes into the world record, and the world
 * record is freed and reallocated on every level load, [g_level] has exactly ONE writer in the
 * whole image, 0x0041CA1E, inside that load. Comparing the live pointer against the one the fog
 * detour handed us is therefore an exact "is this still the level I was told about", and it is the
 * only thing standing between a loading screen's frames and a write into freed memory.
 *
 * The pattern is deliberately address-free: both operands are wildcarded and read out of the
 * matched bytes, then cross-checked against each other. A build where the two disagree is not the
 * function we think it is, and the tick declines. */
static const uint8_t SIG_LEVEL_POINTER[] = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x82, 0x10, 0x02, 0x00, 0x00,
    0x83, 0xE0, 0x01
};
static const uint8_t MSK_LEVEL_POINTER[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_LEVEL_POINTER == sizeof MSK_LEVEL_POINTER,
               "the level pointer pattern and its mask are different lengths");
#define OFFSET_LEVEL_POINTER_CMP 0x02u   /* operand of `cmp dword ptr [g_level],0` */
#define OFFSET_LEVEL_POINTER_MOV 0x0Fu   /* operand of `mov edx,[g_level]`, must be the same */

enum {
    SITE_APPLY_LEVEL_FOG,
    SITE_FOG_TABLE_CAP,
    SITE_FOG_TABLE_MODE_DELTA,
    SITE_FOG_TABLE_MODE_COMMIT,
    SITE_LEVEL_POINTER,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("apply_level_fog",           SIG_APPLY_LEVEL_FOG),
    SIGNATURE_ENTRY("fog_table_cap",             SIG_FOG_TABLE_CAP),
    SIGNATURE_ENTRY_MASKED("fog_table_mode_delta",  SIG_FOG_TABLE_MODE_DELTA,
                           MSK_FOG_TABLE_MODE_DELTA),
    SIGNATURE_ENTRY_MASKED("fog_table_mode_commit", SIG_FOG_TABLE_MODE_COMMIT,
                           MSK_FOG_TABLE_MODE_COMMIT),
    SIGNATURE_ENTRY_MASKED("level_pointer",         SIG_LEVEL_POINTER, MSK_LEVEL_POINTER)
};

/* The bytes each fog-regime site must currently carry, and what replaces them. The query is
 * neutered at its entry rather than at the mask, so the anchor five bytes further on keeps
 * matching and a second look can still tell an unpatched site from a patched one. */
static const uint8_t FOG_CAP_QUERY_HEAD[]  = { 0xA1 };              /* mov eax,[abs32] */
static const uint8_t FOG_CAP_QUERY_OFF[]   = { 0x33, 0xC0, 0xC3 };  /* xor eax,eax ; ret */
static const uint8_t FOG_TABLE_LINEAR[]    = { 0x6A, 0x03 };        /* push D3DFOG_LINEAR */
static const uint8_t FOG_TABLE_NONE[]      = { 0x6A, 0x00 };        /* push D3DFOG_NONE */

/* ==============================================================================================
 * D, installation
 * ============================================================================================ */

/* One validated write, so the three fog-regime sites all report failure the same way. */
static bool write_fog_regime_byte(const char *what, uintptr_t address,
                                  const uint8_t *replacement, size_t size)
{
    patch_result_t result = patch_write_bytes(address, replacement, size);

    if (result != PATCH_RESULT_OK) {
        log_error("%s at %08X could not be written (%s)", what, (unsigned)address,
                  patch_result_text(result));
        return false;
    }
    return true;
}

/* THE DEVICE'S OWN FOG CAPABILITIES, read once the device exists and reported once.
 *
 * The engine asks this hardware one question, "can you do table fog", and acts on it. There is a
 * second bit in the same word that decides whether the fog it then configures can work at all:
 *
 *   D3DPRASTERCAPS_FOGTABLE 0x00000100  the engine's own gate; set means it leaves fog to the
 *                                       device and does not run its per-vertex ramp
 *   D3DPRASTERCAPS_WFOG     0x00100000  the device measures fog against eye-space w
 *
 * That second bit matters because the band the engine hands over is in WORLD units, and w in this
 * engine is world units, so the shipped configuration is correct for a w-fog device and meaningless
 * for a z-fog one. Which of the two a modern wrapper offers is not knowable from the executable, so
 * it is logged rather than assumed.
 *
 * The offset is the one the query itself uses. The record holds a copy of the device description at
 * +0x138, D3DPRIMCAPS dpcTriCaps sits at +0x64 inside that, and dwRasterCaps is eight bytes into
 * D3DPRIMCAPS: 0x138 + 0x64 + 0x08 = 0x1A4. */
/* IDirect3DDevice3's vtable. SetRenderState at +0x58 is proven by the signature above, which
 * matches `call [eax+0x58]` at the state commit, and SetTransform is three slots further on. */
#define D3D_VTBL_SET_TRANSFORM    0x64u
#define D3D_TRANSFORM_PROJECTION  3u

/* A w-compliant projection, and it is only ever a fog configuration channel: the geometry is
 * pre-transformed, so Direct3D never multiplies anything by this. What matters is the fourth
 * column, (0,0,1,0) rather than (0,0,0,1). An affine matrix makes the runtime measure fog against
 * device depth in [0,1]; a non-affine one makes it use the reciprocal of rhw, which in this engine
 * is a distance in world units, and world units are what the level's own band is written in.
 *
 * The near and far values behind _33 and _43 (0.1 and 1000) do not reach the fog factor. They only
 * have to make the matrix non-affine, and _34 must stay exactly 1.0 for the normalisation the
 * runtime expects. _11 and _22 are decoration. */
static const float PROJECTION_W_COMPLIANT[16] = {
    1.0f, 0.0f, 0.0f,        0.0f,
    0.0f, 1.0f, 0.0f,        0.0f,
    0.0f, 0.0f, 1.0001f,     1.0f,
    0.0f, 0.0f, -0.10001f,   0.0f
};

typedef long (__stdcall *set_transform_fn_t)(void *device, uint32_t state, const void *matrix);

#define DEVICE_RASTER_CAPS_OFFSET 0x1A4u
#define RASTER_CAPS_FOGTABLE      0x00000100u
#define RASTER_CAPS_WFOG          0x00100000u

void report_device_fog_caps(void)
{
    const void *record;
    uint32_t    caps = 0;

    if (fog_state.caps_reported || fog_state.device_record_ptr == 0) {
        return;
    }
    /* The device is opened after this DLL installs, so the pointer is null for the first frames.
     * Waiting rather than reporting a zero is the difference between "no device yet" and "a device
     * that offers nothing", which are not the same answer. */
    record = *(const void *const *)fog_state.device_record_ptr;
    if (record == NULL) {
        return;
    }
    if (!memory_is_readable_range((uintptr_t)record + DEVICE_RASTER_CAPS_OFFSET, sizeof caps)) {
        fog_state.caps_reported = true;
        log_warning("the device record at %08X is not readable at +0x1A4, its fog capabilities "
                    "are unknown", (unsigned)(uintptr_t)record);
        return;
    }
    caps = *(const uint32_t *)((const char *)record + DEVICE_RASTER_CAPS_OFFSET);
    fog_state.caps_reported = true;
    fog_state.device_caps = caps;

    log_info("device raster caps %08X: table fog %s, w fog %s. %s",
             (unsigned)caps,
             (caps & RASTER_CAPS_FOGTABLE) ? "YES" : "no",
             (caps & RASTER_CAPS_WFOG) ? "YES" : "no",
             (caps & RASTER_CAPS_WFOG)
                 ? "The device measures fog against eye-space w, which is what this engine's band "
                   "is already expressed in, so per-pixel fog is reachable here."
                 : "The device measures fog against device depth, so the engine's world-unit band "
                   "cannot be handed to it unconverted and the per-vertex ramp is the only path.");
}

/* Hand the fog back to the device, per pixel, and give it the one thing it was missing.
 *
 * The engine's table-fog branch is not wrong. It pushes the level's band to FOGSTART and FOGEND in
 * world units, and eye-space w in this engine IS world units, so that pairing is correct for a
 * device measuring fog against w. What decides whether the device measures w or device depth is the
 * projection matrix, and the engine never sets one: SetTransform is not called anywhere in the
 * image, so the runtime sees the identity, calls it affine, and measures depth in [0,1]. A
 * world-unit band against a [0,1] depth fogs nothing, which is why the game looks unfogged on any
 * device that reports table fog.
 *
 * So this does not convert the band. It sets a w-compliant projection and gives the engine its own
 * branch back, which removes the per-vertex ramp and with it the two artefacts the ramp cannot
 * avoid: an 8-bit fog factor per vertex, and interpolation of that factor across polygons without
 * regard to where the band actually is.
 *
 * It happens HERE, on the frame path, rather than at install, because the device does not exist at
 * install time and neither do its capabilities. Installing the ramp first and reverting it once the
 * device proves it can do better is the order that degrades safely: a device that cannot keeps the
 * fog it already had. */
static bool device_supports_pixel_fog(uint32_t caps)
{
    return (caps & RASTER_CAPS_FOGTABLE) != 0u && (caps & RASTER_CAPS_WFOG) != 0u;
}

static bool set_device_projection(void *device, const float *matrix)
{
    void **vtable;
    set_transform_fn_t set_transform;

    if (device == NULL || !memory_is_readable_range((uintptr_t)device, sizeof(void *))) {
        return false;
    }
    vtable = *(void ***)device;
    if (vtable == NULL ||
        !memory_is_readable_range((uintptr_t)vtable + D3D_VTBL_SET_TRANSFORM, sizeof(void *))) {
        return false;
    }
    set_transform = (set_transform_fn_t)vtable[D3D_VTBL_SET_TRANSFORM / sizeof(void *)];
    if (set_transform == NULL) {
        return false;
    }
    return set_transform(device, D3D_TRANSFORM_PROJECTION, matrix) >= 0;
}

/* Undo the three writes install_vertex_fog made, in the reverse order it made them. */
static bool restore_engine_table_fog(void)
{
    if (!fog_state.vertex_fog_installed) {
        return true;                       /* never armed: nothing to give back */
    }
    if (patch_write_bytes(fog_state.commit_push, FOG_TABLE_LINEAR,
                          sizeof FOG_TABLE_LINEAR) != PATCH_RESULT_OK ||
        patch_write_bytes(fog_state.applier_push, FOG_TABLE_LINEAR,
                          sizeof FOG_TABLE_LINEAR) != PATCH_RESULT_OK ||
        patch_write_bytes(fog_state.query_entry, fog_state.saved_query,
                          sizeof fog_state.saved_query) != PATCH_RESULT_OK) {
        return false;
    }
    fog_state.vertex_fog_installed = false;
    return true;
}

bool fog_regime_level_opening(void)
{
    return fog_state.open_left > 0.0f;
}

void fog_regime_set_band_scale(float scale)
{
    if (!(scale > 0.0f) || fog_state.config.band_scale == scale) {
        return;
    }
    fog_state.config.band_scale = scale;
    log_info("fog band: scaled to %.2f of where the terms above it put it. The band eases to the "
             "new target rather than stepping.", (double)scale);
}

void consider_pixel_fog(uint32_t caps)
{
    void *device;

    if (!fog_state.config.pixel_fog || fog_state.pixel_fog_active ||
        fog_state.device_ptr_addr == 0) {
        return;
    }
    if (!device_supports_pixel_fog(caps)) {
        log_info("PixelFog=1, but this device does not offer both table fog and w fog, so the "
                 "per-vertex ramp stays in charge. Nothing is changed.");
        fog_state.config.pixel_fog = false;
        return;
    }
    device = *(void **)fog_state.device_ptr_addr;
    if (device == NULL) {
        return;                            /* not open yet; asked again next frame */
    }
    if (!set_device_projection(device, PROJECTION_W_COMPLIANT)) {
        log_warning("SetTransform refused the projection, so fog would be measured against device "
                    "depth and a world-unit band would fog nothing. The per-vertex ramp stays.");
        fog_state.config.pixel_fog = false;
        return;
    }
    if (!restore_engine_table_fog()) {
        log_error("the projection was set but the per-vertex patches could not be reverted. The "
                  "engine is now in a mixed state; the ramp still runs and the fog may be wrong.");
        fog_state.config.pixel_fog = false;
        return;
    }

    fog_state.pixel_fog_active = true;
    fog_state.projection_device = device;

    /* ONE WAY, and that is the reason there is no switch back. Going the other way needs the
     * device reprogrammed, because this engine only ever sets FOGTABLEMODE from inside
     * applyLevelFog and that runs at a level load. Reverting the three writes changes what the
     * NEXT load will push and nothing else, so the engine goes back to computing a per-vertex
     * factor while the device is still told to ignore it, and nothing is fogged. Reverting the
     * writes, handing the identity projection back, and calling applyLevelFog's original by hand
     * were all tried in the game and none of them brought the fog back. So the delivery is chosen
     * once, at startup, from FogImplementation, and what the panel offers is the band. */
    log_info("pixel fog active: the device measures eye-space w, the band goes to it in world "
             "units unconverted, and the engine's own per-vertex ramp is switched back off. No "
             "8-bit fog factor and no interpolation of it across polygons.");

    /* The band only reaches the device through applyLevelFog, and that has already run for this
     * level. Push what we hold now, or the first level entered this way keeps the authored band. */
    push_band_to_device();
}

/* Move the engine from device table fog, which a pre-transformed vertex cannot feed, onto its own
 * per-vertex ramp. See the site comments above for why all three writes belong together. */
static void install_vertex_fog(void)
{
    uintptr_t query   = sites[SITE_FOG_TABLE_CAP].address;
    uintptr_t applier = sites[SITE_FOG_TABLE_MODE_DELTA].address;
    uintptr_t commit  = sites[SITE_FOG_TABLE_MODE_COMMIT].address;
    uintptr_t query_entry;
    uintptr_t applier_push;
    uintptr_t commit_push;
    uint32_t  device_record_operand = 0;
    uint8_t   saved_query[sizeof FOG_CAP_QUERY_OFF];

    /* BEFORE the early return and before any patch. The query's own operand is the only place this
     * DLL can learn where the device record pointer lives, and arming vertex fog writes over it. */
    if (query != 0) {
        uintptr_t entry = (uintptr_t)((intptr_t)query + FOG_CAP_QUERY_HEAD_OFFSET);
        uint32_t  operand = 0;

        if (patch_validate_bytes(entry, FOG_CAP_QUERY_HEAD, sizeof FOG_CAP_QUERY_HEAD) &&
            memory_read_u32(entry + sizeof FOG_CAP_QUERY_HEAD, &operand) &&
            memory_is_inside_image(operand, sizeof(uint32_t))) {
            fog_state.device_record_ptr = (uintptr_t)operand;
        }
    }

    /* And the device INTERFACE pointer, which is a different thing from the record above. The state
     * commit's signature begins `push 0x23; mov ecx,[abs32]`, so the operand sits four bytes in.
     * Taken here for the same reason as the record: nothing later in this file has another way to
     * find it, and this site is about to be written over. */
    if (commit != 0) {
        uint32_t operand = 0;

        if (memory_read_u32(commit + 4u, &operand) &&
            memory_is_inside_image(operand, sizeof(uint32_t))) {
            fog_state.device_ptr_addr = (uintptr_t)operand;
        }
    }

    if (!fog_state.config.vertex_fog) {
        log_info("FogImplementation=0, the fog regime is left exactly as the device asks for "
                 "it, which on modern hardware means no fog arrives at all");
        return;
    }
    if (query == 0 || applier == 0 || commit == 0) {
        log_warning("the fog regime is NOT changed: cap %08X, state machine %08X, commit %08X. "
                    "All three have to resolve, because half of this change would either do "
                    "nothing or paint the world in the fog colour",
                    (unsigned)query, (unsigned)applier, (unsigned)commit);
        return;
    }

    query_entry  = (uintptr_t)((intptr_t)query   + FOG_CAP_QUERY_HEAD_OFFSET);
    applier_push = (uintptr_t)((intptr_t)applier + FOG_TABLE_MODE_OFFSET);
    commit_push  = (uintptr_t)((intptr_t)commit  + FOG_TABLE_MODE_OFFSET);

    /* The anchor starts inside the query, so prove the five bytes in front of it really are the
     * `mov eax,[abs32]` that loads the device record before writing a `ret` over them. */
    if (!patch_validate_bytes(query_entry, FOG_CAP_QUERY_HEAD, sizeof FOG_CAP_QUERY_HEAD) ||
        !memory_read_u32(query_entry + sizeof FOG_CAP_QUERY_HEAD, &device_record_operand) ||
        !memory_is_inside_image(device_record_operand, sizeof(uint32_t))) {
        log_warning("%08X is not the capability query's entry point (operand %08X), the fog "
                    "regime is unchanged", (unsigned)query_entry, (unsigned)device_record_operand);
        return;
    }

    /* Validate BOTH FOGTABLEMODE writers before either is touched. */
    if (!patch_validate_bytes(applier_push, FOG_TABLE_LINEAR, sizeof FOG_TABLE_LINEAR) ||
        !patch_validate_bytes(commit_push, FOG_TABLE_LINEAR, sizeof FOG_TABLE_LINEAR)) {
        log_warning("FOGTABLEMODE is not D3DFOG_LINEAR at %08X / %08X, so the fog regime is "
                    "unchanged", (unsigned)applier_push, (unsigned)commit_push);
        return;
    }

    if (!memory_read(query_entry, saved_query, sizeof saved_query)) {
        log_warning("%08X is not readable, the fog regime is unchanged", (unsigned)query_entry);
        return;
    }

    if (!write_fog_regime_byte("the table-fog capability query", query_entry,
                               FOG_CAP_QUERY_OFF, sizeof FOG_CAP_QUERY_OFF)) {
        return;
    }
    if (!write_fog_regime_byte("FOGTABLEMODE in the state machine", applier_push,
                               FOG_TABLE_NONE, sizeof FOG_TABLE_NONE)) {
        (void)patch_write_bytes(query_entry, saved_query, sizeof saved_query);
        log_warning("the capability query was restored, the fog regime is unchanged");
        return;
    }
    if (!write_fog_regime_byte("FOGTABLEMODE in the state commit", commit_push,
                               FOG_TABLE_NONE, sizeof FOG_TABLE_NONE)) {
        (void)patch_write_bytes(applier_push, FOG_TABLE_LINEAR, sizeof FOG_TABLE_LINEAR);
        (void)patch_write_bytes(query_entry, saved_query, sizeof saved_query);
        log_warning("the state machine and the capability query were restored, the fog regime is "
                    "unchanged");
        return;
    }

    fog_state.vertex_fog_installed = true;
    fog_state.query_entry  = query_entry;
    fog_state.applier_push = applier_push;
    fog_state.commit_push  = commit_push;
    memcpy(fog_state.saved_query, saved_query, sizeof fog_state.saved_query);

    log_info("distance fog runs on the engine's own per-vertex ramp: capability query %08X now "
             "answers 'no table fog', FOGTABLEMODE is D3DFOG_NONE at %08X and %08X. The fog "
             "factor travels in the specular alpha, so the authored world-unit band is used as "
             "authored instead of being read as a device-space depth.",
             (unsigned)query_entry, (unsigned)applier_push, (unsigned)commit_push);
}

/* The per-frame tick needs two cells that are read out of code rather than assumed: the live world
 * pointer and the engine's own frame delta. Without the world pointer the tick stays OFF, a
 * write through a remembered pointer during a level load would land in freed memory. */
static void resolve_tick_cells(void)
{
    uintptr_t site = sites[SITE_LEVEL_POINTER].address;
    uintptr_t frame_end = frame_hook_site();
    uint32_t  from_cmp = 0;
    uint32_t  from_mov = 0;
    uint32_t  delta_address = 0;

    if (frame_end != 0 &&
        memory_read_u32(frame_end + FRAME_HOOK_FRAME_DELTA_OPERAND_OFFSET, &delta_address) &&
        memory_is_inside_image(delta_address, sizeof(float))) {
        fog_state.frame_delta = (const volatile float *)(uintptr_t)delta_address;
    }

    /* The tick is a frame-hook callback. Claiming it is active while nothing calls it is exactly
     * the silent failure this project keeps paying for, so the hook decides first. */
    if (!frame_hook_is_installed()) {
        log_warning("no per-frame hook, there is NO fog tick. The band is still computed and "
                    "written once per level, so a field-of-view change taken mid-session reaches "
                    "the fog at the next level load rather than at once.");
        return;
    }
    if (site == 0) {
        log_warning("level_pointer did not resolve, there is NO per-frame fog tick. The band is "
                    "still computed and written once per level, so a field-of-view change takes "
                    "effect on the next level rather than at once.");
        return;
    }
    if (!memory_read_u32(site + OFFSET_LEVEL_POINTER_CMP, &from_cmp) ||
        !memory_read_u32(site + OFFSET_LEVEL_POINTER_MOV, &from_mov) ||
        from_cmp != from_mov ||
        !memory_is_inside_image(from_cmp, sizeof(void *))) {
        log_warning("the two world-pointer operands at %08X disagree (%08X vs %08X), no "
                    "per-frame fog tick", (unsigned)site, (unsigned)from_cmp, (unsigned)from_mov);
        return;
    }

    fog_state.level_pointer = (void * volatile *)(uintptr_t)from_cmp;
    fog_state.tick_active   = true;

    if (fog_state.frame_delta == NULL) {
        log_warning("g_frameDelta did not resolve, the fog tick runs but cannot ease, so every "
                    "change steps in one frame");
    }
    log_info("fog tick active, g_level %08X, g_frameDelta %08X, settle %.2f s",
             (unsigned)from_cmp, (unsigned)delta_address,
             (double)fog_state.config.settle_seconds);
}

static void install_level_fog(void)
{
    uintptr_t site = sites[SITE_APPLY_LEVEL_FOG].address;

    if (site == 0) {
        log_warning("apply_level_fog did not resolve, the fog band stays exactly as authored and "
                    "does NOT follow the field of view. The cut edge moves in as the picture "
                    "widens, so geometry will appear at a visible boundary.");
        return;
    }

    if (!detour_install(&fog_state.apply_detour, site, (const void *)hook_apply_fog,
                        APPLY_LEVEL_FOG_PROLOGUE_SIZE)) {
        log_error("the baplight_applyLevelFog detour at %08X failed, the fog band is not "
                  "coupled to anything", (unsigned)site);
        return;
    }

    log_info("fog band coupled at %08X: the authored band is scaled by "
             "(cut-%.2f)*cos(hFOV/2) measured against the same expression at the authored "
             "%.0f degrees%s. FogScale=%.2f.",
             (unsigned)site, (double)FOG_CELL_MARGIN_UNITS,
             (double)FOG_REFERENCE_FOV_DEGREES,
             (fog_state.config.inside_cut == FOG_END_NO_SATURATION)
                 ? ", then ended just beyond the cut so nothing drawn is ever fully fogged"
                 : (fog_state.config.inside_cut == FOG_END_NO_POP_IN)
                     ? ", then capped to the corner limit"
                     : " (neither bound)",
             (double)fog_state.config.fog_scale);
}

void fog_regime_set_authored_band(bool authored)
{
    if (fog_state.config.authored_band == authored) {
        return;
    }
    fog_state.config.authored_band = authored;
    log_info("fog band: %s", authored
                 ? "each level's own, untouched. Nine of the eleven shipped levels already hide "
                   "their own draw edge this way; Coruscant and the Federation ship will show "
                   "theirs."
                 : "scaled against the draw distance and the field of view, so the fog is solid "
                   "before the geometry stops on every level.");
}

void fog_regime_install(const fog_regime_config_t *config)
{
    if (fog_state.installed || config == NULL) {
        return;
    }

    fog_state.config = *config;
    fog_state.horizontal_fov_degrees = FOG_REFERENCE_FOV_DEGREES;
    fog_trace_configure(config->log_band);
    fog_trace_counters_install();
    fog_state.installed = true;

    signature_resolve_table(sites, SITE_COUNT);

    install_vertex_fog();

    if (!fog_state.config.follow_fov && !fog_state.config.inside_cut &&
        fog_state.config.fog_scale <= 1.0f) {
        log_info("FogFollowFov=0, FogInsideCut=0 and FogScale=1, the fog band is left exactly as "
                 "each level authored it");
        return;
    }

    resolve_tick_cells();
    install_level_fog();
}

