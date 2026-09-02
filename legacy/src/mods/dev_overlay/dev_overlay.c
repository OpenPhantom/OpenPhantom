/* dev_overlay.c: brings the four parts up in the order their dependencies allow.
 *
 * The order is not arbitrary. Drawing has to resolve before input, because a panel that can be
 * opened but not painted is a game that has gone modal with nothing on screen and no way out that
 * a player would find. So input, which is the only thing that can open it, is installed last and
 * only if everything it would show has already answered for itself.
 *
 * The cheats are the exception in the other direction: they are installed first and a failure
 * there is not fatal. A panel with one working tab is worth having, and the log names which half
 * is missing.
 */
#include "dev_overlay.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "cheats_original_actions.h"
#include "input_freeze.h"
#include "sim_pause.h"
#include "overlay_draw.h"
#include "overlay_input.h"
#include "overlay_model.h"

#include "common/patch.h"
#include "common/signature.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>

#define DEV_OVERLAY_SECTION "dev_overlay"

static bool overlay_entered;

/* ==============================================================================================
 * WHERE THE PAINT HAPPENS, AND WHY IT MOVED
 *
 * The panel was first painted from the shared per-frame hook, which runs its callbacks AFTER the
 * function it sits on. That function ends by closing the scene and flipping the page, so the panel
 * was drawn into a buffer that had already been shown. It appeared a frame late and, because the
 * engine does not clear between frames, it stayed there: moving the pointer smeared it. That is
 * what ghosting looked like.
 *
 * The right instant is after the world and its overlays are drawn and before the scene is closed.
 * The engine puts both of those next to each other:
 *
 *   0046C32D  6A 00 E8 .. 83 C4 04            the call before it, which is what makes this unique
 *   0046C337  C7 05 C4 6F 86 00 00 00 00 00   a flag, cleared
 *   0046C341  E8 BA B8 01 00                  close the scene      <- this call is redirected
 *   0046C346  E8 07 2D 02 00                  show the page
 *
 * The three instructions at the top are not decoration. Without them the pattern also matches a
 * flag-clear followed by two calls at 0x00401D3E, in the startup path, and redirecting that would
 * have put the panel somewhere entirely different.
 *
 * So the call that closes the scene is redirected here. The panel is drawn, then the original runs
 * exactly as it was. Nothing else about the frame changes, and the engine's own readouts, which are
 * drawn a few instructions earlier, still sit under the panel where they belong.
 * ============================================================================================ */
static const uint8_t SIG_SCENE_END[] = {
    0x6A, 0x00,                                                   /* push 0          */
    0xE8, 0x00, 0x00, 0x00, 0x00,                                 /* a call          */
    0x83, 0xC4, 0x04,                                             /* add esp,4       */
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* a flag, cleared */
    0xE8, 0x00, 0x00, 0x00, 0x00,                                 /* close the scene */
    0xE8, 0x00, 0x00, 0x00, 0x00                                  /* show the page   */
};
static const uint8_t MSK_SCENE_END[] = {
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_SCENE_END) == sizeof(MSK_SCENE_END),
               "the scene end pattern and its mask are different lengths");
#define OFFSET_SCENE_END_CALL 20u

typedef void (__cdecl *scene_end_fn_t)(void);
static scene_end_fn_t scene_end_original;

static void __cdecl hook_scene_end(void)
{
    if (overlay_input_is_open()) {
        /* THE PANEL CLOSES ITSELF WHEN IT CANNOT BE SEEN.
         *
         * This is one of three places the scene is closed from; the other two belong to the front
         * end and to a movie. While one of those is running the panel would be open, the player
         * held and every key swallowed, with nothing painted, and the only way out would be a key
         * the person could not know still worked. Refusing to draw is the wrong answer to that;
         * closing is the right one, because the panel holding the game is the part that matters. */
        if (!overlay_draw_paint()) {
            overlay_input_close();
        } else {
            overlay_input_update_pointer();
            overlay_input_update_scroll();
            overlay_model_rebuild();
        }
    }
    scene_end_original();
}

