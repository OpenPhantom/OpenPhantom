/* diag_x87.c: see diag_x87.h. */
#include "diag_x87.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The seven sites, each a function entry, so every pattern starts with the prologue the detour
 * overwrites and the resolver looks past it once another DLL has placed a hook. Absolute
 * operands are masked. */

/* --- fx_thingDraw 0x00438E78: halos when asked, the shield when the thing carries one -------- *
 *   55 8B EC 83 7D 14 00 74 0C 8B 45 08 50 E8 <rel32> 83 C4 04 */
static const uint8_t SIG_THING_DRAW_FX[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x14, 0x00, 0x74, 0x0C, 0x8B, 0x45, 0x08, 0x50, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04
};
static const uint8_t MSK_THING_DRAW_FX[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
#define THING_DRAW_FX_PROLOGUE 7u

/* --- halo_drawForThing 0x00439A54 --------------------------------------------------------- *
 *   55 8B EC 51 83 7D 08 00 75 02 EB 52 8B 45 08 8B 08 83 E1 10 85 C9 */
static const uint8_t SIG_HALO_DRAW[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x75, 0x02, 0xEB, 0x52, 0x8B, 0x45, 0x08,
    0x8B, 0x08, 0x83, 0xE1, 0x10, 0x85, 0xC9
};
#define HALO_DRAW_PROLOGUE 8u

/* --- fxprint_projectFloor 0x0043A5FF: the shadow decal's projector ------------------------- *
 *   55 8B EC 83 EC 5C 83 3D <cell> 00 75 05 E9 1F 02 00 00 83 3D */
static const uint8_t SIG_PROJECT_FLOOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x5C, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x05,
    0xE9, 0x1F, 0x02, 0x00, 0x00, 0x83, 0x3D
};
static const uint8_t MSK_PROJECT_FLOOR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PROJECT_FLOOR_PROLOGUE 6u

/* --- bapdraw_flushQueue 0x00402155: the deferred queue drawn, between asset groups --------- *
 *   55 8B EC 81 EC 60 08 00 00 83 3D <cell> 00 7F 05 E9 3D 04 00 00 */
static const uint8_t SIG_FLUSH_QUEUE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x60, 0x08, 0x00, 0x00, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7F, 0x05, 0xE9, 0x3D, 0x04, 0x00, 0x00
};
static const uint8_t MSK_FLUSH_QUEUE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define FLUSH_QUEUE_PROLOGUE 9u

/* --- bapthing_dispatch 0x00417930: the model draw, rdThing_Draw inside it ------------------ *
 *   55 8B EC 51 83 3D <cell> 00 75 04 33 C0 EB 4E 8B 45 08 8B 08 */
static const uint8_t SIG_THING_DISPATCH[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x04, 0x33, 0xC0,
    0xEB, 0x4E, 0x8B, 0x45, 0x08, 0x8B, 0x08
};
static const uint8_t MSK_THING_DISPATCH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define THING_DISPATCH_PROLOGUE 11u

/* --- rdPuppet_updateTrack 0x00483D20: one track advanced; returns the frames advanced ------- *
 *   53 8B 5C 24 08 56 57 83 3B 00 74 0A D9 05 <zero> 5F 5E 5B C3
 * Frame pointer omitted, a float return in ST(0), four cdecl arguments. */
static const uint8_t SIG_UPDATE_TRACK[] = {
    0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x57, 0x83, 0x3B, 0x00, 0x74, 0x0A, 0xD9, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x5F, 0x5E, 0x5B, 0xC3
};
static const uint8_t MSK_UPDATE_TRACK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
#define UPDATE_TRACK_PROLOGUE 7u

/* --- bapobj_dispatchClipEvents 0x00411897: the events a track advance raised --------------- *
 *   55 8B EC 83 EC 2C 8B 45 08 8B 88 9C 00 00 00 8B 51 18 89 55 E0 */
static const uint8_t SIG_CLIP_EVENTS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0x9C, 0x00, 0x00, 0x00,
    0x8B, 0x51, 0x18, 0x89, 0x55, 0xE0
};
#define CLIP_EVENTS_PROLOGUE 6u

_Static_assert(sizeof SIG_THING_DRAW_FX == sizeof MSK_THING_DRAW_FX, "fx mask length");
_Static_assert(sizeof SIG_PROJECT_FLOOR == sizeof MSK_PROJECT_FLOOR, "projector mask length");
_Static_assert(sizeof SIG_FLUSH_QUEUE == sizeof MSK_FLUSH_QUEUE, "flush mask length");
_Static_assert(sizeof SIG_THING_DISPATCH == sizeof MSK_THING_DISPATCH, "dispatch mask length");
_Static_assert(sizeof SIG_UPDATE_TRACK == sizeof MSK_UPDATE_TRACK, "track mask length");

