/* npc_census.c: see npc_census.h. */
#include "npc_census.h"

#include "entity_names.h"
#include "entity_offer.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stddef.h>
#include <string.h>

static npc_census_t census = { .chosen = -1 };

npc_census_t *npc_census(void)
{
    return &census;
}

/* One placement's record and the name of its actor file, read without trusting either pointer.
 * False for a record this will not offer: unreadable, hosted, not a class 1 to 3, a model index
 * past the table, no model there, or the script anchor's inviso.baf, which has no body to see. */
bool npc_census_read_placement(const uint8_t *level, uint32_t index, uint8_t *record,
                               char *stem, uint32_t stem_size, char *file, uint32_t file_size)
{
    uint32_t directory = 0;
    uint32_t address = 0;
    uint32_t models = 0;
    uint32_t model = 0;
    int32_t  model_index;
    int32_t  model_count;
    int32_t  class_id;
    char     name[ACTOR_FILE_NAME_LENGTH + 1];
    uint32_t i;

    if (!memory_try_read((uintptr_t)level + WORLD_PLACEMENTS, &directory, sizeof directory) ||
        !memory_try_read((uintptr_t)directory + 4u * index, &address, sizeof address) ||
        address == 0 || !memory_try_read((uintptr_t)address, record, PLACE_SIZE)) {
        return false;
    }
    if ((*(const uint32_t *)(record + PLACE_FLAGS) & PLACE_FLAG_HOSTED) != 0) {
        return false;
    }
    memcpy(&class_id, record + PLACE_CLASS, sizeof class_id);
    if (class_id < CLASS_OFFERED_LOW || class_id > CLASS_OFFERED_HIGH) {
        return false;
    }
    memcpy(&model_index, record + PLACE_MODEL_INDEX, sizeof model_index);
    if (!memory_try_read((uintptr_t)level + WORLD_MODEL_COUNT, &model_count, sizeof model_count) ||
        model_index < 0 || model_index >= model_count ||
        !memory_try_read((uintptr_t)level + WORLD_MODELS, &models, sizeof models) ||
        !memory_try_read((uintptr_t)models + 4u * (uint32_t)model_index, &model, sizeof model) ||
        model == 0 ||
        !memory_try_read((uintptr_t)model + ACTOR_FILE_NAME, name, ACTOR_FILE_NAME_LENGTH)) {
        return false;
    }
    name[ACTOR_FILE_NAME_LENGTH] = '\0';
    if (file != NULL) {
        text_format(file, file_size, "%s", name);
    }
    /* The stem, lower-cased so "DDroid.baf" and "ddroid.baf" read as one; the engine's own test
     * for the anchor is case-insensitive too. */
    for (i = 0; i < stem_size - 1u && name[i] != '\0' && name[i] != '.'; ++i) {
        stem[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] - 'A' + 'a') : name[i];
    }
    stem[i] = '\0';
    return strcmp(stem, "inviso") != 0;
}

/* How plain a placement is, 0 to 2: one point for being spawned by the activation scan and not
 * by a script, one for a name of the level's own "enemyNNN" pattern and not a given one. */
static uint32_t plainness(const uint8_t *record)
{
    uint32_t flags;
    uint32_t points = 0;

    memcpy(&flags, record + PLACE_FLAGS, sizeof flags);
    if ((flags & PLACE_FLAG_SCANNED) != 0) {
        points++;
    }
    if (strncmp((const char *)record + PLACE_NAME, "enemy", 5) == 0) {
        points++;
    }
    return points;
}

/* Whether a placement's kind is one the level offers as its own. Its file must have a clip, read
 * off the model the level loaded, which is the one the spawn would bind; and it must not be a file
 * that stands on a shelf of its own, a pickup or the gun, which the archive offers there so that
 * one file is never offered twice. Counted either way, for the line below. */
static bool offered_as_level_kind(const uint8_t *level, const uint8_t *record, const char *file,
                                  uint32_t *no_clips, uint32_t *own_shelf)
{
    uint32_t       models = 0;
    uint32_t       model  = 0;
    uint32_t       clips  = 0;
    int32_t        model_index;
    entity_facts_t facts;
    entity_offer_t offer;

    memcpy(&model_index, record + PLACE_MODEL_INDEX, sizeof model_index);
    if (!memory_try_read((uintptr_t)level + WORLD_MODELS, &models, sizeof models) ||
        !memory_try_read((uintptr_t)models + 4u * (uint32_t)model_index, &model, sizeof model) ||
        !memory_try_read((uintptr_t)model + ACTOR_FILE_CLIPS, &clips, sizeof clips) ||
        clips == 0u) {
        ++*no_clips;
        return false;
    }
    entity_offer_row_facts(entity_offer_row(file), &facts);
    offer = entity_offer_judge(file, &facts);
    if (offer.verdict == ENTITY_OFFERED &&
        (offer.section == ENTITY_SECTION_PICKUPS || offer.section == ENTITY_SECTION_GUNS)) {
        ++*own_shelf;
        return false;
    }
    return true;
}

/* The words a kind's row starts with: the name a person reads, or the stem. */
static const char *display_of(const npc_spawner_kind_t *kind)
{
    const char *name = entity_name_of(kind->file);

    return name != NULL ? name : kind->name;
}

