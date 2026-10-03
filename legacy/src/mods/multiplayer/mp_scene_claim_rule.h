/* mp_scene_claim_rule.h: whose a script's doors are on the host, as arithmetic.
 *
 * Layer 1, pure. The reading of the engine and the session is mp_scene_claim.c; what is here is
 * every decision it makes, so that each can be driven over its edges with no game in the process.
 *
 * A scene is the host's alone, and the far players go on playing through it. Their scripts run on
 * the host all the same, and the engine has one lock, one camera and one pair of bars. So every
 * run of an actor's script on the host is either the host's or a far player's, and what it takes
 * and gives back is judged by that:
 *
 *   The RUN. A run is a far player's when somebody is joined and the actor's own fresh answer to
 *   "where is the player" was a far player, or it died and a far player hurt it lately, or it has
 *   no fresh answer and its placement woke for a far player lately. The actor of the host's own
 *   scene, the actor that drives his body, and an actor that has taken something here and not
 *   given it back are the host's whatever they last heard. Every other run is the host's.
 *
 *   The MARKS. What an actor took on this host, a bit each for the camera, the lock and the bars,
 *   kept per placement. A run of the host's takes and sets its bit, gives back and clears it. A
 *   far player's run takes nothing; it gives back only what its own actor took, bit by bit: the
 *   end of a camera dolly is three calls, the camera, the lock and the bars, and none of them may
 *   spend the mark of the next. So an actor that took for the host can always give back, with a
 *   far player standing nearer by then, and a script beside a far player neither takes the host
 *   nor ends his scene.
 *
 *   The DOORS OF A RUN. What a far player's run was refused before it reached the door of a
 *   scene, the bars and the camera, in their order: at the door they are made up, because from
 *   there the scene is the host's.
 *
 *   The LATCH. Placements whose doors are refused: written down when a player gave himself back
 *   what a scene held, the ones that had taken something by then and the ones that open a door
 *   in the window after it, until the level ends; and a lock of a far player's that is no scene
 *   of the host's, until a run of that actor is the host's again. A hero on a placement written
 *   down for the first reason is refused the grab of the host as well.
 *
 *   The WAKING. Which far player a placement woke for, kept for a short while by the range gate:
 *   an actor that opens a door in its first run has asked for no player yet.
 *
 *   A release OWED. A release refused once in a far player's run, whose actor then takes itself
 *   away, can never come again, so it is made up for the host. An actor refused again a run
 *   later gives back on every run and is owed nothing.
 */
#ifndef MULTIPLAYER_MP_SCENE_CLAIM_RULE_H
#define MULTIPLAYER_MP_SCENE_CLAIM_RULE_H

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * The run.
 * ============================================================================================ */

/* The rule that decided whose a run is, in the order they are asked. */
typedef enum mp_scene_run_rule {
    MP_SCENE_RUN_ALONE = 0,          /* nobody else is in the session */
    MP_SCENE_RUN_OF_THE_SCENE,       /* the actor of the host's scene, the actor driving his body,
                                      * or the actor of a scene that has just ended */
    MP_SCENE_RUN_OF_A_TAKER,         /* it took the camera, the lock or the bars here by its own
                                      * script and has not given back */
    MP_SCENE_RUN_BY_OWN_ANSWER,      /* its own last answer was a player, lately */
    MP_SCENE_RUN_BY_LAST_ATTACKER,   /* it died, and a player hurt it lately */
    MP_SCENE_RUN_BY_THE_WAKING,      /* no fresh answer, and its placement woke for a far player */
    MP_SCENE_RUN_BY_HOST_ANCHOR,     /* nothing named a player: the world is the host's */
    MP_SCENE_RUN_RULES
} mp_scene_run_rule_t;

/* What is known about the actor whose script runs. */
typedef struct mp_scene_run_evidence {
    bool                joined;         /* somebody else is in the session */
    bool                of_the_scene;
    bool                taker;          /* mp_scene_mark_keeps_the_host on its marks */
    mp_scene_evidence_t actor;          /* its own answer, its death and who hurt it */
    bool                woke_for_far;   /* its placement woke for a far player lately */
    uint8_t             woke_bank;
} mp_scene_run_evidence_t;

/* Whose the run is: `bank` is 0 for the host and 1 and up for a far player. The first three rules
 * answer the host without a look at what the actor last heard, so a far player who steps nearer
 * to an actor of the host's scene does not make its next door or its release his. */
mp_scene_run_rule_t mp_scene_run(const mp_scene_run_evidence_t *evidence, uint8_t *bank);

/* The rule as a line says it after "by". */
const char *mp_scene_run_text(mp_scene_run_rule_t rule);

/* ==============================================================================================
 * The marks.
 * ============================================================================================ */

#define MP_SCENE_MARK_CAMERA 0x01u
#define MP_SCENE_MARK_LOCK   0x02u
#define MP_SCENE_MARK_BARS   0x04u
/* The camera bit stands from a spoken line's camera alone, which the judgement of the line let
 * through and no dolly took. */
