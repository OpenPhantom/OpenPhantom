/* world_camera.c: see world_camera.h.
 *
 * The site is the tail of bapvrt_frameSetup (0x00419800), which runs once per frame from
 * render_prepareFrame, after the substeps and before the world is drawn:
 *
 *   0041996D  A1 E4 83 6F 00        mov eax,[rdCamera_pCurCamera]
 *   00419972  5E                    pop esi
 *   00419973  8B 48 04              mov ecx,[eax+4]           the camera's canvas
 *   00419976  8B 51 08              mov edx,[ecx+8]           its centre x, an f32
 *   00419979  89 15 1C FA 6B 00     mov [g_screenCX],edx
 *   0041997F  8B 48 04              mov ecx,[eax+4]
 *   00419982  8B 51 0C              mov edx,[ecx+0xC]         its centre y
 *   00419985  33 C9                 xor ecx,ecx
 *   00419987  89 15 80 B5 5B 00     mov [g_screenCY],edx
 *   0041998D  8B 40 3C              mov eax,[eax+0x3C]        the camera's focal length in pixels
 *   00419990  A3 E8 F9 5B 00        mov [g_projScale],eax
 *
 * bapvrt_projectVertex (0x0041AF30) reads the three cells for every vertex: sx = x * projScale /
 * depth + screenCX, sy = -z * projScale / depth + screenCY. The camera's world transform is the
 * twelve floats at +0x08 of the camera the first operand names, which bapvrt_transformWorld and
 * candy_projectPoint both apply to a world point with mat34_transform before projecting it.
 *
 * The four absolute operands are masked and read back; everything else is literal. Measured: one
 * match in each of the six images checked, the recompile included, all at 0x0041996D.
 */
#include "world_camera.h"

#include "character_prop.h"
#include "cheats_internal.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <math.h>
#include <string.h>

static const uint8_t SIG_CAMERA_CELLS[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,             /* mov eax,[rdCamera_pCurCamera]              */
    0x5E,                                     /* pop esi                                    */
    0x8B, 0x48, 0x04,                         /* mov ecx,[eax+4]                            */
    0x8B, 0x51, 0x08,                         /* mov edx,[ecx+8]                            */
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,       /* mov [g_screenCX],edx                       */
    0x8B, 0x48, 0x04,                         /* mov ecx,[eax+4]                            */
    0x8B, 0x51, 0x0C,                         /* mov edx,[ecx+0xC]                          */
    0x33, 0xC9,                               /* xor ecx,ecx                                */
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,       /* mov [g_screenCY],edx                       */
    0x8B, 0x40, 0x3C,                         /* mov eax,[eax+0x3C]                         */
    0xA3, 0x00, 0x00, 0x00, 0x00              /* mov [g_projScale],eax                      */
};
static const uint8_t MSK_CAMERA_CELLS[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_CAMERA_CELLS == sizeof MSK_CAMERA_CELLS, "mask length");
#define OFFSET_CAMERA_CELL      1u
#define OFFSET_CENTRE_X_CELL   14u
#define OFFSET_CENTRE_Y_CELL   28u
#define OFFSET_FOCAL_CELL      36u

#define CAMERA_VIEW            0x08u   /* rdCamera.worldTransform, twelve floats */
#define VIEW_EYE               0x24u   /* bapView.eye, the composed camera position */
#define VIEW_CAMERA            0x44u   /* bapView.pCamera */

static struct {
    bool      tried;
    uintptr_t camera_cell;
    uintptr_t centre_x_cell;
    uintptr_t centre_y_cell;
    uintptr_t focal_cell;
    bool      decided;            /* the self-check said what it found, or cannot be made */
    bool      said_undecided;
} cam;

static bool read_cell(uintptr_t site, uint32_t offset, uintptr_t *out)
{
    uint32_t cell = 0;

    if (!memory_read_u32(site + offset, &cell) || !memory_is_inside_image(cell, sizeof(float))) {
        return false;
    }
    *out = (uintptr_t)cell;
    return true;
}

