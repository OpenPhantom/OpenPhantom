/* subtitle_scale.h: subtitles keep the size they had at 640x480, whatever the display is.
 *
 * ============================== What the engine does ==========================================
 *
 * DLG_DrawLine, which draws the subtitle and the reply options, sets the glyph scale to
 *
 *     640.0 / screenWidth
 *
 * so at the game's authored 640x480 the scale is exactly 1 and at 1920 it is a third. That holds
 * the text at a constant number of PIXELS, as a 1999 game wanted, and it is why subtitles shrink
 * into nothing the higher the display goes. The credits crawl has the same fault from the
 * other direction, its row spacing being hardcoded for a 480 line screen, and ending_resolution.h
 * already says that belongs with this rather than with the mode switch.
 *
 * ============================== How this changes it ===========================================
 *
 * By telling the layout the screen is smaller than it is, and touching nothing the font layer
 * does for anyone else.
 *
 * The layout's three divisions are `fdiv dword [screenWidth]` and its two siblings, each with a
 * plain absolute operand four bytes wide. Pointing those at cells of this module's own leaves the
 * instructions, the register state and the stack exactly as they were, and turns the engine's own
 * expression into
 *
 *     640.0 / ourCell
 *
 * so ourCell = 640 gives a scale of 1, which is the size the text has at 640x480, at every
 * resolution. A user scale s is ourCell = 640/s. Because the cells are ours and live, a new value
 * takes effect on the next subtitle drawn, with no repatching, so the developer menu's slider can
 * be dragged and watched.
 *
 * The rest of the layout has to be told the same lie, and each piece is told it at its own call
 * site inside DLG_DrawLine rather than by detouring the function it calls: the two centring calls
 * go to two getters of this module's that answer the told size, the wrap's measure call goes to a
 * function that adds the spacing the engine's measure leaves unscaled, the line height call goes
 * to one that answers from beneath the menu scale's hook, the two wrap comparisons read a cell of
 * ours, and the two clamps that floored the offset at zero are opened. Only the backdrop bar is
 * detoured, because it is drawn from one function with nothing in the caller to redirect. Every
 * other caller of the font layer, every menu string and every HUD readout, keeps the engine's own
 * arithmetic, a promise a detour on the font layer could not have made without working out which
 * caller it was serving.
 *
 * ============================== Why the box and not the font =================================
 *
 * Scaling the glyphs alone was tried first and is wrong, visibly: the rows stayed 18 pixels apart
 * while the letters grew, so three lines of subtitle landed on top of each other. The layout is
 * not a font size, it is a 640x480 box of fixed pixels with the text measured inside it, and the
 * only two things that put that box on the screen are the centring and the position scale.
 *
 * So both of those are told the screen is smaller than it is, and the arithmetic inside the box
 * is left as the engine wrote it. The row pitch, the baseline and the two nudges live in the one
 * part of the dialogue file the decompilation admits it never reconstructed, and none of them is
 * patched; the wrap is the exception, because its measure disagrees with the draw by the spacing,
 * and the disagreement was photographed as text running out of the bar.
 */
#ifndef ENHANCED_RESOLUTION_SUBTITLE_SCALE_H
#define ENHANCED_RESOLUTION_SUBTITLE_SCALE_H

#include <stdbool.h>
#include <stdint.h>

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
