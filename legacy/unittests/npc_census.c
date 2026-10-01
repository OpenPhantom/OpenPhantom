/* npc_census.c: what a loaded level offers the entity spawner as its own, counted over a level
 * record laid out in this process's memory the way the engine lays out its world.
 *
 * What would be silent if it were wrong: a level kind whose file has no clip offered, which the
 * spawn would play clip 0 on and the script opcodes then write in front of its track array; and a
 * pickup or the gun offered twice, once as the level's and once on its own shelf.
 */
#include "unittest.h"

#include "common/text.h"

#include "npc_census.h"

#include <string.h>

#define MODELS     4u
#define PLACEMENTS 5u
#define MODEL_SIZE (ACTOR_FILE_CLIPS + 4u)

static uint8_t  s_level[0x210];
static uint8_t  s_model[MODELS][MODEL_SIZE];
static uint32_t s_models[MODELS];
static uint8_t  s_place[PLACEMENTS][PLACE_SIZE];
static uint32_t s_directory[PLACEMENTS];

static void put(uint8_t *at, uint32_t offset, uint32_t value)
{
    memcpy(at + offset, &value, sizeof value);
}

static void model(uint32_t index, const char *file, uint32_t clips)
{
    memset(s_model[index], 0, MODEL_SIZE);
    memcpy(s_model[index] + ACTOR_FILE_NAME, file, strlen(file));
    put(s_model[index], ACTOR_FILE_CLIPS, clips);
    s_models[index] = (uint32_t)(uintptr_t)s_model[index];
}

static void placement(uint32_t index, uint32_t model_index, uint32_t class_id)
{
    memset(s_place[index], 0, PLACE_SIZE);
    put(s_place[index], PLACE_CLASS, class_id);
    put(s_place[index], PLACE_MODEL_INDEX, model_index);
    s_directory[index] = (uint32_t)(uintptr_t)s_place[index];
}

/* A level of five placements: a figure with clips twice, a figure with none, a pickup placed as a
 * living thing, and a second figure with clips. */
static void a_level(uint32_t clipless_clips)
{
    memset(s_level, 0, sizeof s_level);
    model(0u, "tusken.baf", 12u);
    model(1u, "nothing.baf", clipless_clips);
    model(2u, "pwrhlth1.baf", 2u);
    model(3u, "baron.baf", 40u);
    placement(0u, 0u, 2u);
    placement(1u, 1u, 2u);
    placement(2u, 2u, 2u);
    placement(3u, 3u, 3u);
    placement(4u, 0u, 2u);
    put(s_level, WORLD_MODEL_COUNT, MODELS);
    put(s_level, WORLD_MODELS, (uint32_t)(uintptr_t)s_models);
    put(s_level, WORLD_PLACEMENT_COUNT, PLACEMENTS);
    put(s_level, WORLD_PLACEMENTS, (uint32_t)(uintptr_t)s_directory);
}

static bool offers(const char *file)
{
    uint32_t i;

    for (i = 0; i < npc_census()->count; ++i) {
        if (strcmp(npc_census()->kind[i].file, file) == 0) {
            return true;
        }
    }
    return false;
}

int main(void)
{
    char what[160];

    ut_section("a level's own kinds");
    a_level(0u);
    npc_census_count(s_level);
    text_format(what, sizeof what, "two kinds are offered, the figures with clips (%u)",
                npc_census()->count);
    ut_check(npc_census()->count == 2u && offers("tusken.baf") && offers("baron.baf"), what);
    ut_check(!offers("nothing.baf"),
             "a kind whose loaded file has no clip is not offered: the spawn would play clip 0");
    ut_check(!offers("pwrhlth1.baf"),
             "a pickup is left to its own shelf even where the level places it as a living thing");
    ut_check(npc_census()->kind[0].placements + npc_census()->kind[1].placements == 3u,
             "and the placements of the offered kinds are counted, the figure placed twice as one");

    ut_section("the same file with a clip");
    a_level(1u);
    npc_census_count(s_level);
    ut_check(npc_census()->count == 3u && offers("nothing.baf"),
             "one clip is enough: the gate is the clip, not the name");

    ut_section("no level");
    npc_census_count(NULL);
    ut_check(npc_census()->count == 0u && npc_census()->level == NULL, "no level offers nothing");
    return ut_summary("npc census");
}