#define MP_SCENE_MARK_SPOKEN 0x08u

/* A take that went through: `bit` is set. A spoken line's camera sets the camera bit and flags it;
 * a camera the dolly or the lock opcode took stands unflagged, and a spoken line's on top of it
 * leaves it so. Returns the marks after. */
uint8_t mp_scene_mark_taken(uint8_t marks, uint8_t bit, bool spoken);

/* A release asked for `bit`. In a run of the host's it goes through and the bit is cleared. In a
 * far player's it goes through only where the bit stands, and clears it; no other bit is touched
 * either way. True when the release goes through. */
bool mp_scene_mark_given_back(uint8_t *marks, uint8_t bit, bool hosts_run);

/* Whether the engine's own answer to "where is the player" is kept for the actor these marks are
 * of, and its runs are the host's: a camera the dolly or the lock opcode took, the lock, or the
 * bars. A spoken line's camera alone does not: every conversation takes one, and its speaker is
 * no actor of a scene. */
bool mp_scene_mark_keeps_the_host(uint8_t marks);

/* ==============================================================================================
 * The doors of a run.
 * ============================================================================================ */

/* A run remembers this many refused takes; a camera dolly and a lock opcode are three between
 * them. */
#define MP_SCENE_RUN_DOORS 6u

typedef struct mp_scene_run_door {
    uint8_t bit;        /* MP_SCENE_MARK_BARS or MP_SCENE_MARK_CAMERA */
    int32_t argument;   /* what the engine was handed: the bars' flag, the camera's group */
} mp_scene_run_door_t;

typedef struct mp_scene_run_doors {
    mp_scene_run_door_t door[MP_SCENE_RUN_DOORS];
    size_t              count;
    uint32_t            left_out;   /* refused takes past the room, over every run */
} mp_scene_run_doors_t;

/* A run begins or ends: nothing is remembered. The count of takes left out is the report's and
 * stays. */
void mp_scene_run_doors_forget(mp_scene_run_doors_t *doors);

/* A take refused in this run, kept in its order. False past the room, and counted. */
bool mp_scene_run_doors_keep(mp_scene_run_doors_t *doors, uint8_t bit, int32_t argument);

/* ==============================================================================================
 * The latch.
 * ============================================================================================ */

/* How long the window stands after a release, in substeps: two seconds, in which a script that
 * holds its scene by raising the lock on every run has run many times. And how many placements
 * are kept. */
#define MP_SCENE_LATCH_SUBSTEPS 64u
#define MP_SCENE_LATCH_KEYS     8u

/* Why a placement's doors are refused. */
typedef enum mp_scene_refusal {
    MP_SCENE_REFUSAL_NONE = 0,
    MP_SCENE_REFUSAL_REPAIRED,   /* written down in the window: until the level ends */
    MP_SCENE_REFUSAL_FOREIGN     /* its lock for a far player was no scene of the host's: until a
                                  * run of it is the host's again */
} mp_scene_refusal_t;

typedef struct mp_scene_latch {
    bool               open;                       /* a window was opened and has not run out */
    uint32_t           since;                      /* the substep it was opened in */
    uint32_t           key[MP_SCENE_LATCH_KEYS];   /* the placements whose doors are refused */
    mp_scene_refusal_t why[MP_SCENE_LATCH_KEYS];
    size_t             count;
    uint32_t           refused;                    /* doors refused for a placement written down */
    uint32_t           left_out;                   /* placements not written down, the set full */
    uint32_t           foreign;                    /* placements written down as foreign */
    uint32_t           taken_back;                 /* of those, back with a run of the host's */
} mp_scene_latch_t;

/* Opens the window at `now`. A window that stands is opened again from `now`; the placements
 * written down stay. */
void mp_scene_latch_open(mp_scene_latch_t *latch, uint32_t now);

/* A door opened by the actor of placement `key` at `now`, in a run that is the host's or not. True
 * when the door is refused: the placement is written down, or the window stands, and then it is
 * written down. A placement written down as foreign is let go by a run of the host's and is then
 * judged like any other. A window that has run out is closed here. */
bool mp_scene_latch_door(mp_scene_latch_t *latch, uint32_t key, uint32_t now, bool hosts_run);

/* The lock of placement `key` was a far player's and no scene of the host's: its doors are
 * refused until a run of it is the host's. False with the set full, which is counted. */
bool mp_scene_latch_foreign(mp_scene_latch_t *latch, uint32_t key);

/* Placement `key` is written down as the window writes one, with no door opened: the actor that
 * had taken something here when the player gave it back to himself, and the hero that release
 * sent away. One written down as foreign becomes a repaired one, which no run takes back. False
 * with the set full, which is counted. */
bool mp_scene_latch_repaired(mp_scene_latch_t *latch, uint32_t key);

/* The actor of placement `key` was removed. A foreign row of it goes with it, because no run of
 * that actor can take it back any more and the row would hold one of the eight until the level
 * ends; a repaired row stays, for the placement's next actor. */
