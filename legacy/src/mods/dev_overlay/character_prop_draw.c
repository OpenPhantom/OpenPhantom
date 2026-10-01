/* character_prop_draw.c: the three detours that let the borrowed weapon be seen, shot from and hit
 * with.
 *
 * character_prop.c holds the arithmetic and the second render handle; this file holds nothing but
 * the places the engine has to be interrupted for any of it to matter. They are apart because the
 * arithmetic file is at its size limit, and because these are the only lines of the feature that
 * change what the engine does rather than what this module computes.
 *
 * The third one lives in character_mount.c and is installed from here rather than from the swap,
 * so that the picture, the muzzle and the blow are armed by one call and never two of the three.
 *
 * The dispatch pattern is a second copy of the one in character_prop_sites.c, under its own
 * symbol. That module uses the address as a FUNCTION, to draw the prop; this one needs it as a
 * DETOUR TARGET, to learn when the body is drawn. Sharing one copy would mean one module reaching
 * into another's private state to save forty bytes of pattern. They cannot disagree: it is one
 * site, and if a build ever moved it, both resolve from the same bytes or neither does.
 */
#include "character_prop.h"
#include "model_blade_guard.h"

#include "character_mount.h"
#include "character_reentry.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The render handle dispatcher: the gate on the render module, the handle type, and the jump table
 * the type indexes. Prologue boundaries are at 1, 3, 4 and 11 by decoding:
 *
 *   55              push ebp                    boundary at 0
 *   8B EC           mov ebp,esp                 boundary at 1
 *   51              push ecx                    boundary at 3
 *   83 3D m32,i8    cmp [the gate],0            boundary at 4, and the next is at 11
 *
 * Six is inside that compare. Eleven is the first boundary at or past the five bytes a jump needs,
 * and every one of the eleven bytes is position independent. */
static const uint8_t SIG_PROPDRAW_DISPATCH[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,  /* cmp [the render module gate],0           */
    0x75, 0x04, 0x33, 0xC0, 0xEB, 0x00, 0x8B, 0x45, 0x08, 0x8B, 0x08, 0x89, 0x4D, 0xFC,
    0x83, 0x7D, 0xFC, 0x06,                    /* cmp [ebp-4],6        the handle type     */
    0x77, 0x00, 0x8B, 0x55, 0xFC,
    0xFF, 0x24, 0x95, 0x00, 0x00, 0x00, 0x00   /* jmp [edx*4+the jump table]               */
};
static const uint8_t MSK_PROPDRAW_DISPATCH[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_PROPDRAW_DISPATCH) == sizeof(MSK_PROPDRAW_DISPATCH),
               "the dispatch pattern and its mask are different lengths");

#define PROPDRAW_DISPATCH_PROLOGUE 11u
/* And the node sphere, the same six bytes every other detour in this mod declares, named here
 * so this file depends on no other module for it. */
#define PROPDRAW_SPHERE_PROLOGUE 6u

/* The node sphere: where the shot leaves the weapon. It answers a centre and a radius for a node's
 * mesh, and it does NOT write the output vector when the node carries no mesh, which is why a shot
 * from a rig without the weapon geometry came out of uninitialised stack. The pushed source line is
 * what makes the match certain rather than the prologue. */
static const uint8_t SIG_PROPDRAW_NODE_SPHERE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70,
    0x8B, 0x45, 0x08, 0x89, 0x45, 0xCC,
    0x8B, 0x4D, 0xCC,
    0x8B, 0x91, 0x9C, 0x00, 0x00, 0x00,        /* mov edx,[ecx+0x9C]   the render thing    */
    0x89, 0x55, 0xC0,
    0x83, 0x7D, 0xC0, 0x00,
    0x75, 0x00,
    0x68, 0x79, 0x0A, 0x00, 0x00               /* push 2681, the asserted source line      */
};
static const uint8_t MSK_PROPDRAW_NODE_SPHERE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_PROPDRAW_NODE_SPHERE) == sizeof(MSK_PROPDRAW_NODE_SPHERE),
               "the node sphere pattern and its mask are different lengths");

/* Both prototypes carry their return value. The dispatcher answers the handle it drew, and the
 * sphere answers its radius in ST(0); a hook that dropped either would turn a working shot into one
 * aimed at zero. */
typedef void *(__cdecl *dispatch_fn_t)(void *thing, const float *root);
typedef float (__cdecl *node_sphere_fn_t)(void *obj, uint32_t node, float *out);

static struct {
    bool                        armed;
    detour_t                    dispatch;
    detour_t                    sphere;
    dispatch_fn_t               dispatch_original;
    node_sphere_fn_t            sphere_original;
    character_reentry_t         inside;
    character_prop_draw_extra_t extra;   /* a second drawer, the placement mode's ghost */
} draw;

