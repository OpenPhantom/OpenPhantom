/* object_interpolation.c: see object_interpolation.h. */
#include "object_interpolation.h"

#include "object_track.h"
#include "substep_counter.h"

#include "common/logging.h"
#include "common/numeric.h"
#include "common/patch.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- 0x0041125B  bapobj_drawAll, the position blend ------------------------------------------ *
 *   8B 55 F8 / 8B 45 F8        edx = eax = obj            (from [ebp-8])
 *   D9 42 18                   fld  [obj+0x18]            cur.x
 *   D8 60 54                   fsub [obj+0x54]            - prev.x
 *   D9 9D 60 FB FF FF          fstp [ebp-0x4A0]           d.x
 *   ... the same for y into [ebp-0x49C] and z into [ebp-0x498] ...
 *   D9 85 60 FB FF FF          fld  [ebp-0x4A0]           d.x
 *   D8 8D E0 FB FF FF          fmul [ebp-0x420]           * the substep alpha
 *   D8 42 54                   fadd [obj+0x54]            + prev.x
 *   D9 9D 54 FB FF FF          fstp [ebp-0x4AC]           drawn.x
 *   ... the same for y into [ebp-0x4A8] and z into [ebp-0x4A4] ...
 *
 * So the engine computes prev + (cur - prev) * alpha, per object, per frame. 126 bytes, and the
 * region carries no absolute address at all: every operand is relative to the frame pointer or to
 * the object. So the pattern needs no mask, and it is the whole region rather than a prefix of it,
 * which means every byte being replaced is checked before anything is written.
 *
 * The replacement is a call, in the shape draw_interpolation.c already uses for the euler a few
 * instructions further on. The contract, checked against the bytes:
 *
 *   * eax, ecx and edx are dead across the region. Read off the disassembly of the rest of
 *     bapobj_drawAll rather than assumed: 0x4112D9 reloads edx from [ebp-8], and eax and ecx are
 *     both written before anything reads them;
 *   * the x87 stack is empty on both sides. The region's last operation is an fstp, the next x87
 *     op is the euler's own fld, and the tag word was sampled at entry across forty thousand calls
 *     in play without once finding a register in use;
 *   * six locals are written and all six are filled by the replacement. The three deltas are
 *     never read again after the region, by the same disassembly, so they are filled with the
 *     value the engine's own instructions put there and nothing of this file's.
 *
 * The C inside the call may not classify a float through the CRT, and it may not copy one
 * through the x87 unit to get it there. With `isfinite` in this path a lightsaber
 * blade was drawn out of the player's hand at many times its length, and it took twenty runs to
 * corner because it did not depend on a single value this file computes: the same call with the
 * engine's own arithmetic drawn had it, and the same call with those classifications done on the
 * bit pattern did not. The instruction-level cause was not established; the assembly reproduction
 * of the same operations did not show it, and single negatives on an artefact that needs a
 * trigger are worth less than they look. What is established is the rule, so every finiteness
 * test here and in object_track.c is two integer instructions, and the only x87 instruction the
 * whole path executes is the one that receives object_track_weight's return.
 *
 * Uniqueness was measured in the retail image: one match at 126 bytes, and still one at 32. */
