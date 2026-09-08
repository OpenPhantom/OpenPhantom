/* menu_art_census.h: what a menu screen is actually made of, measured rather than assumed.
 *
 * ==============================================================================================
 * The question this exists to answer
 *
 * Menu artwork is upscaled on disk, by a converter run against the player's own installation, and
 * that is why the menus are locked to one resolution: the engine's blitter copies one source pixel
 * to one destination pixel, so a picture only fills a bigger canvas if the file itself is bigger.
 * Below a resolution the artwork was converted for, the whole scale stands down, because a canvas
 * larger than the screen writes past the end of the frame buffer.
 *
 * Upscaling at draw time instead would remove all of that. The mechanism is already here and
 * proven: menu_preview.c resamples the four animated previews into buffers of its own, swaps them
 * in for the duration of the draw and restores afterwards, and caches on a hash of the source, so a
 * picture that has not changed is not resampled again.
 *
 * What is not known is how much of a menu could be reached that way, and it turns on which of the
 * engine's two blit paths each picture takes. swpic_blit routes on the widget's own fontIndex,
 * which doubles as a blit mode: 2 or more goes to the plain surface copy, which is the path
 * menu_preview already upscales, and below 2 goes to swrle_blit, a run length blitter with no scale
 * term in it at all. Save game thumbnails are the compressed case that is known about. Whether any
 * of the artwork is, nobody has measured.
 *
 * ==============================================================================================
 * What it records, and what it cannot see
 *
 * One line the first time each distinct picture shape appears, and a summary of the counts when it
 * is asked for. First-time-only because a per-frame line for every widget would be thousands of
 * lines a minute and would answer the same question worse.
 *
 * It sits in the swpic_draw detour that is already installed, so it resolves no new engine site and
 * costs nothing when it is off. That also bounds what it can see: PICTURE WIDGETS, and nothing
 * else. If a menu screen turns out to draw pixels some other way, this census will not show them,
 * and a screen whose reported shapes plainly do not add up to what is on it is itself the finding.
 * Reading it that way is the point, so the summary says how many screens were opened as well as
 * what was drawn.
 */
#ifndef MENU_ART_CENSUS_H
#define MENU_ART_CENSUS_H

#include <stdbool.h>
#include <stdint.h>

/* Off unless LogMenuArt is set. A diagnostic, so it ships off and nothing below runs.
 *
 * It places its OWN detour on swpic_draw rather than riding in the menu scale's. The first version
 * rode in that one and measured nothing at all, because the scale does not install without
 * converted artwork on disk, and the whole question this answers is whether that artwork could stop
 * being needed. A measurement that requires the thing it is measuring the need for is no
 * measurement. Two detours on one site is safe; common/detour.c chains them. */
bool menu_art_census_install(void);

/* Called from the swpic_draw detour, with the widget and its surface. Cheap and silent once a shape
 * has been seen before. */
void menu_art_census_note(const void *widget, const void *frame);

/* Called when a menu screen opens, so the summary can say how many screens produced these shapes.
 * A screen that draws nothing this census sees is the interesting case. */
void menu_art_census_note_screen(void);

/* Called from the textured sprite blitter, whose destination edges are floats and therefore
 * already scale. Counted rather than tabulated: what is being asked is whether the menu is
 * built from these at all. */
void menu_art_census_note_sprite(float left, float right, float top, float bottom);

/* Reported by the census itself, once, after enough screens have been opened for a walk through
 * the front end to have covered them. Nothing else has to remember to ask, which matters for a
 * diagnostic whose whole value is being read after somebody has wandered around the menus. */

#endif /* MENU_ART_CENSUS_H */
