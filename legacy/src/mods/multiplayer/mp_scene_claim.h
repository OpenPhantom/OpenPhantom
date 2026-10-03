/* mp_scene_claim.h: whose a script's doors are on the host.
 *
 * Layer 3. The rules are mp_scene_claim_rule's and are pure; this reads what they are asked about
 * and keeps it from one substep to the next: the actor whose script runs, the marks of what each
 * placement's actor took on this host, what a far player's run was refused before a door, the
 * placements whose doors are refused, and the releases owed to the host.
 *
 * Three modules ask here. The scene watch asks at every door, inside the engine: whose the run is,
 * whether a take goes through, whether a release does. The target resolver asks, for every
 * question a script puts about the player, whether the engine's own answer is kept for the host.
 * The judgement of a spoken line asks whether the script that speaks is a run of the host's.
 *
 * The host's scene tells this which actors are its own, and nothing here asks the host's scene
 * back: the one direction keeps the three files from deciding the same thing twice.
 */
#ifndef MULTIPLAYER_MP_SCENE_CLAIM_H
#define MULTIPLAYER_MP_SCENE_CLAIM_H

#include "mp_scene_claim_rule.h"
#include "mp_target_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * The run.
 * ============================================================================================ */

/* From the scene watch's hull on the script runner: `actor`'s script begins to run. Returns the
 * actor whose run it stands inside, nought for none, which the end hands back. What the run
 * before it was refused is forgotten: a camera alone or bars alone of a far player's run lapse
 * with the run. */
uintptr_t mp_scene_claim_run_begins(uintptr_t actor);
void      mp_scene_claim_run_ends(uintptr_t outer);

/* The actor whose script runs now, nought outside every script. */
uintptr_t mp_scene_claim_running(void);

/* Whose a run of `actor`'s script is, with the rule that decided and what the actor's own last
 * answer was, for the count of why it did not decide. */
typedef struct mp_scene_claim_run {
    mp_scene_run_rule_t rule;
    uint8_t             bank;   /* 0 the host, 1 and up a far player */
    mp_scene_answer_t   own;
} mp_scene_claim_run_t;

void mp_scene_claim_whose(uintptr_t actor, mp_scene_claim_run_t *out);

/* Whether the script that runs now is a run of the host's. True with no script running: a line
 * the dialogue speaks by itself is this machine's own conversation. */
bool mp_scene_claim_run_is_the_hosts(void);

/* What the target resolver is told about `actor` before it weighs a far player against the
 * engine's own answer (mp_target_rule_claim). `evidence` is filled but for whether the engine
 * answered, which only the resolver knows. */
void mp_scene_claim_answer_evidence(uintptr_t actor, mp_target_claim_evidence_t *evidence);

/* ==============================================================================================
 * The doors.
 * ============================================================================================ */

/* What became of a take. */
typedef enum mp_scene_claim_take {
    MP_SCENE_CLAIM_PASSES = 0,   /* a run of the host's: through, and the bit is set */
    MP_SCENE_CLAIM_KEPT,         /* a far player's run: refused, and remembered for this run */
    MP_SCENE_CLAIM_LATCHED       /* the placement's doors are refused: nothing is remembered */
} mp_scene_claim_take_t;

/* The bars told to come or a camera taken by the dolly or the lock opcode, by the run `run` of
 * `actor`: `bit` is MP_SCENE_MARK_BARS or MP_SCENE_MARK_CAMERA and `argument` what the engine was
 * handed. Counted here. */
mp_scene_claim_take_t mp_scene_claim_take(uintptr_t actor, const mp_scene_claim_run_t *run,
                                          uint8_t bit, int32_t argument);

/* The lock's own door asks the two halves apart, because a far player's lock is the door of a
 * scene: whether the placement's doors are refused, which also lets a foreign placement go for a
 * run of the host's and writes one down while a window stands; and that a take went through.
 * `spoken` marks the camera of a spoken line. */
bool mp_scene_claim_latched_door(uintptr_t actor, bool hosts_run);
void mp_scene_claim_took(uintptr_t actor, uint8_t bit, bool spoken);

