/* mp_menu_metrics.h: the advance width of every printable glyph in the three frontend fonts, so a
 * string can be cut to the column it lives in before the engine draws it. Generated from the
 * advance field of obi/font/{indust,sysfont,courier}.gcf; the tables are the fonts as shipped and
 * change only if the fonts do. Index is the ASCII code; anything outside 32..126 is 0, which is
 * also what the screens refuse to draw. */
#ifndef MULTIPLAYER_MP_MENU_METRICS_H
#define MULTIPLAYER_MP_MENU_METRICS_H

#include <stdint.h>

static const uint8_t MP_MENU_ADVANCE_INDUST[128] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      5,   5,  15,  15,  10,  11,  12,  15,   7,   8,  10,  15,   5,   9,   5,   9,
     10,   7,  10,  10,  12,  10,  10,  10,  10,  10,   5,   5,  15,  15,  15,  10,
     15,  10,  10,  10,  10,  10,  10,  10,  10,   5,  10,  10,  10,  13,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  13,  10,  11,  10,   8,  15,   8,  15,  20,
      9,  10,  10,  10,  10,  10,   9,  10,  10,   5,   5,  10,   5,  15,  10,  10,
     10,  10,   7,  10,  10,  10,  10,  15,  11,  10,  10,  11,  15,  11,  15,   0,
};

static const uint8_t MP_MENU_ADVANCE_SYSFONT[128] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      5,   4,   7,  13,  11,  16,  13,   4,   7,   7,   7,  12,   4,   7,   4,   8,
     10,   7,  10,  10,  11,  10,  10,  10,  10,  10,   4,   4,  11,  11,  11,  10,
     19,  13,  12,  13,  13,  12,  11,  14,  13,   4,   9,  13,  10,  15,  13,  14,
     12,  14,  14,  12,  12,  13,  13,  19,  13,  14,  12,   6,   8,   6,   9,  12,
      5,  10,  10,   9,  10,  10,   8,  10,  10,   4,   6,  10,   4,  14,  10,  10,
     10,  10,   7,   9,   6,  10,  11,  17,  10,  11,  10,   8,   4,   8,  12,   0,
};

static const uint8_t MP_MENU_ADVANCE_COURIER[128] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      4,   5,   5,  10,   7,  11,  11,   3,   5,   5,   9,   9,   4,   6,   3,   9,
     10,   9,   9,  10,  10,  10,   9,   8,  10,   9,   3,   4,   8,   8,   8,   9,
     11,   9,   9,  10,  10,   8,   8,  10,   9,   3,   6,  10,   8,  11,   9,  10,
      8,  10,   9,  10,   9,   8,   9,  13,   9,   9,   9,   5,   9,   5,   9,  10,
      5,   8,   8,   7,   8,   8,   7,   8,   8,   3,   5,   9,   3,  11,   8,   8,
      8,   8,   6,   7,   7,   8,   9,  12,   7,   9,   6,   5,   3,   5,   9,   0,
};

#endif /* MULTIPLAYER_MP_MENU_METRICS_H */
