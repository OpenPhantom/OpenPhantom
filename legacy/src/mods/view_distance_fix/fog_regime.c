/* fog_regime.c: the fog the engine computes itself, and the band it computes it over.
 *
 * The geometry this file implements, why the cut edge is a circle of cell centres in world
 * units, why a wider picture needs the fog nearer, and why the WHOLE band has to move rather than
 * only its far end, is written out in fog_regime.h. Everything below is the byte evidence for
 * each site and the arithmetic itself.
 *
 * SIZE NOTE (rule 9): this file is over 600 lines because rule 8 wants the byte evidence at the
 * site rather than only in a document. The code itself is well inside the normal band; the site
 * comments are the bulk.
 */
#include "fog_regime.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

static const uint8_t SIG_FOG_TABLE_MODE_COMMIT[] = {
    0x6A, 0x23, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,   /* push 0x23; mov ecx,[std3D_pDevice] */
    0x51, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,         /* push it; mov edx,[std3D_pDevice] */
    0x8B, 0x02, 0xFF, 0x50, 0x58                      /* mov eax,[edx]; call [eax+0x58] */
};
static const uint8_t MSK_FOG_TABLE_MODE_COMMIT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
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

/* World record fields, all proven at the level loader that fills them from the B3D header:
 *   +0x210 <- hdr+0x88 render flags, bit 0 = has fog   (default 0    at 0x0041CC3A)
 *   +0x214 <- hdr+0x8C packed fog colour               (default 0x408020 at 0x0041CC61)
 *   +0x218 <- hdr+0x90 fog start   (0x0041D0A3)        (default 10.0f at 0x0041CC47)
 *   +0x21C <- hdr+0x94 fog end     (0x0041D0B5)        (default 22.0f at 0x0041CC54)
 *   +0x014 <- hdr+0x854 draw distance (0x0041CFDB)     (default 24    at 0x0041CFC8) */
#define WORLD_VIEW_DISTANCE 0x014
#define WORLD_RENDER_FLAGS  0x210
#define WORLD_FOG_START     0x218
#define WORLD_FOG_END       0x21C
#define WORLD_FOG_BIT       0x001u
#define WORLD_PROBE_SIZE    0x220   /* everything above must be readable before anything is read */

/* The field of view the levels were authored at: rdCamera_new's own first argument, `push
 * 0x42700000` at 0x00417F79, and that call site (bapview_newView) is the only one in the image. */
#define FOG_REFERENCE_FOV_DEGREES 60.0f

/* Half the diagonal of the one-unit cell the circle test measures to. A cell whose CENTRE is just
 * outside the circle is dropped, and geometry inside it can sit up to this much nearer than the
 * centre, so the honest cut edge is the circle minus this. */
#define FOG_CELL_MARGIN_UNITS 0.70710678f

#define FOG_DEGREES_TO_RADIANS 0.017453292f
#define FOG_MAX_HALF_ANGLE      89.0f
#define FOG_MIN_END              2.0f   /* the engine's own floor on the draw distance */
#define FOG_FOLLOW_FLOOR         0.25f  /* a pathological cut edge must not put fog on the lens */
#define FOG_FOV_DEAD_BAND_DEGREES 1.0f  /* see fog_regime_follow_factor */

/* The remaining fraction of any gap after `settle_seconds`, and the gap below which the target is
 * simply taken. One hundredth of a world unit is far under the ramp's own quantisation: the ramp
 * turns the band into 256 alpha steps (0x004020DF multiplies by the 255.0 at [0x4A8020]), so on a
 * 14-unit band one step is 0.055 units. */
#define FOG_DAMP_REMAINDER       0.1f
#define FOG_DEAD_BAND_UNITS      0.01f
#define FOG_MAX_TRUSTED_SECONDS  0.125f

typedef void (__cdecl *apply_fog_fn_t)(void *level);

typedef struct fog_regime_state {
    bool                installed;
    fog_regime_config_t config;

    detour_t            apply_detour;
    void * volatile    *level_pointer;   /* [g_level], read out of bapdraw_setFrameState */
    const volatile float *frame_delta;   /* the engine's own seconds-per-frame */

    /* The level we were handed, and the band it was loaded with. EVERY target is computed from
     * `authored`; nothing here ever reads the live field back as an input, which is what stops a
     * repeated apply from squaring its own effect. */
    void               *level;
    void               *level_without_fog;   /* reported once, not once per effects restore */
    fog_regime_band_t   authored;
    fog_regime_band_t   written;         /* what we last put into the record */
    fog_regime_band_t   current;         /* the eased value; `written` mirrors it */
    int32_t             level_view_distance;

    float               horizontal_fov_degrees;
    float               reference_cut;
    float               live_cut;
    bool                cut_observed;

    bool                tick_active;
    bool                span_refused;    /* the "end came out below start" complaint, logged once */
} fog_regime_state_t;

