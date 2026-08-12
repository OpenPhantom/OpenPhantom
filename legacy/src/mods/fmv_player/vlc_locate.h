/* vlc_locate.h: find a 32-bit libVLC on this machine.
 *
 * Kept apart from vlc_playback.c for the same reason player_sites.c and music_sites.c are kept
 * apart from the code that uses what they find: "where is it" and "drive it" fail for entirely
 * different reasons, and only one of the two has anything to say about somebody's registry.
 *
 * WHY 32-BIT SPECIFICALLY
 * The game is a 32-bit process and a 32-bit process cannot load a 64-bit DLL under any
 * circumstances. That is an architecture wall, not a version mismatch, so a 64-bit VLC is not a
 * near miss to be worked around: it is invisible here, and saying so in the log is the only way a
 * player learns why the machine they can watch films on cannot play these ones.
 */
#ifndef VLC_LOCATE_H
#define VLC_LOCATE_H

#include <stdbool.h>
#include <stddef.h>

#include <wchar.h>

/* Writes the directory holding libvlc.dll into `out_dir` and returns true.
 *
 * Three candidates, in this order, and the order is the point:
 *
 *   1. <game>\mods\fmv, the runtime the installer puts there. First because it is the only one
 *      whose version this project chose, so it is the only one a bug report can be reproduced
 *      against. A player who has it never depends on what else is on the machine.
 *   2. %ProgramFiles(x86)%\VideoLAN\VLC, where a 32-bit VLC installer puts itself by default.
 *   3. the VLC installer's own registry key, for an install somewhere else.
 *
 * Every branch logs: which candidate was tried, whether libvlc.dll was there, and, when nothing
 * was found, all three places that were looked at. A player whose movies quietly kept using Bink
 * has to be able to read the reason out of the log rather than guess it. */
bool vlc_locate_directory(wchar_t *out_dir, size_t out_capacity);

#endif /* VLC_LOCATE_H */
