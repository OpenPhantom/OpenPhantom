/* creeping_mover.h: which movers are creeping rather than carrying, with no engine in it.
 *
 * A crusher in the final level lowers a floor panel five centimetres over about two seconds, and
 * the characters standing on it ride it down through the floor drawn beneath them. Refusing every
 * carry a crusher makes was tried and is wrong: decoding every shipped level's animated-object
 * records found sixty four of them, fifteen with walkable faces that descend, and several are
 * plainly transport, including an articulated ninety unit lift.
 *
 * ================================ The rate is what separates them ==============================
 *
 * Descent per tick, measured out of the level files across all eleven levels the executable names,
 * so this is the whole game rather than a sample:
 *
 *     the one at fault                                     0.0008
 *     the slowest genuine platform, three of them          0.0323
 *     the fastest, a ninety unit lift                      0.5820
 *
 * A factor of forty, with nothing in between. So the question is not what the mover IS but what it
 * is DOING. One creeping down slower than the limit below is transporting nobody anywhere, and the
 * only thing carrying a rider on it achieves is to sink them.
 *
 * The limit sits six times from the fault and six times from the slowest real platform, so neither
 * side of the measurement is near it.
 */
#ifndef GROUND_CLIP_FIX_CREEPING_MOVER_H
#define GROUND_CLIP_FIX_CREEPING_MOVER_H

#include <stdbool.h>
#include <stdint.h>

/* Units of descent per tick. Anything at or above this is a platform doing its job. */
#define CREEPING_MOVER_LIMIT 0.005f

/* How many creeping movers are remembered at once. Far more than any level holds: the whole game
 * has one. A full table stops recording new ones rather than growing or evicting, because evicting
 * would let a mover already being refused start being carried again halfway through its run. */
#define CREEPING_MOVER_MAX 16u

typedef struct creeping_mover_set {
    uint32_t id[CREEPING_MOVER_MAX];
    unsigned count;
} creeping_mover_set_t;

/* True when a fall of `fell` units in one tick is a creep rather than transport. A rise or a
 * standstill is not a creep: `fell` is positive downward, and a value of zero or less answers
 * false, which is what leaves a mover carrying somebody upward alone. */
bool creeping_mover_is_creep(float fell);

/* Remember `id`, and answer whether this is the first time it has been seen, so a caller can report
 * each mover once. A full table answers false rather than recording. */
bool creeping_mover_note(creeping_mover_set_t *set, uint32_t id);

/* True when `id` has been recorded. This is how the ground snap recognises the same mover the rider
 * carry refused: the snap is handed no delta of its own and cannot measure the rate itself. */
bool creeping_mover_known(const creeping_mover_set_t *set, uint32_t id);

#endif /* GROUND_CLIP_FIX_CREEPING_MOVER_H */
