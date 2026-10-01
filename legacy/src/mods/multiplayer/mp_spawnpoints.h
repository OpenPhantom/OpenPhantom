/* mp_spawnpoints.h: where a deathmatch puts a body that has just come back.
 *
 * ============================== A campaign level has no spawn points ==========================
 *
 * Nothing in a shipped level is authored as "somewhere a player may appear". There is exactly one
 * such point, the level's own start, and a round with more than one player needs more than one.
 *
 * What a level does author, in the hundreds, is where an ENEMY stands. Those records are the best
 * source there is and it is not a compromise: every one of them is a position a level designer
 * put a walking body on, on floor the designer meant to be walked on, with a facing the designer
 * chose. In a deathmatch the arena buries them all before the round starts, so none of them is
 * occupied, and the record stays behind as data after it is buried.
 *
 * ================================= Which placements qualify ===================================
 *
 * Four tests, and the third is the one that is easy to leave out:
 *
 *   the class has to be an enemy class, which is what makes it a body on the ground rather than a
 *   pickup, a door or a script anchor;
 *
 *   the player-host flag has to be clear. Such a placement is a cutscene handover rather than a
 *   standing position, and the arena buries it for its own reasons;
 *
 *   the move mode has to be below 2. From 2 up the engine NEVER snaps the record to the ground:
 *   fish, vehicles, props and pickups keep whatever height the editor gave them, so such a record
 *   can name a point in mid air as easily as a point on the floor. Six hundred of the twenty two
 *   hundred shipped records are of that kind;
 *
 *   the difficulty and detail gates have to pass, because a placement the engine would refuse to
 *   spawn under the current settings is a placement whose surroundings were never played there.
 *   The two gates read in opposite directions and both are taken from the engine's own cells.
 *
 * The level's own start point is added as well and is always index 0, so there is something to
 * fall back to even on a level where every placement fails.
 *
 * ==================================== Thinning and choosing ===================================
 *
 * The surviving set is far too dense: an enemy squad is five records inside one room, and a round
 * that keeps them all puts two players in the same doorway. So the set is thinned greedily, in
 * table order, keeping a point only when it is at least MP_SPAWNPOINTS_MIN_SEPARATION away from
 * every point already kept. Greedy rather than optimal on purpose: the answer has to be the same
 * on every machine in the round and has to be reproducible from the level file alone.
 *
 * Choosing between them, once a round is running, is a separate decision and it belongs to the
 * moment rather than to the level: the point that is FARTHEST from the nearest living player,
 * with the point somebody just died on refused, and with a point somebody else has just come back
 * through refused for a while. If everything is refused the locks are dropped, and if that still
 * answers nothing the level start is taken.
 */
#ifndef MULTIPLAYER_MP_SPAWNPOINTS_H
#define MULTIPLAYER_MP_SPAWNPOINTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many points a level keeps. The thinned count over the eleven shipped levels runs from six
 * to twenty, so this has room to spare and exists to bound the table rather than to bind it. */
#define MP_SPAWNPOINTS_MAX 64u

/* No point answered. Index 0 is a legitimate answer, so it cannot double as the failure value. */
#define MP_SPAWNPOINTS_NONE ((size_t)-1)

/* The thinning distance in world units. One unit is one metre and one grid cell, so this is a
 * fifteen metre gap: far enough that two points are never in the same room, close enough that the
 * weakest shipped level still keeps six of them. */
#define MP_SPAWNPOINTS_MIN_SEPARATION 15.0f

/* How many points a round wants before the thinning is allowed to have the last word.
 *
 * The fifteen above was measured on the campaign levels and is wrong for a small one. The field
 * run of 2026-09-17 played a deathmatch on bridge.b3d: eight placements, five of them qualified,
 * and every one of the five stood inside fifteen units of the level start, so the level kept ONE
 * point. One is not a deathmatch. Every player comes back on the same square, and that square is
 * the one the choice refuses for whoever just died on it, so the choice has nothing left to
 * answer with but a lock it has dropped. Four is the fewest at which it still has a decision. */
#define MP_SPAWNPOINTS_WANTED 4u

/* How close together the relaxing may push two points, in world units. A body is well under a
 * unit wide and the seat search steps two units, so four keeps two re-entries out of one doorway;
 * below that the thinning would not be thinning any more. */
#define MP_SPAWNPOINTS_SEPARATION_FLOOR 4.0f

/* How long a point stays refused after somebody has come back through it, in frames of the pump
 * that hands `now` in. Long enough that two re-entries a second apart do not land on each other. */
#define MP_SPAWNPOINTS_REUSE_LOCK 96u

/* How close to the spot somebody died a point may be before it is refused, in world units. A
 * re-entry on the square you were just killed on is a re-entry into whatever killed you. */
#define MP_SPAWNPOINTS_DEATH_LOCK 8.0f

/* One point. `taken` and `taken_at` are the reuse lock and nothing else; they are part of the
 * structure rather than a side table because the choice reads them in the same loop. */
