/* mp_blade_draw.c: a far Jedi's blade drawn from four vertices of its own. See the header.
 *
 * SIZE NOTE: over 600 lines, one subject: no far body writes the shared blade mesh. The mesh
 * lookup, the guard over a spawn and a take down, the watch over a window, the rows, the two hulls
 * and the frame witness share one thing that cannot be split without a second copy of it: the set
 * of fields a mesh may be pointed at. The guard refuses a mesh that points into a row, and the
 * witness puts back a mesh that points into the guard's copy. The seam, if it grows, is the guard
 * and the watch, which would take the lookup with them and ask this file for its fields.
 *
 * The offsets, as the engine reads them. The player block: the actor at +0x00, the object at
 * +0x0C, the blade node at +0x4C, the hero at +0x6C, the blade vectors at +0x1C8 and the length at
 * +0x210. The object: its actor at +0x14 and its render handle at +0x9C. The handle: the model it
 * draws at +0x04. The actor: the model its bind hands the handle at +0xE0. The model: the first
 * geoset's mesh array at +0x28, the node count at +0x54 and the node array at +0x58, 0xB4 bytes a
 * node. The node: its name inline at +0x00, its matrix slot at +0x44 and its mesh index at +0x4C,
 * negative for none. The mesh: 0x70 bytes, the vertex pointer at +0x30, an absolute address the
 * draw reads anew at every call, and the vertex count at +0x48.
 *
 * Whatever a row names is read again at every use. The overlay can dress a body a substep before
 * this side hears of it, and a level restart or a savegame keeps the far bodies standing, so the
 * memory a row was filled from may have been given to something else since.
 */
#include "mp_blade_draw.h"

#include "mp_bank.h"
#include "mp_blade_rule.h"
#include "mp_cells.h"
#include "mp_signatures.h"
#include "mp_twist.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(void *) == 4,
               "the vertex pointer exchanged here is a 32 bit field of an engine mesh");

#define BLOCK_ACTOR      0x00u
#define BLOCK_OBJECT     0x0Cu
#define BLOCK_NODE       0x4Cu
#define BLOCK_HERO       0x6Cu
#define BLOCK_BLADE      0x1C8u
#define BLOCK_SIZE       0x210u
#define OBJECT_ACTOR     0x14u
#define OBJECT_THING     0x9Cu
#define THING_MODEL      0x04u
#define ACTOR_MODEL      0xE0u
#define MODEL_MESHES     0x28u
#define MODEL_NODE_COUNT 0x54u
#define MODEL_NODES      0x58u
#define NODE_BYTES       0xB4u
#define NODE_SLOT        0x44u
#define NODE_MESH        0x4Cu
#define MESH_BYTES       0x70u
#define MESH_VERTS       0x30u
#define MESH_COUNT       0x48u

/* The node count word carries two bits the animation path never reads. The widest shipped
 * skeleton is 48 nodes, so a count past 256 is no skeleton. */
#define NODE_COUNT_MASK 0x3FFFFFFFu
#define NODES_MAX       256u

/* A node's name as the engine's own lookup compares it, and the one the spawn asks for. */
#define NODE_NAME_BYTES 0x40u
#define BLADE_NODE_NAME "sabreblad01"

/* A blade mesh is four vertices, and the setter's own buffer is the 0x30 bytes of them. */
#define BLADE_VERTS 4u
#define BLADE_BYTES (MP_BLADE_VERT_FLOATS * sizeof(float))
_Static_assert(MP_BLADE_VERT_FLOATS == 3u * BLADE_VERTS, "four vertices of three floats each");

/* One far bank's body, as far as its blade is drawn from vertices of its own. */
typedef struct blade_row {
    bool      filled;
    bool      open;      /* the mesh points at `verts` for the engine call under way */
    bool      off;       /* a pointer of this row would not go back: it opens no more */
    uint32_t  object;
    uint32_t  thing;
    uint32_t  model;     /* the hero's model, the one the body's actor binds */
    uintptr_t mesh;      /* the mesh the last open pointed at `verts` */
    uint32_t  saved;     /* what that mesh pointed at before */
    float     verts[MP_BLADE_VERT_FLOATS];
} blade_row_t;

