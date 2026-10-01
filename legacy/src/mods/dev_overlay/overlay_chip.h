/* overlay_chip.h: the word on the right of a row, and how much room the panel must leave for it.
 *
 * Three things have to agree about a chip and they used to be three copies of one conditional:
 * the word a row draws, the width the panel is sized to, and the width the label beside it is
 * fitted against. They were two copies for one commit and the longest label was drawn clipped,
 * which is one commit longer than that is safe. They are one function and its two measurements
 * here.
 *
 * The measuring is done through the panel's own text measure, which selects the font and sets the
 * glyph scales before it reads anything: a width taken without that describes whatever font the
 * last thing to draw left behind, which at a wide resolution came back about two and a half times
 * too large and overflowed the panel with its own tabs.
 */
#ifndef DEV_OVERLAY_OVERLAY_CHIP_H
#define DEV_OVERLAY_OVERLAY_CHIP_H

#include "overlay_model.h"

#include <stdbool.h>

/* The word on the button at the end of a track row. One definition, because the drawing
 * writes it and the measuring below has to leave room for exactly what is written; the same
 * pair this file exists to keep together for a chip. */
#define OVERLAY_DEFAULT_WORD "Default"

/* The word in this row's chip. Never NULL. A row that is not available reads its reason rather
 * than a flat `n/a`, and a heading reads whatever the model put on it, which may be nothing. */
const char *overlay_chip_word(const overlay_row_t *row);

/* Whether this row's chip is a button rather than a switch: something that starts something when
 * it is pressed. The word below picks "RUN" by it and the drawing picks the chip's colour by it,
 * and those were two copies of one conditional in two files. */
bool overlay_chip_is_button(const overlay_row_t *row);

/* The widest name, and the widest chip, on the rows that are on screen right now. Their sum is
 * what the panel is sized to. */
float overlay_chip_widest_name(void);
float overlay_chip_widest(void);

#endif /* DEV_OVERLAY_OVERLAY_CHIP_H */
