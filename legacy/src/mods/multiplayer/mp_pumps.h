/* mp_pumps.h: the bridge's two idle pumps.
 *
 * Armed once, from the arming that both ways into a session share.
 */
#ifndef MULTIPLAYER_MP_PUMPS_H
#define MULTIPLAYER_MP_PUMPS_H

/* Adds the frame hook and the thread timer that keep the sessions serviced between substeps and
 * through a level load. Runs on the thread that will run the engine's message loop. */
void mp_pumps_arm(void);

#endif /* MULTIPLAYER_MP_PUMPS_H */