/* A mesh's pointer and its four vertices before a window, to be held against after it. */
typedef struct blade_watch {
    bool              open;
    size_t            bank;
    mp_blade_window_t kind;
    uintptr_t         mesh;
    uint32_t          pointer;
    float             verts[MP_BLADE_VERT_FLOATS];
} blade_watch_t;

/* A mesh pointed at a copy of itself for one spawn or take down. A copy that would not go back
 * keeps its saved pointer and holds every later guard off until the frame witness has put it back:
 * a second guard would overwrite the one pointer that can repair the first. */
typedef struct blade_guard {
    bool          open;
    bool          stuck;
    uint32_t      saved;
    blade_watch_t watch;
    float         copy[MP_BLADE_VERT_FLOATS];
} blade_guard_t;

static struct {
    blade_row_t              rows[MP_BANK_FAR_MAX];
    size_t                   filled;
    blade_watch_t            window;
    blade_guard_t            guard;
    detour_t                 dispatch;
    detour_t                 halo;
    mp_blade_dispatch_fn_t   dispatch_original;
    mp_blade_halo_fn_t       halo_original;
    bool                     install_tried;
    bool                     any_length;
    bool                     stuck_said;
    bool                     unguarded_said;
    bool                     changed_said;
    bool                     no_row_said;
    mp_blade_draw_counters_t counters;
} draw;

static bool read_u32(uintptr_t address, uint32_t *out)
{
    return address != 0u && memory_try_read(address, out, sizeof *out);
}

static blade_row_t *row_of(size_t bank)
{
    return mp_bank_index_ok(bank) ? &draw.rows[bank - 1u] : NULL;
}

static const char *window_name(mp_blade_window_t kind)
{
    if (kind == MP_BLADE_WINDOW_SPAWN) {
        return "spawn";
    }
    return kind == MP_BLADE_WINDOW_DESPAWN ? "despawn" : "puppet run";
}

/* The model the body of bank `bank` draws: [[[block+0x0C]+0x9C]+0x04]. */
static bool drawn_model(size_t bank, uint32_t *model)
{
    uint32_t object = 0u;
    uint32_t thing = 0u;

    return read_u32(mp_bank_block_at(bank) + BLOCK_OBJECT, &object) &&
           read_u32(object + OBJECT_THING, &thing) && read_u32(thing + THING_MODEL, model) &&
           *model != 0u;
}

/* ==============================================================================================
 * The mesh.
 * ============================================================================================ */

/* The node the lookup found answers its matrix slot, and the vertex getter and setter index the
 * node array with that slot. So the mesh is the slot's node's, which is the named node itself on
 * every shipped rig. */
static uintptr_t mesh_of_node(uint32_t model, uint32_t nodes, uint32_t count, uintptr_t named,
                              uint32_t *verts)
{
    int32_t   slot = -1;
    int32_t   index = -1;
    uint32_t  meshes = 0u;
    uintptr_t mesh;

    if (!memory_try_read(named + NODE_SLOT, &slot, sizeof slot) || slot < 0 ||
        (uint32_t)slot >= count ||
        !memory_try_read((uintptr_t)nodes + (uintptr_t)slot * NODE_BYTES + NODE_MESH, &index,
                         sizeof index) ||
        index < 0 || !read_u32((uintptr_t)model + MODEL_MESHES, &meshes) || meshes == 0u) {
        return 0u;
    }
    mesh = (uintptr_t)meshes + (uintptr_t)index * MESH_BYTES;
    return read_u32(mesh + MESH_COUNT, verts) ? mesh : 0u;
}

