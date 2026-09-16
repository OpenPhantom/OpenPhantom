/* npc_foreign.c: see npc_foreign.h. */
#include "npc_foreign.h"

#include "actor_catalog.h"
#include "actor_loader.h"
#include "npc_census.h"
#include "spawn_scripts.h"

#include "common/text.h"

#include <string.h>

/* The archive's actor files the level did not load, as catalogue indices, offered after the
 * level's own kinds. */
static struct {
    uint32_t index[NPC_SPAWNER_FOREIGN_MAX];
    uint32_t count;
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

void npc_foreign_count(void)
{
    uint32_t i;

    st.count = 0;
    if (!actor_loader_is_available() || !actor_catalog_load()) {
        return;
    }
    for (i = 0; i < actor_catalog_count() && st.count < NPC_SPAWNER_FOREIGN_MAX; ++i) {
        const actor_catalog_entry_t *entry = actor_catalog_at(i);

        /* Every creature, and the tripod, which has no head or chest and is worth having:
         * the player can mount it. */
        if ((entry->has_body || _stricmp(entry->name, "tripod.baf") == 0) &&
            !level_has_file(entry->name)) {
            st.index[st.count++] = i;
        }
    }
}

uint32_t npc_foreign_kind_count(void)
{
    return st.count;
}

/* The stem of an archive name, lower-cased, as the level's own kinds are named. */
static void stem_of(const char *file, char *out, uint32_t out_size)
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
    entry = actor_catalog_at(st.index[index]);
    if (entry == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    stem_of(entry->name, out->name, sizeof out->name);
    text_format(out->file, sizeof out->file, "%s", entry->name);
    out->foreign = true;
    return true;
}

void npc_foreign_write_record(const uint8_t *level, uint8_t *record, const char *stem,
                              const char *file)
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

    if (npc_census()->count == 0 ||
        !npc_census_read_placement(level, npc_census()->source[0], record, probe, sizeof probe,
                                   NULL, 0u)) {
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
    memcpy(record + PLACE_RANGE, &word, sizeof word);
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
