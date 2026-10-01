#include "character_model_roster.h"

#include "character_model.h"

#include "common/character_profile.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct model_entry {
    const char *label;
    const char *asset;
} model_entry_t;

/* The five the roster starts with, because they are the only assets a player can already be
 * wearing and the row for the model he is in has to read Current. Four of them are the hero asset
 * table's own four names, read out of the image at 0x004B5180; the fifth is Mace, who rides
 * Qui-Gon's slot. Everything below them is the actor table the character tab reads, which is the
 * roster the data file carries rather than a list in code, and which lists none of these five. */
static const model_entry_t HEROES[] = {
    { "Look like Obi-Wan Kenobi", "obiwan.baf" },
    { "Look like Qui-Gon Jinn",   "quigon.baf" },
    { "Look like Mace Windu",     "mace.baf"   },
    { "Look like Captain Panaka", "panaka.baf" },
    { "Look like Queen Amidala",  "queen.baf"  }
};
#define HERO_ROWS       ((uint32_t)(sizeof HEROES / sizeof HEROES[0]))

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/* Everything up to the last dot, so "quigon" and "QuiGon.baf" are the same asset. */
static size_t stem_length(const char *name)
{
    const char *dot = strrchr(name, '.');

    return (dot != NULL) ? (size_t)(dot - name) : strlen(name);
}

bool character_model_asset_matches(const char *left, const char *right)
{
    size_t length;
    size_t i;

    if (left == NULL || right == NULL) {
        return false;
    }
    length = stem_length(left);
    if (length == 0u || length != stem_length(right)) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        if (lower(left[i]) != lower(right[i])) {
            return false;
        }
    }
    return true;
}

uint32_t character_model_roster_count(void)
{
    uint32_t count = HERO_ROWS + character_profile_count();

    return (count > MODEL_ROWS_MAX) ? MODEL_ROWS_MAX : count;
}

const char *character_model_roster_asset(uint32_t id)
{
    const character_profile_t *profile;

    if (id < HERO_ROWS) {
        return HEROES[id].asset;
    }
    profile = character_profile_at(id - HERO_ROWS);
    return (profile != NULL) ? profile->asset : NULL;
}

const char *character_model_roster_label(uint32_t id)
{
    static char label[CHARACTER_ASSET_MAX + 16];
    const char *asset;
    char       *dot;

    if (id < HERO_ROWS) {
        return HEROES[id].label;
    }
    asset = character_model_roster_asset(id);
    if (asset == NULL) {
        return NULL;
    }
    /* A player is choosing a character, not a file, so the suffix goes. */
    text_format(label, sizeof label, "Look like %s", asset);
    dot = strrchr(label, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    return label;
}

int32_t character_model_candidate_of(const char *asset)
{
    uint32_t count = character_model_roster_count();
    uint32_t i;

    if (asset == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (character_model_asset_matches(asset, character_model_roster_asset(i))) {
            return (int32_t)i;
        }
    }
    return -1;
}
