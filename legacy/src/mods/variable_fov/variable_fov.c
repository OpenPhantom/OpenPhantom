/* variable_fov.c: the camera hook. One number, written just before the engine derives everything.
 *
 * ==============================================================================================
 * BYTE BASIS
 *
 *   rdCamera  +0x00 projType   +0x04 pCanvas
 *             +0x38 fovDeg     (HORIZONTAL; rdCamera_init clamps it to [5,179])
 *             +0x3C focalPx    (recomputed by rdCamera_updateProjection 0x475FFA)
 *             +0x40 pixelAspect                     +0x44 orthoScale
 *             +0x48 pProjection -> rdClipFrustum    +0x4C/+0x50 projector function pointers
 *   rdCanvas  +0x18 x0  +0x1C y0  +0x20 x1  +0x24 y1   (0x47691E: x1 = vbufWidth - 1)
 *
 *   0x475FFA arm 1 (projType == 1):  focalPx = ((x1-x0)/2) / tan(fovDeg/2)        @0x47615E
 *   0x419990 bapvrt_frameSetup, PER FRAME:  g_projScale = cam->focalPx
 *   bapvrt:  sx = x*(g_projScale/y) + cx      sy = -z*(g_projScale/y) + cy
 *
 * fovDeg is therefore the ONLY input to the projection, and it takes effect in the SAME frame we
 * write it. The world cull reads pCurCamera->fovDeg + 3.0f itself (bapdraw_drawWorld), so a wider
 * picture widens its own cell search and nothing pops in at the edge. The GAMEPLAY cones,
 * auto-aim 16 deg, NPC vision, line of sight, are world constants and never read this field, so
 * a larger field of view changes no mechanic.
 *
 * ==============================================================================================
 * This engine has exactly one number, and two earlier claims to the contrary are byte-refuted
 * by reading the bytes.
 *
 * (1) "The world camera runs on arm 0, which never sees cam+0x38." FALSE.
 *     Arm 0 (0x476033) is the ORTHOGONAL arm: it divides by cam+0x44 (orthoScale, not the focal
 *     length) and writes cam+0x3C = 0 at 0x4760C4. A camera on arm 0 would have g_projScale == 0
 *     and project every vertex onto the screen centre. And there is exactly ONE camera in the
 *     image: rdCamera_new 0x475C50 has a single caller (0x417F7E, bapview_newView, with
 *     `push 0x42700000` = 60 degrees), rdCamera_init calls rdCamera_setProjType(cam, 1), and
 *     rdCamera_setProjType has no other caller. Arm 0 is dead code in this game.
 *
 * (2) "cam+0x40 is the knob for the vertical field of view." ALSO FALSE.
 *     It only divides vertical terms, but they are the CLIP EDGES, not the projection:
 *     0x47616D / 0x47619B / 0x4761CB write pProjection +0x1C/+0x20/+0x2C (slopeTop, slopeBottom),
 *     read by bapdraw_clipPoly and rdClip_testSphere. The world projection does NOT go through
 *     the camera's projector pointers cam+0x4C/+0x50 at all, bapvrt_frameSetup pulls focalPx
 *     into g_projScale and does the arithmetic itself, and all four rdCamera_project* routines
 *     have zero call sites.
 *     So cam+0x40 cannot widen the picture. It can only cut geometry away at top and bottom
 *     (factor < 1) or let the clipper pass polygons that land beside the canvas anyway
 *     (factor > 1). That is why there is no vertical slider.
 *
 * THE HONEST CONSEQUENCE: the vertical field of view falls out of the horizontal one and the
 * canvas height, vFOV = 2*atan((halfHeight/halfWidth) * tan(hFOV/2)), and is not a free
 * parameter. Instead of a second slider, the caption shows both numbers.
 *
 * ==============================================================================================
 * THE ONE COUPLING is the 3-D front-end menu: FUN_0045C3D8 places menu objects with a hard-coded
 * focal length of 554.256 at [0x4A8888], which has exactly ONE reader in the whole image
 * (0x45C431). Change the field of view or the resolution and the menu box moves. We repoint that
 * one operand at our own cell and keep it consistent, we do NOT edit the constant, because an
 * operand repoint affects one instruction and a constant edit affects every reader.
 */
#include "variable_fov.h"

#include "fov_math.h"
#include "fov_menu.h"
#include "fov_strings.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOV_SECTION "variable_fov"