/* The kinds by the name a person reads, carrying their parallel columns with them. At most a few
 * dozen kinds, once a level. */
static void sort_by_name(uint32_t *seen)
{
    uint32_t i;

    for (i = 1; i < census.count; ++i) {
        npc_spawner_kind_t kind   = census.kind[i];
        uint32_t           source = census.source[i];
        uint32_t           plain  = census.plain[i];
        int32_t            model  = census.model[i];
        uint32_t           mask   = seen[i];
        uint32_t           j      = i;

        while (j > 0 && _stricmp(display_of(&census.kind[j - 1u]), display_of(&kind)) > 0) {
            census.kind[j]   = census.kind[j - 1u];
            census.source[j] = census.source[j - 1u];
            census.plain[j]  = census.plain[j - 1u];
            census.model[j]  = census.model[j - 1u];
            seen[j]          = seen[j - 1u];
            --j;
        }
        census.kind[j]   = kind;
        census.source[j] = source;
        census.plain[j]  = plain;
        census.model[j]  = model;
        seen[j]          = mask;
    }
}

/* The generated class table held against the level in memory: two readings of the same placements,
 * one from the files on the disk and one from the world record. A kind the level raises under a
 * class the table does not list is a kind one of the two reads wrongly. */
static void check_the_table(const uint32_t *seen)
{
    uint32_t agree = 0;
    uint32_t k;

    for (k = 0; k < census.count; ++k) {
        if ((seen[k] & ~entity_offer_classes(census.kind[k].file)) == 0u) {
            ++agree;
        }
    }
    log_info("npc spawner: the class table agrees with this level on %u of its %u kinds, and "
             "%u differ", agree, census.count, census.count - agree);
}

void npc_census_count(const uint8_t *level)
{
    int32_t  placements = 0;
    uint32_t left_off = 0;
    uint32_t no_clips = 0;
    uint32_t own_shelf = 0;
    uint32_t seen[NPC_SPAWNER_KINDS_MAX];
    uint32_t i;

    memset(&census, 0, sizeof census);
    memset(seen, 0, sizeof seen);
    census.chosen = -1;
    census.donor  = NPC_SPAWN_NO_SOURCE;
    census.level  = level;
    if (level == NULL ||
        !memory_try_read((uintptr_t)level + WORLD_PLACEMENT_COUNT, &placements,
                         sizeof placements) ||
        placements <= 0) {
        return;
    }
    if ((uint32_t)placements > PLACEMENTS_MAX) {
        log_warning("npc spawner: the level says it has %d placements, past the %u the engine "
                    "itself allows, so the first %u are read", placements, PLACEMENTS_MAX,
                    PLACEMENTS_MAX);
        placements = (int32_t)PLACEMENTS_MAX;
    }
    for (i = 0; i < (uint32_t)placements; ++i) {
        uint8_t  record[PLACE_SIZE];
        char     stem[NPC_SPAWNER_NAME_MAX];
        char     file[NPC_SPAWNER_FILE_MAX];
        int32_t  model_index;
        int32_t  class_id;
        uint32_t k;

        if (!npc_census_read_placement(level, i, record, stem, sizeof stem, file, sizeof file) ||
            !offered_as_level_kind(level, record, file, &no_clips, &own_shelf)) {
            continue;
        }
        memcpy(&model_index, record + PLACE_MODEL_INDEX, sizeof model_index);
        memcpy(&class_id, record + PLACE_CLASS, sizeof class_id);
        for (k = 0; k < census.count; ++k) {
            if (census.model[k] == model_index) {
                census.kind[k].placements++;
                seen[k] |= 1u << (uint32_t)class_id;
                /* The copy is taken from the plainest placement of the kind: one the activation
                 * scan spawns itself and the level did not trouble to name. The named ones
                 * ("tomo", "igotyazzz", "beast") and the script-spawned ones carry the level's
                 * own business in their scripts, and a plain "enemy031" carries a patrol. */
                if (plainness(record) > census.plain[k]) {
                    census.source[k] = i;
                    census.plain[k]  = plainness(record);
                }
                break;
            }
        }
        if (k < census.count) {
            continue;
        }
        if (census.count == NPC_SPAWNER_KINDS_MAX) {
            left_off++;
            continue;
        }
        census.model[k]  = model_index;
        census.source[k] = i;
        census.plain[k]  = plainness(record);
        seen[k]          = 1u << (uint32_t)class_id;
        census.kind[k].placements = 1;
        text_format(census.kind[k].name, sizeof census.kind[k].name, "%s", stem);
        text_format(census.kind[k].file, sizeof census.kind[k].file, "%s", file);
        census.count++;
    }
    if (left_off != 0) {
        log_warning("npc spawner: the level uses %u more actor files than the %u the list holds, "
                    "so those are not offered", left_off, NPC_SPAWNER_KINDS_MAX);
    }
    census.donor = census.count != 0 ? census.source[0] : NPC_SPAWN_NO_SOURCE;
    check_the_table(seen);
    sort_by_name(seen);
    log_info("npc spawner: the level at %08X has %d placements using %u actor files (%u "
             "placement(s) left to their own shelf, %u of a file without a clip)",
             (unsigned)(uintptr_t)level, placements, census.count, own_shelf, no_clips);
}
