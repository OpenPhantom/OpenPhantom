/* freecam_teleport.h: the free camera's teleport key, dropping the player at the camera.
 *
 * Taken out of cheats_free_camera.c as a seam of its own: one write into a world the camera has
 * frozen, guarded by one height cap. The account of the cap and of the two writes the player
 * position needs is with the code.
 */
#ifndef FREECAM_TELEPORT_H
#define FREECAM_TELEPORT_H

/* Puts the player at the camera's position, or refuses when the drop from there is past the cap,
 * and says which in the log. Called on the way out of a flight, before the simulation is handed
 * back, so the player's own physics resumes from the new place. */
void freecam_teleport_player_to(float x, float y, float z);

#endif /* FREECAM_TELEPORT_H */
