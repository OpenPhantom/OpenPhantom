/* character_profile.c: the profile table and the chain that answers when a role is empty.
 *
 * The parser is here rather than in ini.c on purpose. That module owns one file, the shared
 * configuration next to the executable, with one section per DLL, and this is not configuration:
 * it is a generated data table with a hundred and sixty four sections that ships whole beside the
 * game.
 * Mixing the two would swamp the file a player edits with rows nobody edits by hand.
 */
#include "character_profile.h"

#include "ini.h"
#include "logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROFILE_MAX      192u    /* 164 ship today; the headroom is for hand written rows */
#define LINE_MAX         256u

/* The shipped file holds 691 Action lines. The headroom is for hand written ones, and the pool is
 * shared rather than per profile because they are spread very unevenly: 40 characters have none at
 * all and one has 47. */
#define ACTION_POOL_MAX  1024u

typedef struct profile_state {
    bool                loaded;
    uint32_t            count;
    uint32_t            action_count;
    character_profile_t entry[PROFILE_MAX];
    character_action_t  action[ACTION_POOL_MAX];
} profile_state_t;

static profile_state_t profiles;

/* The spelling in characters.ini, in enum order. The generator writes exactly these words and
 * nothing else the runtime has to store, so a key here and a key there cannot drift apart. */
static const char *ROLE_KEY[CHARACTER_ROLE_COUNT] = {
    "Idle", "Walk", "Run", "Back", "Jump", "Attack", "Fire", "Melee", "Block",
    "Death", "Hit", "GetUp", "Strafe", "Talk"
};

bool character_profile_is_death_clip(const character_profile_t *profile, int32_t clip)
{
    uint32_t i;

    if (profile == NULL || clip < 0) {
        return false;
    }
    for (i = 0; i < profile->death_clip_count; ++i) {
        if ((int32_t)profile->death_clip[i] == clip) {
            return true;
        }
    }
    return false;
}

int32_t character_profile_death_clip(const character_profile_t *profile)
{
    if (profile == NULL) {
        return CHARACTER_PROFILE_NO_CLIP;
    }
    if (profile->clip[CHARACTER_ROLE_DEATH] != CHARACTER_PROFILE_NO_CLIP) {
        return profile->clip[CHARACTER_ROLE_DEATH];
    }
    if (profile->death_clip_count > 0u) {
        return (int32_t)profile->death_clip[0];
    }
    return CHARACTER_PROFILE_NO_CLIP;
}

const char *character_role_name(character_role_t role)
{
    if ((unsigned)role >= (unsigned)CHARACTER_ROLE_COUNT) {
        return "?";
    }
    return ROLE_KEY[role];
}

/* ============================================================================================ */
/* The pure half. No file, no engine, and therefore the half a test can drive. */

int32_t character_profile_convention(character_role_t role)
{
    switch (role) {
    case CHARACTER_ROLE_IDLE:
        return 0;
    case CHARACTER_ROLE_WALK:
        return 1;
    case CHARACTER_ROLE_RUN:
        return 2;
    default:
        return CHARACTER_PROFILE_NO_CLIP;
    }
}

/* The chains, written out.
 *
 * The first attempt at this was one hop plus a special case, and it was wrong in a way only a test
 * would have caught: asking an actor that has nothing but a run for a backward walk hopped to the
 * walk, found none, and then took the special case straight to the idle, so a character that could
 * have shuffled backwards stood still instead. A list per role cannot make that mistake and cannot
 * loop, and it can be read off the page.
 *
 * Each entry ends with CHARACTER_ROLE_COUNT. A role not listed here answers only for itself, which
 * is deliberate: a flinch that borrows the death animation kills the character on a graze, and a
 * jump that borrows the idle snaps the body to a stand in mid air, while a jump that answers
 * nothing simply holds the pose the body was already in.
 */