uintptr_t mp_blade_draw_mesh_of(uint32_t model, uint32_t *verts)
{
    uint32_t count = 0u;
    uint32_t nodes = 0u;
    uint32_t found = 0u;
    uint32_t i;
    char     name[NODE_NAME_BYTES];

    if (verts != NULL) {
        *verts = 0u;
    }
    if (model == 0u || !read_u32((uintptr_t)model + MODEL_NODE_COUNT, &count) ||
        !read_u32((uintptr_t)model + MODEL_NODES, &nodes) || nodes == 0u) {
        return 0u;
    }
    count &= NODE_COUNT_MASK;
    for (i = 0u; i < count && i < NODES_MAX; ++i) {
        uintptr_t node = (uintptr_t)nodes + (uintptr_t)i * NODE_BYTES;
        uintptr_t mesh;

        if (!memory_try_read(node, name, sizeof name)) {
            return 0u;
        }
        name[sizeof name - 1u] = '\0';
        if (strcmp(name, BLADE_NODE_NAME) != 0) {
            continue;
        }
        mesh = mesh_of_node(model, nodes, count, node, &found);
        if (mesh != 0u && verts != NULL) {
            *verts = found;
        }
        return mesh;   /* the first node of that name, as the engine's lookup takes it */
    }
    return 0u;
}

/* The saved pointer of the field `pointer` names, when it names one of this module's. */
static bool owner_of(uint32_t pointer, uint32_t *saved)
{
    size_t i;

    for (i = 0u; i < MP_BANK_FAR_MAX; ++i) {
        if (pointer == (uint32_t)(uintptr_t)draw.rows[i].verts) {
            *saved = draw.rows[i].saved;
            return true;
        }
    }
    if (pointer == (uint32_t)(uintptr_t)draw.guard.copy) {
        *saved = draw.guard.saved;
        return true;
    }
    return false;
}

static void note_stuck(const char *what, size_t bank)
{
    ++draw.counters.stuck;
    if (!draw.stuck_said) {
        draw.stuck_said = true;
        log_warning("a far body's blade pointer could not be put back into the mesh it was "
                    "borrowed from (%s, bank %u); every body of that asset draws from the borrowed "
                    "field until the frame witness puts it back", what, (unsigned)bank);
    }
}

/* ==============================================================================================
 * The watch over a window.
 * ============================================================================================ */

static bool watch_open(blade_watch_t *watch, size_t bank, mp_blade_window_t kind, uintptr_t mesh)
{
    memset(watch, 0, sizeof *watch);
    if (!read_u32(mesh + MESH_VERTS, &watch->pointer) || watch->pointer == 0u ||
        !memory_try_read(watch->pointer, watch->verts, BLADE_BYTES)) {
        return false;
    }
    watch->open = true;
    watch->bank = bank;
    watch->kind = kind;
    watch->mesh = mesh;
    return true;
}

/* A mesh that no longer reads is counted as changed: whatever happened to it, it is not what the
 * window found. */
static void watch_close(blade_watch_t *watch)
{
    uint32_t pointer = 0u;
    float    verts[MP_BLADE_VERT_FLOATS];

    if (!watch->open) {
        return;
    }
    watch->open = false;
    ++draw.counters.watched[watch->kind];
    if (read_u32(watch->mesh + MESH_VERTS, &pointer) && pointer == watch->pointer &&
        memory_try_read(pointer, verts, sizeof verts) &&
        memcmp(verts, watch->verts, sizeof verts) == 0) {
        return;
    }
    ++draw.counters.changed[watch->kind];
    if (!draw.changed_said) {
        draw.changed_said = true;
        log_warning("the blade mesh %08X changed inside bank %u's %s; later cases are counted",
                    (unsigned)watch->mesh, (unsigned)watch->bank, window_name(watch->kind));
    }
}

void mp_blade_draw_window_open(size_t bank, mp_blade_window_t kind)
{
    uint32_t  model = 0u;
    uint32_t  verts = 0u;
    uintptr_t mesh;

    draw.window.open = false;
    if (!drawn_model(bank, &model)) {
        return;
    }
    mesh = mp_blade_draw_mesh_of(model, &verts);
    if (mesh != 0u && verts == BLADE_VERTS) {
        (void)watch_open(&draw.window, bank, kind, mesh);
    }
}