static const uint8_t SIG_DRAW_POSITION[] = {
    0x8B, 0x55, 0xF8, 0x8B, 0x45, 0xF8, 0xD9, 0x42, 0x18, 0xD8, 0x60, 0x54,
    0xD9, 0x9D, 0x60, 0xFB, 0xFF, 0xFF, 0x8B, 0x4D, 0xF8, 0x8B, 0x55, 0xF8,
    0xD9, 0x41, 0x1C, 0xD8, 0x62, 0x58, 0xD9, 0x9D, 0x64, 0xFB, 0xFF, 0xFF,
    0x8B, 0x45, 0xF8, 0x8B, 0x4D, 0xF8, 0xD9, 0x40, 0x20, 0xD8, 0x61, 0x5C,
    0xD9, 0x9D, 0x68, 0xFB, 0xFF, 0xFF, 0xD9, 0x85, 0x60, 0xFB, 0xFF, 0xFF,
    0xD8, 0x8D, 0xE0, 0xFB, 0xFF, 0xFF, 0x8B, 0x55, 0xF8, 0xD8, 0x42, 0x54,
    0xD9, 0x9D, 0x54, 0xFB, 0xFF, 0xFF, 0xD9, 0x85, 0x64, 0xFB, 0xFF, 0xFF,
    0xD8, 0x8D, 0xE0, 0xFB, 0xFF, 0xFF, 0x8B, 0x45, 0xF8, 0xD8, 0x40, 0x58,
    0xD9, 0x9D, 0x58, 0xFB, 0xFF, 0xFF, 0xD9, 0x85, 0x68, 0xFB, 0xFF, 0xFF,
    0xD8, 0x8D, 0xE0, 0xFB, 0xFF, 0xFF, 0x8B, 0x4D, 0xF8, 0xD8, 0x41, 0x5C,
    0xD9, 0x9D, 0x5C, 0xFB, 0xFF, 0xFF
};
#define POSITION_PATCH_LENGTH (sizeof SIG_DRAW_POSITION)

/* bapObj fields the replacement reads, and never writes. */
#define OBJECT_POSITION          0x18
#define OBJECT_PREVIOUS_POSITION 0x54

/* Frame-pointer-relative locals inside bapobj_drawAll. */
#define LOCAL_SUBSTEP_ALPHA 0x420
#define LOCAL_DELTA_X       0x4A0
#define LOCAL_DELTA_Y       0x49C
#define LOCAL_DELTA_Z       0x498
#define LOCAL_DRAWN_X       0x4AC
#define LOCAL_DRAWN_Y       0x4A8
#define LOCAL_DRAWN_Z       0x4A4

static float object_travel_limit;

/* How often the travel limit turns a rider away, and the furthest step it saw.
 *
 * Never counted before, so nobody knew. It matters because of what the camera does: the engine
 * feeds bapview_setCamTarget the player record's own position once per substep and interpolates
 * that pair on the same alpha, so the camera aims at the player's simulation position. A refused
 * body is drawn at its current position instead, a whole step ahead of where the camera is
 * pointing, and the difference shows as the body moving against the frame whenever the camera is
 * following it.
 *
 * The limit is 2.0 units per step, chosen against a carried player moving 0.045. The mover guard
 * allows a platform 64 units in the same step, so a rider on a fast platform can exceed 2.0 every
 * step and be refused every step. */
static uint32_t refused_by_limit;
static uint32_t blends_seen;
static float    worst_travel_squared;   /* squared, so the root is taken once at report time */

/* 1 uses the remembered previous position, 2 reproduces the engine's own arithmetic, 3 uses
 * the engine's and reports where the remembered one would have disagreed. */
static int   object_mode;

/* Mode 3 only. Reporting each disagreement spent the whole budget on the first two objects in
 * the opening seconds, which said only that the calm case is calm. What decides this is the
 * WORST one, so the worst is kept and summarised every so often instead. */
#define DISAGREEMENT_SUMMARY_FRAMES 200u
static unsigned  disagreements_seen;
static float     worst_apart_squared;
static uintptr_t worst_object;
static float     worst_mine[3];
static float     worst_engine[3];
static float     worst_current[3];
static bool      worst_not_a_number;
static unsigned  summary_countdown;
static unsigned  limit_countdown;
static bool      limit_reported;
/* Objects handed a position that was not a number, which retail draws as nothing and so does
 * this. Counted apart from the limit because they are a different thing: not a teleport but an
 * object the engine has parked, and a session where the count is large is a session where the
 * pool is handing out records with their previous position unwritten. */
static uint32_t  not_a_number_seen;
static bool      nan_reported;

static bool finite3(const float *v)
{
    return numeric_is_finite(v[0]) && numeric_is_finite(v[1]) && numeric_is_finite(v[2]);
}

/* Reports where the remembered previous position differs from the engine's by enough to be
 * seen, and says whether either is a number at all. A value that is not finite is the case
 * worth naming loudest: the travel limit compares distances, and every comparison against a
 * NaN is false, so the guard that should refuse a bad jump lets it straight through. */
