/* ending_resolution.h: the ending drops the game to 640x480, and this stops it.
 *
 * ==============================================================================================
 * The defect, as a player sees it
 *
 * Finishing the game appears to MINIMISE it. The ending cutscene and the credits keep running, and
 * clicking back into the process finds the game as a small 640x480 window in the corner of the
 * screen. Every other menu holds the resolution correctly, which is what makes this one look like
 * a bug in the credits.
 *
 * ==============================================================================================
 * What it actually is, and it is not the credits
 *
 * The ending sequence at 0x0043EF06 reads, in order:
 *
 *   0043EF06  83 3D 6C 13 88 00 0B   cmp  dword [0x0088136C],0x0B   ; the final level
 *   0043EF0D  7C 45                  jl   0043EF54
 *   0043EF0F  68 E0 01 00 00         push 480
 *   0043EF14  68 80 02 00 00         push 640
 *   0043EF19  E8 <rel32>             call graphics_setResolution    ; <- THIS
 *   0043EF1E  83 C4 08               add  esp,8
 *   0043EF21  68 <"movie\scene8">    push
 *   0043EF40  E8 <rel32>             call graphics_playMovie  0x0046C35A
 *   0043EF48  E8 <rel32>             call credits_screen      0x004470F8
 *   0043EF4D  E8 <rel32>             call stats_screen        0x00446CEB
 *
 * So the mode change belongs to the ending MOVIE, and the credits merely inherit it. It was found
 * by asking the call itself: enhanced_resolution's LogResolutionCalls reported
 * "graphics_setResolution(640, 480) called from 0043EF1E" on the first run of the ending.
 *
 * THIS IS RETAIL BEHAVIOUR, NOT SOMETHING THIS PROJECT BROKE. graphics_setResolution has seven
 * callers; six push 640x480 and the seventh pushes -1,-1, which means "re-read obi.ini". There is
 * no restore-to-the-previous-mode call anywhere in the image, so the retail game also ran its
 * ending and its credits at 640x480 and only recovered when the front end re-read the ini. On a
 * 1999 machine already at 640x480 that was invisible. At a modern resolution through a Direct3D 9
 * translation layer the same change costs the device its exclusive full screen, which is the
 * "minimise", and it comes back as a window the size of the new mode at screen 0,0.
 *
 * ==============================================================================================
 * Why removing it is safe, including for somebody with no converted cutscenes
 *
 * graphics_playMovie sets 640x480 ITSELF, at 0x0046C3B6 inside its own body. The call removed here
 * is redundant even on the retail path: a player whose scene8 is still Bink gets the mode the
 * retail player expects from the movie player, and fmv_player bypasses both and plays at full size.
 * Nothing loses a mode change it needed.
 *
 * The five bytes of the CALL are replaced with NOPs and the two pushes are left alone, so the
 * `add esp,8` that follows still balances the stack exactly as it did. Nothing else in the
 * function moves.
 *
 * ==============================================================================================
 * What this does NOT fix
 *
 * The crawl geometry. g_creditRowY[20] is a hardcoded 0, 24, 48 ... 456, authored for a 480 line
 * screen, so the credits text keeps its authored spacing on whatever canvas it is given. That is
 * the same family of defect as the subtitles and belongs with them, not here.
 */
#ifndef ENDING_RESOLUTION_H
#define ENDING_RESOLUTION_H

#include <stdbool.h>

/* Neutralises the ending's 640x480 switch. `enabled` false declines and says so, so a reader
 * comparing two logs can tell "switched off" from "did not resolve". */
void ending_resolution_install(bool enabled);

#endif /* ENDING_RESOLUTION_H */
