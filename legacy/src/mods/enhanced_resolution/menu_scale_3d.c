/* menu_scale_3d.c: where a menu's 3-D models are placed, and how big they are drawn.
 *
 * The seam: the rest of the feature works in canvas pixels, where a rectangle is scaled and that
 * is the end of it. These two hooks are the only part that has to reason about the camera, so
 * they are the only part that reads the projection, cancels the lens out of a model's size and
 * has to know where that cancellation must stop. The lens is nobody else's concern and none of
 * these constants is read anywhere else.
 *
 * Both hooks call the engine's own body whenever there is no camera to read or the scale has
 * stood down, so the untouched game is always the fallback rather than an approximation of it.
 */
#include "menu_scale_3d.h"

#include "menu_scale_internal.h"
#include "menu_scale_sites.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

typedef void(__cdecl *sw3d_project_fn_t)(float *offset, const int32_t *rect);
typedef void(__cdecl *sw3d_draw_fn_t)(void *widget);

/* The bottom inset, authored in canvas pixels: a model stands 5 pixels up from the bottom edge of
 * its box rather than on it. */
#define SW3D_BOTTOM_INSET 5.0f

/* 320 / tan(30 deg): the focal length, in pixels, of the lens the menus were authored under. */
#define SW3D_AUTHORED_FOCAL 554.256f

/* HOW FAR THE SIZE COMPENSATION IS ALLOWED TO GO, and it has to stop somewhere.
 *
 * Holding a model's apparent size while the lens widens means growing it at a fixed distance, and a
 * model that grows far enough pushes its own front face through the near plane and is culled
 * entirely. Moving it further away does not rescue it: the compensation grows the model in
 * proportion to the extra distance, so the two cancel and the sign of the near margin never changes.
 * Past some lens the model cannot be both the right size and in front of the camera.
 *
 * Measured rather than guessed: at a field of view above about 108 degrees the hero and the
 * inventory vanished. That works out at a factor near 1.79, and this sits below it with room.
 *
 * The factor depends only on the field of view, not on the resolution, which is why one number
 * serves every setup: with a fixed vertical field of view the focal length is proportional to the
 * screen height, and so is the reference above, so the ratio cancels the resolution out.
 *
 * Past the clamp the models resume shrinking as the lens widens, which is the shipped behaviour and
 * is visibly better than their disappearing. */
#define SW3D_MAX_SIZE_COMPENSATION 1.6f

/* The cell the three matrix-scale reads in sw3d_draw are repointed at. Written immediately before
 * each of those reads, by the hook below, so it can never be a frame behind the lens. */
float menu_sw3d_model_scale = 1.0f;

/* Holds a 3-D widget's model at one size whatever the field of view is. See the note by
 * SIG_SW3D_DRAW for why the size follows the lens at all, and what the reference lens is. */
void __cdecl hook_sw3d_draw(void *widget)
{
    sw3d_draw_fn_t original = (sw3d_draw_fn_t)scale_state.sw3d_draw_detour.original;
    float          engine_scale = *(const float *)(uintptr_t)ENGINE_MENU_SCALE_CELL;
    float          projection   = *(const float *)(uintptr_t)ENGINE_PROJ_SCALE_CELL;

    if (original == NULL) {
        return;
    }

    if (scale_state.stood_down || !(projection > 0.0f)) {
        menu_sw3d_model_scale = engine_scale;      /* exactly what the engine would have read */
    } else {
        float compensation = (SW3D_AUTHORED_FOCAL * scale_state.ratio_y) / projection;

        if (compensation > SW3D_MAX_SIZE_COMPENSATION) {
            compensation = SW3D_MAX_SIZE_COMPENSATION;
            if (!scale_state.warned_compensation) {
                scale_state.warned_compensation = true;
                log_info("the field of view is wide enough that holding the 3-D models at their "
                         "authored size would push them through the near plane and cull them, so "
                         "the compensation stops at %.2f. Past this they shrink as the lens widens, "
                         "which is what the unpatched game does",
                         (double)SW3D_MAX_SIZE_COMPENSATION);
            }
        }
        menu_sw3d_model_scale = engine_scale * compensation;
    }
    original(widget);
}

