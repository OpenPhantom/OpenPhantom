/* character_profile.h: what an animation ordinal means on one particular actor.
 *
 * The engine's player code was written against four rigs and asks for clips by ordinal, so the
 * same number is a run on Obi-Wan and a death on a battle droid. This module is the one place that
 * knows the difference, and it is deliberately the only kind of knowledge it holds: no engine
 * calls, no memory writes, no state machine. It answers questions and nothing else, which is what
 * makes the interesting half of it testable without the game.
 *
 * It lives in common because more than one DLL reads the roster: the multiplayer and the developer
 * overlay both load it. What an ordinal means on an actor is a property of the data file, not of
 * any one feature, and a second parser of the same file would be a second place for the two to
 * disagree.
 *
 * Where the answers come from, in the order the generator trusted them, recorded here because a
 * reader deciding whether to believe a row needs to know:
 *
 *   the level scripts. Ten AI opcodes carry a clip ordinal and each names its role. Move To is
 *     locomotion, ShootEmUp is the fire clip and carries the projectile kind beside it, Slash and
 *     Chop are melee, Block is the base of a ten ordinal fan, Sidestep and Jump are themselves.
 *     This is authored truth: the level says what that actor plays in that state.
 *   the usage tag in the model, sparse but structural, and the only reliable source for death,
 *     knockback and get up. All five hero rigs carry none, which is why an earlier pass that looked
 *     only at heroes called the field unused. The word sits at +0x18 of the clip descriptor in
 *     the model. Over the 2116 shipped clips it reads 1 on 52 (a death), 2 on 1 (a hit reaction),
 *     3 on 68 (a knockback), 5 on 18 (getting up: every one of the eighteen is named getup, gtup
 *     or falup), 6 on 1 (a severed limb), 7 on 2 (a jump), 9 on 4 (aiming or recovering), and 0
 *     everywhere else.
 *   the clip name, which agrees with the other two in 90 per cent of the cases where both speak,
 *     and which the generator also lets veto a locomotion slot: a walk that plays a block is worse
 *     than no walk at all.
 *
 * A role no source named is left empty rather than guessed, and the fallback chain below answers
 * for it. A guessed ordinal would be believed later.
 *
 * What is left over is also answered here. The fourteen roles cover the ordinals a feature driving
 * an actor asks for; the clips they do not take are the ones nothing in the game will ever play
 * while a player is wearing that rig, and they are 691 of the 1446 the roster carries. The
 * generator writes them as Action lines with the name the artist gave each one, and this module
 * stores them in the order they were written, which is ordinal order. It knows nothing about what
 * any of them depicts, and that is the point: the list is the actor's own clip table minus its
 * roles, and no rule beyond that chooses it.
 */
#ifndef COMMON_CHARACTER_PROFILE_H
#define COMMON_CHARACTER_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum character_role {
    CHARACTER_ROLE_IDLE = 0,
    CHARACTER_ROLE_WALK,
    CHARACTER_ROLE_RUN,
    CHARACTER_ROLE_BACK,
    CHARACTER_ROLE_JUMP,
    CHARACTER_ROLE_ATTACK,
    /* The clip a shot leaves on, which is not always the attack. An actor that both swings and
     * throws has two, and the generator mines them from two different opcodes: 0x307 ShootEmUp
     * carries the fire clip beside the projectile kind, while the attack slot also accepts a melee
     * opcode. The battle droid captain's attack is a swing and his shot is a throw, so a weapon row
     * given the attack ordinal would have him swinging while a bolt leaves his forearm. */
    CHARACTER_ROLE_FIRE,
    CHARACTER_ROLE_MELEE,
    CHARACTER_ROLE_BLOCK,
    CHARACTER_ROLE_DEATH,
    CHARACTER_ROLE_HIT,
    CHARACTER_ROLE_GETUP,
    CHARACTER_ROLE_STRAFE,
    CHARACTER_ROLE_TALK,
    CHARACTER_ROLE_COUNT
} character_role_t;

#define CHARACTER_PROFILE_NO_CLIP   (-1)
#define CHARACTER_ASSET_MAX         32u

/* The clips no role took, which is what the fourteen slots above leave behind. The generator
 * writes one Action line per such clip, in ordinal order, and the runtime
 * stores them exactly as written: no vocabulary, no per character table, nothing here that knows a
 * destroyer folds up. The name is the one the artist gave the clip and the mode is the model's own
 * playback word.
 *
 * Twelve characters is enough for every name in the shipped data, where the longest is eight. */
#define CHARACTER_ACTION_NAME_MAX   12u

typedef struct character_action {
    int32_t  clip;                        /* the ordinal into this actor's own clip table */
    uint16_t mode;                        /* the descriptor's +0x14 playback mode word */
    char     name[CHARACTER_ACTION_NAME_MAX];
} character_action_t;

/* The three bits that say the clip does not end by itself, which is the difference between a
 * gesture and a pose. Named here because the log words it. */
#define CHARACTER_ACTION_HOLDS      0x03u   /* stops on the last frame, with or without the latch */
#define CHARACTER_ACTION_LOOPS      0x04u   /* wraps to the loop start and never stops */