void mp_blade_draw_window_close(void)
{
    watch_close(&draw.window);
}

/* ==============================================================================================
 * The guard over a spawn and a take down.
 * ============================================================================================ */

static void note_unguarded(size_t bank, mp_blade_window_t kind, const char *why)
{
    ++draw.counters.unguarded;
    if (!draw.unguarded_said) {
        draw.unguarded_said = true;
        log_warning("bank %u's %s runs on the shared blade mesh: %s; later cases are counted",
                    (unsigned)bank, window_name(kind), why);
    }
}

static void guard_open(size_t bank, mp_blade_window_t kind, uint32_t model)
{
    blade_guard_t *guard = &draw.guard;
    uint32_t       verts = 0u;
    uint32_t       ignored = 0u;
    uint32_t       field;
    uintptr_t      mesh = mp_blade_draw_mesh_of(model, &verts);

    if (guard->stuck) {
        note_unguarded(bank, kind, "an earlier copy has not gone back yet");
        return;
    }
    if (mesh == 0u || verts != BLADE_VERTS) {
        note_unguarded(bank, kind, "its model carries no blade mesh of four vertices");
        return;
    }
    if (!watch_open(&guard->watch, bank, kind, mesh) || owner_of(guard->watch.pointer, &ignored)) {
        guard->watch.open = false;
        note_unguarded(bank, kind, "its blade mesh did not read, or points at a field of ours");
        return;
    }
    memcpy(guard->copy, guard->watch.verts, sizeof guard->copy);
    guard->saved = guard->watch.pointer;
    field = (uint32_t)(uintptr_t)guard->copy;
    if (!memory_try_write(mesh + MESH_VERTS, &field, sizeof field)) {
        guard->watch.open = false;
        note_unguarded(bank, kind, "its blade mesh would not take a copy");
        return;
    }
    guard->open = true;
    ++draw.counters.guarded;
}

void mp_blade_draw_guard_spawn_over(size_t bank, int32_t slot, int32_t local_hero,
                                    const void *probed, uintptr_t local_block)
{
    uint32_t actor = 0u;
    uint32_t model = 0u;

    mp_blade_draw_guard_close();
    if (slot < 0 || !mp_blade_rule_jedi((uint32_t)slot)) {
        return;   /* the spawn reads a blade only for hero 0 or 1, and folds nothing else */
    }
    if (slot == local_hero) {
        (void)read_u32(local_block + BLOCK_ACTOR, &actor);
    } else {
        actor = (uint32_t)(uintptr_t)probed;
    }
    if (actor == 0u || !read_u32((uintptr_t)actor + ACTOR_MODEL, &model) || model == 0u) {
        note_unguarded(bank, MP_BLADE_WINDOW_SPAWN, "its model did not read");
        return;
    }
    guard_open(bank, MP_BLADE_WINDOW_SPAWN, model);
}

void mp_blade_draw_guard_spawn(size_t bank, int32_t slot, int32_t local_hero, const void *probed)
{
    mp_blade_draw_guard_spawn_over(bank, slot, local_hero, probed,
                                   mp_cells_address(MP_CELL_HERO_BLOCK));
}

void mp_blade_draw_guard_despawn(size_t bank)
{
    uintptr_t block = mp_bank_block_at(bank);
    uint32_t  hero = MP_BLADE_NO_HERO;
    uint32_t  node = 0u;
    uint32_t  model = 0u;

    mp_blade_draw_guard_close();
    if (!read_u32(block + BLOCK_HERO, &hero) || !mp_blade_rule_jedi(hero)) {
        return;   /* the take down parks a blade only for hero 0 or 1 */
    }
    if (!read_u32(block + BLOCK_NODE, &node) || node == 0u) {
        note_unguarded(bank, MP_BLADE_WINDOW_DESPAWN, "its record names no blade node");
        return;
    }
    if (!drawn_model(bank, &model)) {
        note_unguarded(bank, MP_BLADE_WINDOW_DESPAWN, "its model did not read");
        return;
    }
    guard_open(bank, MP_BLADE_WINDOW_DESPAWN, model);
}