/* --- 0x00475FFA  rdCamera_BuildProjection (function start) ----------------------------------- *
 *   55 8B EC 83 EC 24   prologue, 6 bytes, clean instruction boundary                           */
static const uint8_t SIG_RDCAMERA_BUILD_PROJECTION[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x04,
    0x89, 0x4D, 0xFC, 0x83, 0x7D, 0xFC, 0x00
};
#define BUILD_PROJECTION_PROLOGUE_SIZE 6u

/* --- 0x0045C431  the ONE reader of [0x4A8888] = 554.256 (3-D front-end menu focal) ------------ *
 *   D8 35 88 88 4A 00   fdiv dword ptr [0x4A8888]   -> operand at +0x02                         */
static const uint8_t SIG_MENU_3D_FOCAL[] = { 0xD8, 0x35, 0x88, 0x88, 0x4A, 0x00 };
#define MENU_3D_FOCAL_OPERAND_OFFSET 0x02

enum {
    SITE_RDCAMERA_BUILD_PROJECTION,
    SITE_MENU_3D_FOCAL,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("rdcamera_build_projection", SIG_RDCAMERA_BUILD_PROJECTION,
                           BUILD_PROJECTION_PROLOGUE_SIZE),
    SIGNATURE_ENTRY("menu_3d_focal",             SIG_MENU_3D_FOCAL)
};

/* The camera record, addressed as an int32 array so the documented byte offsets stay visible. */
#define CAMERA_PROJECTION_TYPE_INDEX  0    /* +0x00 */
#define CAMERA_CANVAS_INDEX           1    /* +0x04 */
#define CAMERA_FOV_DEGREES_INDEX     14    /* +0x38 */
#define CAMERA_FOCAL_INDEX           15    /* +0x3C */
#define CAMERA_PIXEL_ASPECT_INDEX    16    /* +0x40 */
#define CAMERA_ORTHO_SCALE_INDEX     17    /* +0x44 */
#define CAMERA_PROJECTION_PERSPECTIVE 1

#define CANVAS_X0_INDEX  6   /* +0x18 */
#define CANVAS_Y0_INDEX  7   /* +0x1C */
#define CANVAS_X1_INDEX  8   /* +0x20 */
#define CANVAS_Y1_INDEX  9   /* +0x24 */

typedef uint32_t (__cdecl *build_projection_fn_t)(int32_t *camera);

typedef struct variable_fov_config {
    bool           enabled;
    aspect_mode_t  aspect_mode;
    float          base_vertical_degrees;   /* 46.826 = the byte-native 4:3 vertical */
    float          extra_degrees;
    menu_3d_mode_t menu_3d_mode;
    bool           menu_slider;
    int            slider_min_fov_degrees;
    int            slider_max_fov_degrees;
    char           language[8];
} variable_fov_config_t;

typedef struct variable_fov_state {
    bool             installed;
    variable_fov_config_t config;
    detour_t         build_projection_detour;

    /* Our own cell for the front-end menu focal. It replaces [0x4A8888] for that ONE instruction. */
    volatile float   menu_focal_cell;

    /* The camera BuildProjection was last called on. rdCamera_BuildProjection is NOT a per-frame
     * function: its callers are rdCamera_setCanvas, setFov, setProjType, setOrthoScale,
     * setPixelAspect and bapview_rebuildCanvas (bapview's "the display mode changed" arm). So
     * changing the offset at run time does nothing until one of those happens, which in a
     * running level is the next level load. fov_refresh() exists for exactly that. */
    int32_t         *last_camera;
    int              last_width;
    int              last_height;
    float            last_horizontal_degrees;
    float            last_vertical_degrees;
    bool             log_pending;
} variable_fov_state_t;

static variable_fov_state_t fov_state;

