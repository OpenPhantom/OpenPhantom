/* sensitivity_row.h: mouse sensitivity, as the overlay's own row sees it.
 *
 * The key is [enhanced_input] MouseDegreesPerCount: how far the view turns for one count from the
 * mouse. A slider, because it is the setting in this whole panel that can only be found by feel.
 *
 * IT EXISTS BECAUSE THE GAME'S OWN SCREEN NO LONGER OFFERS IT. Mouse look ships on, which is a real
 * change from the original, and the sensitivity slider that came with it sat on the controls screen
 * the game shipped without one. That screen is vanilla by default now, so the adjustment had to go
 * somewhere, and a feature that ships on with no way to tune it would be worse than either.
 *
 * The ends are the same band the in-game slider used, so the two agree about what is adjustable
 * rather than offering different ranges for one number.
 */
#ifndef DEV_OVERLAY_SENSITIVITY_ROW_H
#define DEV_OVERLAY_SENSITIVITY_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* Degrees of turn per mouse count. The band the controls screen's own slider spanned. */
#define SENSITIVITY_MIN 0.001f
#define SENSITIVITY_MAX 0.100f

/* What enhanced_input uses with the key absent, so the row shows what the game is really doing
 * rather than an end of the range. */
#define SENSITIVITY_DEFAULT 0.030f

float sensitivity_row_clamp(float degrees);

/* Parses what was typed: a bare decimal number. False, leaving `out` untouched, for anything else.
 * Three decimals matter here, so a parser that stopped at two would refuse half the band. */
bool sensitivity_row_parse(const char *text, float *out);

/* "0.030" into `out`. No unit: "degrees per mouse count" does not fit on a chip, and the row's own
 * name carries the meaning. */
void sensitivity_row_format(float degrees, char *out, size_t size);

float sensitivity_row_get(void);
bool  sensitivity_row_set(float degrees);

#endif /* DEV_OVERLAY_SENSITIVITY_ROW_H */
