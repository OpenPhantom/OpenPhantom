/* window_poll.h: notice when the dev panel changes a window setting, and apply it.
 *
 * The dev panel is a different DLL, and the rule in this tree is that feature DLLs do not depend on
 * each other. What they share is the ini file: a row writes its key, and whoever owns the setting
 * reads it back and acts. That is how every other row that reaches out of dev_overlay works, and
 * this is the reading half for the window group.
 *
 * Once a second rather than once a frame. A player moves a slider or presses a row and then waits
 * to see what happened, so a second is not felt, while a file read every frame at a hundred frames
 * a second would be a hundred file reads a second for a setting that changes twice in a session.
 * The same interval and the same reasoning as the other polled settings in this patch.
 *
 * Only the settings that CAN change while the game runs are polled. WindowedPresent decides how the
 * device is built and the device is built once, so it is read at startup and never here; the panel
 * says as much on its own row rather than letting a player wait for something that is not coming.
 */
#ifndef WINDOW_POLL_H
#define WINDOW_POLL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct window_poll_config {
    /* Seeded with what was read at startup, so the first poll compares against what is actually in
     * force and does not re-apply the whole lot one second in. */
    int32_t mode;
    int32_t windowed_width;
    int32_t windowed_height;
    int32_t pointer_release_key;
    bool    windowed_fill;
} window_poll_config_t;

bool window_poll_install(const window_poll_config_t *config);

/* Tells the poll that something else in this DLL has already written and applied a window mode,
 * so it does not read that change back as one it has yet to make and apply it a second time.
 * Re-seeding the shadow like this is the third part of the rule every setting with two writers
 * in this patch follows. */
void window_poll_note_mode(int32_t mode);

/* Whether a change of window shape can be shown NOW rather than on the next start.
 *
 * It cannot when the device was not built windowed, because an exclusive device owns the screen
 * and reshaping the window under it makes the engine set the real display resolution instead;
 * and it cannot for the engine's own shape, because that shape goes with an exclusive device and
 * the device is built once. Both cases are what the panel's fullscreen row means when it says it
 * takes effect on a restart, and this is what makes that true rather than merely written. */
bool window_poll_shape_is_live(int32_t mode);

#endif /* WINDOW_POLL_H */