/* ============================================================================================ */
static void load_config(void)
{
    int aspect_mode;
    int menu_mode;

    fov_state.config.enabled = ini_read_bool(FOV_SECTION, "Enabled", true);

    aspect_mode = ini_read_int(FOV_SECTION, "AspectMode", (int)ASPECT_MODE_HOR_PLUS);
    if (aspect_mode < 0 || aspect_mode >= (int)ASPECT_MODE_COUNT) {
        aspect_mode = (int)ASPECT_MODE_HOR_PLUS;
    }
    fov_state.config.aspect_mode = (aspect_mode_t)aspect_mode;

    menu_mode = ini_read_int(FOV_SECTION, "Menu3dMode", (int)MENU_3D_MODE_STRETCH);
    if (menu_mode < 0 || menu_mode >= (int)MENU_3D_MODE_COUNT) {
        menu_mode = (int)MENU_3D_MODE_STRETCH;
    }
    fov_state.config.menu_3d_mode = (menu_3d_mode_t)menu_mode;

    fov_state.config.base_vertical_degrees =
        fov_clamp_float(ini_read_float(FOV_SECTION, "BaseVerticalDegrees", 46.826f), 30.0f, 60.0f);
    fov_state.config.extra_degrees =
        /* NEGATIVE IS ALLOWED, and it has to be: on any frame wider than 4:3 the aspect correction
         * already puts the horizontal well above 60, so selecting 60 means subtracting. */
        fov_clamp_float(ini_read_float(FOV_SECTION, "ExtraDegrees", 0.0f), -120.0f, 120.0f);

    fov_state.config.menu_slider = ini_read_bool(FOV_SECTION, "MenuSlider", true);

    /* The slider is an ABSOLUTE angle, so its ends are the numbers the caption shows. The floor is
     * 60 because that is the authored horizontal field of view at 4:3, the narrowest view the
     * game was ever designed around, and a sensible bottom for a control whose top is a matter of
     * taste. */
    fov_state.config.slider_min_fov_degrees =
        (int)fov_clamp_float((float)ini_read_int(FOV_SECTION, "SliderMinFovDegrees", 60),
                             30.0f, 120.0f);
    fov_state.config.slider_max_fov_degrees =
        (int)fov_clamp_float((float)ini_read_int(FOV_SECTION, "SliderMaxFovDegrees", 120),
                             (float)(fov_state.config.slider_min_fov_degrees + 2), 170.0f);

    /* SliderMaxDegrees meant "the top of the range in degrees of OFFSET". Reading a tuned 40 as an
     * absolute angle would silently mean 40 deg of view, so the key is RENAMED rather than
     * reinterpreted, and a file still carrying it is told once. */
    if (ini_read_int(FOV_SECTION, "SliderMaxDegrees", -1) != -1) {
        log_warning("SliderMaxDegrees is no longer read. The slider now selects an ABSOLUTE field "
                    "of view instead of an offset, so its ends are SliderMinFovDegrees (%d) and "
                    "SliderMaxFovDegrees (%d), the numbers the caption shows.",
                    fov_state.config.slider_min_fov_degrees,
                    fov_state.config.slider_max_fov_degrees);
    }

    ini_read_string(FOV_SECTION, "Language", "", fov_state.config.language,
                    sizeof(fov_state.config.language));
}

/* ============================================================================================ */
static void update_menu_focal(int width, int height, float focal_pixels)
{
    float cell = fov_menu_focal_cell(fov_state.config.menu_3d_mode, width, height, focal_pixels);

    if (cell > 0.0f) {
        fov_state.menu_focal_cell = cell;
    }
}

/* Writes the field of view this canvas should have into rdCamera+0x38 and keeps the 3-D menu
 * focal in step. Called immediately before the engine's own BuildProjection, which then derives
 * focal (+0x3C) and the six frustum planes (+0x48) from it, so the pair can never fall out of
 * sync, which is the failure mode other FOV patches for this game exhibit. */