/* The take down releases the body's reference to its asset, and when that was the last one the
 * model may go with it. So the word is written back only while it still names the copy: a word
 * that names anything else is no longer ours to write, and its mesh no longer ours to watch. */
void mp_blade_draw_guard_close(void)
{
    blade_guard_t *guard = &draw.guard;
    uint32_t       pointer = 0u;

    if (!guard->open) {
        return;
    }
    guard->open = false;
    if (!read_u32(guard->watch.mesh + MESH_VERTS, &pointer) ||
        pointer != (uint32_t)(uintptr_t)guard->copy) {
        guard->watch.open = false;
        return;
    }
    if (!memory_try_write(guard->watch.mesh + MESH_VERTS, &guard->saved, sizeof guard->saved)) {
        guard->watch.open = false;
        guard->stuck      = true;
        note_stuck(window_name(guard->watch.kind), guard->watch.bank);
        return;
    }
    watch_close(&guard->watch);
}

/* ==============================================================================================
 * The rows and the two hulls.
 * ============================================================================================ */

/* The row's mesh, when the row's body is what the row says: the bank's block still names its
 * object, the object its render handle, the handle draws the hero's model, and the body is a Jedi
 * whose blade mesh has four vertices. The mesh is found again from the model every time. */
static bool live_mesh(size_t bank, const blade_row_t *row, uintptr_t *mesh)
{
    uintptr_t block = mp_bank_block_at(bank);
    uint32_t  object = 0u;
    uint32_t  thing = 0u;
    uint32_t  model = 0u;
    uint32_t  hero = MP_BLADE_NO_HERO;
    uint32_t  verts = 0u;

    if (!row->filled || !read_u32(block + BLOCK_OBJECT, &object) || object != row->object ||
        !read_u32(object + OBJECT_THING, &thing) || thing != row->thing ||
        !read_u32(thing + THING_MODEL, &model) || !read_u32(block + BLOCK_HERO, &hero)) {
        return false;
    }
    *mesh = mp_blade_draw_mesh_of(row->model, &verts);
    return *mesh != 0u &&
           mp_blade_rule_draws_own(model != row->model, mp_blade_rule_jedi(hero), verts);
}

static void note_length(float size)
{
    if (!draw.any_length || size < draw.counters.shortest) {
        draw.counters.shortest = size;
    }
    if (!draw.any_length || size > draw.counters.longest) {
        draw.counters.longest = size;
    }
    draw.any_length = true;
}

static bool open_row(size_t bank, blade_row_t *row)
{
    mp_blade_vectors_t vectors;
    uintptr_t          block = mp_bank_block_at(bank);
    uintptr_t          mesh = 0u;
    uint32_t           saved = 0u;
    uint32_t           ignored = 0u;
    uint32_t           field;
    float              size = 0.0f;

    if (!live_mesh(bank, row, &mesh) ||
        !memory_try_read(block + BLOCK_BLADE, &vectors, sizeof vectors) ||
        !memory_try_read(block + BLOCK_SIZE, &size, sizeof size) ||
        !mp_blade_rule_verts(&vectors, size, row->verts) ||
        !read_u32(mesh + MESH_VERTS, &saved) || saved == 0u || owner_of(saved, &ignored)) {
        ++draw.counters.refused;
        return false;
    }
    field = (uint32_t)(uintptr_t)row->verts;
    if (!memory_try_write(mesh + MESH_VERTS, &field, sizeof field)) {
        ++draw.counters.refused;
        return false;
    }
    row->open  = true;
    row->mesh  = mesh;
    row->saved = saved;
    note_length(size);
    return true;
}

