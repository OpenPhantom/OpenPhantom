/* cutscene_pose_sync.h: keep the player's own render-interpolation state from going stale right as
 * a cutscene begins.
 *
 * Produces: cutscene_pose_sync.dll
 */
#ifndef CUTSCENE_POSE_SYNC_H
#define CUTSCENE_POSE_SYNC_H

void cutscene_pose_sync_install(void);

#endif /* CUTSCENE_POSE_SYNC_H */
