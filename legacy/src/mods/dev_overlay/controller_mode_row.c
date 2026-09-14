/* controller_mode_row.c: see controller_mode_row.h. */
#include "controller_mode_row.h"

#include "air_control_row.h"
#include "camera_follow_row.h"
#include "free_look_row.h"
#include "strafe_row.h"

bool controller_mode_row_get(void)
{
    return strafe_row_get() && free_look_row_get() && camera_follow_row_get() &&
           air_control_row_get();
}

bool controller_mode_row_set(bool enabled)
{
    /* On: strafe first, because the two rows built on free look are only offered while it is on,
     * then free look, then the two. Off: free look first, which takes the two down with it as its
     * own row does, then strafe. Every key is written through its own row so the rules those rows
     * keep, which one switches which on, hold here as well. */
    if (enabled) {
        return strafe_row_set(true) && free_look_row_set(true) && camera_follow_row_set(true) &&
               air_control_row_set(true);
    }
    return free_look_row_set(false) && strafe_row_set(false);
}