static void report_disagreement(const char *object, const float *mine, const float *engine,
                                const float *current)
{
    float dx = mine[0] - engine[0];
    float dy = mine[1] - engine[1];
    float dz = mine[2] - engine[2];
    float apart_squared = dx * dx + dy * dy + dz * dz;
    bool  mine_finite = finite3(mine);

    if (mine_finite && apart_squared < 0.0001f) {
        return;
    }
    ++disagreements_seen;

    /* A value that is not a number wins outright, whatever any distance says: it fails every
     * comparison, so a squared distance involving one is not a ranking at all. */
    if (!mine_finite) {
        worst_not_a_number = true;
    } else if (!(apart_squared > worst_apart_squared)) {
        return;
    }

    worst_apart_squared = apart_squared;
    worst_object = (uintptr_t)object;
    memcpy(worst_mine, mine, sizeof worst_mine);
    memcpy(worst_engine, engine, sizeof worst_engine);
    memcpy(worst_current, current, sizeof worst_current);
}

/* Fills the SAME six stack slots the original filled, reading the SAME alpha, from a previous
 * position the carry cannot flatten. An object the tracker has no answer for falls back to the
 * engine's own previous position, which reproduces the original arithmetic exactly. */
static void __cdecl hook_draw_position(char *object, char *frame_pointer)
{
    const float *current = (const float *)(object + OBJECT_POSITION);
    const float *engine  = (const float *)(object + OBJECT_PREVIOUS_POSITION);
    float        alpha   = *(const float *)(frame_pointer - LOCAL_SUBSTEP_ALPHA);
    float        previous[3];
    float        drawn[3];
    uint32_t     step;
    /* The engine's own pair is exactly one step apart, so a fallback leaves the weight equal to
     * the alpha and the limit unscaled, which is the replaced arithmetic exactly. The tracker
     * does not write through this on the paths where it has no answer, so it is set here
     * rather than there. */
    uint32_t     gap = 1u;

    if (!substep_counter_read(&step)) {
        /* Unreachable: the install refuses to place the patch without the counter. The guard
         * stays because that is a property of the install path rather than of this function, and
         * a stamp that never changes would leave every object on the engine's own pair, which is
         * the behaviour that shipped. */
        step = 0u;
    }

    if (object_mode == 3) {
        /* The engine's own answer is what gets drawn, so the game stays playable while this
         * says what the remembered one WOULD have been. */
        float    mine[3];
        uint32_t mine_gap = 1u;

        if (object_track_sample((uintptr_t)object, step, current, mine, &mine_gap)) {
            report_disagreement(object, mine, engine, current);
        }
        previous[0] = engine[0];
        previous[1] = engine[1];
        previous[2] = engine[2];
    } else if (object_mode != 1 ||
               !object_track_sample((uintptr_t)object, step, current, previous, &gap) ||
               !finite3(previous)) {
        /* The third test: a remembered previous that is not a number is one this file copied
         * from a current position that was, and the engine's own may be perfectly good by now.
         * Falling back keeps this from hiding an object retail shows; the blend below is what
         * keeps it from showing one retail hides. */
        previous[0] = engine[0];
        previous[1] = engine[1];
        previous[2] = engine[2];
        gap = 1u;
    }

    /* The limit is per step, so two samples several steps apart are allowed proportionally more
     * travel between them; without that a frame slow enough to span two steps reads an ordinary
     * walk as a teleport and stops smoothing exactly where the frame rate needs it most. */
    if (!object_track_blend(previous, current, object_track_weight(alpha, gap),
                            object_travel_limit * (float)gap, drawn)) {
        if (!finite3(previous) || !finite3(current) || !numeric_is_finite(alpha)) {
            /* Not a teleport: an object retail draws as nothing, and so does this. */
            ++not_a_number_seen;
        } else {
            float dx = current[0] - previous[0];
            float dy = current[1] - previous[1];
            float dz = current[2] - previous[2];
            float travel_squared = dx * dx + dy * dy + dz * dz;

            ++refused_by_limit;
            if (travel_squared > worst_travel_squared) {
                worst_travel_squared = travel_squared;
            }
        }
    }
    ++blends_seen;

    /* The deltas are the ENGINE'S, against obj+0x54, exactly as the replaced bytes computed them.
     * The disassembly of the rest of bapobj_drawAll shows nothing reading these three slots after
     * the region, so they are scratch the original left behind; they are filled with the value
     * the original left in them, because a slot this code cannot prove dead is filled with what
     * retail put there rather than with something of ours. */
    *(float *)(frame_pointer - LOCAL_DELTA_X) = current[0] - engine[0];
    *(float *)(frame_pointer - LOCAL_DELTA_Y) = current[1] - engine[1];
    *(float *)(frame_pointer - LOCAL_DELTA_Z) = current[2] - engine[2];
    *(float *)(frame_pointer - LOCAL_DRAWN_X) = drawn[0];
    *(float *)(frame_pointer - LOCAL_DRAWN_Y) = drawn[1];
    *(float *)(frame_pointer - LOCAL_DRAWN_Z) = drawn[2];
}

