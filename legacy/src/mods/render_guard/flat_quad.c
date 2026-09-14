/* flat_quad.c: see flat_quad.h. */
#include "flat_quad.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RENDER_GUARD_SECTION "render_guard"

/* --- 0x00419660, the flat quad. The entry, its first load and the multiply against the pixel
 * snap, whose operand is masked. The same pattern dev_overlay and fmv_player resolve it by. */
static const uint8_t SIG_DRAW_QUAD[] = {
    0x81, 0xEC, 0x84, 0x00, 0x00, 0x00,              /* sub esp,0x84             */
    0xD9, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00,        /* fld dword [esp+0x90]     */
    0xD8, 0x25, 0x00, 0x00, 0x00, 0x00               /* fsub the pixel snap      */
};
static const uint8_t MSK_DRAW_QUAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_DRAW_QUAD == sizeof MSK_DRAW_QUAD,
               "the flat quad pattern and its mask are different lengths");
#define DRAW_QUAD_PROLOGUE 6u                         /* sub esp,0x84, one whole instruction */

/* --- The two arms, at fixed offsets from the entry -------------------------------------------- *
 *
 *   +0x14E  68 00 92 00 00          push 0x9200           the render state word
 *   +0x153  E8 <rel32>              call host_setRenderState
 *   +0x158  83 C4 04 / 56           add esp,4 / push esi  esi is zero throughout the body
 *   +0x15C  E8 <rel32>              call host_bindTexture
 *   +0x164  8D 54 24 0C             lea edx,[esp+0x0C]    the four vertices
 *   +0x168  56 6A 04 52             push 0 / push 4 / push edx
 *   +0x16C  E8 <rel32>              call host_drawTriangleFan(v, 4, 0)
 *
 *   +0x17D  56 8D 44 24 10          push esi / lea eax,[esp+0x10]
 *   +0x182  6A 04 50 68 00 92 00 00 push 4 / push eax / push 0x9200
 *   +0x18A  56                      push esi
 *   +0x18B  E8 <rel32>              call host_queueSortedFan(0, 0x9200, v, 4, 0)
 */
#define ARM_NOW_PUSH_STATE   0x14Eu
#define ARM_NOW_CALL_STATE   0x153u
#define ARM_NOW_CALL_TEXTURE 0x15Cu
#define ARM_NOW_CALL_FAN     0x16Cu
#define ARM_QUEUE_HEAD       0x17Du
#define ARM_QUEUE_CALL       0x18Bu
static const uint8_t ARM_NOW_HEAD[]  = { 0x68, 0x00, 0x92, 0x00, 0x00, 0xE8 };
static const uint8_t ARM_NOW_MID[]   = { 0x83, 0xC4, 0x04, 0x56, 0xE8 };
static const uint8_t ARM_NOW_TAIL[]  = { 0x8D, 0x54, 0x24, 0x0C, 0x56, 0x6A, 0x04, 0x52, 0xE8 };
static const uint8_t ARM_QUEUE[]     = { 0x56, 0x8D, 0x44, 0x24, 0x10, 0x6A, 0x04, 0x50,
                                         0x68, 0x00, 0x92, 0x00, 0x00, 0x56, 0xE8 };
#define CALL_LENGTH          5u
#define RENDER_STATE_WORD    0x9200u

typedef void (__cdecl *set_render_state_fn_t)(uint32_t state);
typedef void (__cdecl *bind_texture_fn_t)(int32_t texture);
typedef void (__cdecl *draw_fan_fn_t)(const void *vertices, int32_t count, int32_t flags);
typedef void (__cdecl *queue_fan_fn_t)(int32_t layer, uint32_t state, const void *vertices,
                                       int32_t count, int32_t flags);

/* A transformed vertex as Direct3D lays it out, the 32 bytes both calls take. */
typedef struct tl_vertex {
    float    sx, sy, sz, rhw;
    uint32_t colour;
    uint32_t specular;
    float    tu, tv;
} tl_vertex_t;
_Static_assert(sizeof(tl_vertex_t) == 32, "a transformed vertex is 32 bytes");

static struct {
    detour_t              quad;
    set_render_state_fn_t set_render_state;
    bind_texture_fn_t     bind_texture;
    draw_fan_fn_t         draw_fan;
    queue_fan_fn_t        queue_fan;
} flat;

/* The snap, from the bytes at 0x00419660 and not from the recreation's C, which the gate marks
 * MISMATCH for this routine. The far edges add the word at 0x004a81b0, which is the float
 * 0xbf7ff972 subtracted (so 0.9999 added), and go through the CRT's ceil at 0x0049a960 and then
 * its floor at 0x0049a480; the near edges go through floor alone. So an integer far edge moves
 * out by a whole pixel: the letterbox's W - 1 becomes W, and its H / 12 becomes H / 12 + 1. The
 * recreation floors the far edge instead, and the first version of this file copied that, which
 * drew every quad a pixel short on the right and at the bottom on every driver; the cutscene
 * bars showed it as a sliver at their edges (2026-09-14, NVIDIA and the Deck alike, and gone
 * with the routine left alone). The arithmetic is done in double the way the x87 code does it,
 * the float operands widened and the CRT calls taking doubles, so the rounding lands where the
 * engine's does at every size. */
#define FAR_EDGE_BIAS_BITS 0xbf7ff972u