/* The door of a scene: what this run was refused before it is made up through the engine, in its
 * order, and marked as taken by `actor`. Returns the bits made up. */
uint8_t mp_scene_claim_make_up(uintptr_t actor);

/* A release of `bit` asked by the run `run` of `actor`. True when it goes through. A refusal is
 * counted, said with its placement for the first MP_SCENE_CLAIM_RELEASE_LINES placements, and
 * for the lock kept as owed while a scene of the host's stands. */
#define MP_SCENE_CLAIM_RELEASE_LINES 16u
bool mp_scene_claim_gives_back(uintptr_t actor, const mp_scene_claim_run_t *run, uint8_t bit);

/* ==============================================================================================
 * What the host's scene tells.
 * ============================================================================================ */

/* Once a substep from the host's tick: the substep, and the actor that drives the host's body,
 * nought for none. */
void mp_scene_claim_tick(uint32_t substep, uintptr_t driver);

/* A scene of the host's began: the actor whose script opened its door, nought where none is
 * known, and the placement its hero was spawned on where it has one. */
void mp_scene_claim_scene_began(uintptr_t actor, bool hero_keyed, uint32_t hero_key);

/* A scene of the host's was taken over with no door heard, a savegame having restored it: the
 * actor that drives the host's body stands in for its actor, nought where nobody drives. While
 * it stands no release of a script is refused. */
void mp_scene_claim_scene_adopted(uintptr_t driver);

/* The scene became a hero's behind its lock: the placement the hero was spawned on. */
void mp_scene_claim_scene_hero(uint32_t hero_key);

/* The one exit of a scene. Its actor is kept for the host a little longer, so that the next part
 * of a conversation in several parts still means him. */
#define MP_SCENE_CLAIM_AFTER_SUBSTEPS 64u
void mp_scene_claim_scene_ended(void);

/* The lock of the scene that stands was a far player's and is no scene of the host's: the marks
 * of its actor fall and its placement's doors are refused until a run of it is the host's again.
 * Before the one exit. The placement, or -1 where the actor's did not read. */
int32_t mp_scene_claim_foreign(void);

/* Whether a release kept as owed is due now: its actor went away with a scene of the host's still
 * standing. One a call; `key` names the placement. The caller lets the host go. */
bool mp_scene_claim_owed_due(bool scene_stands, uint32_t *key);

/* ==============================================================================================
 * The ways out.
 * ============================================================================================ */

/* Every mark falls: a player gave himself back what a scene held, and what the takers took is no
 * longer taken. */
void mp_scene_claim_forget_marks(void);

/* The actor of placement `key` was removed: its marks fall, and it is no actor of a scene. */
void mp_scene_claim_actor_removed(uint32_t key);

/* The player gave himself back what a scene held: every placement whose actor had taken the
 * camera, the lock or the bars here, and the actor of the scene just left, is written down, every
 * mark falls, and the latch's window opens at the substep the host's tick last told. */
void mp_scene_claim_latch(void);

/* A hero's placement is written down: the hero the player's own release sends away, and the hero
 * a spawner written down puts there. No actor on it takes the host again before the level ends. */
void mp_scene_claim_latch_hero(uint32_t key);

/* Whether any placement is written down, the cheap question in front of the two below. */
bool mp_scene_claim_any_written(void);

/* Whether placement `key`, or the placement of `actor`, is written down by a player's own
 * release. */
bool mp_scene_claim_key_repaired(uint32_t key);
bool mp_scene_claim_repaired(uintptr_t actor);

/* Whether the grab of the host is refused to `actor`, the actor that asks for it: its placement
 * is written down by a player's own release. Counted. */
bool mp_scene_claim_grab_refused(uintptr_t actor);

/* The placements the latch wrote down, for a line; returns how many were written. */
size_t mp_scene_claim_latched(uint32_t *keys, size_t max);

/* The one exit, at every end of a world: nothing is marked, written down, remembered or owed. */
void mp_scene_claim_world_ended(void);

/* The host's lines of the doors. */
void mp_scene_claim_report(void);

#endif /* MULTIPLAYER_MP_SCENE_CLAIM_H */