void object_interpolation_frame(void)
{
    object_track_frame();

    /* Counted in every mode, because a refusal is invisible on screen except as the body drifting
     * against a camera that is following it. Said ONCE, on the first window that sees one: it is
     * a setting to change, not a running measurement, and a line every two hundred frames is a
     * line every second and a half at a modern frame rate. Silence afterwards means nothing has
     * been refused, or that the first report already named the number to raise. */
    if (!limit_reported && ++limit_countdown >= DISAGREEMENT_SUMMARY_FRAMES) {
        limit_countdown = 0;
        if (refused_by_limit != 0) {
            limit_reported = true;
            log_warning("rider: the travel limit refused %u of %u blends over %u frames, worst "
                        "step %.2f units against a limit of %.2f. A refused body is drawn at its "
                        "current position while the camera keeps aiming at the interpolated one, "
                        "so it moves against the frame. Raise RiderTravelLimitPerStep past the "
                        "worst step if that is an ordinary ride rather than a teleport. Reported "
                        "once.",
                        (unsigned)refused_by_limit, (unsigned)blends_seen,
                        (unsigned)DISAGREEMENT_SUMMARY_FRAMES,
                        sqrt((double)worst_travel_squared), (double)object_travel_limit);
        }
        if (not_a_number_seen != 0 && !nan_reported) {
            nan_reported = true;
            log_info("rider: %u of %u blends over %u frames had a position that was not a "
                     "number, and were drawn as retail draws them, which is not at all. That is "
                     "an object whose previous position was never written or was parked on "
                     "purpose; the engine hides it and so does this. Reported once.",
                     (unsigned)not_a_number_seen, (unsigned)blends_seen,
                     (unsigned)DISAGREEMENT_SUMMARY_FRAMES);
        }
        refused_by_limit     = 0;
        blends_seen          = 0;
        worst_travel_squared = 0.0f;
        not_a_number_seen    = 0;
    }

    if (object_mode != 3) {
        return;
    }
    if (++summary_countdown < DISAGREEMENT_SUMMARY_FRAMES) {
        return;
    }
    summary_countdown = 0;
    if (disagreements_seen == 0) {
        log_info("rider: no disagreement at all over the last %u frames",
                 (unsigned)DISAGREEMENT_SUMMARY_FRAMES);
        return;
    }
    log_info("rider: %u disagreements over %u frames, worst %.2f at obj %08X%s: "
             "mine(%.3f %.3f %.3f) engine(%.3f %.3f %.3f) cur(%.3f %.3f %.3f)",
             disagreements_seen, (unsigned)DISAGREEMENT_SUMMARY_FRAMES,
             sqrt((double)worst_apart_squared), (unsigned)worst_object,
             worst_not_a_number ? " and something was NOT A NUMBER" : "",
             (double)worst_mine[0], (double)worst_mine[1], (double)worst_mine[2],
             (double)worst_engine[0], (double)worst_engine[1], (double)worst_engine[2],
             (double)worst_current[0], (double)worst_current[1], (double)worst_current[2]);
    disagreements_seen = 0;
    worst_apart_squared = 0.0f;
    worst_not_a_number = false;
}