static void close_row(size_t bank, blade_row_t *row, const char *what)
{
    if (!row->open) {
        return;
    }
    row->open = false;
    if (!memory_try_write(row->mesh + MESH_VERTS, &row->saved, sizeof row->saved)) {
        row->off = true;
        note_stuck(what, bank);
    }
}

/* A row left open is put back only while its mesh still names the row's own field: after a take
 * down the model may have been released, and a word that no longer holds our address is no
 * longer ours to write. */
static bool put_back_if_ours(blade_row_t *row)
{
    uint32_t pointer = 0u;

    row->open = false;
    return read_u32(row->mesh + MESH_VERTS, &pointer) &&
           pointer == (uint32_t)(uintptr_t)row->verts &&
           memory_try_write(row->mesh + MESH_VERTS, &row->saved, sizeof row->saved);
}

/* The bank whose row names `thing` or `object`, 0 for none. */
static size_t bank_of(uint32_t thing, uint32_t object)
{
    size_t bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        const blade_row_t *row = row_of(bank);

        if (row->filled && ((thing != 0u && row->thing == thing) ||
                            (object != 0u && row->object == object))) {
            return bank;
        }
    }
    return 0u;
}

/* Whether the row of `bank` opens for this call: a row already open is a call that came back in
 * through its own draw and is left to the outer one, and a row that is off is refused. */
static bool opens(size_t bank)
{
    blade_row_t *row = row_of(bank);

    if (row == NULL || row->open) {
        return false;
    }
    if (row->off) {
        ++draw.counters.refused;
        return false;
    }
    return open_row(bank, row);
}

int32_t mp_blade_draw_dispatch(uint32_t thing, uint32_t pose, mp_blade_dispatch_fn_t original)
{
    size_t  bank;
    int32_t answer;

    if (draw.filled == 0u) {
        return original(thing, pose);
    }
    bank = bank_of(thing, 0u);
    if (bank == 0u || !opens(bank)) {
        return original(thing, pose);
    }
    ++draw.counters.draws;
    answer = original(thing, pose);
    close_row(bank, row_of(bank), "the handle draw");
    return answer;
}

void mp_blade_draw_halo(uint32_t object, mp_blade_halo_fn_t original)
{
    size_t bank;

    if (draw.filled == 0u) {
        original(object);
        return;
    }
    bank = bank_of(0u, object);
    if (bank == 0u || !opens(bank)) {
        original(object);
        return;
    }
    ++draw.counters.halos;
    original(object);
    close_row(bank, row_of(bank), "the halo pass");
}

/* The handle draw, for every object and mirror image the engine draws. It answers the handle's
 * visibility, and the glow card pass behind it runs only on an answer that is not 0.
 * engine: int bapthing_dispatch(rdThing *pThing, f32 *pPose) */
static int32_t __cdecl hook_thing_dispatch(uint32_t thing, uint32_t pose)
{
    if (draw.filled == 0u) {
        return draw.dispatch_original(thing, pose);
    }
    return mp_blade_draw_dispatch(thing, pose, draw.dispatch_original);
}

/* Every glow card an object owns, each drawn between two vertices of its node's mesh.
 * engine: void halo_drawForThing(bapObj *obj) */
static void __cdecl hook_halo_draw_for_thing(uint32_t object)
{
    if (draw.filled == 0u) {
        draw.halo_original(object);
        return;
    }
    mp_blade_draw_halo(object, draw.halo_original);
}

/* The first far Jedi places both hulls. The halo pass is placed only behind the handle draw: a
 * glow card at a far body's own length over a core at the shared one would be two lengths on one
 * blade. */
