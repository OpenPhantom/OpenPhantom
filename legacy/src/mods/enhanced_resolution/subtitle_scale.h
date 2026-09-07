/* subtitle_scale.h: subtitles keep the size they had at 640x480, whatever the display is.
 *
 * ============================== What the engine does ==========================================
 *
 * DLG_DrawLine, which draws the subtitle and the reply options, sets the glyph scale to
 *
 *     640.0 / screenWidth
 *
 * so at the game's authored 640x480 the scale is exactly 1 and at 1920 it is a third. That holds
 * the text at a constant number of PIXELS, which is what a 1999 game wanted and is why subtitles
 * shrink into nothing the higher the display goes. The credits crawl has the same fault from the
 * other direction, its row spacing being hardcoded for a 480 line screen, and ending_resolution.h
 * already says that belongs with this rather than with the mode switch.
 *
 * ============================== How this changes it ===========================================
 *
 * By moving the DIVISOR, not by patching arithmetic or detouring the font layer.
 *
 * The division is `fdiv dword [screenWidth]`, and the address in it is a plain absolute operand
 * four bytes wide. Pointing it at a cell of this module's own leaves the instruction, the register
 * state and the stack exactly as they were, and turns the engine's own expression into
 *
 *     640.0 / ourCell
 *
 * so ourCell = 640 gives a scale of 1, which is the size the text has at 640x480, at every
 * resolution. A user scale s is ourCell = 640/s.
 *
 * Two things fall out of that shape and both are worth having. Nothing else is touched: the same
 * font layer draws every menu string and every HUD readout, and a detour on it would have had to
 * work out which caller it was serving, which is the classifier problem hud_ratio_scaling had to
 * solve for the same reason. And because the cell is ours and live, a new value takes effect on
 * the next subtitle drawn, with no repatching, which is what lets the developer menu's slider be
 * dragged and watched.
 *
 * ============================== Why the box and not the font =================================
 *
 * Scaling the glyphs alone was tried first and is wrong, visibly: the rows stayed 18 pixels apart
 * while the letters grew, so three lines of subtitle landed on top of each other. The layout is
 * not a font size, it is a 640x480 box of fixed pixels with the text measured inside it, and the
 * only two things that put that box on the screen are the centring and the position scale.
 *
 * So both of those are told the screen is smaller than it is, and nothing inside the box is
 * touched. That matters beyond tidiness: the row pitch, the baseline, the wrap and the two nudges
 * live in the one part of the dialogue file the decompilation admits it never reconstructed, and
 * this way none of them has to be understood, let alone patched.
 */
#ifndef ENHANCED_RESOLUTION_SUBTITLE_SCALE_H
#define ENHANCED_RESOLUTION_SUBTITLE_SCALE_H

#include <stdbool.h>

/* The band the row and its slider offer. Below a half the text is smaller than the engine's own at
 * 1080; above three it is wider than the wrap was authored for and would break lines oddly. */
#define SUBTITLE_SCALE_MIN 0.50f
#define SUBTITLE_SCALE_MAX 3.00f

/* Zero leaves the engine's own behaviour alone, shrinking and all, which is the spelling
 * DevMenuSize, MenuScale and FogScale already use for "do not touch this". */
#define SUBTITLE_SCALE_ENGINE 0.00f

/* Reads the key, patches the operand, and starts watching the key for changes. Says in the log
 * what it found and what it did. */
void subtitle_scale_install(void);

#endif /* ENHANCED_RESOLUTION_SUBTITLE_SCALE_H */