void world_camera_resolve(void)
{
    uintptr_t site;

    if (cam.tried) {
        return;
    }
    cam.tried = true;
    site = signature_find_unique(SIG_CAMERA_CELLS, MSK_CAMERA_CELLS, sizeof SIG_CAMERA_CELLS);
    if (site == 0 || !read_cell(site, OFFSET_CAMERA_CELL, &cam.camera_cell) ||
        !read_cell(site, OFFSET_CENTRE_X_CELL, &cam.centre_x_cell) ||
        !read_cell(site, OFFSET_CENTRE_Y_CELL, &cam.centre_y_cell) ||
        !read_cell(site, OFFSET_FOCAL_CELL, &cam.focal_cell)) {
        cam.camera_cell = 0;
        log_warning("npc spawner: the camera cells did NOT resolve (site %08X), so the placement "
                    "mode cannot turn the pointer into a place and stays unavailable",
                    (unsigned)site);
        return;
    }
    log_info("npc spawner: the camera cells resolved at %08X: camera %08X, centre %08X and %08X, "
             "focal length %08X; the placement mode reads the picture's own numbers",
             (unsigned)site, (unsigned)cam.camera_cell, (unsigned)cam.centre_x_cell,
             (unsigned)cam.centre_y_cell, (unsigned)cam.focal_cell);
}

bool world_camera_available(void)
{
    world_camera_resolve();
    return cam.camera_cell != 0;
}

/* Which way the nine floats are read, held against the eye the view record keeps, which the engine
 * composes and the transform is built from. The two residuals are printed side by side, so a wrong
 * reading shows as the small number on the wrong side rather than as a verdict to be taken on
 * trust. A camera looking almost along a world axis, or an eye near the world's origin, puts both
 * near, and then nothing can be told: that is said once, and the check is made again on every read
 * until it can tell (world_pick_reading_verdict). */
static void check_the_reading(const world_pick_camera_t *out, uint32_t camera)
{
    uint32_t             view = 0;
    uint32_t             view_camera = 0;
    float                eye[3];
    float                ours;
    float                other;
    world_pick_reading_t reading;

    if (own_state.camera_view_address == 0 ||
        !memory_try_read(own_state.camera_view_address, &view, sizeof view) || view == 0u ||
        !memory_try_read((uintptr_t)view + VIEW_EYE, eye, sizeof eye) ||
        !memory_try_read((uintptr_t)view + VIEW_CAMERA, &view_camera, sizeof view_camera)) {
        cam.decided = true;
        log_info("npc spawner: camera self-check not made: the view record is not known here; "
                 "focal %.1f, centre %.1f %.1f", (double)out->focal, (double)out->centre_x,
                 (double)out->centre_y);
        return;
    }
    ours    = world_pick_eye_residual(out, eye, false);
    other   = world_pick_eye_residual(out, eye, true);
    reading = world_pick_reading_verdict(ours, other, view_camera == camera);
    if (reading == WORLD_PICK_READING_UNDECIDED) {
        if (!cam.said_undecided) {
            cam.said_undecided = true;
            log_info("npc spawner: camera self-check undecided: the eye lands %.3f from the "
                     "camera's origin read as rows and %.3f read as columns, and the view's camera "
                     "is %s; the two readings cannot be told apart here, so it is asked again "
                     "until they can", (double)ours, (double)other,
                     view_camera == camera ? "the current one" : "NOT the current one");
        }
        return;
    }
    cam.decided = true;
    log_info("npc spawner: camera self-check: the eye lands %.3f from the camera's origin read as "
             "rows, %.3f read as columns, so the rows reading %s; focal %.1f, centre %.1f %.1f",
             (double)ours, (double)other,
             reading == WORLD_PICK_READING_AGREES ? "agrees" : "does NOT agree",
             (double)out->focal, (double)out->centre_x, (double)out->centre_y);
}

bool world_camera_read(world_pick_camera_t *out)
{
    uint32_t camera = 0;
    uint32_t i;

    if (out == NULL || !world_camera_available() ||
        !memory_try_read(cam.camera_cell, &camera, sizeof camera) || camera == 0u ||
        !memory_try_read((uintptr_t)camera + CAMERA_VIEW, out->view, sizeof out->view) ||
        !memory_try_read(cam.focal_cell, &out->focal, sizeof out->focal) ||
        !memory_try_read(cam.centre_x_cell, &out->centre_x, sizeof out->centre_x) ||
        !memory_try_read(cam.centre_y_cell, &out->centre_y, sizeof out->centre_y)) {
        return false;
    }
    for (i = 0; i < 12u; ++i) {
        if (!isfinite(out->view[i])) {
            return false;
        }
    }
    if (!(out->focal >= 1.0f) || !isfinite(out->focal) || !isfinite(out->centre_x) ||
        !isfinite(out->centre_y) || character_prop_mat_skew(out->view) > 0.01f) {
        return false;
    }
    if (!cam.decided) {
        check_the_reading(out, camera);
    }
    return true;
}