/* Every death this actor carries, which is a different question from which one to play.
 *
 * The Death role answers "what do I put on a body that is playing nothing". This set answers "is
 * the body already dying", and the two part company the moment an actor has more than one: the
 * tusken has two die clips and the ishi thug three, so a runtime that only knew the pick would
 * impose its own clip on top of a perfectly good death whenever the script chose another one.
 *
 * Four is the widest row the shipped file writes, on three of its sections. The headroom is for
 * hand written ones. */
#define CHARACTER_DEATH_CLIP_MAX    6u

typedef struct character_profile {
    char     asset[CHARACTER_ASSET_MAX];   /* the .baf file name, lower case */
    int32_t  clip[CHARACTER_ROLE_COUNT];   /* an ordinal, or CHARACTER_PROFILE_NO_CLIP */
    int16_t  death_clip[CHARACTER_DEATH_CLIP_MAX];  /* every death ordinal, ascending */
    uint8_t  death_clip_count;
    int32_t  clip_count;                   /* how many the actor carries, 0 when unknown */
    int32_t  shot_kind;                    /* the projectile the levels saw it fire, -1 for none */
    char     muzzle[CHARACTER_ASSET_MAX];  /* the node the shot leaves from, empty when none */
    char     melee_node[CHARACTER_ASSET_MAX]; /* the node a blow lands from, empty when none */
    /* A span of the shared action pool rather than an array per profile. 691 actions are spread
     * over 124 of the 164 characters and one of them carries 47, so an array wide enough for the
     * worst case would be 47 entries on every row and more than nine tenths of it empty. */
    uint16_t action_first;
    uint16_t action_count;
} character_profile_t;

/* Reads <game>\characters.ini once. False when the file is absent, and then every actor falls back
 * to the convention alone, which is still playable for most of them. */
bool character_profile_load(void);

uint32_t character_profile_count(void);

/* The profile for one asset name, case insensitive, with or without the .baf suffix. NULL when the
 * file names no such actor. */
const character_profile_t *character_profile_find(const char *asset);

/* By position, in the order the file lists them, which is alphabetical because the generator sorts.
 * NULL past the end. This is what lets the roster be the data file rather than a list in code. */
const character_profile_t *character_profile_at(uint32_t index);

/* The fallback chain, and it is the whole reason an actor with three clips is playable at all.
 *
 * Each answer is justified by what the engine already does when a clip is simply refused, which
 * this project's guards make a silent no operation:
 *
 *   RUN     falls back to WALK, then to IDLE. An actor that has one gait uses it for both.
 *   WALK    falls back to RUN, then to IDLE, for the actors that only ever run.
 *   BACK    falls back to WALK, then RUN, then IDLE. Walking backwards played forwards reads as
 *           odd, not as broken.
 *   JUMP    falls back to nothing: the refusal holds the previous pose, and a body that keeps its
 *           run pose through a jump looks far better than one that snaps to a stand.
 *   FIRE    falls back to ATTACK and then to MELEE. Only 40 of the 164 shipped rows name a fire
 *           clip, so most weapon rows are answered by the attack.
 *   MELEE   falls back to ATTACK, since most actors have exactly one.
 *   ATTACK  falls back to FIRE and then to MELEE.
 *   HIT     falls back to nothing. A flinch that plays a death is worse than no flinch.
 *   the rest fall back to nothing for the same reason.
 *
 * Answers CHARACTER_PROFILE_NO_CLIP when the chain runs out, which the caller must pass on to the
 * engine unchanged: an ordinal below zero is refused by the guards and nothing happens. */
int32_t character_profile_clip(const character_profile_t *profile, character_role_t role);

/* The same chain over a profile that does not exist, i.e. the convention alone: ordinal 0 is the
 * rest pose, 1 is the first gait, 2 the second. Measured over the shipped file, idle at 0 holds
 * for 152 of the 164 rows and the 1 and 2 hold for about half, which is why it is a last resort
 * and not the rule. */
int32_t character_profile_convention(character_role_t role);

/* How many clips this actor carries that no role took. Zero for NULL, and zero for the 40
 * characters whose whole clip table is spoken for. */
uint32_t character_profile_action_count(const character_profile_t *profile);

/* One of them, in the order the data file lists them, which is ordinal order. NULL past the end. */
const character_action_t *character_profile_action(const character_profile_t *profile,
                                                   uint32_t index);

/* Is this ordinal one of the actor's deaths? False for NULL, and false for an actor the file does
 * not list, which is the answer that keeps a caller from claiming a body it knows nothing about. */
bool character_profile_is_death_clip(const character_profile_t *profile, int32_t clip);

/* The death to impose on a body that is playing none, or CHARACTER_PROFILE_NO_CLIP.
 *
 * The Death role first, because that is the arbitrated pick and it weighed the level scripts, the
 * model's usage tag and the clip name against each other. The lowest ordinal of the set only when
 * no role was filled, which happens when the arbitration refused every candidate but the clip
 * table still plainly carries a death. */
int32_t character_profile_death_clip(const character_profile_t *profile);

/* For the log, and for whatever shows a role by name. Never NULL. */
const char *character_role_name(character_role_t role);

#endif /* COMMON_CHARACTER_PROFILE_H */
