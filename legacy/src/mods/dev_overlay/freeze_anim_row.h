/* freeze_anim_row.h: the Free camera group's switch for holding the animations with the pause.
 *
 * With the simulation held, by the panel while it is open or by the free camera while it flies,
 * every walk cycle still ran on the spot and every idle swayed, because the puppet tracks are
 * stepped by the draw and not by the substeps. The engine's own pause menu stops them through a
 * draw latch of its own, and sim_pause writes that latch beside the flag while this is on. Off
 * as shipped, so the pause keeps the look it had; on holds everything exactly where it is. Kept
 * as [dev_overlay] PauseFreezesAnimation.
 */
#ifndef DEV_OVERLAY_FREEZE_ANIM_ROW_H
#define DEV_OVERLAY_FREEZE_ANIM_ROW_H

#include <stdbool.h>

/* Reads the setting and hands it to sim_pause. Called once at install, after sim_pause's. */
void freeze_anim_row_load(void);

bool freeze_anim_row_get(void);
bool freeze_anim_row_set(bool enabled);

/* Whether the draw latch resolved, without which the switch can do nothing. */
bool freeze_anim_row_available(void);

#endif /* DEV_OVERLAY_FREEZE_ANIM_ROW_H */
