/* idle_clip.h: write a generated idle over one of a model's clips, in memory, at the moment it
 * is first needed.
 *
 * A .baf model carries its clips as keyframe blocks loaded straight into memory, each a header,
 * a node table and per-node entry arrays that the puppet interpolates between. The block is a
 * plain heap image, so a clip can be given a different set of entries by pointing its nodes at
 * arrays of this DLL's own: the same record shape the engine reads, the node positions copied
 * out of the clip being replaced, and the rotations authored here as slow sums of sines. The
 * model's other clips, its geometry and its rig are untouched, and nothing on disk changes.
 *
 * Why this exists. The jail prisoner's own clips are a still stand, a hands-up-at-the-bars
 * pass and three talking passes; every one either freezes or reads as talking once the hold in
 * dialogue_anim_fix has taken his stuck talk animation away. A man waiting in a cell should
 * shift his weight and look about, and no clip he was shipped with does that. The stand, clip
 * 0, is the one written over: it is the model's own idle, flagged to loop, and near enough
 * still that nothing is lost; the hands-up pass he is authored to sit in before he is spoken
 * to is left exactly as shipped.
 */
#ifndef IDLE_CLIP_H
#define IDLE_CLIP_H

#include <stdbool.h>
#include <stdint.h>

/* Replaces clip `clip_index` of the model behind `body` with the generated idle. Idempotent: a
 * clip already pointing at this DLL's arrays is left alone, and a level reload, which loads the
 * model afresh, is written again on the next call. Returns true when the clip now carries the
 * idle, false when the chain from the body to the clip did not read as expected, in which case
 * nothing was written. */
bool idle_clip_install(uint32_t body, int32_t clip_index);

#endif /* IDLE_CLIP_H */
