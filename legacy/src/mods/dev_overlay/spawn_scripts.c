/* spawn_scripts.c: see spawn_scripts.h.
 *
 * The engine's script record, as the level loader leaves it in memory and as the VM reads it
 * (the loader bapworld_loadAi, and the label builder aiscript_buildStateLabels at 0x00429CDF):
 *
 *   +0x00  entryCount     the VM checks every jump against it
 *   +0x04  pStateLabel    u16 per mode, the entry index of the mode's own entry; the loader
 *                         builds it from the opcode 1 entries in order, and this does the same
 *   +0x08  size           the record's length, redundant
 *   +0x0C  pOperandPool   the loader derives it: the record plus 0x20 plus eight per entry
 *   +0x10  sixteen bytes, zero in every shipped record
 *   +0x20  entries        eight bytes each: i16 opcode word, i16 branch, u32 operand
 *          pool           u32 words, indexed by the operand of a pool entry
 *
 * A pool entry's operand is a dword index into the pool and its arguments follow from there,
 * so a clip number sits at a known pool index; an opcode with one argument carries it in the
 * entry's own operand word instead (bit 0x4000 of the opcode word). Both are what the generated
 * header lists as slots.
 */
#include "spawn_scripts.h"

#include "actor_catalog.h"
#include "spawn_script_data.h"

#include "common/text.h"

#include <string.h>

#define RECORD_HEAD   0x20u
#define ENTRY_BYTES   8u
#define CLIP_USAGE_DEATH 1u   /* the clip descriptor's usage mark for a death */

/* The chase: the duel Maul's run, or Follow's walk for a model with no run clip, which would
 * slide at his speed. The walk: Follow's, or none for a model with no walk clip at all, a
 * turret of a droid, which then turns in place and never slides after the player, and none for
 * the few the level authors keep in place by script, HOLDERS below. A flyer moves on its hover
 * clip, clip 0, as the gunboat's own script moves it (BIGCITY "gunb": move_to with clip 0 at
 * speed 2), so it needs no walk clip to follow. A shooter that does not move fires from twice
 * as far, since it cannot close. The reach: how close an attacker comes before it swings, by
 * what it swings. A sabre reaches; a staff or a club a little less; hands and teeth have to
 * touch. */
#define CHASE_SPEED_RUN   5.05f
#define CHASE_SPEED_WALK  3.0f
#define WALK_SPEED        3.0f
#define WALK_SPEED_NONE   0.0f
#define FIRE_RANGE        6.0f
#define FIRE_RANGE_FIXED  12.0f
#define REACH_SABRE       0.7f
#define REACH_WEAPON      0.65f
#define REACH_HANDS       0.6f
/* A helper's reach is the model's plus a little: two NPC bodies hold each other a little
 * further apart than a body is held from the player, and the helper's chase pushes until they
 * touch (played 2026-09-16: on the player's reach a Maul never got inside his own of a Tusken;
 * on a unit more he stood two units off and swung at air, since a swing mode stands still). */
#define ALLY_REACH_EXTRA  0.3f

/* The shooter's attack kind: 8 is "Weapon 1", the record's first weapon through the fire op's
 * remap; 2 is the twin muzzle weapon, which op_shoot tests for by the kind it is called with,
 * before the remap, and fires from the two nodes the model marks by usage. The level scripts
 * choose it, not the records: every destroyer, tripod and hovering droid placement in the
 * retail levels carries empty weapon slots and a script that names kind 2 outright (read out
 * of all 22 levels, 2026-09-16), so a copy names it by model (played 2026-09-16: a droideka
 * firing single bolts through Weapon 1). */
#define ATTACK_KIND_WEAPON_1  8u
#define SHOT_KIND_TWIN        2u

/* Move To's destination: 3 is the player's own position (move_resolveDest, kind 3 through
 * resolve_target 0), 0 the placement's authored position, which for a copy is the spawner's
 * own record. A flyer's height is tracked toward its destination's z (move_trackZ), so a
 * flyer sent at the player comes down to their feet, as the level's gunboat does when its
 * script sends it at them (played 2026-09-16); a copy flies to its record instead, which the
 * spawner keeps over the player's head every frame. */
#define MOVE_TARGET_PLAYER    3u
#define MOVE_TARGET_RECORD    0u
static const char *const TWIN_MUZZLES[] = { "destroyr.baf", "tripod.baf", "hovdroid.baf", NULL };

