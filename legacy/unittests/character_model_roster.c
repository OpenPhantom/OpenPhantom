/* character_model_roster.c: the half of the model swap that can be wrong without a game.
 *
 * The swap itself needs a body, a rig and five resolved engine entries, and none of that exists in
 * a test process. What does not need any of it is the question the panel asks first and the far
 * machine asks last: is THIS name the same asset as THAT one. It is asked with a name typed into a
 * data file, a name read out of the engine's own hero table, and a name that arrived over a wire,
 * and the three spell things differently: one carries a suffix, one is upper case, one is neither.
 *
 * Every way it can be wrong is quiet. A comparison that is too strict marks no row Current, so the
 * panel shows a player wearing nothing he can see; one that is too loose marks several, and the
 * one it picks decides which body a remote player is built with.
 */
#include "unittest.h"

#include "character_model_roster.h"

/* character_model_candidate_of is DEFINED in the roster and DECLARED in the panel's own
 * header, because it is what the panel asks and the roster answers. */
#include "character_model.h"

#include "common/character_profile.h"

#include <stddef.h>
#include <string.h>

static void check_two_names_are_one_asset(void)
{
    ut_section("when two names mean the same asset");

    ut_check(character_model_asset_matches("quigon.baf", "quigon.baf"), "the same name");
    ut_check(character_model_asset_matches("quigon", "quigon.baf"),
             "with the suffix and without it: the data file writes one, the wire the other");
    ut_check(character_model_asset_matches("QuiGon.BAF", "quigon.baf"),
             "in either case, because the engine's own table is not written the way the file is");
    ut_check(character_model_asset_matches("obiwan.3do", "obiwan.baf"),
             "and whatever the suffix says, because the stem is the asset");

    ut_section("and when they do not");
    ut_check(!character_model_asset_matches("quigon.baf", "obiwan.baf"), "two assets");
    ut_check(!character_model_asset_matches("qui.baf", "quigon.baf"),
             "a stem that is a prefix of the other is not the other");
    ut_check(!character_model_asset_matches("quigonn.baf", "quigon.baf"),
             "nor one letter longer");
    ut_check(!character_model_asset_matches(NULL, "quigon.baf"), "no name at all");
    ut_check(!character_model_asset_matches("quigon.baf", NULL), "on either side");
    ut_check(!character_model_asset_matches(".baf", "quigon.baf"),
             "an empty stem matches nothing, or every unnamed thing would be every other");
    ut_check(!character_model_asset_matches(".baf", ".baf"),
             "including another empty one, which is the case that would be worst");
}

static void check_the_rows_written_in_code(void)
{
    uint32_t    count = character_model_roster_count();
    const char *first = character_model_roster_asset(0);
    const char *label = character_model_roster_label(0);
    uint32_t    i;

    ut_section("the rows that are in the source rather than in the data file");

    /* Five, and they are there whether or not a data file was ever read: they are the only assets
     * a player can already be wearing, so the row for the model he is in has to be offered. */
    ut_check(count >= 5u, "the five written rows are always offered");
    ut_check(first != NULL && strcmp(first, "obiwan.baf") == 0,
             "row zero is the first name of the engine's own hero table");
    ut_check(label != NULL && strstr(label, "Obi-Wan") != NULL, "and it reads as a person");

    for (i = 0; i < 5u; ++i) {
        const char *asset = character_model_roster_asset(i);

        ut_checkf(asset != NULL && asset[0] != '\0', "row %u names an asset", (unsigned)i);
        ut_checkf(character_model_candidate_of(asset) >= 0,
                  "and row %u can be found again by that name, which is what marks it Current",
                  (unsigned)i);
    }

    ut_section("and an id that names no row");
    ut_check(character_model_roster_asset(count) == NULL, "one past the end");
    ut_check(character_model_roster_label(count) == NULL, "for the label as well");
    ut_check(character_model_roster_asset(0xFFFFFFFFu) == NULL, "and far past it");
    ut_check(character_model_candidate_of("nothing_like_this.baf") < 0, "a name nobody carries");
    ut_check(character_model_candidate_of(NULL) < 0, "and no name at all");
}

/* The rows past the five are the data file, and a model swap onto an NPC is one of them. This
 * program is built into a directory of its own with the shipped characters.ini beside it, so the
 * load finds the real table; a DLL that never loads it offers the five heroes and nobody else,
 * which is why the overlay loads it at install. */
static void check_the_rows_read_from_the_data_file(void)
{
    const character_profile_t *first;
    uint32_t                   count;

    ut_section("the rows the data file adds, which are every NPC the swap can offer");
    ut_check(character_profile_load(), "the shipped characters.ini beside this program is read");
    count = character_model_roster_count();
    ut_checkf(count == 5u + character_profile_count() && character_profile_count() >= 160u,
              "every profile is a row after the five heroes (%u rows)", (unsigned)count);
    first = character_profile_at(0u);
    ut_check(first != NULL && character_model_roster_asset(5u) != NULL &&
                 strcmp(character_model_roster_asset(5u), first->asset) == 0,
             "row five is the file's first character");
    ut_check(character_model_candidate_of("baron.baf") >= 5,
             "and an NPC by its asset name is a row past the heroes");
    ut_check(character_model_roster_asset(count) == NULL, "with nothing past the last");
}

int main(void)
{
    check_two_names_are_one_asset();
    check_the_rows_written_in_code();
    check_the_rows_read_from_the_data_file();
    return ut_summary("character_model_roster");
}
