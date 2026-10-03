/* overlay_stubs.h: what a test may read off the stubbed half of the overlay.
 *
 * The stubs in overlay_stubs.c stand in for the drawing and the input of the panel and do
 * nothing. One of them is worth observing all the same: a row that has to close the panel before
 * it acts calls the stubbed close, and a test of that row wants to see that it did. */
#ifndef UNITTESTS_OVERLAY_STUBS_H
#define UNITTESTS_OVERLAY_STUBS_H

#include <stdint.h>

/* How often the panel has been asked to close in this process. A test reads it before and after
 * the press it is about. */
extern uint32_t overlay_stubs_closes;

#endif /* UNITTESTS_OVERLAY_STUBS_H */