enum {
    SITE_THING_DRAW_FX,
    SITE_HALO_DRAW,
    SITE_PROJECT_FLOOR,
    SITE_FLUSH_QUEUE,
    SITE_THING_DISPATCH,
    SITE_UPDATE_TRACK,
    SITE_CLIP_EVENTS,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("fx_thingDraw", SIG_THING_DRAW_FX, MSK_THING_DRAW_FX,
                                  THING_DRAW_FX_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("halo_drawForThing", SIG_HALO_DRAW, HALO_DRAW_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("fxprint_projectFloor", SIG_PROJECT_FLOOR, MSK_PROJECT_FLOOR,
                                  PROJECT_FLOOR_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapdraw_flushQueue", SIG_FLUSH_QUEUE, MSK_FLUSH_QUEUE,
                                  FLUSH_QUEUE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("bapthing_dispatch", SIG_THING_DISPATCH, MSK_THING_DISPATCH,
                                  THING_DISPATCH_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("rdPuppet_updateTrack", SIG_UPDATE_TRACK, MSK_UPDATE_TRACK,
                                  UPDATE_TRACK_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("bapobj_dispatchClipEvents", SIG_CLIP_EVENTS, CLIP_EVENTS_PROLOGUE)
};

typedef void    (__cdecl *thing_draw_fx_fn_t)(void *obj, const void *pose, int32_t unused,
                                              int32_t attach_halos);
typedef void    (__cdecl *halo_draw_fn_t)(void *obj);
typedef int32_t (__cdecl *project_floor_fn_t)(const float *pos, float radius, float angle,
                                              int32_t sprite, float life, int32_t owner,
                                              float alpha, int32_t require, int32_t exclude);
typedef void    (__cdecl *flush_queue_fn_t)(void);
typedef int32_t (__cdecl *thing_dispatch_fn_t)(void *thing, float *matrix);
typedef float   (__cdecl *update_track_fn_t)(void *puppet, float dt, uint32_t slot,
                                             float frames);
typedef void    (__cdecl *clip_events_fn_t)(void *obj, int32_t slot, float advanced);

#define X87_TOP_MASK   0x3800u
#define X87_TOP_SHIFT  11
#define DETAIL_LINES   24u      /* individual moves written before only the counts are kept */
#define SUMMARY_FRAMES 300u

typedef struct site_state {
    detour_t detour;
    uint32_t calls;
    uint32_t moved;         /* calls across which TOP changed */
    uint32_t popped;        /* of those, TOP higher on the way out: a net pop */
} site_state_t;

static struct {
    site_state_t site[SITE_COUNT];
    uint32_t     detail_lines;
    uint32_t     frames;
    uint16_t     last_end_status;
    bool         end_sampled;
} x87;

static uint16_t status_word(void)
{
    uint16_t status = 0;

    __asm {
        fnstsw status
    }
    return status;
}

static unsigned top_of(uint16_t status)
{
    return (unsigned)((status & X87_TOP_MASK) >> X87_TOP_SHIFT);
}

/* Counts a call and names it when the pointer moved across it. `before` and `after` are the
 * status words either side of the original. */
static void account(int index, uint16_t before, uint16_t after)
{
    site_state_t *s = &x87.site[index];
    unsigned      in = top_of(before);
    unsigned      out = top_of(after);

    ++s->calls;
    if (in == out) {
        return;
    }
    ++s->moved;
    if (((out - in) & 7u) < 4u) {
        ++s->popped;        /* TOP went up, mod 8, by less than half the ring: a net pop */
    }
    if (x87.detail_lines < DETAIL_LINES) {
        ++x87.detail_lines;
        diag_log_write("x87  %s entered with status %04X (TOP %u) and left with %04X (TOP %u): "
                       "the stack pointer moved across the call, a net %s",
                       sites[index].name, (unsigned)before, in, (unsigned)after, out,
                       ((out - in) & 7u) < 4u ? "pop" : "push");
    }
}

static void __cdecl hook_thing_draw_fx(void *obj, const void *pose, int32_t unused,
                                       int32_t attach_halos)
{
    uint16_t before = status_word();

    ((thing_draw_fx_fn_t)x87.site[SITE_THING_DRAW_FX].detour.original)(obj, pose, unused,
                                                                        attach_halos);
    account(SITE_THING_DRAW_FX, before, status_word());
}

static void __cdecl hook_halo_draw(void *obj)
{
    uint16_t before = status_word();

    ((halo_draw_fn_t)x87.site[SITE_HALO_DRAW].detour.original)(obj);
    account(SITE_HALO_DRAW, before, status_word());
}

static int32_t __cdecl hook_project_floor(const float *pos, float radius, float angle,
                                          int32_t sprite, float life, int32_t owner, float alpha,
                                          int32_t require, int32_t exclude)
{
    uint16_t before = status_word();
    int32_t  result;

    result = ((project_floor_fn_t)x87.site[SITE_PROJECT_FLOOR].detour.original)(
        pos, radius, angle, sprite, life, owner, alpha, require, exclude);
    account(SITE_PROJECT_FLOOR, before, status_word());
    return result;
}

static void __cdecl hook_flush_queue(void)
{
    uint16_t before = status_word();

    ((flush_queue_fn_t)x87.site[SITE_FLUSH_QUEUE].detour.original)();
    account(SITE_FLUSH_QUEUE, before, status_word());
}

static int32_t __cdecl hook_thing_dispatch(void *thing, float *matrix)
{
    uint16_t before = status_word();
    int32_t  result;

    result = ((thing_dispatch_fn_t)x87.site[SITE_THING_DISPATCH].detour.original)(thing, matrix);
    account(SITE_THING_DISPATCH, before, status_word());
    return result;
}

/* The float result is taken into a local first, which pops it, so the sample after it sees the
 * stack as the caller will. */
static float __cdecl hook_update_track(void *puppet, float dt, uint32_t slot, float frames)
{
    uint16_t before = status_word();
    float    advanced;

    advanced = ((update_track_fn_t)x87.site[SITE_UPDATE_TRACK].detour.original)(puppet, dt,
                                                                                 slot, frames);
    account(SITE_UPDATE_TRACK, before, status_word());
    return advanced;
}

static void __cdecl hook_clip_events(void *obj, int32_t slot, float advanced)
{
    uint16_t before = status_word();

    ((clip_events_fn_t)x87.site[SITE_CLIP_EVENTS].detour.original)(obj, slot, advanced);
    account(SITE_CLIP_EVENTS, before, status_word());
}

/* Once a frame: the pointer at the frame's end when it has moved, and the counts every so
 * many frames. */
static void x87_frame(void)
{
    uint16_t status = status_word();
    int      i;

    if (!x87.end_sampled || top_of(status) != top_of(x87.last_end_status)) {
        diag_log_write("x87  frame %u ends with status %04X, stack pointer %u", x87.frames,
                       (unsigned)status, top_of(status));
    }
    x87.end_sampled = true;
    x87.last_end_status = status;
    if (++x87.frames % SUMMARY_FRAMES == 0u) {
        for (i = 0; i < SITE_COUNT; ++i) {
            if (x87.site[i].calls != 0u) {
                diag_log_write("x87  %s: %u calls, the stack pointer moved across %u of them, "
                               "%u a net pop", sites[i].name, x87.site[i].calls,
                               x87.site[i].moved, x87.site[i].popped);
            }
            x87.site[i].calls = x87.site[i].moved = x87.site[i].popped = 0u;
        }
    }
}

int diag_x87_install(int level)
{
    static const void *const HOOKS[SITE_COUNT] = {
        (const void *)hook_thing_draw_fx, (const void *)hook_halo_draw,
        (const void *)hook_project_floor, (const void *)hook_flush_queue,
        (const void *)hook_thing_dispatch, (const void *)hook_update_track,
        (const void *)hook_clip_events
    };
    static const size_t PROLOGUES[SITE_COUNT] = {
        THING_DRAW_FX_PROLOGUE, HALO_DRAW_PROLOGUE, PROJECT_FLOOR_PROLOGUE,
        FLUSH_QUEUE_PROLOGUE, THING_DISPATCH_PROLOGUE, UPDATE_TRACK_PROLOGUE,
        CLIP_EVENTS_PROLOGUE
    };
    int installed = 0;
    int i;

    if (level <= 0) {
        return 0;
    }
    signature_resolve_table(sites, SITE_COUNT);
    for (i = 0; i < SITE_COUNT; ++i) {
        installed += diag_install_observer(sites, i, &x87.site[i].detour, HOOKS[i],
                                           PROLOGUES[i],
                                           "the x87 status word either side of the call") ? 1
                                                                                          : 0;
    }
    if (frame_hook_add(x87_frame)) {
        diag_log_write("x87  the stack pointer is reported at the frame's end when it has moved, "
                       "and the counts every %u frames", SUMMARY_FRAMES);
        ++installed;
    }
    return installed;
}
