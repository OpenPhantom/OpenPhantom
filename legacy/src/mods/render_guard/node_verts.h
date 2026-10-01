/* node_verts.h: the x87 stack left one short by every halo drawn.
 *
 * bapobj_getNodeMeshVerts (0x0041378A) copies a node's mesh vertices out and returns nothing on
 * the path that does the copy; its two failure arms, a node index past the count and a model with
 * no vertex table, leave through `fld` of a 0.0f, a float return. Its twin bapobj_setNodeMeshVerts
 * (0x00413866) has the same shape. Every caller of either, halo_draw for each halo it draws and
 * the two blade mesh routines in the player's code, follows the call with `fstp st(0)`, discarding
 * a float the success path never pushed. That is a stack underflow: the x87 stack pointer, TOP in
 * the status word, goes up by one for every halo drawn, with every register still tagged empty.
 * Measured 2026-09-15 with the diagnostics DLL's x87 observer: across halo_drawForThing the
 * pointer moved on every frame the player's halos drew, and across nothing else in the object
 * draw.
 *
 * It ships that way, and in 1999 it went unseen. Here it is the blade drawn out of a hand, on
 * the player on Coruscant and on Obi-Wan in Mos Espa: with the pointer off, a value the engine
 * stores from what it takes to be the top of the stack is the x87's indefinite NaN, and which
 * float takes it depends on the code around the fault, so the beam moved with every rebuild of
 * this project and was never in any of its DLLs.
 *
 * The repair makes the two functions return nothing on every path and the three callers pop
 * nothing: the four `fld` instructions of the failure arms and the three `fstp st(0)` are each
 * replaced by no-ops, through the journal, so a site that does not read as expected puts every
 * earlier write back and the log says which. The return value had no consumer; every caller
 * discarded it.
 */
#ifndef NODE_VERTS_H
#define NODE_VERTS_H

#include "common/patch.h"

#include <stdbool.h>
#include <stdint.h>

/* The two instructions the repair makes no-ops: `fld dword [abs32]`, six bytes, and
 * `fstp st(0)`, two. */
typedef enum node_verts_instruction {
    NODE_VERTS_FLD_M32,
    NODE_VERTS_FSTP_ST0
} node_verts_instruction_t;

/* Replaces the instruction at `at` with no-ops of its length, through `journal`, after checking
 * that its opcode bytes are the ones expected; refused, with nothing written, when they are not.
 * Public for the unit test, which hands it bytes of its own. */
bool node_verts_replace(patch_journal_t *journal, uintptr_t at, node_verts_instruction_t which,
                        const char *what);

/* Reads [render_guard] BalanceNodeVerts, on by default. */
void node_verts_install(void);

#endif /* NODE_VERTS_H */
