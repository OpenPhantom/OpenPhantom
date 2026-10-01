/* entity_names.h: the names a person reads for the entity spawner's actor files.
 *
 * The list shows a file's stem, "baron" or "pwrhlth1", unless this table knows what a person calls
 * it. The table is a courtesy and never a filter: a file it does not name is offered all the same
 * and shows its stem, so the table can grow or shrink without the offer changing. What is offered
 * is entity_offer.c's alone.
 *
 * One more thing a name carries is a vehicle mark. The data cannot tell a fighter from a creature:
 * both are raised by the retail levels as living things without a head or a chest, and the move
 * modes cross over. Which shelf of the list a vehicle stands on is therefore said here, beside
 * the name, and it moves a row between two headings and nothing else.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_ENTITY_NAMES_H
#define DEV_OVERLAY_ENTITY_NAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The longest name, so a row keeps room for the stem and a count behind it. */
#define ENTITY_NAME_MAX 22u

/* The name a person reads for `file` ("baron.baf", case aside), or NULL when the table has none
 * and the row shows the stem. */
const char *entity_name_of(const char *file);

/* Whether the table marks `file` as a vehicle. */
bool entity_name_is_vehicle(const char *file);

/* The table itself, for the test: how many rows, and row `index` as file stem and name. */
uint32_t entity_names_count(void);
bool     entity_names_at(uint32_t index, const char **stem, const char **name);

#endif /* DEV_OVERLAY_ENTITY_NAMES_H */
