/* camera_handback_fix.h: a dialogue gives the camera back when it closes.
 *
 * ============================ The fault, as it reaches a player ================================
 *
 * Otoh Gunga, after Jar Jar joins and while running up the escape route: the camera stops
 * following and cannot be turned back on. The developer menu row still reads ON, because the row
 * and the feature are both fine. What has gone is underneath them.
 *
 * ============================ What is actually wrong ===========================================
 *
 * The engine keeps one flag at a fixed cell saying "a script owns the camera", with a companion
 * cell holding the region that script forced. While the flag is set, `bapview_updateCam` takes its
 * region index from the forced cell instead of the region the player is standing in. The flag is
 * not advisory: it decides which camera the level uses, and the save writer copies it into the
 * savegame block, so a save taken while it is stuck carries the fault out of the session.
 *
 * Two functions in the whole image write it, and seven callers take against six that release.
 * `Dialog_SpeakSingle` takes it for any spoken line that names a camera group, which is most
 * lines. `Dialog_Close` releases it like this:
 *
 *     if (Dialog_LeaveInputLock(1) != 0 && choiceCount != 0) bapview_overrideOff();
 *
 * `choiceCount` is the number of rows in the choice MENU, and `Dialog_SpeakSingle` sets it to zero
 * at the top of every new line, with its own comment in the decompilation calling that
 * unconditional. An ordinary line therefore closes with the count at zero and the camera is never
 * handed back. It stays taken until the level is reloaded, because nothing else clears it.
 *
 * ============================ How that was established =========================================
 *
 * Not by reading. A census on both writers logged every take and release with the caller that
 * asked for it, and three field runs said the same thing: every take in the level came from a
 * spoken line, the healthy ones were released by the dialogue closing or by the cutscene opcode a
 * moment later, and the one that broke the camera had no release after it at all until the level
 * tore down. A fourth run watched `Dialog_Close` itself and reported the count as zero, which is
 * what separates the two halves of that condition and says the count is what refused.
 *
 * The three other candidates all died on the evidence rather than on argument: the cutscene opcode
 * takes the camera and raises the lock to 5, the tripod gun and the fall-death camera take it too,
 * and not one of them ever fired as a leak in any run.
 *
 * ============================ Why the lock half is kept ========================================
 *
 * Only the count is dropped. The lock test is the thing that stops a dialogue from stealing a
 * camera a CUTSCENE is holding: a cutscene takes the input lock to level 5, `Dialog_LeaveInputLock`
 * refuses to unwind anything above the level it is asked for, and the lock is still standing when
 * a dialogue nested inside it closes. This keeps that guard and states it directly, as "nobody
 * above this dialogue is still holding the lock".
 *
 * And it goes one better than restoring the engine's intent, because the engine's own release can
 * never fire for a dialogue that took no input lock at all: `Dialog_LeaveInputLock` returns zero
 * when the lock is already zero, so that branch is unreachable for a bark. Asking whether the lock
 * is clear NOW covers both, where deleting the count test alone would have left the second.
 *
 * Nothing else's camera is ever touched. The take is attributed to the dialogue by the address
 * control returns to, derived from the resolved `Dialog_SpeakSingle` rather than written down, so
 * a take from the cutscene opcode, a menu, the tripod gun or the fall-death camera is remembered
 * as not ours and left alone.
 */
#ifndef CAMERA_HANDBACK_FIX_H
#define CAMERA_HANDBACK_FIX_H

void camera_handback_fix_install(void);

#endif /* CAMERA_HANDBACK_FIX_H */
