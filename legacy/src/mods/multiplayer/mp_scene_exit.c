/* mp_scene_exit.c: the one way out of a scene's world, as a cell. See the header. */
#include "mp_scene_exit.h"

#include <stddef.h>

static mp_scene_exit_fn_t scene_exit;

void mp_scene_exit_set(mp_scene_exit_fn_t exit)
{
    scene_exit = exit;
}

void mp_scene_exit_run(void)
{
    if (scene_exit != NULL) {
        scene_exit();
    }
}
