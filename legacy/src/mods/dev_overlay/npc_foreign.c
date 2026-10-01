/* npc_foreign.c: see npc_foreign.h. */
#include "npc_foreign.h"

#include "actor_catalog.h"
#include "actor_loader.h"
#include "entity_names.h"
#include "entity_offer.h"
#include "npc_census.h"
#include "spawn_scripts.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdlib.h>
#include <string.h>

/* The archive's actor files the offer rule passes and the level did not load, as catalogue
 * indices with their shelf, offered after the level's own kinds in shelf order and by name. */
typedef struct foreign_entry {
    uint32_t index;
    uint8_t  section;
} foreign_entry_t;

/* What one count came to, for the line a field run reads. Said when it changes, since the list is
 * counted again every time it is opened. */
typedef struct foreign_tally {
    uint32_t files;
    uint32_t offered;
    uint32_t no_clips;
    uint32_t anchors;
    uint32_t unproven;
    uint32_t level_own;
    uint32_t table_agrees;
    uint32_t table_differs;
    uint32_t table_missing;
    uint32_t table_extra;     /* rows of the table the archive does not hold */
    uint32_t names_stale;     /* names in the name table for a file the rule does not offer */
    uint32_t names;
} foreign_tally_t;

static struct {
    foreign_entry_t entry[NPC_SPAWNER_FOREIGN_MAX];
    uint32_t        count;
    foreign_tally_t said;
} st;

/* Whether the catalogue's file `name` is one of the level's own kinds already. */
static bool level_has_file(const char *name)
{
    uint32_t k;

    for (k = 0; k < npc_census()->count; ++k) {
        if (_stricmp(npc_census()->kind[k].file, name) == 0) {
            return true;
        }
    }
    return false;
}

/* The words a row starts with: the name a person reads, or the stem. */
static void display_of(const char *file, char *out, uint32_t out_size)
{
    const char *name = entity_name_of(file);

    if (name != NULL) {
        text_format(out, out_size, "%s", name);
        return;
    }
    npc_foreign_stem(file, out, out_size);
}

static int by_shelf_then_name(const void *a, const void *b)
{
    const foreign_entry_t *x = (const foreign_entry_t *)a;
    const foreign_entry_t *y = (const foreign_entry_t *)b;
    char                   one[NPC_SPAWNER_FILE_MAX];
    char                   two[NPC_SPAWNER_FILE_MAX];

    if (x->section != y->section) {
        return (int)x->section - (int)y->section;
    }
    display_of(actor_catalog_at(x->index)->name, one, sizeof one);
    display_of(actor_catalog_at(y->index)->name, two, sizeof two);
    return _stricmp(one, two);
}

/* The archive as read at run time held against the generated table, file by file: the two are two
 * readings of one archive, and a file they disagree about is one whose offer rests on only one. */
static void tally_table(const actor_catalog_entry_t *entry, const entity_facts_t *facts,
                        foreign_tally_t *tally)
{
    const entity_class_row_t *row = entity_offer_row(entry->name);
    entity_facts_t            listed;

    if (row == NULL) {
        ++tally->table_missing;
        return;
    }
    entity_offer_row_facts(row, &listed);
    if (listed.clips == facts->clips && listed.body == facts->body &&
        listed.mount == facts->mount) {
        ++tally->table_agrees;
    } else {
        ++tally->table_differs;
    }
}

/* The other way round: what the table and the name table hold that the archive, as this process
 * read it, does not offer. A row with no file behind it is a table made from another archive; a
 * name for a file the rule refuses is a name nobody can see. */
static void tally_the_tables(foreign_tally_t *tally)
{
    uint32_t i;

    for (i = 0; i < entity_offer_rows(); ++i) {
        if (actor_catalog_find(entity_offer_row_at(i)->file) == NULL) {
            ++tally->table_extra;
        }
    }
    tally->names = entity_names_count();
    for (i = 0; i < tally->names; ++i) {
        const char                  *stem = NULL;
        const char                  *name = NULL;
        const actor_catalog_entry_t *entry;
        entity_facts_t               facts;
        char                         file[NPC_SPAWNER_FILE_MAX];

        if (!entity_names_at(i, &stem, &name)) {
            continue;
        }
        text_format(file, sizeof file, "%s.baf", stem);
        entry = actor_catalog_find(file);
        if (entry == NULL) {
            ++tally->names_stale;
            continue;
        }
        facts.clips   = entry->clips;
        facts.body    = entry->has_body;
        facts.mount   = entry->has_mount;
        facts.classes = entity_offer_classes(entry->name);
        if (entity_offer_judge(entry->name, &facts).verdict != ENTITY_OFFERED) {
            ++tally->names_stale;
        }
    }
}

static void say_the_tally(const foreign_tally_t *tally)
{
    if (memcmp(tally, &st.said, sizeof *tally) == 0) {
        return;
    }
    st.said = *tally;
    log_info("npc spawner: the offer: %u of the archive's %u actor files are offered beyond the "
             "level's own (%u already the level's), %u refused without a clip, %u as the script "
             "anchor, %u with nothing that proves them; the table agrees with the archive on %u, "
             "differs on %u, lacks %u and lists %u it does not hold; %u of the %u names name no "
             "offered file", tally->offered, tally->files, tally->level_own, tally->no_clips,
             tally->anchors, tally->unproven, tally->table_agrees, tally->table_differs,
             tally->table_missing, tally->table_extra, tally->names_stale, tally->names);
}

