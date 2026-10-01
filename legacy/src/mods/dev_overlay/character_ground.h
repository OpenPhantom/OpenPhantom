/* character_ground.h: the marks a borrowed body leaves on the floor.
 *
 * Every ground effect the player makes, the footprint, the mud print, the wet print that dries
 * out, the sand puff, the water droplets, the sand kick under a run, the landing splash, the swamp
 * wake and the spray off the hands, is placed at the world position of one named skeleton node.
 * game/footstep.c asks for that position through a single function, and there are only five names
 * in the whole module: `lfoot`, `rfoot`, `chest`, `lhand` and `rhand`.
 *
 * The one defect, and it is a disagreement between two answers to "which model".
 *
 * The lookup that turns a name into a node index reads the model off the RENDER HANDLE
 * (`bapObj+0x9c` then `rdThing+0x04`), which is the model the body is WEARING. The transform that
 * turns that index into a world position reads the model off the ACTOR (`bapObj+0x14` then
 * `bapActor+0xe0`), which is the model the body was BUILT from. Those two are the same model for
 * everything the game ships, and they are different for exactly as long as a model is borrowed:
 * the swap writes the render handle and never touches the actor, because the actor is a shared
 * refcounted asset and nothing may be written into it.
 *
 * So the index is the borrowed rig's, and the geometry it is looked up in is the player's own. The
 * joint matrix is taken from the render handle and is therefore the right one; what is wrong is the
 * point that matrix is applied to. The engine transforms the bounding box centre of the mesh that
 * hangs on the node, and it takes that centre out of the wrong model, at the same array position.
 * Obi-Wan's node 47 is his left foot; on the rig being worn, node 47 is whatever that artist put
 * there, and on most rigs there is no node 47 at all.
 *
 * Measured over the 94 rows the panel offers, against Obi-Wan: the left foot print lands a median
 * of 0.414 world units from the foot, which is 47 per cent of the worn body's own height, and up to
 * 1.62 units, which is 168 per cent of it. 83 of the 85 rigs that carry a `lfoot` node are out by
 * more than a quarter of their own height. That is the reported symptom in one number.
 *
 * It also reads off the end of an allocation. The transform does not test the mesh index for the
 * -1 the engine writes on a node without geometry, so a name the borrowed rig does not carry (the
 * lookup answers 0, and Obi-Wan's node 0 is `dummy01`) or a hand that lands on his node 10
 * (`weapon`) makes the engine read three floats 112 bytes in front of the mesh array. 9 of the
 * offered rigs carry no `lfoot`, 6 carry no `rfoot`, and 22 put their `lhand` on his `weapon`.
 *
 * What is corrected, and why it is safe. The world position is recomputed from the model the
 * render handle wears, with the same node index, the same joint matrix and the same arithmetic the
 * engine uses. It is the engine's own convention: `bapobj_nodeSphere`, the general node
 * position API that the weapon muzzle goes through, reads the model off the render handle and
 * tests the mesh index, and this makes the footstep module agree with it. Nothing is written except
 * the caller's own output vector, which the engine filled in one instruction earlier.
 *
 * The correction runs only while a model swap is armed AND the two models actually disagree, so a
 * player in his own body reaches the original and nothing else.
 *
 * What is not corrected, and said plainly: the blob shadow's SIZE and the size of the prints
 * themselves. Neither is a swap defect and neither has an answer this module can reach. The
 * shadow is drawn at the body origin, which is right for any worn model, with a radius of the
 * ACTOR's cylinder radius times 1.75 (pActor+0xEC, loaded at 0x004117A2). The worn rig's own
 * radius is in its actor, and nothing this module is handed reaches that: the render handle
 * carries the model, and nothing points back from a model to its actor. The prints are 0.125 for
 * every hero, a literal at 0x0043840F, so their size never followed the body in the first place.
 */
#ifndef CHARACTER_GROUND_H
#define CHARACTER_GROUND_H

#include <stdbool.h>
#include <stdint.h>

/* THE ENGINE'S OWN 3x4 TRANSFORM, and the half a test can drive. The layout is not a guess: the
 * multiplication reads the matrix at 0x00, 0x0c, 0x18 for the first output word and adds 0x24, so
 * the nine rotation floats are three COLUMNS of three and the translation is the last three.
 *
 * False when any input or any output is not a finite number, and then nothing was written. A
 * refusal always means "leave the position the engine computed alone", which is the safe answer:
 * a wrong print is a wrong picture, and a NaN fed back into a decal projector is not. */
bool character_ground_transform(const float matrix[12], const float point[3], float out[3]);

/* The engine's own out of range rule, applied to the right model. A node index at or past the
 * model's node count is CLAMPED TO 0 rather than refused. The engine does that itself, and a
 * correction that refused instead would leave the original's wrong answer standing. A negative
 * index answers 0 for the same reason. */
uint32_t character_ground_node_of(int32_t node_index, uint32_t node_count);

/* Resolves the footstep module's node position entry and puts the correction behind it.
 * Idempotent. False when the site did not resolve, and then a borrowed body leaves its marks where
 * the engine puts them, which is what it did before this module existed. A model swap is still
 * offered in that case: prints in the wrong place are not a crash. */
bool character_ground_install(void);

bool character_ground_is_installed(void);

#endif /* CHARACTER_GROUND_H */