static const char *const BEHAVIOUR_NAMES[SPAWN_BEHAVIOUR_COUNT] = { "Stand", "Follow",
                                                                     "Attack", "Help" };

const char *spawn_behaviour_name(spawn_behaviour_t which)
{
    return ((uint32_t)which < SPAWN_BEHAVIOUR_COUNT) ? BEHAVIOUR_NAMES[which] : "";
}

/* The first clip whose name carries one of the candidates, in the order to prefer them, none of
 * the words to avoid, and not `skip`; -1 for none. */
static int32_t clip_named(const actor_clip_t *clips, uint32_t count, const char *const *want,
                          const char *const *avoid, int32_t skip)
{
    uint32_t w;

    for (w = 0; want[w] != NULL; ++w) {
        uint32_t i;

        for (i = 0; i < count; ++i) {
            uint32_t a;
            bool     avoided = ((int32_t)i == skip);

            if (strstr(clips[i].name, want[w]) == NULL) {
                continue;
            }
            for (a = 0; avoid != NULL && avoid[a] != NULL; ++a) {
                if (strstr(clips[i].name, avoid[a]) != NULL) {
                    avoided = true;
                }
            }
            if (!avoided) {
                return (int32_t)i;
            }
        }
    }
    return -1;
}

static int32_t clip_with_usage(const actor_clip_t *clips, uint32_t count, uint32_t usage)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        if (clips[i].usage == usage) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* The clip names, as the retail files spell them: sthswng1, mrgswgr, sthtwrl1, tuschop1,
 * tusthrst, th3slash for a swing; jwgfire1, brnfire, crgfire1 for a shot; r2broll and dstroll1
 * for the droids that roll, whose walk it is (played 2026-09-16, six astromechs on Help that
 * sat where they were). Left out on purpose: "hit" (sithhit1 is the flinch), "jab" (jabba) and
 * "atk" (watkypad, Watto at a keypad). */
static const char *const WALK[]  = { "walk1", "walkf", "wlkf", "walk", "wlk", "roll", "run",
                                     NULL };
static const char *const RUN[]   = { "run", "walk1", "walkf", "wlkf", "walk", "wlk", "roll",
                                     NULL };
static const char *const BACK[]  = { "wlkb", "walkb", "back", NULL };
static const char *const SWING[] = { "swng", "swg", "twrl", "chop", "thrst", "slash", "swing",
                                     "punch", "attack", NULL };
static const char *const FIRE[]  = { "fire", "shoot", "shot", "blast", NULL };
static const char *const DEATH[] = { "death", "die", NULL };

/* Models with a walk clip the game never lets walk after the player: the kneeling bazooka man
 * kneels, fires and gets up by his level script, and the droid fighter deploys where it lands
 * and holds; a copy of either walking after the player looked wrong (played 2026-09-16).
 * Everyone else with a walk clip walks. */
static const char *const HOLDERS[] = { "baronbaz.baf", "drdfitr.baf", NULL };

/* The archive's flyers and the move mode the retail levels place each with as a creature: 2
 * hovers at its height and tracks it (the gunboat, the probe droid), 5 pitches toward where it
 * is going (the STAP, the birds and the fish; the engine's move_trackZ, moveMode >= 4). Read out
 * of all 22 levels, class 2 and 3 placements only: the droid fighter is placed flying as a
 * class 0 ship and walking as a fighter, and a copy is the fighter (played 2026-09-16, one
 * that spawned in the air). */
static const struct {
    const char *file;
    int32_t     mode;
} FLYERS[] = {
    { "hovdroid.baf", 2 }, { "sithdrd.baf", 2 }, { "stap.baf", 5 }, { "pekopeko.baf", 5 },
    { "pekosmal.baf", 4 }, { "fish.baf", 5 }, { "grouper.baf", 5 }, { "groupblu.baf", 5 },
    { "angelfsh.baf", 5 }
};

/* The archive's shooters and the shot kind the retail levels arm each with in its first
 * weapon slot, the most common where they differ; read out of the same 22 levels, from the
 * placements whose scripts fire Weapon 1. A model not here gets the blaster. */