static void install_hulls(void)
{
    uintptr_t           dispatch = mp_signatures_address(MP_SITE_THING_DISPATCH);
    uintptr_t           halo = mp_signatures_address(MP_SITE_HALO_DRAW_FOR_THING);
    mp_twist_counters_t twist;

    if (draw.install_tried) {
        return;
    }
    draw.install_tried = true;
    if (dispatch == 0u ||
        !detour_install(&draw.dispatch, dispatch, (const void *)hook_thing_dispatch,
                        mp_signatures_prologue(MP_SITE_THING_DISPATCH))) {
        log_warning("the far blades are drawn from the shared mesh: the handle draw %s, so a far "
                    "Jedi shows the length the local player's blade gives that asset",
                    dispatch == 0u ? "did not resolve" : "could not be detoured");
        return;
    }
    draw.dispatch_original = (mp_blade_dispatch_fn_t)draw.dispatch.original;
    if (halo == 0u ||
        !detour_install(&draw.halo, halo, (const void *)hook_halo_draw_for_thing,
                        mp_signatures_prologue(MP_SITE_HALO_DRAW_FOR_THING))) {
        log_warning("the far blades' halo pass %s, so a far Jedi's core has a length of its own "
                    "and its glow card the shared one",
                    halo == 0u ? "did not resolve" : "could not be detoured");
        halo = 0u;
    } else {
        draw.halo_original = (mp_blade_halo_fn_t)draw.halo.original;
    }
    mp_twist_get_counters(&twist);
    log_info("the far blades are drawn from their own vertices: the handle draw is hooked at %08X "
             "and the halo pass at %08X; %s", (unsigned)dispatch, (unsigned)halo,
             twist.installed ? "the frame witness watches every borrowed pointer at the head of "
                               "the world draw"
                             : "no frame witness runs, because the world draw is not hooked, so a "
                               "pointer an interrupted draw leaves behind stays until that body is "
                               "taken down");
}

static void note_no_row(size_t bank, const char *why)
{
    if (!draw.no_row_said) {
        draw.no_row_said = true;
        log_warning("bank %u's blade has no row of its own: %s, so it is drawn as the shared mesh "
                    "stands; later cases are not said", (unsigned)bank, why);
    }
}

void mp_blade_draw_fill(size_t bank)
{
    blade_row_t *row = row_of(bank);
    uintptr_t    block = mp_bank_block_at(bank);
    uintptr_t    mesh;
    uint32_t     hero = MP_BLADE_NO_HERO;
    uint32_t     object = 0u;
    uint32_t     thing = 0u;
    uint32_t     actor = 0u;
    uint32_t     model = 0u;
    uint32_t     verts = 0u;
    uint32_t     local_actor = 0u;
    uint32_t     local = 0u;

    if (row == NULL) {
        return;
    }
    mp_blade_draw_forget(bank);
    if (!read_u32(block + BLOCK_HERO, &hero) || !mp_blade_rule_jedi(hero)) {
        return;   /* no Jedi, no blade of its own to draw */
    }
    if (!read_u32(block + BLOCK_OBJECT, &object) || !read_u32(object + OBJECT_THING, &thing) ||
        !read_u32(object + OBJECT_ACTOR, &actor) || !read_u32(actor + ACTOR_MODEL, &model) ||
        thing == 0u || model == 0u) {
        note_no_row(bank, "its body did not read");
        return;
    }
    mesh = mp_blade_draw_mesh_of(model, &verts);
    if (mesh == 0u || !mp_blade_rule_draws_own(false, true, verts)) {
        note_no_row(bank, "its model carries no blade mesh of four vertices");
        return;
    }
    row->filled = true;
    row->object = object;
    row->thing  = thing;
    row->model  = model;
    ++draw.filled;
    if (read_u32(mp_cells_address(MP_CELL_HERO_BLOCK) + BLOCK_ACTOR, &local_actor) &&
        !read_u32((uintptr_t)local_actor + ACTOR_MODEL, &local)) {
        local = 0u;
    }
    log_info("bank %u's blade is drawn from its own four vertices: mesh %08X, %s", (unsigned)bank,
             (unsigned)mesh,
             local == model ? "shared with the local player" : "shared with no local body");
    install_hulls();
}