typedef struct mp_spawnpoint {
    float    position[3];
    float    heading;     /* degrees, the unit the placement authored and the player keeps */
    uint32_t taken_at;    /* the stamp handed to the last re-entry through this point */
    bool     taken;       /* whether taken_at means anything yet */
} mp_spawnpoint_t;

/* Which of the three tiers answered, so that a report can say it rather than only give a point. */
typedef enum mp_spawn_pick_tier {
    MP_SPAWN_PICK_NONE,    /* the table is empty */
    MP_SPAWN_PICK_BEST,    /* farthest from the nearest living player, no lock broken */
    MP_SPAWN_PICK_ANY,     /* every point was locked, so a locked one was taken anyway */
    MP_SPAWN_PICK_START    /* nothing answered at all, so the level start was taken */
} mp_spawn_pick_tier_t;

/* ==============================================================================================
 * The pure decisions. None of these touches the engine, and each of them is the whole rule rather
 * than a step of it, so a test pins the behaviour and not the implementation.
 * ============================================================================================ */

/* Does this placement record qualify as a spawn point? The two gates are handed in rather than
 * read, because the answer has to be checkable against the values a level authored without a game
 * in the process. */
bool mp_spawnpoints_accepts(uint32_t flags, int32_t class_id, int32_t move_mode,
                            int32_t min_difficulty, int32_t detail_gate,
                            int32_t difficulty, int32_t detail_level);

/* Squared distance between two points. Squared throughout: every comparison here is against
 * another distance, so the root would be arithmetic nobody reads. */
float mp_spawnpoints_distance_sq(const float a[3], const float b[3]);

/* The thinning test: is this candidate at least `min_separation` from everything kept so far? */
bool mp_spawnpoints_far_enough(const float candidate[3], const mp_spawnpoint_t *kept,
                               size_t kept_count, float min_separation);

/* The separation the NEXT pass over the table should use, or 0 when the set as it stands is the
 * answer. Halved each time and never taken below the floor, and not asked for at all when the
 * pass kept everything that qualified: relaxing cannot invent a point that was never there.
 *
 * `kept` counts the points the pass ended with, the level start among them; `qualified` counts
 * the placements that passed the four rules before the thinning. */
float mp_spawnpoints_relaxed(float separation, size_t kept, size_t qualified);

/* The choice. `living` is the position of every player still standing, `died_at` the point the
 * returning player was killed on or NULL when that is not known, and `now` the frame stamp the
 * reuse lock is measured against. Answers an index into `points`, or MP_SPAWNPOINTS_NONE for an
 * empty table, and says through `tier` which of the three fallbacks produced it. */
size_t mp_spawnpoints_pick(const mp_spawnpoint_t *points, size_t count,
                           const float (*living)[3], size_t living_count,
                           const float *died_at, uint32_t now, mp_spawn_pick_tier_t *tier);

/* ==============================================================================================
 * The engine side.
 * ============================================================================================ */

/* Read the level's placement table once and build the thinned set. Only meaningful while a world
 * stands, so it belongs in the level-opened path. Returns how many points the level ended up
 * with; 0 means not even the level start could be read, which is said in the log. */
size_t mp_spawnpoints_build(void);

/* Throw the set away. The next level builds its own. */
void mp_spawnpoints_clear(void);

size_t mp_spawnpoints_count(void);

/* One point's pose. False for an index past the end. */
bool mp_spawnpoints_get(size_t index, float position[3], float *heading);

/* Choose a point, stamp it against the reuse lock and hand back its pose. The one call a caller
 * needs; `mp_spawnpoints_pick` is exposed beside it so the rule can be driven without a level.
 * False when the level has no points at all. */
bool mp_spawnpoints_take(const float (*living)[3], size_t living_count,
                         const float *died_at, uint32_t now,
                         float position[3], float *heading);

/* The co-operative choice, for a player who found no seat beside a standing team mate: the free
 * point NEAREST to `anchor`, where the deathmatch takes the one farthest from the living, with the
 * same two locks; the level start when nothing is free or `anchor` is NULL. Stamps the reuse lock
 * like the choice above. `level_start` says whether the answer is the level start. False when the
 * level has no points at all. */
bool mp_spawnpoints_take_nearest(const float *anchor, const float *died_at, uint32_t now,
                                 float position[3], float *heading, bool *level_start);

/* The stamp the reuse lock is measured in: the engine's own frame counter, which has one writer
 * in the whole image. It lives here rather than at each caller so that the lock, the report and
 * whoever asks for a point are all speaking the same clock. 0 when the cell did not resolve,
 * which makes every point read as never used and is the harmless direction to fail in. */
uint32_t mp_spawnpoints_now(void);

/* The thinning distance, for a mode that wants a tighter or a looser map than the default. Values
 * that are not finite or not positive are refused and the current one is kept. */
bool mp_spawnpoints_set_separation(float units);

/* What was built and what was chosen, for the run report. */
void mp_spawnpoints_report(void);

#endif /* MULTIPLAYER_MP_SPAWNPOINTS_H */