static const struct {
    const char *file;
    uint16_t    kind;
} MODEL_WEAPONS[] = {
    { "drdfitr.baf", 13 }, { "popgun.baf", 13 }, { "greedgun.baf", 20 }, { "gunggrd.baf", 12 },
    { "maintdrd.baf", 15 }, { "r2d2.baf", 15 }, { "tuskgun.baf", 17 }, { "tathum4g.baf", 7 },
    { "panaknpc.baf", 19 }, { "queennpc.baf", 14 }, { "baron.baf", 10 }, { "baronbaz.baf", 8 },
    { "tank.baf", 9 }, { "stap.baf", 6 }
};

uint16_t spawn_script_model_weapon(const char *actor_file)
{
    uint32_t i;

    for (i = 0; actor_file != NULL && i < sizeof MODEL_WEAPONS / sizeof MODEL_WEAPONS[0]; ++i) {
        if (_stricmp(actor_file, MODEL_WEAPONS[i].file) == 0) {
            return MODEL_WEAPONS[i].kind;
        }
    }
    return 0;
}

int32_t spawn_script_flyer_mode(const char *actor_file)
{
    uint32_t i;

    for (i = 0; actor_file != NULL && i < sizeof FLYERS / sizeof FLYERS[0]; ++i) {
        if (_stricmp(actor_file, FLYERS[i].file) == 0) {
            return FLYERS[i].mode;
        }
    }
    return 0;
}

