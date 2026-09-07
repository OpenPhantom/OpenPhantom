/* diag_camera_owner.h: who took the camera away from the player, and did they give it back.
 *
 * The engine keeps one flag saying "a script owns the camera", `gOver` at 0x005BB4E8, and one
 * companion cell holding the region it forced. While the flag is set, bapview_updateCam takes its
 * region index from the forced cell instead of the region the player is actually standing in, so
 * the flag is not advisory: it decides which camera the level uses.
 *
 * Two one-line functions are the ONLY writers in the whole image, which is what makes this
 * observable cheaply. Seven places call the setter and six call the clearer, and they are not
 * paired one to one, so a flag that never comes back is a real possibility rather than a
 * hypothesis. It presents as a camera stuck on a shot the player has long walked away from, and it
 * survives until the level is reloaded, because nothing else clears the cell.
 *
 * What this answers is only "which caller set it last and did anybody clear it". That is enough,
 * because the setters are few and each one belongs to a different subsystem: the dialogue, the
 * cutscene opcode, the menu, the fall-death camera, the tripod gun, and the module restore. The
 * line names the caller where the return address is one we have already identified, and prints the
 * bare address where it is not, so an unknown caller reports itself rather than being missed.
 */
#ifndef DIAG_CAMERA_OWNER_H
#define DIAG_CAMERA_OWNER_H

/* `level` of 0 installs nothing. Returns the number of observers that went live, 0 or 2: the pair
 * is installed both or neither, since a set with no clear and a clear with no set are both
 * unreadable on their own. */
int diag_camera_owner_install(int level);

#endif /* DIAG_CAMERA_OWNER_H */