static void apply_fov(int32_t *camera)
{
    int32_t *canvas;
    int      width;
    int      height;
    float    horizontal;
    float    half_width;
    float    half_height;

    if (camera[CAMERA_PROJECTION_TYPE_INDEX] != CAMERA_PROJECTION_PERSPECTIVE) {
        return;
    }

    canvas = (int32_t *)(uintptr_t)camera[CAMERA_CANVAS_INDEX];
    if (canvas == NULL || !memory_is_readable_range((uintptr_t)canvas,
                                                    (CANVAS_Y1_INDEX + 1) * sizeof(int32_t))) {
        return;
    }

    /* +1: rdCanvas stores x1 = vbufWidth - 1 (0x47691E), so the span is x1-x0+1. Without it a
     * 640x480 canvas reported 639x479 and the field of view came out 60.025 instead of 60.000. */
    width  = canvas[CANVAS_X1_INDEX] - canvas[CANVAS_X0_INDEX] + 1;
    height = canvas[CANVAS_Y1_INDEX] - canvas[CANVAS_Y0_INDEX] + 1;
    if (width <= 0 || height <= 0) {
        return;
    }

    horizontal = fov_horizontal_degrees(fov_state.config.aspect_mode,
                                        fov_state.config.base_vertical_degrees,
                                        fov_state.config.extra_degrees, width, height);
    if (horizontal > 0.0f) {
        *(float *)&camera[CAMERA_FOV_DEGREES_INDEX] = horizontal;
    } else {
        /* ASPECT_MODE_STRETCH: hands off, but the caption must still name the TRUE number, not
         * the last one we computed. */
        horizontal = *(float *)&camera[CAMERA_FOV_DEGREES_INDEX];
    }

    /* The vertical is computed with THE ENGINE'S OWN half extents, i.e. (x1-x0)/2 and (y1-y0)/2
     * WITHOUT the +1 above. 0x476113 and 0x47612E form exactly those two numbers, and only that
     * way does the caption name the angle that is really on screen. */
    half_width  = (float)(canvas[CANVAS_X1_INDEX] - canvas[CANVAS_X0_INDEX]) * 0.5f;
    half_height = (float)(canvas[CANVAS_Y1_INDEX] - canvas[CANVAS_Y0_INDEX]) * 0.5f;
    fov_state.last_vertical_degrees = fov_vertical_degrees(horizontal, half_width, half_height);

    if (width != fov_state.last_width || height != fov_state.last_height ||
        horizontal != fov_state.last_horizontal_degrees) {
        float focal = fov_focal_pixels(horizontal, (float)width * 0.5f);

        update_menu_focal(width, height, focal);
        fov_state.log_pending = true;
        log_info("canvas %dx%d -> projType %d, hFOV %.3f deg, vFOV %.3f deg, expected focal %.2f, "
                 "menu cell %.3f",
                 width, height, (int)camera[CAMERA_PROJECTION_TYPE_INDEX], (double)horizontal,
                 (double)fov_state.last_vertical_degrees, (double)focal,
                 (double)fov_state.menu_focal_cell);

        fov_state.last_width  = width;
        fov_state.last_height = height;
        fov_state.last_horizontal_degrees = horizontal;
    }
}

static void log_projection_readback(const int32_t *camera)
{
    if (!fov_state.log_pending) {
        return;
    }
    fov_state.log_pending = false;

    /* Only NOW does cam+0x3C hold anything. This readback is the actual measurement: it comes
     * from the engine, not from our arithmetic, and through g_projScale it IS the projection. */
    log_info("after the rebuild, cam+0x38 %.3f, cam+0x3C %.2f, cam+0x40 %.4f, cam+0x44 %.4f. "
             "cam+0x3C lands in g_projScale once per frame and IS the projection.",
             (double)*(const float *)&camera[CAMERA_FOV_DEGREES_INDEX],
             (double)*(const float *)&camera[CAMERA_FOCAL_INDEX],
             (double)*(const float *)&camera[CAMERA_PIXEL_ASPECT_INDEX],
             (double)*(const float *)&camera[CAMERA_ORTHO_SCALE_INDEX]);
}

static uint32_t __cdecl hook_build_projection(int32_t *camera)
{
    build_projection_fn_t original =
        (build_projection_fn_t)fov_state.build_projection_detour.original;
    uint32_t result;

    if (camera != NULL) {
        fov_state.last_camera = camera;
        apply_fov(camera);
    }

    result = original(camera);

    if (camera != NULL) {
        log_projection_readback(camera);
    }
    return result;
}

/* Re-derives the projection from the CURRENT configuration without waiting for a canvas event.
 * This is what makes the slider mean anything: rdCamera_BuildProjection is a pure recompute,
 * it reads pCanvas, projType and fovDeg and writes focal plus the frustum record, with no
 * allocation and no global (byte-read at 0x475FFA..0x4760F9), so calling it again is exactly
 * what the engine's own five setters do. */
static void fov_refresh(void)
{
    build_projection_fn_t original =
        (build_projection_fn_t)fov_state.build_projection_detour.original;

    if (!fov_state.installed || original == NULL) {
        return;
    }
    if (fov_state.last_camera == NULL) {
        /* The slider can sit in the main menu BEFORE any level has run, then no camera exists
         * yet. That is not an error, but it must be in the log, or the next person looks for the
         * effect in the wrong place. The value takes hold at the next level start. */
        log_info("no camera yet, no level has been loaded. The setting is saved and takes "
                 "effect at the next level start.");
        return;
    }

    apply_fov(fov_state.last_camera);
    original(fov_state.last_camera);
    log_projection_readback(fov_state.last_camera);
}

