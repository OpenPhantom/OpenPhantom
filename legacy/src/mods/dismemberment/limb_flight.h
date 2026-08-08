/* limb_flight.h: the flight of the severed piece, and why it would not lie still.
 *
 * candy_stuntTick 0x42F64C is the task enemy_detachPiece registers. Byte-read:
 *
 *     spin.x = 3.3, rand01*6.6        <- PER SUBSTEP, so up to 211 deg/s
 *     spin.z = 2.0, rand01*4.0        <- PER SUBSTEP, so up to 128 deg/s
 *     vel.z -= 0.2                     <- PER SUBSTEP = 204.8 u/s^2, FIVE TIMES world gravity
 *
 * That explains both complaints: the piece cannot be recognised (it rotates faster than the eye
 * follows) and it snaps away instead of flying (it is thrown up and instantly pulled down).
 *
 * All five constants have readers ONLY in candy_stuntTick and candy_stuntOnContact, i.e. inside
 * the flight code itself, so they can be set directly, with no operand repointing and no side
 * effect on anything else. NOT touched: the normaliser [0x4A8384] = 1/32768, which has four
 * readers and is the rand15() conversion, not a flight constant.
 *
 * NOT FIXED, named honestly: the DIRECTION. enemy_detachPiece computes
 * `dir = nodeMat[0].t, nodeMat[n].t` (root minus joint) and from it the impulse with
 * `dz*0.25 + 0.15`; candy_stuntTick then rotates it out of the object's LOCAL frame into the
 * world. For a high node (head, neck) `dir` points DOWN, so the upward part flips negative. Fixing
 * that would mean inserting code behind the rotation, a separate change needing its own review.
 */
#ifndef LIMB_FLIGHT_H
#define LIMB_FLIGHT_H

void limb_flight_install(void);

#endif /* LIMB_FLIGHT_H */