static fog_regime_state_t fog_state;

/* ==============================================================================================
 * A, the arithmetic. No engine memory is touched below this line until section C.
 * ============================================================================================ */
float fog_regime_edge_limit(float horizontal_fov_degrees, float cut_units)
{
    float reach = cut_units - FOG_CELL_MARGIN_UNITS;
    float half_angle;

    if (!(reach > 0.0f)) {
        return 0.0f;
    }

    half_angle = 0.5f * horizontal_fov_degrees;
    if (!(half_angle > 0.0f)) {
        /* An unobservable or nonsensical camera falls back to the authored picture rather than to
         * an arbitrary number, so the limit stays the one the levels were built for. */
        half_angle = 0.5f * FOG_REFERENCE_FOV_DEGREES;
    }
    if (half_angle > FOG_MAX_HALF_ANGLE) {
        half_angle = FOG_MAX_HALF_ANGLE;
    }

    return reach * cosf(half_angle * FOG_DEGREES_TO_RADIANS);
}

float fog_regime_follow_factor(float horizontal_fov_degrees, float reference_cut, float live_cut)
{
    float now;
    float reference;
    float factor;

    /* A picture no wider than the authored one, with a cut edge no shorter, gets the level's band
     * untouched, and it gets it EXACTLY, tested on the angle rather than on the quotient. The
     * dead band is there because the field of view arrives as a measured number: rebuilding the
     * projection from a canvas of 639x479 pixels rather than 640x480 lands on 60.025 rather than
     * 60.000, and a coupling that answered 0.9998 to that would move every shipped number by a
     * hair for no reason anybody could see. */
    if (horizontal_fov_degrees <= FOG_REFERENCE_FOV_DEGREES + FOG_FOV_DEAD_BAND_DEGREES &&
        live_cut >= reference_cut) {
        return 1.0f;
    }

    /* Both sides go through the SAME function, so identical arguments give identical bits. */
    now = fog_regime_edge_limit(horizontal_fov_degrees, live_cut);
    reference = fog_regime_edge_limit(FOG_REFERENCE_FOV_DEGREES, reference_cut);

    if (!(reference > 0.0f) || !(now > 0.0f)) {
        return 1.0f;
    }

    factor = now / reference;
    if (factor > 1.0f) {
        return 1.0f;                       /* a NARROWER picture never pushes the fog out */
    }
    if (factor < FOG_FOLLOW_FLOOR) {
        return FOG_FOLLOW_FLOOR;
    }
    return factor;
}

void fog_regime_target_band(const fog_regime_config_t *config,
                            const fog_regime_band_t *authored,
                            float horizontal_fov_degrees,
                            float reference_cut,
                            float live_cut,
                            fog_regime_band_t *out)
{
    float end;
    float ratio;

    if (config == NULL || authored == NULL || out == NULL) {
        return;
    }

    /* A level that authored no fog must not gain any. The engine treats a band it cannot use by
     * disarming the ramp, and a disarmed ramp means a fog-coloured world, so the safe answer for
     * an unusable band is to hand it back untouched and let the level keep whatever it had. */
    if (!(authored->end > 0.0f) || !(authored->end > authored->start)) {
        *out = *authored;
        return;
    }

    end = authored->end;
    if (config->fog_scale > 1.0f) {
        end *= config->fog_scale;
    }
    if (config->follow_fov) {
        end *= fog_regime_follow_factor(horizontal_fov_degrees, reference_cut, live_cut);
    }
    if (config->inside_cut) {
        float limit = fog_regime_edge_limit(horizontal_fov_degrees, live_cut);

        if (limit > 0.0f && end > limit) {
            end = limit;
        }
    }
    if (end < FOG_MIN_END) {
        end = FOG_MIN_END;
    }

    /* ONE ratio for the whole band. See the header: moving the end alone crosses the authored
     * start in two shipped levels, and a negative span makes the engine paint the world in the fog
     * colour instead of showing less of it. */
    ratio = end / authored->end;
    out->end   = end;
    out->start = authored->start * ratio;
}

float fog_regime_ease(float current, float target, float seconds, float settle_seconds)
{
    float keep;
    float gap;

    if (!(settle_seconds > 0.0f)) {
        return target;                     /* easing switched off: the old instant step */
    }
    if (!(seconds > 0.0f)) {
        return current;                    /* no time passed, or a delta that cannot be believed */
    }
    if (seconds > FOG_MAX_TRUSTED_SECONDS) {
        seconds = FOG_MAX_TRUSTED_SECONDS; /* a loading hitch must not teleport the fog */
    }

    gap = target - current;
    if (gap > -FOG_DEAD_BAND_UNITS && gap < FOG_DEAD_BAND_UNITS) {
        return target;
    }

    /* The fraction of the gap left after `settle_seconds` is FOG_DAMP_REMAINDER however the
     * interval was cut into frames; that is the whole point of taking the exponent from the
     * engine's live delta instead of assuming a frame rate. */
    keep = powf(FOG_DAMP_REMAINDER, seconds / settle_seconds);
    return (1.0f - keep) * target + keep * current;
}

