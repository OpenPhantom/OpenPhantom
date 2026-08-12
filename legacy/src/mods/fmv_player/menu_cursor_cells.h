/* menu_cursor_cells.h: put the engine's DRAWN menu cursor back in the middle of its own island.
 *
 * The drawn menu cursor is not a Windows cursor. It is a position the engine keeps in two integer
 * cells and draws itself, and the window procedure moves it by ACCUMULATION: every real mouse
 * message adds (client_x - 320, client_y - 240) to whatever those cells already held, then clamps
 * the result to the menu's 640x480 island. See menu_cursor_cells.c for the bytes.
 *
 * That accumulator is why this exists. A movie leaves the game window's mouse messages queued and
 * unprocessed, and warping the real pointer to any particular place before resuming only adds a
 * delta to a value this DLL has no way to know, so where the drawn cursor lands depends on session
 * state. Writing the cells is deterministic; warping is not.
 *
 * One responsibility, kept apart for the same reason vlc_locate.c and movie_path.c are: this is
 * the only part of fmv_player that reads or writes the engine's own memory, and it is the only
 * part that needs a signature.
 * ============================================================================================ */
#ifndef MENU_CURSOR_CELLS_H
#define MENU_CURSOR_CELLS_H

#include <stdbool.h>

/* Resolves the four cells out of the window procedure's own instruction stream. Call once, at
 * install time. Returns false when the pattern does not match this build, in which case
 * menu_cursor_cells_recentre() does nothing and says so once. */
bool menu_cursor_cells_resolve(void);

/* Puts the drawn menu cursor in the middle of the menu island, by writing the two cells rather
 * than by moving the real pointer and hoping the engine's accumulator lands it there. Safe to
 * call when nothing resolved: it does nothing. */
void menu_cursor_cells_recentre(void);

#endif /* MENU_CURSOR_CELLS_H */