void object_interpolation_install(int mode, float travel_limit)
{
    uint8_t   replacement[POSITION_PATCH_LENGTH];
    uintptr_t site;
    uint32_t  call_displacement;
    size_t    index;

    if (mode <= 0) {
        log_info("InterpolateRiders=0, a character carried by a platform keeps stepping at the "
                 "simulation rate");
        return;
    }

    object_mode = mode;
    object_travel_limit = (mode == 1) ? travel_limit : 0.0f;
    object_track_reset();

    /* Before the patch, not after: with no way to tell one simulation step from the next there is
     * nothing to remember a previous position against, and replacing the engine's blend with one
     * that cannot work is worse than leaving it alone. Modes 2 and 3 read the counter too, mode 3
     * because its whole purpose is to compare against what mode 1 would have drawn. */
    if (!substep_counter_resolve()) {
        log_warning("without the simulation step counter a carried character keeps stepping at "
                    "the simulation rate. Nothing else is affected: the engine's own blend is "
                    "left exactly as it shipped");
        return;
    }

    site = signature_find_unique(SIG_DRAW_POSITION, NULL, sizeof SIG_DRAW_POSITION);
    if (site == 0) {
        log_warning("the object position blend in bapobj_drawAll did not resolve, so a carried "
                    "character keeps stepping at the simulation rate. Nothing else is affected: "
                    "the engine's own blend is left exactly as it shipped");
        return;
    }

    /*  55                push ebp
     *  8B 45 F8          mov  eax,[ebp-8]           ; obj
     *  50                push eax
     *  E8 <rel32>        call hook_draw_position    ; __cdecl(obj, ebp)
     *  83 C4 08          add  esp,8
     *  90 ...            pad to the full region
     *
     * The three registers the region leaves holding obj are not put back. They were, for a while,
     * on the theory that the code after the region might read one; the disassembly of that code
     * shows all three written before use, so the reloads bought nothing and are gone. */
    for (index = 0; index < POSITION_PATCH_LENGTH; ++index) {
        replacement[index] = 0x90;
    }
    replacement[0]  = 0x55;
    replacement[1]  = 0x8B; replacement[2] = 0x45; replacement[3] = 0xF8;
    replacement[4]  = 0x50;
    replacement[5]  = 0xE8;
    call_displacement = (uint32_t)((uintptr_t)hook_draw_position - (site + 5 + 5));
    replacement[6]  = (uint8_t)(call_displacement         & 0xFFu);
    replacement[7]  = (uint8_t)((call_displacement >>  8) & 0xFFu);
    replacement[8]  = (uint8_t)((call_displacement >> 16) & 0xFFu);
    replacement[9]  = (uint8_t)((call_displacement >> 24) & 0xFFu);
    replacement[10] = 0x83; replacement[11] = 0xC4; replacement[12] = 0x08;

    if (patch_write_bytes(site, replacement, POSITION_PATCH_LENGTH) != PATCH_RESULT_OK) {
        log_error("the object position blend at %08X could not be written, so nothing is changed",
                  (unsigned)site);
        return;
    }

    if (object_mode == 3) {
        log_info("InterpolateRiders=3: the blend at %08X is the engine's own and the game is "
                 "untouched. Every %u frames this reports how far the remembered previous "
                 "position was from the object's own at its WORST. That is what mode 1 fed the "
                 "blend when characters flew", (unsigned)site,
                 (unsigned)DISAGREEMENT_SUMMARY_FRAMES);
        return;
    }

    if (object_mode == 2) {
        log_info("InterpolateRiders=2: the position blend at %08X is REPLACED but the arithmetic "
                 "is the engine's own, reading the object's own previous position. This is a "
                 "measurement, not a feature: the game should look exactly as it does with this "
                 "switched off, and if it does not then the fault is the patch rather than "
                 "anything this DLL remembers", (unsigned)site);
        return;
    }

    log_info("carried characters are now drawn between simulation steps (%u bytes at %08X, travel "
             "limit %.1f). The previous position comes from this DLL rather than from the object, "
             "because a platform's carry overwrites the object's own with its current one inside "
             "the same step and leaves the engine's blend nothing to work with. obj->pos and "
             "obj->prevPos are read and never written; only what is DRAWN changes, and an object "
             "this DLL has no previous position for falls back to the engine's own",
             (unsigned)POSITION_PATCH_LENGTH, (unsigned)site, (double)travel_limit);
}