/* ==============================================================================================
 * B, the live cut edge and the live camera
 * ============================================================================================ */
static float clamp_cut(int32_t range)
{
    /* The world walk clamps the draw distance to [2,64] itself at 0x00404F33..0x00404F48, after
     * bapmat_viewDistance has returned. Both numbers are put through the same clamp here so the
     * ratio between them is the ratio the renderer actually sees. */
    if (range < 2)  { range = 2; }
    if (range > 64) { range = 64; }
    return (float)range;
}

void fog_regime_set_fov(float horizontal_fov_degrees)
{
    if (horizontal_fov_degrees > 1.0f && horizontal_fov_degrees < 180.0f) {
        fog_state.horizontal_fov_degrees = horizontal_fov_degrees;
    }
}

void fog_regime_note_cut(int32_t reference_range, int32_t effective_range)
{
    fog_state.reference_cut = clamp_cut(reference_range);
    fog_state.live_cut      = clamp_cut(effective_range);
    fog_state.cut_observed  = true;
}

/* The cut edge to work from. Without an observation, the draw-distance detour declined, or the
 * world walk has not run yet, the level's own draw distance is the honest stand-in, and it makes
 * the follow factor exactly 1.0 rather than an invented number. */
static void current_cut(float *reference, float *live)
{
    if (fog_state.cut_observed) {
        *reference = fog_state.reference_cut;
        *live      = fog_state.live_cut;
        return;
    }
    *reference = clamp_cut(fog_state.level_view_distance);
    *live      = *reference;
}

static void target_now(fog_regime_band_t *out)
{
    float reference;
    float live;

    current_cut(&reference, &live);
    fog_regime_target_band(&fog_state.config, &fog_state.authored,
                           fog_state.horizontal_fov_degrees, reference, live, out);
}

/* ==============================================================================================
 * C, the level record
 * ============================================================================================ */
static void write_band(const fog_regime_band_t *band)
{
    float *fields;

    if (fog_state.level == NULL) {
        return;
    }

    /* The one invariant the renderer cannot survive being wrong about: bapdraw_setFrameState
     * disarms the ramp on a negative span and the emitter then writes a zero specular, which is
     * FULLY FOGGED, not "no fog". Refusing here leaves the level exactly as authored. */
    if (!(band->end > band->start)) {
        if (!fog_state.span_refused) {
            fog_state.span_refused = true;
            log_warning("refused a fog band of %.2f..%.2f, the engine disarms its own ramp on a "
                        "non-positive span and then paints every world vertex in the fog colour. "
                        "The level keeps the band it was authored with.",
                        (double)band->start, (double)band->end);
        }
        return;
    }

    fields = (float *)((char *)fog_state.level + WORLD_FOG_START);
    fields[0] = band->start;               /* +0x218 */
    fields[1] = band->end;                 /* +0x21C */
    fog_state.written = *band;
}

/* Is this the world we were last told about? The pointer ALONE is not an identity: the record is
 * reallocated on every level load and an allocator hands out the same address more often than not,
 * so a new level could inherit the previous level's remembered band. The second and third
 * criteria are what close that: if the record no longer holds exactly what WE last wrote, or
 * carries another draw distance, the loader has been through it.
 *
 * The reads need no range check of their own. They only happen once the pointer matches one that
 * remember_level already validated, and the function this hook sits on reads the very same fields
 * three instructions later, so a record these would fault on is one the engine faults on too. */
static bool is_the_same_level(const void *level)
{
    const float   *fog = (const float *)((const char *)level + WORLD_FOG_START);
    const int32_t *view_distance =
        (const int32_t *)((const char *)level + WORLD_VIEW_DISTANCE);

    return fog_state.level == level &&
           fog[0] == fog_state.written.start &&
           fog[1] == fog_state.written.end &&
           *view_distance == fog_state.level_view_distance;
}