void mp_blade_draw_forget(size_t bank)
{
    blade_row_t *row = row_of(bank);

    if (row == NULL || !row->filled) {
        return;
    }
    if (row->open && !put_back_if_ours(row)) {
        note_stuck("the take down", bank);
    }
    memset(row, 0, sizeof *row);
    --draw.filled;
}

/* A row open at the head of the world draw is a draw that never came back, and a mesh that points
 * at one of this module's fields outside every draw is the same fault seen from the mesh. Either
 * is put back, and only for a row that passes the live check. */
void mp_blade_draw_witness(void)
{
    size_t bank;
    bool   stray = false;

    if (draw.filled == 0u) {
        return;
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        blade_row_t *row = row_of(bank);
        uintptr_t    mesh = 0u;
        uint32_t     pointer = 0u;
        uint32_t     saved = 0u;

        if (!row->filled || !live_mesh(bank, row, &mesh)) {
            continue;
        }
        if (row->open) {
            stray = true;
            if (row->mesh == mesh && put_back_if_ours(row)) {
                ++draw.counters.put_back;
            } else {
                row->off = true;
                note_stuck("the frame witness", bank);
            }
            continue;
        }
        if (!read_u32(mesh + MESH_VERTS, &pointer) || !owner_of(pointer, &saved)) {
            continue;
        }
        stray = true;
        if (saved != 0u && memory_try_write(mesh + MESH_VERTS, &saved, sizeof saved)) {
            ++draw.counters.put_back;
            if (pointer == (uint32_t)(uintptr_t)draw.guard.copy) {
                draw.guard.stuck = false;   /* the one pointer that could repair it has done so */
            }
        } else {
            note_stuck("the frame witness", bank);
        }
    }
    if (stray) {
        ++draw.counters.stray_frames;
    }
}

void mp_blade_draw_get_counters(mp_blade_draw_counters_t *out)
{
    if (out != NULL) {
        *out = draw.counters;
    }
}

void mp_blade_draw_report(uint32_t steps, uint32_t step_faults)
{
    mp_blade_draw_counters_t c;
    uint32_t                 watched = 0u;
    uint32_t                 changed = 0u;
    int                      kind;

    mp_blade_draw_get_counters(&c);
    for (kind = 0; kind < (int)MP_BLADE_WINDOW_KINDS; ++kind) {
        watched += c.watched[kind];
        changed += c.changed[kind];
    }
    log_info("  the blade mesh a far body draws or builds, inside its windows: %u window(s) "
             "watched (%u puppet run(s), %u spawn(s), %u despawn(s)), %u changed it (%u in a "
             "puppet run, %u in a spawn, %u in a despawn)",
             (unsigned)watched, (unsigned)c.watched[MP_BLADE_WINDOW_PUPPET],
             (unsigned)c.watched[MP_BLADE_WINDOW_SPAWN],
             (unsigned)c.watched[MP_BLADE_WINDOW_DESPAWN], (unsigned)changed,
             (unsigned)c.changed[MP_BLADE_WINDOW_PUPPET],
             (unsigned)c.changed[MP_BLADE_WINDOW_SPAWN],
             (unsigned)c.changed[MP_BLADE_WINDOW_DESPAWN]);
    log_info("  the far blades, own vertices: %u draw(s) and %u halo pass(es) at a length of their "
             "own, %d to %d thousandths, %u refused by the live check or a read; %u substep(s) "
             "stepped in the block (%u refused); %u spawn and despawn write(s) kept off the shared "
             "mesh, %u without a mesh to guard; %u frame(s) found a mesh pointing at a far field "
             "outside a draw, %u put back; %u pointer(s) that would not go back",
             (unsigned)c.draws, (unsigned)c.halos, (int)(c.shortest * 1000.0f),
             (int)(c.longest * 1000.0f), (unsigned)c.refused, (unsigned)steps,
             (unsigned)step_faults, (unsigned)c.guarded, (unsigned)c.unguarded,
             (unsigned)c.stray_frames, (unsigned)c.put_back, (unsigned)c.stuck);
}
