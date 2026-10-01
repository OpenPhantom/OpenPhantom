/* entity_offer.c: the entity spawner's offer rule, on made-up facts and on every file of the game.
 *
 * The second half is the one that matters. The table is generated from the retail archive and the
 * eleven retail levels, so the rule is run here over the real data, and the claims are about the
 * game: 303 actor files, 204 of them offered, not one of the eighteen files without a clip among
 * them, and every offered file offered for a reason the data hold.
 */
#include "unittest.h"

#include "common/text.h"

#include "entity_names.h"
#include "entity_offer.h"

#include <string.h>

static entity_facts_t facts_of(uint32_t clips, bool body, bool mount, uint32_t classes)
{
    entity_facts_t f;

    f.clips   = clips;
    f.body    = body;
    f.mount   = mount;
    f.classes = classes;
    return f;
}

static void the_rule(void)
{
    entity_facts_t f;
    entity_offer_t o;

    ut_section("the rule, on made-up facts");
    f = facts_of(0u, true, false, 1u << 2);
    ut_check(entity_offer_judge("x.baf", &f).verdict == ENTITY_NO_CLIPS,
             "a file without a clip is refused even with a body and a living class");
    f = facts_of(1u, true, false, 1u << 2);
    ut_check(entity_offer_judge("INVISO.BAF", &f).verdict == ENTITY_ANCHOR,
             "the script anchor is refused by name, case aside");
    f = facts_of(4u, true, false, 0u);
    o = entity_offer_judge("x.baf", &f);
    ut_check(o.verdict == ENTITY_OFFERED && o.section == ENTITY_SECTION_FIGURES,
             "a head or a chest offers a file no level places, as a figure");
    f = facts_of(3u, false, false, 1u << 3);
    o = entity_offer_judge("x.baf", &f);
    ut_check(o.verdict == ENTITY_OFFERED && o.section == ENTITY_SECTION_CREATURES &&
                 o.pickup_class == 0,
             "a bodiless file the levels raise as class 3 is a creature");
    o = entity_offer_judge("drdfitr.baf", &f);
    ut_check(o.verdict == ENTITY_OFFERED && o.section == ENTITY_SECTION_VEHICLES,
             "the same facts under a name the table marks a vehicle stand on the vehicle shelf");
    f = facts_of(2u, false, false, 1u << 14);
    o = entity_offer_judge("x.baf", &f);
    ut_check(o.verdict == ENTITY_OFFERED && o.section == ENTITY_SECTION_PICKUPS &&
                 o.pickup_class == 14,
             "class 14 is a pickup raised as 14");
    f = facts_of(2u, false, false, (1u << 3) | (1u << 13));
    o = entity_offer_judge("x.baf", &f);
    ut_check(o.section == ENTITY_SECTION_PICKUPS && o.pickup_class == 13,
             "a file raised as class 3 and as 13 is the pickup, raised as 13");
    f = facts_of(2u, false, false, (1u << 10) | (1u << 27));
    ut_check(entity_offer_judge("x.baf", &f).pickup_class == 10,
             "the pickup range is 10 to 27 inclusive, and the lowest class is taken");
    f = facts_of(2u, false, false, (1u << 9) | (1u << 28));
    ut_check(entity_offer_judge("x.baf", &f).verdict == ENTITY_UNPROVEN,
             "classes 9 and 28 lie outside the pickup range and prove nothing");
    f = facts_of(1u, false, true, (1u << 0) | (1u << 4));
    o = entity_offer_judge("x.baf", &f);
    ut_check(o.verdict == ENTITY_OFFERED && o.section == ENTITY_SECTION_GUNS,
             "turret and target with class 4 is the gun");
    f = facts_of(1u, false, true, 1u << 0);
    ut_check(entity_offer_judge("x.baf", &f).verdict == ENTITY_UNPROVEN,
             "turret and target without class 4 or a living class are not enough");
    f = facts_of(1u, false, false, (1u << 0) | (1u << 4));
    ut_check(entity_offer_judge("x.baf", &f).verdict == ENTITY_UNPROVEN,
             "class 4 without turret and target is not enough either");
    f = facts_of(4u, false, true, 1u << 2);
    ut_check(entity_offer_judge("x.baf", &f).section == ENTITY_SECTION_GUNS,
             "a mount raised as a living turret still stands on the gun shelf");
    f = facts_of(5u, false, false, (1u << 0) | (1u << 8));
    ut_check(entity_offer_judge("x.baf", &f).verdict == ENTITY_UNPROVEN,
             "a prop of class 0 and a class 8 vehicle are not offered");
    ut_check(entity_offer_judge(NULL, &f).verdict == ENTITY_UNPROVEN &&
                 entity_offer_judge("x.baf", NULL).verdict == ENTITY_UNPROVEN,
             "no file or no facts is never offered");
}

