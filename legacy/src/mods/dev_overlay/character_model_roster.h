/* character_model_roster.h: the list of models a player may be put into, and nothing else.
 *
 * The seam character_model.c's own size note has been naming for two passes. What is here is the
 * half with no engine in it: three rows written in code, the actor table read from the data file,
 * the label a panel shows, and the case folding stem comparison that decides whether two names are
 * the same asset. None of it needs a game to be wrong, which is why it is worth having apart, for
 * the same reason fov_math.c is apart from the feature it serves.
 *
 * What stayed behind is the swap itself: the borrow and the way home have to undo each other field
 * for field, and separating those is how a restore comes to miss one.
 */
#ifndef CHARACTER_MODEL_ROSTER_H
#define CHARACTER_MODEL_ROSTER_H

#include <stdbool.h>
#include <stdint.h>

/* The roster is bounded so that the per row answers can be a plain array. The data file ships 164
 * actors and the extraction found 265 assets in all, so the cap is not close. */
#define MODEL_ROWS_MAX  320u

/* How many rows there are: the ones written below plus the actor table, clamped to the cap. */
uint32_t character_model_roster_count(void);

/* The asset file name of a row, or NULL for an id out of range. A row past the written ones
 * answers out of the actor table, whose strings live in that module's own storage. */
const char *character_model_roster_asset(uint32_t id);

/* The line a panel shows for a row. It is built in a buffer this module owns, so exactly one row
 * can be described at a time and a caller reads what it needs before asking about the next. */
const char *character_model_roster_label(uint32_t id);

/* Whether two file names name the same asset: everything up to the last dot, case folded, so
 * "quigon" and "QuiGon.baf" are one. Two empty stems are not a match, because that answer would
 * make every unnamed thing equal to every other. */
bool character_model_asset_matches(const char *left, const char *right);

#endif /* CHARACTER_MODEL_ROSTER_H */
