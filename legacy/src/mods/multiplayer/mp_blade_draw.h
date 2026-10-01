/* mp_blade_draw.h: a far Jedi's blade drawn from four vertices of its own, and the shared blade
 * mesh kept off every far body's writes.
 *
 * The blade is the node sabreblad01 of the hero's model, and a model is the asset's: every body
 * wearing that asset draws the same four vertices, the local player's among them. The engine's
 * length setter writes a new length into those vertices, so whichever body wrote last decided the
 * length every body showed, and a far body's spawn folded the local player's blade to its hilt.
 *
 * So no far body writes the mesh. Its length moves in its own block (mp_body_wear), and it is
 * drawn by pointing the mesh's vertex pointer at twelve floats worked out of that block for the
 * length of exactly one engine call, the handle draw or the glow card pass, and putting it back in
 * the same call. This is a blend shape with one weight per instance, evaluated at the draw: the
 * hilt and the two deltas are the asset's, the length is the body's.
 *
 * The spawn and the take down still run the engine's own code, which reads and folds the mesh the
 * body binds or parks it at full length. Each of those two calls is given a copy of the mesh to
 * write instead, by the same pointer, for the length of that one call.
 *
 * Every write a far body's window could make to the mesh is watched: the four vertices before and
 * after each puppet window, spawn and take down. The pointer is watched once per frame at the head
 * of the world draw, where no draw is open, so a draw that never came back cannot leave the asset
 * pointing at a far body's field unseen.
 *
 * Nothing here runs without a far Jedi. The two hulls are placed at the first far Jedi's spawn
 * and stay for the life of the process, as every detour here does; their first statement and the
 * frame witness's is "no row, then through", and the rows are emptied where a far body is taken
 * down.
 */
#ifndef MULTIPLAYER_MP_BLADE_DRAW_H
#define MULTIPLAYER_MP_BLADE_DRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Which kind of far body window a watch was held over. */
typedef enum mp_blade_window {
    MP_BLADE_WINDOW_PUPPET,    /* a puppet's window, once per substep */
    MP_BLADE_WINDOW_SPAWN,     /* the engine's spawn of a far body */
    MP_BLADE_WINDOW_DESPAWN,   /* the engine's take down of one */
    MP_BLADE_WINDOW_KINDS
} mp_blade_window_t;

/* The one way to a model's blade mesh, the way the engine's own lookup goes: the first node named
 * sabreblad01 in the model's node array, the node its matrix slot names, and that node's mesh in
 * the first geoset. 0 when the model has no such node or mesh or a read fails. `verts` gets the
 * mesh's vertex count, 0 with no mesh; a blade the setter writes has four. */
uintptr_t mp_blade_draw_mesh_of(uint32_t model, uint32_t *verts);

/* Far bank `bank`'s body has just been spawned: a Jedi in a model with a blade mesh of four
 * vertices gets its row, and the first such row places the two hulls. Any other body has none. */
void mp_blade_draw_fill(size_t bank);

/* Far bank `bank`'s body has been taken down. A row that is somehow still open is closed first. */
void mp_blade_draw_forget(size_t bank);

/* The spawn of far bank `bank` as hero slot `slot` is about to run, with the local player on
 * `local_hero` and, for any other slot, `probed` the actor the spawn will bind. For a Jedi slot
 * the model's blade mesh is pointed at a copy of itself until mp_blade_draw_guard_close, so the
 * spawn's capture reads the mesh as it stands and folds the copy. The model is the local player's
 * actor's for his own slot and the probed actor's otherwise. */
void mp_blade_draw_guard_spawn(size_t bank, int32_t slot, int32_t local_hero, const void *probed);

/* The same with the local player's hero block named, which the call above reads from its cell
 * and a test makes up. */
void mp_blade_draw_guard_spawn_over(size_t bank, int32_t slot, int32_t local_hero,
                                    const void *probed, uintptr_t local_block);

/* The take down of far bank `bank` is about to run inside its window. The engine parks a Jedi's
 * blade at full length on the mesh of the model the body draws, through the blade node its record
 * holds; that mesh is given a copy the same way. A record with no blade node is left alone: the
 * setter never reaches the mesh through node 0. */
void mp_blade_draw_guard_despawn(size_t bank);

/* The one engine call above has returned: the mesh gets its own vertices back, and the watch over
 * the call is read. Safe to call with no guard open. */
void mp_blade_draw_guard_close(void);

/* A puppet window of far bank `bank` is about to open and has just closed: the blade mesh of the
 * model its body draws is held against itself across the window. */
void mp_blade_draw_window_open(size_t bank, mp_blade_window_t kind);
void mp_blade_draw_window_close(void);

/* The two hulls' bodies, with the engine call handed in so that a test can stand in for it. The
 * handle draw answers what the original answered. A call for any render handle or object that is
 * no far Jedi's in its hero's own model goes straight through. */
typedef int32_t(__cdecl *mp_blade_dispatch_fn_t)(uint32_t thing, uint32_t pose);
typedef void(__cdecl *mp_blade_halo_fn_t)(uint32_t object);
int32_t mp_blade_draw_dispatch(uint32_t thing, uint32_t pose, mp_blade_dispatch_fn_t original);
void mp_blade_draw_halo(uint32_t object, mp_blade_halo_fn_t original);

/* Once per frame at the head of the world draw, where no draw is open: a row left open is closed,
 * and a mesh found pointing at a field of this module is put back. Called by the world draw's
 * hull, which exists for the puppet's node rotations; with that hull missing nothing watches, and
 * the line that says the hulls are placed says so. */
void mp_blade_draw_witness(void);

typedef struct mp_blade_draw_counters {
    uint32_t watched[MP_BLADE_WINDOW_KINDS];   /* windows whose mesh was held against itself */
    uint32_t changed[MP_BLADE_WINDOW_KINDS];   /* and those it came out of changed */
    uint32_t draws;          /* handle draws at a length of the body's own */
    uint32_t halos;          /* glow card passes at a length of the body's own */
    uint32_t refused;        /* calls for a row's body that the live check or a read turned away,
                              * a row that is off included */
    uint32_t guarded;        /* spawn and take down calls given a copy of the mesh */
    uint32_t unguarded;      /* Jedi spawns and take downs with no mesh to give a copy of */
    uint32_t stray_frames;   /* frames whose witness found a mesh pointing at a field of ours */
    uint32_t put_back;       /* pointers the witness put back */
    uint32_t stuck;          /* pointers that would not go back */
    float    shortest;       /* the lengths drawn at, in 0..1 */
    float    longest;
} mp_blade_draw_counters_t;

void mp_blade_draw_get_counters(mp_blade_draw_counters_t *out);

/* The two report lines, with the far Jedi's length steps counted where they are taken. */
void mp_blade_draw_report(uint32_t steps, uint32_t step_faults);

#endif /* MULTIPLAYER_MP_BLADE_DRAW_H */
