/* mp_twist.h: the per-node rotations a body wears on top of its clips.
 *
 * A clip is not the whole pose. The engine turns single nodes by angle on top of the animation:
 * the root toward the travel direction and the chest toward the aim while the feet go elsewhere,
 * the torso pitched and the root yawed while a hit reaction runs (node 3 by the drop timer times
 * 75, the root by the timer times 50), the head where the player looks. None of that is in a clip
 * ordinal, so a puppet that plays the right clip still walks with its
 * whole body facing its heading while the far player sidesteps. These rotations live in one array
 * on the body's render thing, one pitch, yaw and roll per node, written by the engine's own node
 * setters and read by the pose build every frame. Reading the array on the far machine and writing
 * it on this one is the whole of the replication.
 *
 * Read by the pose build EVERY FRAME, and that is why this module has two halves. A value written
 * once per substep and read once per frame is a staircase: at a hundred frames a second the far
 * player's body holds one orientation for three frames and then jumps, thirty two times a second,
 * while its position, its heading and its clip all move at the frame rate. The root node turns the
 * whole body, so the staircase reads as the animation stuttering rather than as one node stepping.
 * The simulation half below produces a value per substep; the drawn half keeps the pair of the
 * previous and the current one and writes their mixture once per rendered frame, on the weight the
 * engine hands out for its own interpolation.
 */
#ifndef MULTIPLAYER_MP_TWIST_H
#define MULTIPLAYER_MP_TWIST_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The nodes of this object that are turned, lowest node first, up to the wire's cap. A node with
 * both angles zero is not reported. Returns how many were written to `out`. */
size_t mp_twist_read(uint32_t object, mp_wire_twist_t out[MP_WIRE_MAX_TWISTS]);

/* Resolve the weight cells and put the frame hook on the world draw. Optional in the sense the
 * rest of the feature is: a build where the site or a cell did not resolve keeps writing the
 * rotations once per substep and says so once. */
void mp_twist_resolve(void);

/* The start of one substep of the puppet, called before anything in the substep can turn back.
 * It carries the current values over into the previous ones, so a substep that then produces
 * nothing leaves the pair holding one value at both ends and every frame of it draws that value.
 * Without it the frames of such a substep sweep the pair from end to end while the weight runs
 * from zero to one, which is worse than the staircase it replaces. */
void mp_twist_open_substep(size_t bank);

/* Write these rotations onto the object's nodes, and put every node written by an earlier call and
 * absent now back to zero, so a twist that ended on the far side ends here too. Also the substep's
 * new value for the drawn pair: call it inside the puppet's window, after mp_twist_open_substep. */
void mp_twist_apply(size_t bank, uint32_t object, const mp_wire_twist_t *twists, size_t count);

/* The substep's rotations are not for this body: they are node indices of a rig the far player
 * wears and this side does not draw, a model worn over the hero, where the same
 * numbers are other joints. Applies none, which also puts back whatever an earlier substep
 * turned, and counts the substep. */
void mp_twist_withhold(size_t bank, uint32_t object);

/* Forget the pair, the ownership and the object. Run when the peer changes: a restarted peer's
 * body starts from its own defaults and nothing of the old one may be blended into it. */
void mp_twist_reset(size_t bank);

/* What one channel is drawn as: the substep pair mixed on the engine's weight along the shortest
 * arc, folded back into the band the node array holds. A pair whose two ends are equal, which is
 * what a collapsed pair is, answers that value at every weight. Exposed so the unit test can pin
 * the arithmetic without a game. */
float mp_twist_draw_angle(float previous, float current, float weight);

typedef struct mp_twist_counters {
    bool     installed;      /* the frame hook stands, so the rotations are drawn interpolated */
    uint32_t frames;         /* frames written from the pair */
    uint32_t collapses;      /* substeps that opened and then produced no new value */
    uint32_t stalls;         /* pairs the frame half had to close itself: no substep opened */
    uint32_t refusals;       /* frames declined: no puppet object, or it read as the local one */
    uint32_t given_up;       /* claims dropped after a run of frames with no substep behind them */
    uint32_t withheld;       /* substeps whose rotations belonged to a rig not drawn here */
} mp_twist_counters_t;

void mp_twist_get_counters(mp_twist_counters_t *out);

#endif /* MULTIPLAYER_MP_TWIST_H */
