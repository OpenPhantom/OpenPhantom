/* entity_offer.c: see entity_offer.h. */
#include "entity_offer.h"

#include "entity_class_data.h"
#include "entity_names.h"

#include <stddef.h>
#include <string.h>

/* The anchor the engine itself marks as bodiless; the census has always refused it by name too. */
#define ANCHOR_FILE "inviso.baf"

#define LIVING_CLASSES ((1u << 1) | (1u << 2) | (1u << 3))

static bool has_class(uint32_t classes, int32_t klass)
{
    return klass >= 0 && klass < 32 && (classes & (1u << (uint32_t)klass)) != 0u;
}

/* The lowest pickup class the levels give the file, or 0. The Small medpack is the one file raised
 * both ways, as class 3 on three placements and as class 13 on forty; it is a pickup. */
static int32_t lowest_pickup_class(uint32_t classes)
{
    int32_t klass;

    for (klass = ENTITY_PICKUP_CLASS_LOW; klass <= ENTITY_PICKUP_CLASS_HIGH; ++klass) {
        if (has_class(classes, klass)) {
            return klass;
        }
    }
    return 0;
}

/* The shelf of an offered file. The gun first, since the one gun is also raised as a living class 2
 * turret on one level; a pickup before a figure for the same reason; a figure by its body; the rest
 * are creatures unless the name table marks a vehicle. */
static entity_section_t section_of(const char *file, const entity_facts_t *facts, int32_t pickup)
{
    if (facts->mount) {
        return ENTITY_SECTION_GUNS;
    }
    if (pickup != 0) {
        return ENTITY_SECTION_PICKUPS;
    }
    if (facts->body) {
        return ENTITY_SECTION_FIGURES;
    }
    return entity_name_is_vehicle(file) ? ENTITY_SECTION_VEHICLES : ENTITY_SECTION_CREATURES;
}

entity_offer_t entity_offer_judge(const char *file, const entity_facts_t *facts)
{
    entity_offer_t offer;
    int32_t        pickup;
    bool           mountable;

    memset(&offer, 0, sizeof offer);
    offer.verdict = ENTITY_UNPROVEN;
    if (file == NULL || facts == NULL) {
        return offer;
    }
    if (facts->clips == 0u) {
        offer.verdict = ENTITY_NO_CLIPS;
        return offer;
    }
    if (_stricmp(file, ANCHOR_FILE) == 0) {
        offer.verdict = ENTITY_ANCHOR;
        return offer;
    }
    pickup    = lowest_pickup_class(facts->classes);
    mountable = facts->mount && has_class(facts->classes, ENTITY_MOUNT_CLASS);
    if (!facts->body && (facts->classes & LIVING_CLASSES) == 0u && pickup == 0 && !mountable) {
        return offer;
    }
    offer.verdict      = ENTITY_OFFERED;
    offer.pickup_class = pickup;
    offer.section      = section_of(file, facts, pickup);
    return offer;
}

/* Case aside, the way the engine's own loader compares a name. */
const entity_class_row_t *entity_offer_row(const char *file)
{
    uint32_t low  = 0;
    uint32_t high = ENTITY_CLASS_ROWS;

    if (file == NULL) {
        return NULL;
    }
    while (low < high) {
        uint32_t mid   = low + (high - low) / 2u;
        int      order = _stricmp(file, ENTITY_CLASS_TABLE[mid].file);

        if (order == 0) {
            return &ENTITY_CLASS_TABLE[mid];
        }
        if (order < 0) {
            high = mid;
        } else {
            low = mid + 1u;
        }
    }
    return NULL;
}

uint32_t entity_offer_rows(void)
{
    return ENTITY_CLASS_ROWS;
}

const entity_class_row_t *entity_offer_row_at(uint32_t index)
{
    return index < ENTITY_CLASS_ROWS ? &ENTITY_CLASS_TABLE[index] : NULL;
}

void entity_offer_row_facts(const entity_class_row_t *row, entity_facts_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (row == NULL) {
        return;
    }
    out->clips   = row->clips;
    out->body    = (row->facts & ENTITY_FACT_BODY) != 0u;
    out->mount   = (row->facts & ENTITY_FACT_MOUNT) != 0u;
    out->classes = row->classes;
}

uint32_t entity_offer_classes(const char *file)
{
    const entity_class_row_t *row = entity_offer_row(file);

    return row != NULL ? row->classes : 0u;
}

int32_t entity_offer_pickup_class(const char *file)
{
    return lowest_pickup_class(entity_offer_classes(file));
}

const char *entity_section_title(entity_section_t section)
{
    switch (section) {
    case ENTITY_SECTION_LEVEL:     return "From this level";
    case ENTITY_SECTION_FIGURES:   return "Figures";
    case ENTITY_SECTION_CREATURES: return "Creatures and droids";
    case ENTITY_SECTION_VEHICLES:  return "Vehicles";
    case ENTITY_SECTION_PICKUPS:   return "Pickups";
    case ENTITY_SECTION_GUNS:      return "Guns";
    case ENTITY_SECTION_COUNT:
    default:                       return "";
    }
}
