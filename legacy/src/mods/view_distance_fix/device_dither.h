/* device_dither.h: switch on Direct3D dithering, because the frame buffer is 16 bit.
 *
 * The mode enumeration is filtered to 16-bit RGB, so the destination is 5/6/5 whatever the
 * resolution. The cutscene fade draws a full-screen black quad with SRCALPHA and INVSRCALPHA, which
 * for a black source collapses to out = dst * (1 - alpha/255): a multiply over the whole picture.
 * Its alpha is recomputed every frame from a performance-counter clock and is provably smooth.
 *
 * The steps are therefore not in the fade, they are in the buffer. As the multiplier slides, a
 * stored 5-bit channel only changes when it crosses a boundary. An area of ONE flat colour crosses
 * everywhere at once and steps as a solid block; lit geometry crosses pixel by pixel and does not.
 * The boundary between the two is what reads as a flashing line, which is why it only shows where
 * the fog band saturates inside the draw cut and leaves a large flat region to step.
 *
 * Measured: in the fogged region every sampled pixel moved by an identical 4 to 6 levels with the
 * area bit-exactly static between steps, lit geometry moved by a fractional average on 90 to 96 per
 * cent of pixels, and the black letterbox bars moved by exactly nothing, which is only possible for
 * a multiply. Step size and rate both match 5- and 6-bit channel crossings over a four second fade.
 *
 * Dithering is the remedy for exactly this and the engine never asks for it: render state 26 is
 * absent from its whole state machine, so setting it once cannot be undone by anything. It is set
 * per device rather than per frame because nothing here clears it, and a new pointer means a new
 * device that needs it again.
 */
#ifndef VIEW_DISTANCE_FIX_DEVICE_DITHER_H
#define VIEW_DISTANCE_FIX_DEVICE_DITHER_H

#include <stdbool.h>

void device_dither_configure(bool on);

/* Cheap after the first call: it compares the device pointer and returns. */
void device_dither_on_frame(void);

#endif /* VIEW_DISTANCE_FIX_DEVICE_DITHER_H */
