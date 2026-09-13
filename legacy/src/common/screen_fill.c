/* screen_fill.c: see screen_fill.h. */
#include "screen_fill.h"

#include "logging.h"
#include "memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- The routine's immediate arm, at fixed offsets from its entry ----------------------------- *
 *
 *   004197AE  68 00 92 00 00           push 0x9200            the render state word
 *   004197B3  E8 <rel32>               call host_setRenderState
 *   004197B8  83 C4 04                 add esp,4
 *   004197BB  56                       push esi               esi is zero throughout the body
 *   004197BC  E8 <rel32>               call host_bindTexture
 *   004197C1  83 C4 04                 add esp,4
 *   004197C4  8D 54 24 0C              lea edx,[esp+0x0C]     the four vertices
 *   004197C8  56 6A 04 52              push 0 / push 4 / push edx
 *   004197CC  E8 <rel32>               call host_drawTriangleFan
 *
 * The bytes around each call are checked before its target is read, so a build whose arm is
 * shaped differently falls back to the routine instead of calling into the wrong place. */
#define ARM_PUSH_STATE     0x14Eu
#define ARM_CALL_STATE     0x153u
#define ARM_CALL_TEXTURE   0x15Cu
#define ARM_CALL_FAN       0x16Cu
#define ARM_STATE_OPERAND  (ARM_PUSH_STATE + 1u)
static const uint8_t ARM_HEAD[] = { 0x68, 0x00, 0x92, 0x00, 0x00, 0xE8 };
static const uint8_t ARM_MID[]  = { 0x83, 0xC4, 0x04, 0x56, 0xE8 };
static const uint8_t ARM_TAIL[] = { 0x8D, 0x54, 0x24, 0x0C, 0x56, 0x6A, 0x04, 0x52, 0xE8 };
#define CALL_LENGTH        5u
#define DRAW_NOW           1

typedef void (__cdecl *quad_fn_t)(float x0, float y0, float x1, float y1, uint32_t argb,
                                  int32_t now);
typedef void (__cdecl *set_render_state_fn_t)(uint32_t state);
typedef void (__cdecl *bind_texture_fn_t)(int32_t texture);
typedef void (__cdecl *draw_fan_fn_t)(const void *vertices, int32_t count, int32_t flags);

/* A transformed vertex as Direct3D lays it out, the 32 bytes the engine's fan call takes. */
typedef struct tl_vertex {
    float    sx, sy, sz, rhw;
    uint32_t colour;
    uint32_t specular;
    float    tu, tv;
} tl_vertex_t;
_Static_assert(sizeof(tl_vertex_t) == 32, "a transformed vertex is 32 bytes");

static struct {
    quad_fn_t             quad;
    set_render_state_fn_t set_render_state;
    bind_texture_fn_t     bind_texture;
    draw_fan_fn_t         draw_fan;
    uint32_t              render_state;
} fill_state;

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

bool screen_fill_resolve(uintptr_t quad_routine)
{
    uintptr_t set_state = call_target(quad_routine + ARM_CALL_STATE);
    uintptr_t bind = call_target(quad_routine + ARM_CALL_TEXTURE);
    uintptr_t fan = call_target(quad_routine + ARM_CALL_FAN);
    uint32_t  state = 0;

    fill_state.quad     = (quad_fn_t)quad_routine;
    fill_state.draw_fan = NULL;
    if (!bytes_at(quad_routine + ARM_PUSH_STATE, ARM_HEAD, sizeof ARM_HEAD) ||
        !bytes_at(quad_routine + ARM_CALL_TEXTURE + 1u - sizeof ARM_MID, ARM_MID,
                  sizeof ARM_MID) ||
        !bytes_at(quad_routine + ARM_CALL_FAN + 1u - sizeof ARM_TAIL, ARM_TAIL,
                  sizeof ARM_TAIL) ||
        !memory_read_u32(quad_routine + ARM_STATE_OPERAND, &state) ||
        !memory_is_inside_image(set_state, 16) || !memory_is_inside_image(bind, 16) ||
        !memory_is_inside_image(fan, 16)) {
        log_warning("the filled-shape routine's immediate arm at %08X does not read as "
                    "expected, so fills go through the routine itself, with its own vertices",
                    (unsigned)quad_routine);
        return false;
    }
    fill_state.set_render_state = (set_render_state_fn_t)set_state;
    fill_state.bind_texture     = (bind_texture_fn_t)bind;
    fill_state.draw_fan         = (draw_fan_fn_t)fan;
    fill_state.render_state     = state;
    log_info("fills are drawn as transformed vertices of our own, z 0 and rhw 1, through the "
             "filled-shape routine's own three calls (state %08X, texture %08X, fan %08X)",
             (unsigned)set_state, (unsigned)bind, (unsigned)fan);
    return true;
}

void screen_fill(float x0, float y0, float x1, float y1, uint32_t argb)
{
    tl_vertex_t v[4];
    int         i;

    if (fill_state.draw_fan == NULL) {
        if (fill_state.quad != NULL) {
            fill_state.quad(x0, y0, x1, y1, argb, DRAW_NOW);
        }
        return;
    }
    x1 = floorf(x1 + 0.9999f);              /* the far edges snap outward, as the routine's do */
    y1 = floorf(y1 + 0.9999f);
    x0 = floorf(x0);
    y0 = floorf(y0);
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
    fill_state.set_render_state(fill_state.render_state);
    fill_state.bind_texture(0);
    fill_state.draw_fan(v, 4, 0);
}
