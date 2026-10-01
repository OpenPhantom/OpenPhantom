/* subtitle_size_row.h: how big the subtitles are, as a row inside the developer menu.
 *
 * The engine draws subtitles at a constant number of PIXELS, so they shrink into nothing the higher
 * the display goes. enhanced_resolution holds them at the size they have on the game's authored
 * 640x480 screen instead; this row is how that size is chosen without leaving the game.
 *
 * A track is right here, unlike the panel's own size. That one moved the panel being dragged,
 * so it was tried and taken out again. This changes text somewhere else on the screen, the case
 * a slider is good for: pick a line of dialogue, drag, and read it.
 *
 * It goes through the ini for the same reason the rows beside it do. The setting belongs to
 * enhanced_resolution.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other. That DLL re-reads the key once a second, so a
 * drag shows up on the next subtitle drawn.
 */
#ifndef DEV_OVERLAY_SUBTITLE_SIZE_ROW_H
#define DEV_OVERLAY_SUBTITLE_SIZE_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* The same band enhanced_resolution clamps to, repeated rather than shared because these two DLLs
 * do not link against each other. Below a half the text is smaller than the engine's own at 1080;
 * above three it is wider than the line wrap was authored for. */
#define SUBTITLE_SIZE_MIN 0.50f
#define SUBTITLE_SIZE_MAX 3.00f

/* 1.0 is the size the text has at 640x480, which is the point of the whole thing. */
#define SUBTITLE_SIZE_DEFAULT 1.00f

float subtitle_size_row_clamp(float scale);
bool  subtitle_size_row_parse(const char *text, float *out);
void  subtitle_size_row_format(float scale, char *out, size_t size);

/* The value in the ini, parsed and clamped. Reads the file every call. */
float subtitle_size_row_get(void);

/* Clamped and written. enhanced_resolution notices within a second. */
bool  subtitle_size_row_set(float scale);

#endif /* DEV_OVERLAY_SUBTITLE_SIZE_ROW_H */
