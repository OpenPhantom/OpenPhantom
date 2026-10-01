/* character_prop_blade.h: a far player's blade at his own length, without writing a shared mesh.
 *
 * A lit blade is four vertices and nothing else. Plr_SetBladeSize 0x00449C6E copies the four
 * vertices at the player block's +0x1C8 onto its stack, moves the two tip vertices out along the
 * two deltas behind them by the length at +0x210, and hands the 48 bytes to
 * bapobj_setNodeMeshVerts 0x00413866, which writes them into the blade mesh of the model the
 * body's render handle draws. Everything else about a sabre, its hilt, its two glow cards, the
 * sphere a blow is measured from and the place its light sits, is the same at every length.
 *
 * A body in a borrowed model cannot go that way. The mesh that setter writes is the one the
 * body's handle draws, which for such a body is the BORROWED model's, so the multiplayer steps
 * the length in the block and calls no setter at all. The blade that player is shown holding is
 * drawn here instead, on this feature's own handle, out of HIS hero model; and that model's blade
 * mesh is shared with every other body wearing the asset and is written by the engine alone.
 *
 * So nothing is written into it. The four vertices are worked out into a field of the row's own,
 * and for the length of that one draw the mesh's POSITION POINTER at +0x30 names that field. The
 * pointer is put back in the same call. rdMesh_draw 0x0040F1E0 reads it at the head of every call
 * and transforms the vertices out of it into a scratch buffer before it queues anything, so what
 * the draw queue carries is already transformed and holds no pointer into the mesh; and between
 * the exchange and its undoing there is nothing on the stack but that one draw.
 *
 * The player's own row is not exchanged under. His block is ticked by the engine, which hands
 * every length to its own setter, and what keeps that setter off a borrowed rig is the blade
 * guard. A second answer here would be a second answer to a question that already has one.
 */
#ifndef CHARACTER_PROP_BLADE_H
#define CHARACTER_PROP_BLADE_H

#include "character_prop_body.h"

#include <stdbool.h>
#include <stdint.h>

/* The row's handle has just been bound to `reference_model`: find the blade mesh in it, by the
 * name id the engine's own spawn resolves the blade node with. False leaves the row without one,
 * and then no pointer is ever exchanged and the blade is drawn as the shared mesh stands. */
bool character_prop_blade_bind(prop_body_t *body, uintptr_t reference_model, uintptr_t name_table,
                               uint32_t name_id);

/* The row is letting its reference model go, so the mesh in it is forgotten. Called before the
 * asset is handed back, because the pointer is into that asset. */
void character_prop_blade_forget(prop_body_t *body);

/* Around the one draw of the row's own handle, and nowhere else. `open` works the four vertices
 * out of `block` and exchanges the pointer; `close` puts it back and must follow every open on
 * every path, which is why they sit either side of one call and not either side of a function.
 * An open that could not read the block, or could not write the pointer, does not open. */
void character_prop_blade_open(prop_body_t *body, uintptr_t block);
void character_prop_blade_close(prop_body_t *body);

/* What the far blades came to in the level that ended, said once and then counted from zero. */
void character_prop_blade_report(void);

#endif /* CHARACTER_PROP_BLADE_H */
