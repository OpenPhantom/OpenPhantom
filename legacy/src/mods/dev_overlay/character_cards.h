/* character_cards.h: the four-vertex contract the sabre glow cards stand on, enforced.
 *
 * ==============================================================================================
 * What the engine does
 *
 * Every object that is bound to a thing type is offered six sabre effect records. Each record is
 * made by resolving a node NAME against the object and keeping the answer as a node INDEX; four of
 * the six names are the blade cards, `sabreblad01`, `sabreblad02`, `sabreblade1` and `sabreblade2`.
 * An object whose model does not carry the name gets no record, which is why a stormtrooper has
 * none and a hero has four.
 *
 * Once a record exists, the object carries a flag and every drawn frame runs a pass over it. That
 * pass calls one small routine to fetch the record's node geometry, and that routine copies THE
 * WHOLE MESH of the node into the caller's buffer. Both callers in the image hold exactly four
 * vertices, forty eight bytes, because a blade card is a quad and the routine is only ever pointed
 * at one. The routine itself has no bound: it loops to the mesh's own vertex count.
 *
 * ==============================================================================================
 * What a model swap does to that
 *
 * A record holds a node index, not a node. Rebinding the body to another model leaves the index
 * where it is and replaces everything it addresses, so the index now names a different node of a
 * different skeleton, and that node's mesh is a hand, an arm or a shoulder with tens or hundreds
 * of vertices rather than a card with four. The copy then runs off the end of a forty eight byte
 * buffer, and in the drawn pass that buffer is a STACK LOCAL: the tenth vertex, index nine, lands
 * on the saved frame pointer and the return address, and the function returns into a vertex
 * coordinate.
 *
 * There are two ways the index comes to be wrong and this module does not have to tell them apart:
 * the record was made against the model the body wore before the swap, or it was made after the
 * swap and character_mount answered a blade name with the hand the borrowed weapon hangs on. Both
 * end at the same unbounded copy.
 *
 * ==============================================================================================
 * What this module does
 *
 * Two things, and the first would be enough on its own.
 *
 * THE BOUND. The fetch routine is hooked. When the node it is given carries a mesh of more than
 * four vertices the copy is done here instead, for the four the callers hold, and the routine is
 * then called with a node index its own test refuses, so what the caller gets back is what the
 * engine's refusal leaves. Nothing outside the buffer is written, and a node that really is a card
 * reaches the engine's own code unchanged. This holds whether or not anything else in this feature
 * is armed.
 *
 * THE REGISTRATION. The six offers are bracketed, and while they run character_mount answers with
 * the engine's own result. A borrowed rig then gets no blade card records at all, which is what
 * every actor without blade cards has always got, so the pass over them does not run and no slot of
 * the engine's thirty two is spent on a record that describes nothing.
 *
 * ==============================================================================================
 * What render_guard already does to the same routine
 *
 * Two of its repairs sit on it, in another DLL and each behind its own switch. node_verts takes
 * the float the refusal arms push, and the pop after both calls of the routine, out of the image
 * (BalanceNodeVerts); halo_verts sends the drawn pass's call to a bounded copy of its own
 * (GuardHaloVerts). So whether a caller pops is node_verts' alone, and whether the drawn pass
 * reaches this hook at all is halo_verts'. The bound does not need to know either: its refusal is
 * the engine's own refusal arm, which pushes a float or nothing to match what node_verts left. A
 * value made here would be pushed where no caller pops it any more.
 *
 * The bracket is this module's alone. render_guard bounds the copy; it does not keep a borrowed
 * rig from collecting records for cards it does not carry.
 */
#ifndef CHARACTER_CARDS_H
#define CHARACTER_CARDS_H

#include <stdbool.h>
#include <stdint.h>

/* What both callers of the fetch routine hold. It is not a guess and not a margin: one passes a
 * stack local whose next live local starts forty eight bytes further on, and the other passes a
 * span of the player block whose first output field sits forty eight bytes in. */
#define CHARACTER_CARD_VERTICES  4u

/* ==============================================================================================
 * THE PURE HALF
 * ============================================================================================ */

/* How many vertices of a mesh of `mesh_vertices` may be copied into a card buffer. The whole rule
 * is that this can never exceed CHARACTER_CARD_VERTICES, and a mesh with fewer than that is copied
 * whole, because the engine's own loop does exactly that and the caller reads only what it
 * wrote. */
uint32_t character_cards_copy_count(uint32_t mesh_vertices);

/* Whether a mesh of `mesh_vertices` fits the contract, so a reader can ask the question the hook
 * asks without reading the hook. */
bool character_cards_mesh_fits(uint32_t mesh_vertices);

/* ==============================================================================================
 * THE LIVE HALF
 * ============================================================================================ */

/* Resolves and hooks the two sites. Idempotent, and false leaves the engine untouched.
 *
 * Installed from character_nodemap.c as a rider on the swap rather than as a condition of it: the
 * bound is worth wearing wherever a model can be rebound, and refusing the swap because the bound
 * could not be placed would take a working feature away over a hazard that only bites a hero who
 * carries a lightsaber. */
bool character_cards_install(void);

bool character_cards_is_armed(void);

/* How many times the bound has answered instead of the engine, for the log and for a test that
 * wants to know whether it ever bit. */
uint32_t character_cards_bound_count(void);

#endif /* CHARACTER_CARDS_H */