void npc_foreign_count(void)
{
    foreign_tally_t tally;
    uint32_t        i;

    st.count = 0;
    memset(&tally, 0, sizeof tally);
    if (!actor_loader_is_available() || !actor_catalog_load()) {
        return;
    }
    for (i = 0; i < actor_catalog_count(); ++i) {
        const actor_catalog_entry_t *entry = actor_catalog_at(i);
        entity_facts_t               facts;
        entity_offer_t               offer;

        /* The node facts are the archive's as this process read it; only the classes, which live
         * in the levels, come from the table. */
        facts.clips   = entry->clips;
        facts.body    = entry->has_body;
        facts.mount   = entry->has_mount;
        facts.classes = entity_offer_classes(entry->name);
        offer         = entity_offer_judge(entry->name, &facts);
        ++tally.files;
        tally_table(entry, &facts, &tally);
        if (offer.verdict == ENTITY_NO_CLIPS) {
            ++tally.no_clips;
        } else if (offer.verdict == ENTITY_ANCHOR) {
            ++tally.anchors;
        } else if (offer.verdict != ENTITY_OFFERED) {
            ++tally.unproven;
        } else if (level_has_file(entry->name)) {
            ++tally.level_own;
        } else if (st.count < NPC_SPAWNER_FOREIGN_MAX) {
            st.entry[st.count].index   = i;
            st.entry[st.count].section = (uint8_t)offer.section;
            ++st.count;
            ++tally.offered;
        }
    }
    qsort(st.entry, st.count, sizeof st.entry[0], &by_shelf_then_name);
    tally_the_tables(&tally);
    say_the_tally(&tally);
}

uint32_t npc_foreign_kind_count(void)
{
    return st.count;
}

void npc_foreign_stem(const char *file, char *out, uint32_t out_size)
{
    uint32_t i;

    for (i = 0; i + 1u < out_size && file[i] != '\0' && file[i] != '.'; ++i) {
        out[i] = (file[i] >= 'A' && file[i] <= 'Z') ? (char)(file[i] - 'A' + 'a') : file[i];
    }
    out[i] = '\0';
}

bool npc_foreign_kind(uint32_t index, npc_spawner_kind_t *out)
{
    const actor_catalog_entry_t *entry;

    if (out == NULL || index >= st.count) {
        return false;
    }
    entry = actor_catalog_at(st.entry[index].index);
    if (entry == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    npc_foreign_stem(entry->name, out->name, sizeof out->name);
    text_format(out->file, sizeof out->file, "%s", entry->name);
    out->foreign = true;
    out->section = st.entry[index].section;
    return true;
}

void npc_foreign_write_record(const uint8_t *level, uint8_t *record, const char *stem,
                              const char *file, uint8_t donor)
{
    static const float DEFAULT_MASS  = 1.0f;
    static const float DEFAULT_TURN  = 180.0f;
    static const float DEFAULT_FOV   = 90.0f;
    static const float FIRE_INTERVAL = 0.6f;   /* a shooter's reload, seconds, drawn at random
                                                * up to this; the engine's own floor is half a
                                                * second */
    uint16_t weapon = spawn_script_model_weapon(file);
    int32_t  word;
    char     name[PLACE_NAME_MAX + 1u];
    char     probe[NPC_SPAWNER_NAME_MAX];

    if (donor == NPC_SPAWN_NO_SOURCE ||
        !npc_census_read_placement(level, donor, record, probe, sizeof probe, NULL, 0u)) {
        memset(record, 0, PLACE_SIZE);
        memcpy(record + PLACE_MASS, &DEFAULT_MASS, sizeof DEFAULT_MASS);
        memcpy(record + PLACE_TURN_RATE, &DEFAULT_TURN, sizeof DEFAULT_TURN);
        memcpy(record + PLACE_FOV, &DEFAULT_FOV, sizeof DEFAULT_FOV);
        word = PLACE_HIT_POINTS_LEAST;
        memcpy(record + PLACE_HIT_POINTS, &word, sizeof word);
    }
    word = PLACE_FLAG_SCANNED;
    memcpy(record + PLACE_FLAGS, &word, sizeof word);
    word = spawn_script_flyer_mode(file);   /* 0 for a walker */
    memcpy(record + PLACE_MOVE_MODE, &word, sizeof word);
    word = 0;
    memcpy(record + PLACE_MOVE_SPEED, &word, sizeof word);
    memcpy(record + PLACE_ACTIVATION_RANGE, &word, sizeof word);
    memcpy(record + PLACE_FIRE_INTERVAL, &FIRE_INTERVAL, sizeof FIRE_INTERVAL);
    memcpy(record + PLACE_MIN_DIFFICULTY, &word, sizeof word);
    memcpy(record + PLACE_DETAIL, &word, sizeof word);
    memcpy(record + PLACE_SCRIPT_INDEX, &word, sizeof word);
    word = NPC_FOREIGN_MODEL_SLOT;
    memcpy(record + PLACE_MODEL_INDEX, &word, sizeof word);
    /* The copied placement's first weapon is its own, a rocket where it carried a bazooka; an
     * archive model's is the one the retail levels arm it with, else the blaster. */
    if (weapon == 0) {
        weapon = PLACE_SHOT_KIND_BLASTER;
    }
    memcpy(record + PLACE_WEAPON_KINDS, &weapon, sizeof weapon);
    text_format(name, sizeof name, "%s", stem);
    memset(record + PLACE_NAME, 0, 0x10u);
    memcpy(record + PLACE_NAME, name, strlen(name));
}