static float far_edge(float v)
{
    float  bias;
    double widened;

    memcpy(&bias, &(uint32_t){ FAR_EDGE_BIAS_BITS }, sizeof bias);
    widened = (double)v - (double)bias;
    return (float)floor(ceil(widened));
}

static float near_edge(float v)
{
    return (float)floor((double)v);
}

/* The routine as the bytes have it, with the two vertex fields changed and nothing else. */
static void __cdecl hook_draw_quad(float x0, float y0, float x1, float y1, uint32_t argb,
                                   int32_t immediate)
{
    tl_vertex_t v[4];
    int         i;

    x1 = far_edge(x1);
    y1 = far_edge(y1);
    x0 = near_edge(x0);
    y0 = near_edge(y0);
    v[0].sx = x0;  v[0].sy = y0;
    v[1].sx = x1;  v[1].sy = y0;
    v[2].sx = x1;  v[2].sy = y1;
    v[3].sx = x0;  v[3].sy = y1;
    for (i = 0; i < 4; ++i) {
        v[i].sz       = 0.0f;
        v[i].rhw      = 1.0f;
        v[i].colour   = argb;
        v[i].specular = 0u;
        v[i].tu       = 0.0f;
        v[i].tv       = 0.0f;
    }
    if (immediate != 0) {
        flat.set_render_state(RENDER_STATE_WORD);
        flat.bind_texture(0);
        flat.draw_fan(v, 4, 0);
    } else {
        flat.queue_fan(0, RENDER_STATE_WORD, v, 4, 0);
    }
}

static bool bytes_at(uintptr_t address, const uint8_t *expected, size_t size)
{
    uint8_t seen[16];

    return size <= sizeof seen && memory_try_read(address, seen, size) &&
           memcmp(seen, expected, size) == 0;
}

static uintptr_t call_target(uintptr_t call)
{
    int32_t rel = 0;

    if (!memory_try_read(call + 1u, &rel, sizeof rel)) {
        return 0;
    }
    return call + CALL_LENGTH + (uintptr_t)(intptr_t)rel;
}

/* The four targets out of the routine's body, each with the bytes around its call checked. */
static bool resolve_arms(uintptr_t quad)
{
    uintptr_t set_state = call_target(quad + ARM_NOW_CALL_STATE);
    uintptr_t bind = call_target(quad + ARM_NOW_CALL_TEXTURE);
    uintptr_t fan = call_target(quad + ARM_NOW_CALL_FAN);
    uintptr_t queue = call_target(quad + ARM_QUEUE_CALL);

    if (!bytes_at(quad + ARM_NOW_PUSH_STATE, ARM_NOW_HEAD, sizeof ARM_NOW_HEAD) ||
        !bytes_at(quad + ARM_NOW_CALL_TEXTURE + 1u - sizeof ARM_NOW_MID, ARM_NOW_MID,
                  sizeof ARM_NOW_MID) ||
        !bytes_at(quad + ARM_NOW_CALL_FAN + 1u - sizeof ARM_NOW_TAIL, ARM_NOW_TAIL,
                  sizeof ARM_NOW_TAIL) ||
        !bytes_at(quad + ARM_QUEUE_HEAD, ARM_QUEUE, sizeof ARM_QUEUE) ||
        !memory_is_inside_image(set_state, 16) || !memory_is_inside_image(bind, 16) ||
        !memory_is_inside_image(fan, 16) || !memory_is_inside_image(queue, 16)) {
        return false;
    }
    flat.set_render_state = (set_render_state_fn_t)set_state;
    flat.bind_texture     = (bind_texture_fn_t)bind;
    flat.draw_fan         = (draw_fan_fn_t)fan;
    flat.queue_fan        = (queue_fan_fn_t)queue;
    return true;
}

void flat_quad_install(void)
{
    uintptr_t quad;

    if (!ini_read_bool(RENDER_GUARD_SECTION, "GuardFlatQuads", true)) {
        log_info("GuardFlatQuads=0, so the engine's flat quads keep their own vertices, rhw 0 "
                 "and z 1.0 on a 16-bit depth buffer, which Intel does not draw");
        return;
    }
    quad = signature_find_unique(SIG_DRAW_QUAD, MSK_DRAW_QUAD, sizeof SIG_DRAW_QUAD);
    if (quad == 0) {
        log_warning("the flat quad routine did not resolve, so the engine's fades, bars and "
                    "backdrops keep their own vertices");
        return;
    }
    if (!resolve_arms(quad)) {
        log_warning("the flat quad routine at %08X does not read as expected past its entry, so "
                    "it is left as it is", (unsigned)quad);
        return;
    }
    if (!detour_install(&flat.quad, quad, (const void *)hook_draw_quad, DRAW_QUAD_PROLOGUE)) {
        log_warning("the detour on the flat quad routine at %08X failed", (unsigned)quad);
        return;
    }
    log_info("flat quads at %08X are drawn with rhw 1 and z 0 for every caller, the fades, the "
             "letterbox bars, the menu backdrops and the loading bar's black among them, snapped "
             "as the routine snaps them, far edges up through ceil (state %08X, texture %08X, "
             "fan %08X, queue %08X). The routine's own rhw 0 and z 1.0 on a 16-bit depth buffer "
             "are not drawn by Intel.", (unsigned)quad,
             (unsigned)(uintptr_t)flat.set_render_state, (unsigned)(uintptr_t)flat.bind_texture,
             (unsigned)(uintptr_t)flat.draw_fan, (unsigned)(uintptr_t)flat.queue_fan);
}