static bool remember_level(void *level)
{
    const float   *fog;
    const int32_t *view_distance;
    const uint32_t *flags;

    /* A level with no fog reaches this function again on every effects fog restore, so both the
     * page walk below and its log line have to happen once rather than once per restore. */
    if (level == fog_state.level_without_fog) {
        return false;
    }

    if (!memory_is_readable_range((uintptr_t)level, WORLD_PROBE_SIZE)) {
        /* Forget the previous level too: the per-frame tick must not keep writing through a
         * pointer we have just decided we do not trust. */
        fog_state.level = NULL;
        log_warning("the world record at %08X is not readable to +0x220, the fog is left alone",
                    (unsigned)(uintptr_t)level);
        return false;
    }

    flags = (const uint32_t *)((const char *)level + WORLD_RENDER_FLAGS);
    if ((*flags & WORLD_FOG_BIT) == 0) {
        /* Fog switched off for this level. Its band is never read, and writing one would be the
         * one way to give a level fog it does not want. */
        fog_state.level = NULL;
        fog_state.level_without_fog = level;
        log_info("this level has fog disabled (world+0x210 bit 0 clear), nothing is written");
        return false;
    }

    fog           = (const float *)((const char *)level + WORLD_FOG_START);
    view_distance = (const int32_t *)((const char *)level + WORLD_VIEW_DISTANCE);

    fog_state.level_without_fog   = NULL;
    fog_state.level               = level;
    fog_state.authored.start      = fog[0];
    fog_state.authored.end        = fog[1];
    fog_state.level_view_distance = *view_distance;
    fog_state.span_refused        = false;
    return true;
}

static void __cdecl hook_apply_fog(void *level)
{
    apply_fog_fn_t original = (apply_fog_fn_t)fog_state.apply_detour.original;

    if (level == NULL) {
        original(level);
        return;
    }

    if (is_the_same_level(level)) {
        /* The effects fog restore came through, or the level was re-applied: this function
         * re-programs the device from these two fields, so they have to hold ours rather than the
         * authored pair. The eased value is kept; nothing about the level changed. */
        write_band(&fog_state.current);
    } else if (remember_level(level)) {
        /* A fresh level SNAPS. There is nothing on screen to ease away from, and easing here
         * would show the authored band for a moment and then walk it in. */
        target_now(&fog_state.current);
        write_band(&fog_state.current);

        log_info("level fog %.1f..%.1f -> %.1f..%.1f (draw distance %d, %.1f degrees)",
                 (double)fog_state.authored.start, (double)fog_state.authored.end,
                 (double)fog_state.current.start, (double)fog_state.current.end,
                 (int)fog_state.level_view_distance,
                 (double)fog_state.horizontal_fov_degrees);
    }

    original(level);
}

void fog_regime_on_frame(void)
{
    fog_regime_band_t target;
    float             seconds;

    if (!fog_state.tick_active || fog_state.level == NULL) {
        return;
    }
    /* The world is freed and replaced by the level load. Until the fog detour hands us the new
     * one, the remembered pointer is not ours to write through. */
    if (*fog_state.level_pointer != fog_state.level) {
        return;
    }
    /* And the record still has to hold what we last put there. Two things ride on this. A level
     * loaded into the address the previous one had would otherwise be ticked with the previous
     * level's remembered band for however many frames pass before the fog apply reaches us. And
     * anything that deliberately writes this band; nothing in the shipped image does, but a
     * future patch might, gets to keep its value instead of being overwritten sixty times a
     * second by ours. */
    if (!is_the_same_level(fog_state.level)) {
        return;
    }

    target_now(&target);

    seconds = (fog_state.frame_delta != NULL) ? *fog_state.frame_delta : 0.0f;
    fog_state.current.start = fog_regime_ease(fog_state.current.start, target.start, seconds,
                                              fog_state.config.settle_seconds);
    fog_state.current.end   = fog_regime_ease(fog_state.current.end, target.end, seconds,
                                              fog_state.config.settle_seconds);

    if (fog_state.current.start == fog_state.written.start &&
        fog_state.current.end == fog_state.written.end) {
        return;                            /* settled: no write, no cache line touched */
    }
    write_band(&fog_state.current);
}

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

    if (!fog_state.config.vertex_fog) {
        log_info("VertexFog=0, the fog regime is left exactly as the device asks for it");
        return;
    }
    if (query == 0 || applier == 0 || commit == 0) {
        log_warning("the fog regime is NOT changed: cap %08X, state machine %08X, commit %08X - "
                    "all three have to resolve, because half of this change would either do "
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
        log_warning("FOGTABLEMODE is not D3DFOG_LINEAR at %08X / %08X - the fog regime is "
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
             fog_state.config.inside_cut ? ", then capped to that limit" : " (cap OFF)",
             (double)fog_state.config.fog_scale);
}

void fog_regime_install(const fog_regime_config_t *config)
{
    if (fog_state.installed || config == NULL) {
        return;
    }

    fog_state.config = *config;
    fog_state.horizontal_fov_degrees = FOG_REFERENCE_FOV_DEGREES;
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