static bool redirect_scene_end(void)
{
    uintptr_t site = signature_find_unique(SIG_SCENE_END, MSK_SCENE_END, sizeof SIG_SCENE_END);
    uintptr_t call;
    uintptr_t original = 0;

    if (site == 0) {
        log_warning("the end of the scene did not resolve, so there is no instant at which the "
                    "panel could be drawn into the picture. It is not installed.");
        return false;
    }
    call = site + OFFSET_SCENE_END_CALL;
    if (!patch_read_call_target(call, &original)) {
        log_warning("no usable call at %08X", (unsigned)call);
        return false;
    }
    scene_end_original = (scene_end_fn_t)original;
    if (patch_redirect_call(call, (const void *)&hook_scene_end) != PATCH_RESULT_OK) {
        log_warning("the call at %08X was not redirected", (unsigned)call);
        return false;
    }
    log_info("the panel is drawn just before the scene closes, at %08X, so it composites with the "
             "finished picture instead of landing in the next one", (unsigned)call);
    return true;
}

void dev_overlay_install(void)
{
    bool cheats_ready;

    if (overlay_entered) {
        return;
    }
    overlay_entered = true;

    log_init("dev_overlay", false);
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the overlay cannot open");
        return;
    }
    if (!ini_read_bool(DEV_OVERLAY_SECTION, "Enabled", true)) {
        log_info("disabled");
        return;
    }

    overlay_model_reset();
    overlay_input_set_key(ini_read_int(DEV_OVERLAY_SECTION, "OpenKey", 0));
    overlay_draw_set_align(ini_read_int(DEV_OVERLAY_SECTION, "TextAlign", 1));

    /* Either half is worth having on its own, so both are attempted and neither decides the
     * outcome. What decides it is whether anything at all can be offered. */
    cheats_ready = cheats_original_resolve();
    cheats_ready = cheats_original_actions_resolve() || cheats_ready;
    cheats_ready = cheats_openphantom_install() || cheats_ready;
    if (!cheats_ready) {
        log_warning("neither the game's own cheats nor this project's own could be reached, so "
                    "there is nothing for the overlay to show and it is not installed");
        return;
    }

    if (!overlay_draw_resolve()) {
        return;
    }
    if (!redirect_scene_end()) {
        return;
    }
    /* The freeze is not a condition of the panel. If it does not arm, the panel still opens and
     * still switches cheats; the player simply keeps moving behind it, which is what happened
     * before this existed. The log says which of the two the session got. */
    (void)input_freeze_install();
    /* Neither is a condition of the panel, and they fail independently: one stops the player being
       given orders, the other stops the simulation stepping at all. */
    (void)sim_pause_install();

    if (!overlay_input_install()) {
        return;
    }

    /* Free camera's fly speed reads the scroll wheel, which is only ever observable through
     * window messages - overlay_input.c's own domain. Wired here, after both installs have run,
     * rather than cheats_openphantom.c calling overlay_input_take_wheel_delta() by name, so that
     * linking cheats_openphantom.c on its own (the unit test built against the real cheat
     * sources, see unittests/CMakeLists.txt) never has to drag in the whole message-hook
     * subsystem just to satisfy one symbol it never exercises. */
    cheats_openphantom_set_wheel_source(&overlay_input_take_wheel_delta);

    log_info("The key below Escape opens the Cheatmenu. The panel is drawn into the "
             "game's own frame, so it needs "
             "no window and cannot take the focus. %s",
             input_freeze_is_available()
                 ? (sim_pause_is_available()
                        ? "While it is open the game reads nothing from the device and the "
                          "simulation itself is held on the engine's own pause flag, so neither "
                          "the player nor the world moves behind the panel."
                        : "While it is open the game reads nothing from the device, so the player "
                          "stops; the simulation pause did NOT arm, so the world keeps running "
                          "behind the panel.")
                 : (sim_pause_is_available()
                        ? "The input freeze did NOT arm, so the player keeps taking orders behind "
                          "the panel, though the simulation itself is held."
                        : "Neither the input freeze nor the simulation pause armed, so the game "
                          "carries on entirely behind the panel."));
}
