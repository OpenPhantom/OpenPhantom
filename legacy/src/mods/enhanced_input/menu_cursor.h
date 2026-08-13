#ifndef MENU_CURSOR_H
#define MENU_CURSOR_H

#include <stdbool.h>

/* The menu pointer, moved by the device instead of by the screen pointer.
 *
 * The engine's own scheme is the Windows 95 one: warp the system pointer back to a fixed screen
 * point after every mouse message, take the difference, and add it to a position the engine keeps
 * and draws itself. It has three consequences that were invisible at 30 frames a second on a
 * 640x480 screen and are not invisible now. They are in the source file, together with the
 * disassembly of the accumulation and the census that shows nothing else writes those two cells.
 *
 * Nothing here replaces the engine's cursor, its clamp or its hit testing. It replaces only the
 * number the engine adds, and lets the engine do everything else exactly as before, which is what
 * keeps it out of the way of the pointer cage in enhanced_resolution.
 */

/* Installs the detour. `enabled` is the feature's ini gate, MenuCursorRawInput. Returns true only
 * when the cursor is really being driven from the device from now on: a false answer means the
 * engine's own pointer motion is still in charge, and the log line says which of the two reasons
 * it was. */
bool menu_cursor_install(bool enabled);

#endif /* MENU_CURSOR_H */