static bool named_in(const char *file, const char *const *list)
{
    uint32_t i;

    for (i = 0; file != NULL && list[i] != NULL; ++i) {
        if (_stricmp(file, list[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* The model's clip for a role. The stand is clip 0 in every actor file this project has
 * opened. A file with no clip for a role gets the stand, so a follower that cannot walk stands
 * where it is and a fighter with no swing swings its stand, which lands all the same: the
 * contact is the weapon node's, not the clip's. */
static int32_t clip_for(spawn_slot_role_t role, const actor_clip_t *clips, uint32_t count)
{
    int32_t found = -1;

    switch (role) {
    case SPAWN_SLOT_WALK:
        found = clip_named(clips, count, WALK, BACK, -1);
        break;
    case SPAWN_SLOT_RUN:
        found = clip_named(clips, count, RUN, BACK, -1);
        break;
    case SPAWN_SLOT_ATTACK:
        found = clip_named(clips, count, SWING, NULL, -1);
        if (found < 0) {
            found = clip_named(clips, count, FIRE, NULL, -1);
        }
        break;
    case SPAWN_SLOT_ATTACK2:
        /* A second swing when the file has one, else the first again. */
        found = clip_named(clips, count, SWING, NULL, -1);
        if (found >= 0) {
            int32_t second = clip_named(clips, count, SWING, NULL, found);

            found = (second >= 0) ? second : found;
        }
        break;
    case SPAWN_SLOT_FIRE:
        found = clip_named(clips, count, FIRE, NULL, -1);
        break;
    case SPAWN_SLOT_DEATH:
        found = clip_with_usage(clips, count, CLIP_USAGE_DEATH);
        if (found < 0) {
            found = clip_named(clips, count, DEATH, NULL, -1);
        }
        break;
    case SPAWN_SLOT_STAND:
    default:
        break;
    }
    return (found >= 0) ? found : 0;
}

/* The model's number for a float role: the speeds by whether it has a run or a walk clip and
 * is let walk, or flies, the fire range by whether it moves at all, the reach by the weapon
 * node the archive shows on it. */
static float number_for(spawn_slot_role_t role, const actor_clip_t *clips, uint32_t count,
                        const actor_catalog_entry_t *entry, const char *actor_file, bool flies)
{
    static const char *const RUN_ONLY[] = { "run", NULL };
    bool walks = (flies || clip_named(clips, count, WALK, BACK, -1) >= 0) &&
                 !named_in(actor_file, HOLDERS);

    switch (role) {
    case SPAWN_SLOT_CHASE_SPEED:
        return (clip_named(clips, count, RUN_ONLY, BACK, -1) >= 0) ? CHASE_SPEED_RUN
                                                                   : CHASE_SPEED_WALK;
    case SPAWN_SLOT_WALK_SPEED:
        return walks ? WALK_SPEED : WALK_SPEED_NONE;
    case SPAWN_SLOT_FIRE_RANGE:
        return walks ? FIRE_RANGE : FIRE_RANGE_FIXED;
    case SPAWN_SLOT_ALLY_REACH:
        return number_for(SPAWN_SLOT_REACH, clips, count, entry, actor_file, flies) +
               ALLY_REACH_EXTRA;
    default:
        break;
    }
    if (entry != NULL && entry->weapon == ACTOR_WEAPON_SABRE) {
        return REACH_SABRE;
    }
    return (entry != NULL && entry->weapon == ACTOR_WEAPON_MOUNT) ? REACH_WEAPON : REACH_HANDS;
}

static const char *role_word(spawn_slot_role_t role)
{
    switch (role) {
    case SPAWN_SLOT_STAND:       return "stand";
    case SPAWN_SLOT_WALK:        return "walk";
    case SPAWN_SLOT_RUN:         return "run";
    case SPAWN_SLOT_ATTACK:      return "swing";
    case SPAWN_SLOT_ATTACK2:     return "second swing";
    case SPAWN_SLOT_FIRE:        return "fire";
    case SPAWN_SLOT_DEATH:       return "death";
    case SPAWN_SLOT_CHASE_SPEED: return "speed";
    case SPAWN_SLOT_WALK_SPEED:  return "walk speed";
    case SPAWN_SLOT_REACH:       return "reach";
    case SPAWN_SLOT_ALLY_REACH:  return "reach";
    case SPAWN_SLOT_FIRE_RANGE:  return "fire range";
    case SPAWN_SLOT_SHOT_KIND:   return "attack kind";
    case SPAWN_SLOT_MOVE_TARGET: return "move to";
    default:                     return "?";
    }
}

/* Which compiled record a behaviour is for this model: Attack is the destroyer droid's own
 * record for that one model, the melee record for a model with a swing clip, the shoot record
 * for one with a fire clip or twin muzzles and no swing, and the melee record again for one
 * with none of those, which shoves. The tripod gun stands still whatever the row says, never
 * turning: it is a thing the player mounts and fires, and one that turned or fired on its own
 * got in the way of that (played 2026-09-16). */
static uint32_t record_for(spawn_behaviour_t which, const char *actor_file,
                           const actor_clip_t *clips, uint32_t count)
{
    if (actor_file != NULL && _stricmp(actor_file, "tripod.baf") == 0) {
        return SPAWN_RECORD_STILL;
    }
    switch (which) {
    case SPAWN_BEHAVIOUR_STAND:  return SPAWN_RECORD_STAND;
    case SPAWN_BEHAVIOUR_FOLLOW: return SPAWN_RECORD_FOLLOW;
    case SPAWN_BEHAVIOUR_ATTACK:
        if (actor_file != NULL && _stricmp(actor_file, "destroyr.baf") == 0) {
            return SPAWN_RECORD_DROIDEKA;
        }
        /* A twin muzzle gun shoots whatever its clips are called. */
        if (clip_named(clips, count, SWING, NULL, -1) < 0 &&
            (clip_named(clips, count, FIRE, NULL, -1) >= 0 ||
             named_in(actor_file, TWIN_MUZZLES))) {
            return SPAWN_RECORD_SHOOT;
        }
        return SPAWN_RECORD_ATTACK;
    case SPAWN_BEHAVIOUR_HELP:
        if (clip_named(clips, count, SWING, NULL, -1) < 0 &&
            (clip_named(clips, count, FIRE, NULL, -1) >= 0 ||
             named_in(actor_file, TWIN_MUZZLES))) {
            return SPAWN_RECORD_ALLYSHOT;
        }
        return SPAWN_RECORD_ALLY;
    default:
        return SPAWN_SCRIPT_COUNT;
    }
}

void *spawn_script_prepare(spawn_behaviour_t which, const char *actor_file, bool flies,
                           uint8_t *buffer, size_t size, char *note, size_t note_size,
                           bool *shoots)
{
    static actor_clip_t          clips[160];
    const spawn_script_record_t *record;
    const actor_catalog_entry_t *entry;
    uint32_t                     clip_count = 0;
    uint32_t                     which_record;
    uint32_t                     entries_bytes;
    uint32_t                     pool_bytes;
    uint32_t                     labels_bytes;
    uint32_t                     total;
    uint32_t                     i;
    uint8_t                     *pool;
    uint16_t                    *labels;
    size_t                       used = 0;

    if (buffer == NULL) {
        return NULL;
    }
    if (!actor_catalog_clips(actor_file, clips, sizeof clips / sizeof clips[0], &clip_count) ||
        clip_count == 0) {
        return NULL;
    }
    which_record = record_for(which, actor_file, clips, clip_count);
    if (which_record >= SPAWN_SCRIPT_COUNT) {
        return NULL;
    }
    if (shoots != NULL) {
        *shoots = (which_record == SPAWN_RECORD_SHOOT || which_record == SPAWN_RECORD_ALLYSHOT);
    }
    entry         = actor_catalog_find(actor_file);
    record        = &SPAWN_SCRIPTS[which_record];
    entries_bytes = record->entry_count * ENTRY_BYTES;
    pool_bytes    = record->pool_count * 4u;
    labels_bytes  = record->label_count * 2u;
    total         = RECORD_HEAD + entries_bytes + pool_bytes + labels_bytes;
    if (total > size) {
        return NULL;
    }

    memset(buffer, 0, total);
    pool   = buffer + RECORD_HEAD + entries_bytes;
    labels = (uint16_t *)(buffer + RECORD_HEAD + entries_bytes + pool_bytes);
    memcpy(buffer + 0x00, &record->entry_count, 4u);
    memcpy(buffer + 0x04, &labels, 4u);
    memcpy(buffer + 0x08, &total, 4u);
    memcpy(buffer + 0x0C, &pool, 4u);
    for (i = 0; i < record->entry_count; ++i) {
        uint8_t *at = buffer + RECORD_HEAD + i * ENTRY_BYTES;

        memcpy(at + 0, &record->entries[i].word0, 2u);
        memcpy(at + 2, &record->entries[i].word1, 2u);
        memcpy(at + 4, &record->entries[i].operand, 4u);
    }
    memcpy(pool, record->pool, pool_bytes);
    memcpy(labels, record->labels, labels_bytes);

    if (note != NULL && note_size != 0) {
        used = text_format(note, note_size, "%s:", record->name);
    }
    for (i = 0; i < record->slot_count; ++i) {
        const spawn_slot_t *slot = &record->slots[i];
        uint8_t            *at;
        uint32_t            word;
        float               number = 0.0f;
        bool                is_number;

        if (slot->in_entry) {
            if (slot->index >= record->entry_count) {
                return NULL;
            }
            at = buffer + RECORD_HEAD + slot->index * ENTRY_BYTES + 4u;
        } else {
            if (slot->index >= record->pool_count) {
                return NULL;
            }
            at = pool + 4u * slot->index;
        }
        is_number = (slot->role == SPAWN_SLOT_CHASE_SPEED || slot->role == SPAWN_SLOT_WALK_SPEED ||
                     slot->role == SPAWN_SLOT_REACH || slot->role == SPAWN_SLOT_ALLY_REACH ||
                     slot->role == SPAWN_SLOT_FIRE_RANGE);
        if (is_number) {
            number = number_for(slot->role, clips, clip_count, entry, actor_file, flies);
            memcpy(&word, &number, sizeof word);
        } else if (slot->role == SPAWN_SLOT_SHOT_KIND) {
            word = named_in(actor_file, TWIN_MUZZLES) ? SHOT_KIND_TWIN : ATTACK_KIND_WEAPON_1;
        } else if (slot->role == SPAWN_SLOT_MOVE_TARGET) {
            word = flies ? MOVE_TARGET_RECORD : MOVE_TARGET_PLAYER;
        } else {
            word = (uint32_t)clip_for(slot->role, clips, clip_count);
        }
        memcpy(at, &word, sizeof word);
        if (note != NULL && used < note_size) {
            if (is_number) {
                used += text_format(note + used, note_size - used, " %s %.2f",
                                    role_word(slot->role), (double)number);
            } else if (slot->role == SPAWN_SLOT_SHOT_KIND ||
                       slot->role == SPAWN_SLOT_MOVE_TARGET) {
                used += text_format(note + used, note_size - used, " %s %u",
                                    role_word(slot->role), word);
            } else {
                used += text_format(note + used, note_size - used, " %s %u (%s)",
                                    role_word(slot->role), word,
                                    word < clip_count ? clips[word].name : "?");
            }
        }
    }
    return buffer;
}
