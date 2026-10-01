/* entity_offer.h: which actor files the entity spawner may offer, and on which shelf.
 *
 * The rule is four gates, and each one is a fact about the data rather than a look:
 *
 *   1. no clip at all                  never. The spawn plays clip 0, the play refuses it on a file
 *                                      with no tracks and leaves the animation slot at -1, and the
 *                                      slot is then indexed without a check: the script opcodes
 *                                      write 0x14c bytes in front of the track array. No retail
 *                                      level places one of those eighteen files.
 *   2. inviso.baf                      never. The engine marks it a script anchor with no body.
 *   3. a node named head or chest      yes, a figure.
 *   4. a class the retail levels give it:
 *        1 to 3                        yes, a living thing
 *        10 to 27                      yes, a pickup, the range the player's contact takes up
 *        4 with turret and target      yes, the gun the player mounts
 *        anything else, or none        no. Class 0 is inert to the contact pass, and nothing shows
 *                                      that a copy of a class 8 vehicle does anything sensible
 *                                      without its level's own script.
 *
 * The spawn routine asks for no node by name, so gate 3 is not the engine's; it is the old list,
 * and it stays because it is the proven one. The class a file is raised under comes from the
 * levels, which the panel never reads, so it is carried as data (entity_class_data.h, generated).
 *
 * A pickup is raised as its own class and a gun as the class the mount asks for; everything else
 * keeps the builder's rule. That, and which script a copy runs, are functions of the file name
 * alone, so every machine of a session builds the same copy from the same description.
 *
 * Pure: nothing here reads the engine or the disk. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_ENTITY_OFFER_H
#define DEV_OVERLAY_ENTITY_OFFER_H

#include <stdbool.h>
#include <stdint.h>

/* One row of the generated table (entity_class_data.h): the file, lower-cased; its clip count; the
 * ENTITY_FACT_* its node names carry; a mask with bit c set when a retail level places it as class
 * c; and how many retail placements use it. */
#define ENTITY_FACT_BODY  0x01u   /* a node named "head" or "chest" */
#define ENTITY_FACT_MOUNT 0x02u   /* nodes named "turret" and "target" */

typedef struct entity_class_row {
    const char *file;
    uint16_t    clips;
    uint8_t     facts;
    uint32_t    classes;
    uint16_t    placements;
} entity_class_row_t;

/* The shelves, in the order the list shows them. The level's own kinds come from the census; every
 * other shelf is the archive's. */
typedef enum entity_section {
    ENTITY_SECTION_LEVEL = 0,
    ENTITY_SECTION_FIGURES,
    ENTITY_SECTION_CREATURES,
    ENTITY_SECTION_VEHICLES,
    ENTITY_SECTION_PICKUPS,
    ENTITY_SECTION_GUNS,
    ENTITY_SECTION_COUNT
} entity_section_t;

typedef enum entity_verdict {
    ENTITY_OFFERED = 0,
    ENTITY_NO_CLIPS,      /* gate 1 */
    ENTITY_ANCHOR,        /* gate 2 */
    ENTITY_UNPROVEN       /* past gate 4 with nothing that holds */
} entity_verdict_t;

/* What the rule reads about one file. */
typedef struct entity_facts {
    uint32_t clips;
    bool     body;      /* a node named head or chest */
    bool     mount;     /* nodes named turret and target */
    uint32_t classes;   /* bit c: a retail level places the file as class c */
} entity_facts_t;

typedef struct entity_offer {
    entity_verdict_t verdict;
    entity_section_t section;        /* for an offered file */
    int32_t          pickup_class;   /* 10 to 27 for a pickup, else 0 */
} entity_offer_t;

/* The pickup range, Plr_PickUp's own: player_onContact takes a contact code of 0x0A to 0x1B as a
 * pickup, and the contact code of a body is its class. */
#define ENTITY_PICKUP_CLASS_LOW  10
#define ENTITY_PICKUP_CLASS_HIGH 27
#define ENTITY_MOUNT_CLASS        4

entity_offer_t entity_offer_judge(const char *file, const entity_facts_t *facts);

/* The retail classes of `file`, case aside, out of the table; 0 for a file it does not hold. */
uint32_t entity_offer_classes(const char *file);

/* The class a copy of `file` is raised as when it is a pickup, else 0. From the table, so it is the
 * same on every machine that runs this build. */
int32_t entity_offer_pickup_class(const char *file);

/* The table's row for `file`, or NULL; row `index` of it; and the facts a row holds. For the unit
 * test and for the one check at run time that holds the archive as read against the table. */
const entity_class_row_t *entity_offer_row(const char *file);
uint32_t                  entity_offer_rows(void);
const entity_class_row_t *entity_offer_row_at(uint32_t index);
void entity_offer_row_facts(const entity_class_row_t *row, entity_facts_t *out);

/* The heading of a shelf, as the list shows it. */
const char *entity_section_title(entity_section_t section);

#endif /* DEV_OVERLAY_ENTITY_OFFER_H */