/* ============================================================================================ */

/* BEFORE the original, and the order is load bearing. The dispatcher sets nine globals at its head,
 * so a prop drawn afterwards would have every one of them reset by the body's own draw before the
 * halo and emitter passes behind it run. Drawn first, the question does not arise.
 *
 * This hook draws through its own entry point. character_prop_sites.c resolves the dispatcher as a
 * FUNCTION and this file detours it, so the address the prop is drawn with is the address of this
 * hook, and the prop's own draw arrives here a second time. That is intended and it has to stop
 * after one step, so the lock is held across the only call that can come back: the original behind
 * it is the trampoline or the previous hook and is not this function, so it is outside the bracket
 * and every drawn object still reaches the engine exactly once.
 *
 * character_prop.c keeps a flag of its own that does the same job from the other side. It is not
 * redundant with this one and neither replaces the other: that flag says whether the module is
 * drawing, this lock says whether the hook is inside itself, and the flag can be cleared by any
 * path through the module while this call is still on the stack. */
static void *__cdecl hook_dispatch(void *thing, const float *root)
{
    if (character_reentry_enter(&draw.inside)) {
        character_prop_before_thing_draw(thing, root);
        if (draw.extra != NULL) {
            draw.extra(thing, root);   /* inside the same lock: its own draw comes back here too */
        }
        character_reentry_leave(&draw.inside);
    }
    return draw.dispatch_original(thing, root);
}

void character_prop_draw_set_extra(character_prop_draw_extra_t extra)
{
    draw.extra = extra;
}

/* The muzzle. The module answers only for the node the engine would itself return on the worn
 * model, computed out of the same three fields; every other node falls through untouched. */
static float __cdecl hook_node_sphere(void *obj, uint32_t node, float *out)
{
    float radius = 0.0f;

    if (character_prop_node_sphere(obj, (int32_t)node, out, &radius)) {
        return radius;
    }
    return draw.sphere_original(obj, node, out);
}

/* ============================================================================================ */

bool character_prop_draw_install(void)
{
    uintptr_t dispatch_site;
    uintptr_t sphere_site;

    if (draw.armed) {
        return true;
    }
    dispatch_site = signature_find_detour_target(SIG_PROPDRAW_DISPATCH, MSK_PROPDRAW_DISPATCH,
                                                 sizeof SIG_PROPDRAW_DISPATCH,
                                                 PROPDRAW_DISPATCH_PROLOGUE);
    sphere_site = signature_find_detour_target(SIG_PROPDRAW_NODE_SPHERE, MSK_PROPDRAW_NODE_SPHERE,
                                               sizeof SIG_PROPDRAW_NODE_SPHERE,
                                               PROPDRAW_SPHERE_PROLOGUE);
    if (dispatch_site == 0 || sphere_site == 0) {
        log_warning("the handle dispatch or the node sphere did not resolve, so a borrowed weapon "
                    "would be neither drawn nor shot from, and none is offered");
        return false;
    }
    if (!detour_install(&draw.dispatch, dispatch_site, (const void *)&hook_dispatch,
                        PROPDRAW_DISPATCH_PROLOGUE)) {
        log_warning("the handle dispatch could not be detoured");
        return false;
    }
    draw.dispatch_original = (dispatch_fn_t)draw.dispatch.original;

    if (!detour_install(&draw.sphere, sphere_site, (const void *)&hook_node_sphere,
                        PROPDRAW_SPHERE_PROLOGUE)) {
        /* The dispatch detour stays where it is. There is no removal in this project's chained
         * detours, and it is harmless on its own: the module it calls answers nothing until it is
         * armed, and arming is what this function refuses. */
        log_warning("the node sphere could not be detoured, so the weapon would draw in the hand "
                    "while the shot still left the body's centre");
        return false;
    }
    draw.sphere_original = (node_sphere_fn_t)draw.sphere.original;

    draw.armed = true;
    log_info("the borrowed weapon draws at %08X and is shot from %08X", (unsigned)dispatch_site,
             (unsigned)sphere_site);

    /* Last, and not a condition. Without it the weapon is drawn and the shot leaves it while the
     * swing resolves a name the borrowed rig does not carry, which the engine answers with zero
     * and the contact gate reads as no contact at all. With the two above in place that is a
     * weapon that does not hurt anything; without them it would be no weapon. */
    if (!character_mount_install()) {
        log_warning("the node name lookup is not hooked, so a weapon name the borrowed rig cannot "
                    "answer stays zero and a blow passes through whatever it is aimed at");
    }
    return true;
}

bool character_prop_draw_is_armed(void)
{
    return draw.armed;
}