#define CHAIN_MAX 5u
static const character_role_t CHAIN[CHARACTER_ROLE_COUNT][CHAIN_MAX] = {
    /* IDLE   */ { CHARACTER_ROLE_IDLE, CHARACTER_ROLE_COUNT },
    /* WALK   */ { CHARACTER_ROLE_WALK, CHARACTER_ROLE_RUN, CHARACTER_ROLE_IDLE,
                   CHARACTER_ROLE_COUNT },
    /* RUN    */ { CHARACTER_ROLE_RUN, CHARACTER_ROLE_WALK, CHARACTER_ROLE_IDLE,
                   CHARACTER_ROLE_COUNT },
    /* BACK   */ { CHARACTER_ROLE_BACK, CHARACTER_ROLE_WALK, CHARACTER_ROLE_RUN,
                   CHARACTER_ROLE_IDLE, CHARACTER_ROLE_COUNT },
    /* JUMP   */ { CHARACTER_ROLE_JUMP, CHARACTER_ROLE_COUNT },
    /* ATTACK */ { CHARACTER_ROLE_ATTACK, CHARACTER_ROLE_FIRE, CHARACTER_ROLE_MELEE,
                   CHARACTER_ROLE_COUNT },
    /* FIRE   */ { CHARACTER_ROLE_FIRE, CHARACTER_ROLE_ATTACK, CHARACTER_ROLE_MELEE,
                   CHARACTER_ROLE_COUNT },
    /* MELEE  */ { CHARACTER_ROLE_MELEE, CHARACTER_ROLE_ATTACK, CHARACTER_ROLE_COUNT },
    /* BLOCK  */ { CHARACTER_ROLE_BLOCK, CHARACTER_ROLE_COUNT },
    /* DEATH  */ { CHARACTER_ROLE_DEATH, CHARACTER_ROLE_COUNT },
    /* HIT    */ { CHARACTER_ROLE_HIT, CHARACTER_ROLE_COUNT },
    /* GETUP  */ { CHARACTER_ROLE_GETUP, CHARACTER_ROLE_COUNT },
    /* STRAFE */ { CHARACTER_ROLE_STRAFE, CHARACTER_ROLE_COUNT },
    /* TALK   */ { CHARACTER_ROLE_TALK, CHARACTER_ROLE_COUNT }
};

/* All three ways of moving end at the idle, the backward walk included. Stopping it earlier would
 * be wrong: an actor with nothing but an idle would then keep whatever pose it happened to be in
 * while it slid backwards, which can be a death or an attack. Ending at the idle makes the worst
 * case a character that stands upright and slides, which reads as a missing animation rather than
 * as a broken game. */

int32_t character_profile_clip(const character_profile_t *profile, character_role_t role)
{
    uint32_t step;

    if ((unsigned)role >= (unsigned)CHARACTER_ROLE_COUNT) {
        return CHARACTER_PROFILE_NO_CLIP;
    }
    if (profile == NULL) {
        return character_profile_convention(role);
    }
    for (step = 0; step < CHAIN_MAX; ++step) {
        character_role_t at = CHAIN[role][step];

        if (at == CHARACTER_ROLE_COUNT) {
            break;
        }
        if (profile->clip[at] >= 0) {
            return profile->clip[at];
        }
    }
    return CHARACTER_PROFILE_NO_CLIP;
}

/* ============================================================================================ */

static void lower_copy(char *out, size_t out_size, const char *text, size_t length)
{
    size_t i;

    for (i = 0; i + 1u < out_size && i < length && text[i] != '\0'; ++i) {
        char c = text[i];

        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    out[i] = '\0';
}

/* Case insensitive, and it accepts the name with or without the suffix, because the roster carries
 * "baron.baf" while a section is named for the stem. */
static bool same_asset(const char *a, const char *b)
{
    size_t i;

    for (i = 0; i < CHARACTER_ASSET_MAX; ++i) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') { ca = (char)(ca + ('a' - 'A')); }
        if (cb >= 'A' && cb <= 'Z') { cb = (char)(cb + ('a' - 'A')); }
        if (ca == '.' && cb == '\0') { return true; }
        if (cb == '.' && ca == '\0') { return true; }
        if (ca != cb) { return false; }
        if (ca == '\0') { return true; }
    }
    return true;
}

const character_profile_t *character_profile_find(const char *asset)
{
    uint32_t i;

    if (asset == NULL) {
        return NULL;
    }
    for (i = 0; i < profiles.count; ++i) {
        if (same_asset(profiles.entry[i].asset, asset)) {
            return &profiles.entry[i];
        }
    }
    return NULL;
}

uint32_t character_profile_count(void)
{
    return profiles.count;
}

