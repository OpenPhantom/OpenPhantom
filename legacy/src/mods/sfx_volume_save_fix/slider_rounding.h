#ifndef SFX_VOLUME_SAVE_FIX_SLIDER_ROUNDING_H
#define SFX_VOLUME_SAVE_FIX_SLIDER_ROUNDING_H

#include <stdbool.h>

/* The audio screen's two volume sliders both drift downward, 7 of 127 every time the screen is
 * opened, because the nineteen-step widget is seeded with a truncated value and read back with
 * another truncation. See slider_rounding.c for the numbers and why rounding the seed alone is
 * enough. Covers the music slider as well as the SFX one; they share the code that drifts. */
bool slider_rounding_install(void);

#endif /* SFX_VOLUME_SAVE_FIX_SLIDER_ROUNDING_H */
