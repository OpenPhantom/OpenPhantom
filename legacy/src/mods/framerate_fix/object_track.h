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
 * The stamp is the engine's substep counter. The first version of this derived it from the substep
 * alpha instead, taking the alpha dropping as the boundary. That is wrong in both directions:
 * a frame spanning two steps advances it once, and at exactly 32 frames a second the alpha barely
 * moves at all, so the drop that marks the boundary may never arrive.
 *
 * That counter is advanced by the menu renderer as well as by the simulation, so while a menu is
 * on screen it can move without anything having stepped. Nothing here has to defend against that:
 * a stamp that moves while a position does not brings previous up to current, which draws the
 * object where it is, as the game did before any of this existed.
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
 * `out_gap` is how many simulation steps separate the answer from `position`, which is one
 * whenever a step is shared by two frames or more and larger when a frame spanned several steps.
 * It is what object_track_weight below needs, and it holds its value for every frame inside a
 * step rather than only on the frame that moved the record forward.
 *
 * False means there is nothing to answer with and the caller must fall back on whatever the engine
 * holds: the first sighting of an object, and a sighting that found no free slot. Neither answer
 * writes through `out_gap`, so a caller must treat the engine's own pair as one step apart, which
 * it is.
 */
bool object_track_sample(uintptr_t key, uint32_t stamp, const float *position,
                         float *out_previous, uint32_t *out_gap);

/* How far along to draw, for a frame whose substep alpha is `alpha` and whose two samples are
 * `gap` simulation steps apart.
 *
 * The engine's own blend uses the alpha directly, which draws an object exactly one step behind
 * the simulation. That lag is not a defect; it is what keeps the drawn position between two
 * positions the object really held instead of guessing at one it has not reached yet. Every frame
 * inside a step raises the alpha by its own share of the step, so the drawn position advances by
 * one frame of travel whatever the frame rate, and the lag stays put.
 *
 * All of that assumes a step shared by two frames or more. Below 32 frames a second a frame spans
 * several steps, the samples either side of it are `gap` steps apart, and using the alpha alone
 * would cover `gap` steps of travel in one step's worth of alpha: the object runs ahead of itself
 * and drops back, once every frame.
 *
 * Asking instead for the same drawn moment, one step behind a simulation standing at
 * stamp + alpha, gives (alpha + gap - 1) / gap. At a gap of one that is the alpha exactly, so
 * nothing changes at the rates where a step is shared. A gap wide enough to be a hitch or an
 * object coming back on screen tends towards one, which draws it where it is. For two samples
 * with nothing to do with each other that is the right answer, and it needs no branch of its
 * own.
 */
float object_track_weight(float alpha, uint32_t gap);

/* The drawn position: `previous` blended toward `current` by `weight`.
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
 *
 * `limit` is per STEP, so a caller whose samples are several steps apart has to scale it by the
 * same gap it passed to object_track_weight, or a legitimate two-step move is read as a teleport.
 *
 * A position or a weight that is not finite answers with the current position. That case is
 * checked before the limit rather than after, because the limit is a distance comparison and
 * every comparison against a NaN is false, so the limit cannot rank one. It is not caught and
 * replaced: a NaN goes through the arithmetic and comes out a NaN. The engine did the same, and
 * that is what keeps an object with an unwritten previous position out of the picture.
 * Substituting a real position for it draws things retail never showed; object_track.c says what
 * that looked like.
 */
/* False when the pair was refused and `out_position` holds the current position unblended. The
 * caller cannot tell otherwise, and the difference matters: the camera aims at the player's
 * simulation position interpolated on the same alpha, so a refused body is drawn at a different
 * moment from the one the camera is pointing at and moves against the frame. */
bool object_track_blend(const float *previous, const float *current, float weight, float limit,
                        float *out_position);

#endif /* OBJECT_TRACK_H */