void mp_scene_latch_forget_foreign(mp_scene_latch_t *latch, uint32_t key);

/* Why the doors of placement `key` are refused, asked without opening one. */
mp_scene_refusal_t mp_scene_latch_holds(const mp_scene_latch_t *latch, uint32_t key);

/* The placements written down in a window, for a line; returns how many were written into
 * `keys`. */
size_t mp_scene_latch_keys(const mp_scene_latch_t *latch, uint32_t *keys, size_t max);

/* The one exit, at the end of a level: nothing is written down and no window stands. The counts
 * are the report's and stay. */
void mp_scene_latch_clear(mp_scene_latch_t *latch);

/* ==============================================================================================
 * The waking.
 * ============================================================================================ */

/* How long a placement that woke for a far player counts as woken for him, in substeps, and how
 * many are kept: a far player wakes a handful as he walks, each of them once. */
#define MP_SCENE_WOKE_SUBSTEPS 64u
#define MP_SCENE_WOKE_ROWS     32u

typedef struct mp_scene_woke_row {
    uintptr_t record;   /* the placement's record, nought for an empty row */
    uint32_t  at;
    uint8_t   bank;
} mp_scene_woke_row_t;

typedef struct mp_scene_woke {
    mp_scene_woke_row_t row[MP_SCENE_WOKE_ROWS];
    size_t              next;       /* the row a new record takes */
    uint32_t            noted;      /* answers written down, a record's second included */
    uint32_t            replaced;   /* rows a newer record took while they were still fresh */
} mp_scene_woke_t;

/* The scan was answered yes for `record` at `now` because the far player of `bank` stood in its
 * range and the host did not. A record already kept is stamped again. */
void mp_scene_woke_note(mp_scene_woke_t *woke, uintptr_t record, uint8_t bank, uint32_t now);

/* Whether `record` woke for a far player within the last MP_SCENE_WOKE_SUBSTEPS, and for whom. The
 * age is taken in unsigned arithmetic, so a stamp after `now` reads as old. */
bool mp_scene_woke_for(const mp_scene_woke_t *woke, uintptr_t record, uint32_t now, uint8_t *bank);

/* ==============================================================================================
 * A release owed.
 * ============================================================================================ */

/* How long a refused release is kept as owed, in substeps, and how many. An actor that gives a
 * scene back and takes itself away does both within a tick or two. */
#define MP_SCENE_OWED_SUBSTEPS 64u
#define MP_SCENE_OWED_ROWS     4u

typedef struct mp_scene_owed_row {
    bool      have;
    uint32_t  key;
    uintptr_t actor;
    uint32_t  at;
} mp_scene_owed_row_t;

typedef struct mp_scene_owed {
    mp_scene_owed_row_t row[MP_SCENE_OWED_ROWS];
    uint32_t            noted;
    uint32_t            left_out;    /* refused releases past the room */
    uint32_t            due;         /* made up: the actor went away */
    uint32_t            forgotten;   /* not made up: the actor stayed, or no scene stood */
    uint32_t            habitual;    /* rows given up: the actor was refused again a run later */
} mp_scene_owed_t;

/* A release of the lock refused for the actor at `actor`, of placement `key`, while a scene of the
 * host's stood. An actor already kept is stamped again. */
void mp_scene_owed_note(mp_scene_owed_t *owed, uint32_t key, uintptr_t actor, uint32_t now);

typedef enum mp_scene_owed_look {
    MP_SCENE_OWED_NOTHING = 0,   /* the row is empty */
    MP_SCENE_OWED_WAITS,         /* the actor lives: it may still give back itself */
    MP_SCENE_OWED_FORGOTTEN,     /* it stayed past the time, or no scene of the host's stands */
    MP_SCENE_OWED_DUE            /* it went away: the release is made up for the host */
} mp_scene_owed_look_t;

/* One look at row `row`. A row that is due or forgotten is empty after. `actor_lives` says whether
 * the actor is still its placement's live one, `scene_stands` whether a scene of the host's does:
 * with none there is nothing to give back. */
mp_scene_owed_look_t mp_scene_owed_step(mp_scene_owed_t *owed, size_t row, uint32_t now,
                                        bool actor_lives, bool scene_stands);

/* Whether a refusal at `now` is the same actor's second: it was refused before, in an earlier
 * substep and no longer ago than MP_SCENE_OWED_SUBSTEPS. `last` is the substep of its last
 * refusal plus one, nought for never. The three calls of one end of an opcode fall in one substep
 * and are one refusal. An actor refused again a run later gives back on every run, whatever
 * stands: nothing is owed to it. */
bool mp_scene_owed_again(uint32_t last, uint32_t now);

/* Nothing is owed to the actor of placement `key`: its row is given up, and counted. */
void mp_scene_owed_drop(mp_scene_owed_t *owed, uint32_t key);

/* The one exit: nothing is owed. */
void mp_scene_owed_clear(mp_scene_owed_t *owed);

#endif /* MULTIPLAYER_MP_SCENE_CLAIM_RULE_H */