/* Where a 3-D widget's model goes, computed here rather than by repointing the engine's constants.
 *
 * The engine's own arithmetic is
 *
 *     f = depth * (g_menuScale / 554.256f)
 *     x =  f * ((rect.x + rect.width / 2)     - 320.0f)
 *     z = -f * ((rect.y + rect.height - 5.0f) - 240.0f)
 *
 * and it lands a canvas pixel on the screen pixel it names, because the projection is focalPx over
 * depth and 554.256 is that focal at the size the menus were authored for. Written without the
 * coincidence, what it means is
 *
 *     offset = (canvas pixel - canvas centre) * depth / focalPx
 *
 * which is what this computes, with the canvas centre being the scaled one and focalPx read from the
 * camera AT THIS INSTANT.
 *
 * WHY NOT KEEP REPOINTING THE CONSTANTS. That was the first version and it was wrong in a way no
 * amount of care about the arithmetic would have fixed: the cell holding the focal has to be written
 * before the placement happens, and the placement happens after the camera has been rebuilt for the
 * frame. Refreshing it once per menu frame sampled a lens that had not changed yet, so dragging the
 * field of view slider slid the hero sideways, out by exactly the ratio between the lens we had
 * sampled and the one actually projecting. A probe proved it: the lens was logged once, at startup,
 * and never again while the slider moved. Reading it here removes the ordering question entirely,
 * because there is no longer a stored value to be stale.
 *
 * It also hands the [0x4A8888] operand back to variable_fov, which wants it for the same reason and
 * whose own compensation is now harmless: nothing here reads that constant any more.
 *
 * The engine's own body is called when there is no camera to read, and when the scale has stood
 * down, so the untouched game is always the fallback rather than an approximation of it. */
void __cdecl hook_sw3d_project(float *offset, const int32_t *rect)
{
    sw3d_project_fn_t  original = (sw3d_project_fn_t)scale_state.sw3d_project_detour.original;
    const char *const *camera_slot = (const char *const *)(uintptr_t)ENGINE_CURRENT_CAMERA;
    const char        *camera = NULL;
    float              focal;
    float              depth;

    if (original == NULL) {
        return;
    }
    /* g_projScale first, because it is what will be multiplied by. The camera is the fallback for
     * the first frame, before render_prepareFrame has copied anything into it. */
    focal = *(const float *)(uintptr_t)ENGINE_PROJ_SCALE_CELL;
    if (!(focal > 0.0f)) {
        camera = *camera_slot;
        focal  = (camera != NULL) ? *(const float *)(camera + CAMERA_FOCAL_PIXELS) : 0.0f;
    }

    if (scale_state.stood_down || offset == NULL || rect == NULL || !(focal > 0.0f)) {
        original(offset, rect);
        return;
    }

    depth = offset[1];
    {
        float centre_x = (float)scale_state.canvas_width  * 0.5f;
        float centre_y = (float)scale_state.canvas_height * 0.5f;
        float across   = (float)rect[0] + (float)rect[2] * 0.5f;
        float down     = (float)rect[1] + (float)rect[3]
                       - SW3D_BOTTOM_INSET * scale_state.ratio_y;
        float per_pixel = depth / focal;

        offset[0] =  (across - centre_x) * per_pixel;
        offset[1] =  depth;                     /* the caller's own, deliberately preserved */
        offset[2] = -(down   - centre_y) * per_pixel;
    }

    if (!scale_state.logged_focal) {
        scale_state.logged_focal = true;
        log_info("3-D widgets are placed about %.1f,%.1f from the camera's live focal length "
                 "(%.2f right now), so they hold still while the field of view changes",
                 (double)((float)scale_state.canvas_width * 0.5f),
                 (double)((float)scale_state.canvas_height * 0.5f), (double)focal);
    }
}
