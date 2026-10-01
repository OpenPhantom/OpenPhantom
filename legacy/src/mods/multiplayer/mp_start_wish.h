/* mp_start_wish.h: the two wishes the lobby leaves behind for a level, internal to mp_start.
 *
 * The public calls, the pose and the hero a lobby asks for and the pure decisions under them, are
 * declared in mp_start.h, because they are the start's to offer. What is here is only how
 * mp_start.c drives the half that lives in mp_start_wish.c.
 */
#ifndef MULTIPLAYER_MP_START_WISH_H
#define MULTIPLAYER_MP_START_WISH_H

/* Resolves the teleport and the hero swap, names either one that is missing, and lets the two
 * wishes be asked for from here on. Called once, from mp_start_install. */
void mp_start_wish_install(void);

/* Retries, carries out or drops what is held. Once per drawn frame, from mp_start_tick. */
void mp_start_wish_tick(void);

/* The two report lines, the placement and the hero. From mp_start_report. */
void mp_start_wish_report(void);

#endif /* MULTIPLAYER_MP_START_WISH_H */