const character_profile_t *character_profile_at(uint32_t index)
{
    if (index >= profiles.count) {
        return NULL;
    }
    return &profiles.entry[index];
}

uint32_t character_profile_action_count(const character_profile_t *profile)
{
    if (profile == NULL) {
        return 0;
    }
    return profile->action_count;
}

const character_action_t *character_profile_action(const character_profile_t *profile,
                                                   uint32_t index)
{
    uint32_t at;

    if (profile == NULL || index >= profile->action_count) {
        return NULL;
    }
    /* The span was written by the parser and the pool never shrinks, so this cannot be out of
     * range. It is checked anyway because the profile is a struct a hand edited file fills in, and
     * a row whose count outran its span would otherwise read the next character's actions. */
    at = (uint32_t)profile->action_first + index;
    if (at >= profiles.action_count) {
        return NULL;
    }
    return &profiles.action[at];
}

/* ============================================================================================ */

static char *trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    *end = '\0';
    return text;
}

static void begin_entry(const char *section)
{
    character_profile_t *e;
    uint32_t i;

    if (profiles.count >= PROFILE_MAX) {
        return;
    }
    e = &profiles.entry[profiles.count];
    memset(e, 0, sizeof *e);
    for (i = 0; i < (uint32_t)CHARACTER_ROLE_COUNT; ++i) {
        e->clip[i] = CHARACTER_PROFILE_NO_CLIP;
    }
    e->shot_kind = CHARACTER_PROFILE_NO_CLIP;
    lower_copy(e->asset, sizeof e->asset, section, strlen(section));
    ++profiles.count;
}

/* One Action line: an ordinal, the clip's authored name, and the model's playback mode word, in
 * that order and separated by spaces. Nothing is derived from the name here; it is carried through
 * exactly as the artist wrote it, which is the whole of the naming rule.
 *
 * A line that does not parse is dropped rather than half stored, because half of an action is a
 * key that plays a clip nobody named. */
static void append_action(character_profile_t *entry, const char *value)
{
    character_action_t *action;
    const char         *at = value;
    char               *stop = NULL;
    long                clip;
    size_t              length;

    clip = strtol(at, &stop, 10);
    if (stop == at || clip < 0 || clip > 0xFFFF) {
        return;
    }
    at = stop;
    while (*at == ' ' || *at == '\t') {
        ++at;
    }
    if (*at == '\0') {
        return;
    }
    if (profiles.action_count >= ACTION_POOL_MAX) {
        return;
    }
    if (entry->action_count == 0u) {
        entry->action_first = (uint16_t)profiles.action_count;
    } else if ((uint32_t)entry->action_first + entry->action_count != profiles.action_count) {
        /* The span has to stay contiguous, and it only can while every Action line of a section
         * follows the ones before it. A file that scattered them across two sections would silently
         * hand one character the other's actions, so the second run is refused instead. */
        return;
    }
    action = &profiles.action[profiles.action_count];
    action->clip = (int32_t)clip;
    for (length = 0; length + 1u < CHARACTER_ACTION_NAME_MAX &&
                     at[length] != '\0' && at[length] != ' ' && at[length] != '\t'; ++length) {
        action->name[length] = at[length];
    }
    action->name[length] = '\0';
    at += length;
    while (*at == ' ' || *at == '\t') {
        ++at;
    }
    action->mode = (uint16_t)strtoul(at, NULL, 10);
    ++profiles.action_count;
    ++entry->action_count;
}

/* One DeathClips line: ordinals separated by spaces, ascending, and nothing else on the line
 * before the comment the caller has already cut off.
 *
 * A value that does not parse ends the line rather than being skipped over. Skipping would let a
 * typo in the middle of a row quietly halve the set, and a half set is worse than none: the runtime
 * would decide a real death clip is not one and play over the top of it. */
static void append_death_clips(character_profile_t *entry, const char *value)
{
    const char *at = value;
    char       *stop = NULL;
    long        clip;

    entry->death_clip_count = 0u;
    while (entry->death_clip_count < CHARACTER_DEATH_CLIP_MAX) {
        while (*at == ' ' || *at == '\t') {
            ++at;
        }
        if (*at == '\0') {
            return;
        }
        clip = strtol(at, &stop, 10);
        if (stop == at || clip < 0 || clip > 0x7FFF) {
            return;
        }
        entry->death_clip[entry->death_clip_count] = (int16_t)clip;
        ++entry->death_clip_count;
        at = stop;
    }
}

