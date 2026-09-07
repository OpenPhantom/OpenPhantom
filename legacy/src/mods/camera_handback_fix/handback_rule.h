/* handback_rule.h: whether a dialogue that has just closed owes the camera back, with no engine
 * in it.
 *
 * The engine keeps one flag saying a script owns the camera. While it is set, the camera update
 * takes its region from the shot that script forced instead of the region the player is standing
 * in, so the flag decides what the player is looking through. `Dialog_Close` is supposed to clear
 * it and gets the condition wrong:
 *
 *     if (Dialog_LeaveInputLock(1) != 0 && choiceCount != 0) bapview_overrideOff();
 *
 * The count is the number of rows in the choice MENU, and a new spoken line always zeroes it. So
 * an ordinary line that names a camera group, which is most of them, closes with the count at zero
 * and never gives the camera back. Measured in the field: the flag went up on one such line and
 * the next write to it was the level tearing down.
 *
 * The lock half of that test is not the bug and is kept, in the form below. It is what stops this
 * from stealing a camera that a CUTSCENE is holding: a cutscene takes the input lock to level 5,
 * `Dialog_LeaveInputLock(1)` refuses to unwind anything above 1, and the lock is therefore still
 * standing when a dialogue nested inside it closes.
 */
#ifndef CAMERA_HANDBACK_FIX_HANDBACK_RULE_H
#define CAMERA_HANDBACK_FIX_HANDBACK_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* True when the camera has to be handed back, given the three things known at the moment a
 * dialogue has finished closing:
 *
 *   `took_it`   the dialogue itself is what set the flag, rather than a cutscene, a menu, the
 *               tripod gun or the fall-death camera. Nothing else's camera is ever touched.
 *   `flag`      the scripted-camera flag now. Zero means the engine gave it back on its own,
 *               which happens whenever a choice menu was open, and there is nothing owing.
 *   `lock`      the cinematic input lock now. Anything but zero means somebody above the dialogue
 *               is still running and the camera is theirs until they say otherwise.
 */
bool handback_rule_owes_camera(bool took_it, int32_t flag, int32_t lock);

#endif /* CAMERA_HANDBACK_FIX_HANDBACK_RULE_H */
