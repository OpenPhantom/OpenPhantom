/* camera_follow_row.c: see camera_follow_row.h. */
#include "camera_follow_row.h"

#include "free_look_row.h"
#include "strafe_row.h"

#include "common/ini.h"

#define INPUT_SECTION       "enhanced_input"
#define CAMERA_FOLLOW_KEY   "CameraFollow"

bool camera_follow_row_get(void)
{
    /* The default is enhanced_input's own shipped default, and the two have to stay in step: a row
     * reading OFF while the feature is ON would be worse than no row at all. */
    return ini_read_bool(INPUT_SECTION, CAMERA_FOLLOW_KEY, false);
}

bool camera_follow_row_set(bool enabled)
{
    /* FREE LOOK GOES ON WITH IT, and the write is done here rather than asked of enhanced_input.
     *
     * The passive camera aims at the body's heading, and that only follows the player because free
     * look turns the body to face where it travels; without it there is nothing to follow. The
     * dependency is one way, so switching this OFF leaves free look alone.
     *
     * Both keys are written and neither feature is called. enhanced_input re-reads these keys once
     * a second and applies them in its own order, with its own refusals: free look declines while
     * the player phases are stopped, which is exactly the state the game is in while this menu is
     * open, so anything that asked it directly from here would be refused every time. */
    if (enabled && !free_look_row_get() && !free_look_row_set(true)) {
        return false;
    }
    return ini_write_int(INPUT_SECTION, CAMERA_FOLLOW_KEY, enabled ? 1 : 0);
}

bool camera_follow_row_available(void)
{
    /* Asked of the same key the strafe row edits, rather than of enhanced_input, so this answers
     * correctly with that DLL absent and answers immediately when the row above is flipped. */
    return strafe_row_get();
}