static void apply_key(const char *key, const char *value)
{
    character_profile_t *e;
    uint32_t i;

    if (profiles.count == 0) {
        return;
    }
    e = &profiles.entry[profiles.count - 1u];

    if (strcmp(key, "Asset") == 0) {
        lower_copy(e->asset, sizeof e->asset, value, strlen(value));
        return;
    }
    if (strcmp(key, "Muzzle") == 0) {
        lower_copy(e->muzzle, sizeof e->muzzle, value, strlen(value));
        return;
    }
    if (strcmp(key, "MeleeNode") == 0) {
        lower_copy(e->melee_node, sizeof e->melee_node, value, strlen(value));
        return;
    }
    if (strcmp(key, "Clips") == 0) {
        e->clip_count = (int32_t)strtol(value, NULL, 10);
        return;
    }
    if (strcmp(key, "ShotKind") == 0) {
        e->shot_kind = (int32_t)strtol(value, NULL, 10);
        return;
    }
    if (strcmp(key, "Action") == 0) {
        append_action(e, value);
        return;
    }
    if (strcmp(key, "DeathClips") == 0) {
        append_death_clips(e, value);
        return;
    }
    /* Source is the remaining key the generator writes, and it is provenance for whoever reads the
     * file rather than data for the runtime: it names which tier answered, the same thing the
     * trailing comment on every ordinal says. It is not stored, and that is the only key of which
     * that is true. Anything else falling through this loop is a key the two sides disagree about,
     * which is how a key one side writes and the other never reads goes unnoticed. */
    for (i = 0; i < (uint32_t)CHARACTER_ROLE_COUNT; ++i) {
        if (strcmp(key, ROLE_KEY[i]) == 0) {
            e->clip[i] = (int32_t)strtol(value, NULL, 10);
            return;
        }
    }
}

bool character_profile_load(void)
{
    char  path[512];
    char  line[LINE_MAX];
    FILE *file;

    if (profiles.loaded) {
        return profiles.count > 0u;
    }
    profiles.loaded = true;

    /* The data file sits beside the configuration file, which is beside the executable, so the one
     * module that already answers "where is the game" answers this too. Deriving the directory
     * from it beats resolving the same path a second way and having the two disagree. */
    {
        const char *config = ini_path();
        size_t      cut;

        if (config == NULL) {
            return false;
        }
        cut = strlen(config);
        while (cut > 0u && config[cut - 1u] != '\\' && config[cut - 1u] != '/') {
            --cut;
        }
        if (cut + sizeof "characters.ini" > sizeof path) {
            return false;
        }
        memcpy(path, config, cut);
        memcpy(path + cut, "characters.ini", sizeof "characters.ini");
    }

    file = fopen(path, "r");
    if (file == NULL) {
        log_warning("characters.ini not found beside the game, so every actor falls back to the "
                    "convention: ordinal 0 stands, 1 walks, 2 runs. That is right for about half "
                    "of them.");
        return false;
    }

    while (fgets(line, (int)sizeof line, file) != NULL) {
        char *text = trim(line);
        char *split;

        if (text[0] == ';' || text[0] == '#' || text[0] == '\0') {
            continue;
        }
        if (text[0] == '[') {
            char *close = strchr(text, ']');

            if (close != NULL) {
                *close = '\0';
                /* the section is named char.<stem>, and the stem is what we key on */
                begin_entry(strncmp(text + 1, "char.", 5) == 0 ? text + 6 : text + 1);
            }
            continue;
        }
        split = strchr(text, '=');
        if (split == NULL) {
            continue;
        }
        *split = '\0';
        {
            char *value = trim(split + 1);
            char *comment = strchr(value, ';');

            if (comment != NULL) {
                *comment = '\0';
                value = trim(value);
            }
            apply_key(trim(text), value);
        }
    }
    fclose(file);

    log_info("%u character profiles loaded from characters.ini, carrying %u actions between them, "
             "which are the clips no role took",
             profiles.count, profiles.action_count);
    return profiles.count > 0u;
}
