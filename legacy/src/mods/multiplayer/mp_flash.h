/* mp_flash.h: the white screen of a thermal detonator, only when it is near or in the picture.
 *
 * Layer 2. The engine has no distance test for this: a detonator fills the screen white for 1.3
 * seconds wherever it goes off, which in single player is always the player's own throw and in a
 * session is anyone's. The rule is ours.
 *
 * It is built as a TABLE ENTRY and not as a detour. The engine keeps each shot kind's event
 * handler in its shot table, this feature already resolves that table, and the arm that flashes
 * contains the flash and nothing else. So the entry is written when a session stands and put back
 * when it ends, which is what "no multiplayer code runs without a lobby" looks like when it can
 * be arranged structurally instead of asked at run time.
 */
#ifndef MULTIPLAYER_MP_FLASH_H
#define MULTIPLAYER_MP_FLASH_H

#include "mp_flash_rule.h"

#include <stdbool.h>
#include <stdint.h>


/* Writes our arm into the shot table, and puts the engine's back. Arming twice or disarming what
 * was never armed does nothing. A disarm that finds a stranger's pointer in the entry leaves it
 * alone and says so: something else took the kind while the session ran. */
bool mp_flash_arm(void);
void mp_flash_disarm(void);

/* How near counts as near, from the ini. Set once when the module installs, because nothing on
 * the path that arms a session can reach the configuration. */
void mp_flash_set_near(uint32_t units);

void mp_flash_report(void);

#endif /* MULTIPLAYER_MP_FLASH_H */
