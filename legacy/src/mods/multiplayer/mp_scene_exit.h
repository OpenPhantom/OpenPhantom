/* mp_scene_exit.h: the one way out of a scene's world, as a cell.
 *
 * Layer 1, one pointer. Every way out of a level or a session already runs the enemy table's reset
 * (mp_enemy_sync_reset): a level's end, a restart, a savegame restored, the module's init, the
 * session's arming and its teardown. A scene gathered in that world has to end there as well, with
 * its hold taken back, its fade given back and its lock released, or the next world inherits them.
 * The reset calls this, and the gathering puts its exit here once it is installed.
 *
 * It is a module of its own with nothing under it for the reason mp_armed is: the enemy table is
 * linked into a dozen test programs that know nothing of scenes, and a direct call would drag the
 * gathering and its engine binding into every one of them.
 */
#ifndef MULTIPLAYER_MP_SCENE_EXIT_H
#define MULTIPLAYER_MP_SCENE_EXIT_H

typedef void (*mp_scene_exit_fn_t)(void);

/* The gathering's, once, at its install. NULL takes it out again. */
void mp_scene_exit_set(mp_scene_exit_fn_t exit);

/* From every way out of a level or a session. Nothing when no exit is set. */
void mp_scene_exit_run(void);

#endif /* MULTIPLAYER_MP_SCENE_EXIT_H */