/* ============================================================================================ */
bool  variable_fov_is_active(void)          { return fov_state.installed; }
float variable_fov_extra_degrees(void)      { return fov_state.config.extra_degrees; }
float variable_fov_horizontal_degrees(void) { return fov_state.last_horizontal_degrees; }
float variable_fov_vertical_degrees(void)   { return fov_state.last_vertical_degrees; }
int   variable_fov_slider_min_fov_degrees(void) { return fov_state.config.slider_min_fov_degrees; }
int   variable_fov_slider_max_fov_degrees(void) { return fov_state.config.slider_max_fov_degrees; }

/* The aspect mode's own answer, with the offset taken out of the question. Recomputed rather than
 * derived by subtraction: the live value has been through a clamp, and at the ends of the range
 * subtracting the offset from it would quietly give a base that was never used. */
float variable_fov_base_horizontal_degrees(void)
{
    return fov_horizontal_degrees(fov_state.config.aspect_mode,
                                  fov_state.config.base_vertical_degrees,
                                  0.0f, fov_state.last_width, fov_state.last_height);
}

void variable_fov_set_extra_degrees(float degrees)
{
    fov_state.config.extra_degrees = fov_clamp_float(degrees, -120.0f, 120.0f);

    fov_refresh();

    if (!ini_write_float(FOV_SECTION, "ExtraDegrees", fov_state.config.extra_degrees, 1)) {
        log_warning("ExtraDegrees could not be written to %s (%lu), the setting is live but NOT "
                    "saved", ini_path(), (unsigned long)GetLastError());
    }
}

/* ============================================================================================ */
static void repoint_menu_focal(void)
{
    uintptr_t operand;
    uint32_t  previous;

    if (sites[SITE_MENU_3D_FOCAL].address == 0) {
        log_warning("menu_3d_focal did not resolve, the 3-D front-end menu will be MIS-PLACED at "
                    "any resolution or field of view other than 640x480 / 60 deg");
        return;
    }

    operand = sites[SITE_MENU_3D_FOCAL].address + MENU_3D_FOCAL_OPERAND_OFFSET;
    if (!memory_read_u32(operand, &previous)) {
        return;
    }
    if (patch_repoint_operand(operand, previous,
                              (uint32_t)(uintptr_t)&fov_state.menu_focal_cell)
        == PATCH_RESULT_OK) {
        log_info("3-D menu focal operand at %08X repointed from [%08X] to our own cell",
                 (unsigned)operand, (unsigned)previous);
    }
}

void variable_fov_install(void)
{
    log_init("variable_fov", false);

    if (fov_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the field of view is NOT patched");
        return;
    }

    fov_state.menu_focal_cell = 554.256f;          /* the retail value of [0x4A8888] */

    load_config();
    fov_strings_init(fov_state.config.language);

    if (!fov_state.config.enabled) {
        log_info("Enabled=0, the field of view is left as the engine set it");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_RDCAMERA_BUILD_PROJECTION].address == 0) {
        log_warning("rdcamera_build_projection did not resolve, the field of view is NOT patched");
        return;
    }
    if (!detour_install(&fov_state.build_projection_detour,
                        sites[SITE_RDCAMERA_BUILD_PROJECTION].address,
                        (const void *)hook_build_projection,
                        BUILD_PROJECTION_PROLOGUE_SIZE)) {
        log_error("the rdCamera_BuildProjection detour failed, the field of view is NOT patched");
        return;
    }

    fov_state.installed = true;
    log_info("hooked rdCamera_BuildProjection at %08X (aspect mode %d, base vertical %.3f, "
             "extra %.2f). cam+0x40 and cam+0x44 are NOT touched, they only affect clip edges "
             "and the dead orthographic arm.",
             (unsigned)sites[SITE_RDCAMERA_BUILD_PROJECTION].address,
             (int)fov_state.config.aspect_mode, (double)fov_state.config.base_vertical_degrees,
             (double)fov_state.config.extra_degrees);

    repoint_menu_focal();

    if (fov_state.config.menu_slider) {
        fov_menu_install();
    } else {
        log_info("MenuSlider=0, no field-of-view slider in the video options screen");
    }
}
