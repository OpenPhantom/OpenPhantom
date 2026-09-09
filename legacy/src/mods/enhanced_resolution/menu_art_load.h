/* menu_art_load.h: resample a menu bitmap between the engine reading it and compressing it.
 *
 * ==============================================================================================
 * Where, and why one call site rather than the function
 *
 * The engine's BBMP resource handler loads a menu bitmap in three steps: stdBmp_LoadNamed reads the
 * file, stdBitmap_ConvertTo16 makes a new surface in the display's format, and
 * swrle_compressVBuffer turns its pixels into a run length stream and frees them. Between the
 * second and the third the pixels are raw, 16 bit, and about to be discarded, which is the one
 * moment a larger copy can be put in their place for nothing.
 *
 * The design is one redirected CALL rather than a detour on the compressor. swrle_compressVBuffer
 * has exactly two callers: this one, and swpic_setWidgetImage. The second compresses the save
 * game thumbnail, which swmenu_loadSaveThumb reallocates at a fixed 160x120 on
 * every row change and then copies a fixed 160x120 image into. Resampling that buffer would leave
 * the engine writing 160x120 pixels into a header claiming something larger, and reading past the
 * end of what it allocated. Redirecting one call site cannot reach it.
 *
 * ==============================================================================================
 * The buffer has to come from the engine's allocator
 *
 * swrle_compressVBuffer frees the pixels it was given, and the engine's mem_alloc puts a sixteen
 * byte header in front of every block, which mem_free subtracts before handing the pointer back to
 * the C runtime. A block from our own malloc would be freed sixteen bytes before its own start.
 * crt_copy_fix.c records the same header from the other direction.
 *
 * So both halves of the pair are resolved and used: the new pixels are allocated with the engine's
 * allocator, and the pixels being replaced are released with the engine's free.
 *
 * ==============================================================================================
 * Following a resolution change
 *
 * A picture is loaded once and held by name, so a picture resampled at one ratio stays at it until
 * something drops it. Nothing here does that. What this offers instead is menu_art_load_set_ratio,
 * and the caller that drops the cache is menu_scale_refit.c: it calls the engine's own
 * swmenu_freeBitmaps, which zeroes a screen's bitmap slots, and every slot is then reloaded by
 * name on the next draw and arrives back through here at whatever ratio it has last been told.
 */
#ifndef MENU_ART_LOAD_H
#define MENU_ART_LOAD_H

#include <stdbool.h>

/* Arms the redirect. `ratio_x` and `ratio_y` are the canvas the menus are being drawn at, so a
 * picture authored for 640x480 is replicated by those.
 *
 * A ratio of 1 arms it just the same and every picture then declines itself, because a picture
 * already the size it should be is left alone. That is deliberate: the canvas can be made larger
 * later, and a redirect that was never armed could not be told about it.
 *
 * Returns true only when the redirect stands and menu bitmaps really can arrive resampled. */
bool menu_art_load_install(float ratio_x, float ratio_y);

/* The canvas has changed size, so the next picture to load is replicated by these instead.
 *
 * Only the ratio moves. Pictures already loaded are not touched and cannot be from here: they
 * belong to the engine's cache, and dropping that is the caller's half of the change. */
void menu_art_load_set_ratio(float ratio_x, float ratio_y);

/* The canvas the DISPLAY can hold, as a multiple of the authored 640x480.
 *
 * The menu scale has always taken its ratio from the converted artwork, on the doctrine that one
 * number read from the pictures cannot disagree with the pictures. Replicating at load inverts
 * that: the display decides and the artwork is made to fit, so there need be no converted
 * artwork at all.
 *
 * The size is read from the game's own settings file rather than from the engine, because this runs
 * at the host's entry point, before WinMain, and the engine has not opened a display or filled the
 * cells that would answer. That file holds the resolution it is about to open.
 *
 * False when the file cannot be read or the resolution is no larger than the authored canvas, and
 * the caller then has no scale to apply, which is the shipped behaviour. */
bool menu_art_load_display_ratio(float *out_ratio_x, float *out_ratio_y);

#endif /* MENU_ART_LOAD_H */
