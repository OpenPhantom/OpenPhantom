/* object_track.h: where each drawn object was one simulation step ago, remembered here rather than
 * read out of the engine.
 *
 * The engine keeps a previous position beside each object's current one and draws the object
 * between the two, and that interpolation is how anything moves smoothly at a frame rate above
 * the 32 Hz simulation. That pair is right while a character walks. It is not right while one
 * rides a
 * platform: the carry runs a second pose commit in the same step and, because the first has
 * already run, the second captures a previous position that is already the current one. The pair
 * goes flat, there is nothing to blend, and the rider steps 32 times a second against a platform
 * that is drawn every frame.
 *
 * Measured rather than argued. Walking, the pair differed on all 2111 sampled frames; riding, it
 * was identical on all 600. A hardware write watch then named both writers, Plr_CommitPose and the
 * carry's own commit, and they are the same six lines twice over.
 *
 * So the previous position is kept here instead. Nothing in the engine is written and nothing the
 * simulation reads is touched; this decides only what a frame draws.
 *
 * The step boundary arrives as `stamp` rather than being inferred here, and the first attempt at
 * this inferred it: an object whose position differed from the one last seen was taken to have
 * stepped. That works while something moves and fails the moment it stops. A character standing
 * still never changes position, so its previous was never brought forward, and the blend swung it
 * between where it last walked and where it now stands, once per step, for as long as it stood
 * there. It cured the judder on a platform and planted the same judder on solid ground.
 *
 * The caller derives the stamp from the substep alpha, which climbs across a step and drops when
 * a new one begins, so no clock has to be resolved and no second reader of the engine's substep
 * counter is needed.
 *
 * Pure, so the awkward cases can be driven from a console test: an object seen for the first time,
 * one that has not moved, a table with no room left, and a jump too large to be motion.
 */
#ifndef OBJECT_TRACK_H
#define OBJECT_TRACK_H

#include <stdbool.h>
#include <stdint.h>

/* Slots for distinct drawn objects. A level draws far fewer at once; the count is generous because
 * a spare slot costs 40 bytes and running out costs an object that falls back to the engine's own
 * pair for as long as it stays on screen. */
#define OBJECT_TRACK_SLOTS 512u

/* How many rendered frames an entry may go unseen before another object may take its slot. An
 * object being drawn is seen every frame, so anything older than this is off screen or gone. */
#define OBJECT_TRACK_STALE_FRAMES 120u

/* Forgets everything. For a level change, where every object pointer is about to mean something
 * else, and for a test that wants to start from nothing. */
void object_track_reset(void);

/* Marks the start of a rendered frame, which is the only thing here that measures age. Call it
 * once per frame from the frame callback, never from the per-object path. */
void object_track_frame(void);

/* Records `position` for `key` on simulation step `stamp`, and answers where that object was on
 * the step before.
 *
 * Several frames arrive on the same stamp, and only the first of them brings the record forward.
 * That is what keeps the answer identical for every frame within a step rather than collapsing
 * after the first, and it is what lets a stationary object settle: its previous becomes its
 * current on the next step and stays there.
 *
 * False means there is nothing to answer with and the caller must fall back on whatever the engine
 * holds: the first sighting of an object, and a sighting that found no free slot.
 */
bool object_track_sample(uintptr_t key, uint32_t stamp, const float *position,
                         float *out_previous);

/* The drawn position: `previous` blended toward `current` by `alpha`.
 *
 * `limit` is the furthest a CHARACTER may travel in one simulation step, and getting that number
 * wrong is what made the first attempt at this unusable. It was set to 64 by copying
 * MoverTravelLimitPerStep, which describes how far a lift may travel, and a character's step is
 * nothing like that. Measured in play: a platform carried the player 0.045 units per step and
 * walking was about 0.017.
 *
 * The distance is what separates a rider from a teleport, and it has to, because the flatness of
 * the engine's own pair cannot. A carried character and a freshly spawned one both have a
 * previous position identical to their current one; the difference is that the rider really was
 * a fraction of a unit away a step ago and the spawn was somewhere else entirely. Measured
 * against the same run, the spawns and pool reuses sat 35, 44, 125 and 163 units out. A limit of
 * 64 let the first two through and drew them smeared across the level.
 *
 * Past the limit the current position is used unblended, as the engine drew before any of this
 * existed. Being too strict costs a rider its smoothing; being too loose costs the player a
 * character flying across the map. Zero or less disables the test.
 */
void object_track_blend(const float *previous, const float *current, float alpha, float limit,
                        float *out_position);

#endif /* OBJECT_TRACK_H */
