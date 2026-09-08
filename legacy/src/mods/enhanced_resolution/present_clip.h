/* present_clip.h: let a window that is not at the screen's origin show the whole picture.
 *
 * ==============================================================================================
 * What is wrong, and where
 *
 * With WindowedPresent on, the graphics wrapper copies the engine's surface into its own back
 * buffer before presenting. Before it copies, it clips. It takes the surface rectangle, which
 * begins at the origin and is the size the engine renders, and intersects it with the window's
 * client rectangle expressed in DESKTOP coordinates, then copies the overlap at one pixel for one
 * pixel. Two rectangles in two different coordinate spaces are being intersected as though they
 * were in one, and the result only happens to be right when the window's client area sits at the
 * desktop origin.
 *
 * Three cases come out of that arithmetic, and only the middle one is wrong:
 *
 *   the client CONTAINS the surface rectangle   nothing is clipped, the whole picture is sent
 *   the client OVERLAPS it in part              a fragment is sent, stretched over the window
 *   the client MISSES it entirely               the wrapper sends the whole surface instead
 *
 * The third case is the interesting one. When the intersection comes out empty the wrapper leaves
 * both rectangles null and asks for a whole-surface to whole-back-buffer copy, and the present
 * then scales that to the client area. That is exactly the behaviour we want, and it is already
 * written: it is what the code does when its own clip fails.
 *
 * ==============================================================================================
 * So the correction is to make the clip fail
 *
 * The wrapper reads the client rectangle through GetClientRect. That import is replaced in the
 * wrapper's own import table, and only there, so that this module's answer reaches the clip and
 * nothing else in the process is affected. Our own code keeps the truth from the same function,
 * which matters: focus_guard confines the pointer to the client area and window_fit measures it,
 * and both would be wrong if the lie were global.
 *
 * The answer given is an all-zero rectangle. Whatever the wrapper then maps it to, the intersection
 * is empty at every window position, so the whole-surface path is taken every time.
 *
 * ==============================================================================================
 * Why it lies in one case and not in three
 *
 * The first and third cases above already put the whole picture on the screen, so there is nothing
 * to correct in them and correcting anyway would be a change with no purpose and some risk. The
 * risk is real rather than theoretical: the same GetClientRect is read by other parts of the
 * wrapper, and while the ones that matter for a running game are on the present path, two of them
 * feed its resize detection at device creation and one serves a present path this configuration
 * does not take. Confining the lie to the case that is broken today means that if any of that
 * reading is wrong, the damage is confined to a configuration which does not work anyway.
 */
#ifndef PRESENT_CLIP_H
#define PRESENT_CLIP_H

#include <stdbool.h>

typedef struct present_clip_config {
    /* WindowedFill. Off leaves the wrapper's own arithmetic alone, which is what every release
     * before this one did, and is the way to see the uncorrected behaviour for comparison. */
    bool enabled;

    /* Only so the "not installed" line can say WHICH of the two reasons it was. */
    bool windowed_present;
} present_clip_config_t;

/* Returns true only when the import was replaced and the correction is live from now on. Every
 * other outcome logs which one it was, because a feature that is quietly absent reads exactly
 * like a feature that is quietly broken. */
bool present_clip_install(const present_clip_config_t *config);

/* Switches the correction on and off while the game is running. The import stays replaced either
 * way and the thunk simply stops correcting, because putting an import back is the one part of
 * this that is not safely repeatable. */
void present_clip_set_enabled(bool enabled);

#endif /* PRESENT_CLIP_H */