static void the_retail_data(void)
{
    uint32_t offered = 0;
    uint32_t refused = 0;
    uint32_t no_clips = 0;
    uint32_t no_clips_placed = 0;
    uint32_t no_clips_offered = 0;
    uint32_t unfounded = 0;
    uint32_t shelf[ENTITY_SECTION_COUNT];
    bool     sorted = true;
    bool     found = true;
    uint32_t i;
    char     what[160];

    memset(shelf, 0, sizeof shelf);
    ut_section("the retail data");
    ut_check(entity_offer_rows() == 303u, "the archive holds 303 actor files");
    for (i = 0; i < entity_offer_rows(); ++i) {
        const entity_class_row_t *row = entity_offer_row_at(i);
        entity_facts_t            f;
        entity_offer_t            o;

        entity_offer_row_facts(row, &f);
        o = entity_offer_judge(row->file, &f);
        if (i > 0 && strcmp(entity_offer_row_at(i - 1u)->file, row->file) >= 0) {
            sorted = false;
        }
        if (entity_offer_row(row->file) != row) {
            found = false;
        }
        if (row->clips == 0u) {
            ++no_clips;
            no_clips_placed += row->placements != 0u ? 1u : 0u;
        }
        if (o.verdict != ENTITY_OFFERED) {
            ++refused;
            continue;
        }
        ++offered;
        ++shelf[o.section];
        no_clips_offered += row->clips == 0u ? 1u : 0u;
        if (!f.body && row->classes == 0u) {
            ++unfounded;
        }
    }
    ut_check(sorted && found, "the table is sorted by name and every row is found by its name");
    text_format(what, sizeof what, "204 files are offered and 99 are not (%u and %u)",
                offered, refused);
    ut_check(offered == 204u && refused == 99u, what);
    text_format(what, sizeof what, "eighteen files have no clip and no retail level places "
                "one (%u, %u placed)", no_clips, no_clips_placed);
    ut_check(no_clips == 18u && no_clips_placed == 0u, what);
    ut_check(no_clips_offered == 0u, "not one offered file lacks a clip");
    ut_check(unfounded == 0u, "every offered file has a body or a class the levels raise it as");
    text_format(what, sizeof what, "the shelves: 158 figures, 31 creatures, 4 vehicles, "
                "10 pickups and one gun (%u, %u, %u, %u, %u)", shelf[ENTITY_SECTION_FIGURES],
                shelf[ENTITY_SECTION_CREATURES], shelf[ENTITY_SECTION_VEHICLES],
                shelf[ENTITY_SECTION_PICKUPS], shelf[ENTITY_SECTION_GUNS]);
    ut_check(shelf[ENTITY_SECTION_FIGURES] == 158u && shelf[ENTITY_SECTION_CREATURES] == 31u &&
                 shelf[ENTITY_SECTION_VEHICLES] == 4u && shelf[ENTITY_SECTION_PICKUPS] == 10u &&
                 shelf[ENTITY_SECTION_GUNS] == 1u && shelf[ENTITY_SECTION_LEVEL] == 0u,
             what);
    ut_check(entity_offer_pickup_class("pwrhlth1.baf") == 14 &&
                 entity_offer_pickup_class("PWRBACTA.BAF") == 13 &&
                 entity_offer_pickup_class("pwrshld1.baf") == 10 &&
                 entity_offer_pickup_class("pwrbiggn.baf") == 26,
             "the large medpack is 14, the small one 13, the shield 10, the heavy blaster 26");
    ut_check(entity_offer_pickup_class("baron.baf") == 0 &&
                 entity_offer_pickup_class("nosuch.baf") == 0 &&
                 entity_offer_pickup_class(NULL) == 0,
             "a figure, an unknown file and no file are no pickup");
    ut_check((entity_offer_classes("tripod.baf") & (1u << ENTITY_MOUNT_CLASS)) != 0u &&
                 entity_offer_row("TRIPOD.baf") != NULL,
             "the tripod is raised as class 4 somewhere, and found case aside");
}

static void the_names(void)
{
    uint32_t i;
    bool     sorted = true;
    bool     short_enough = true;
    bool     real = true;
    bool     offered = true;

    ut_section("the names");
    for (i = 0; i < entity_names_count(); ++i) {
        const char *stem = NULL;
        const char *name = NULL;
        const char *before = NULL;
        const char *unused = NULL;
        char        file[32];
        entity_facts_t f;

        (void)entity_names_at(i, &stem, &name);
        if (i > 0 && entity_names_at(i - 1u, &before, &unused) && strcmp(before, stem) >= 0) {
            sorted = false;
        }
        if (name != NULL && strlen(name) > ENTITY_NAME_MAX) {
            short_enough = false;
        }
        text_format(file, sizeof file, "%s.baf", stem);
        if (entity_offer_row(file) == NULL) {
            real = false;
            continue;
        }
        entity_offer_row_facts(entity_offer_row(file), &f);
        if (entity_offer_judge(file, &f).verdict != ENTITY_OFFERED) {
            offered = false;
        }
    }
    ut_check(sorted, "the name table is sorted by stem, with no stem twice");
    ut_check(short_enough, "no name is longer than a row keeps room for");
    ut_check(real, "every named stem is a file of the archive");
    ut_check(offered, "every named file is one the rule offers");
    ut_check(entity_name_of("BARON.baf") != NULL && strcmp(entity_name_of("baron.baf"),
                                                           "Battle droid") == 0,
             "a name is found by the file, case aside");
    ut_check(entity_name_of("baronx.baf") == NULL && entity_name_of("baro.baf") == NULL &&
                 entity_name_of(NULL) == NULL,
             "a longer or a shorter stem is not the same file");
    ut_check(entity_name_is_vehicle("mtt.baf") && !entity_name_is_vehicle("baron.baf"),
             "the transport is a vehicle and the battle droid is not");
}

int main(void)
{
    the_rule();
    the_retail_data();
    the_names();
    return ut_summary("entity offer");
}
