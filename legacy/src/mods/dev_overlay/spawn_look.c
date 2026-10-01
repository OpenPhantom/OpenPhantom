/* spawn_look.c: see spawn_look.h. */
#include "spawn_look.h"

#include "actor_loader.h"
#include "cheats_internal.h"
#include "entity_names.h"
#include "floor_probe.h"
#include "npc_census.h"
#include "npc_foreign.h"
#include "npc_spawn_link.h"
#include "npc_spawn_node.h"
#include "npc_spawn_record.h"
#include "npc_spawn_save.h"
#include "npc_spawner.h"
#include "overlay_input.h"
#include "player_slot.h"
#include "spawn_ghost.h"
#include "spawn_scripts.h"
#include "world_camera.h"
#include "world_probe.h"

#include "common/memory.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The live actor: its key, 256 + k for a copy; its body; its position, at the feet. */
#define ACTOR_KEY       0x18u
#define ACTOR_BODY      0x34u
#define ACTOR_POSITION  0xD0u
/* The body's collision cylinder, copied unscaled from its actor file by bapobj_bindActor. */
#define BODY_RADIUS     0xB8u
#define BODY_HEIGHT     0xBCu
/* The actor file as loaded: its one scale, its model, and its collision radius and height, which
 * the model fixup defaulted to 0.25 and 0.8 where the file stored none and bapobj_bindActor copies
 * unscaled into every body raised from it. */
#define ACTOR_SCALE     0xACu
#define ACTOR_MODEL     0xE0u
#define ACTOR_RADIUS    0xECu
#define ACTOR_HEIGHT    0xF4u
/* The player block's body, and a body's render handle. */
#define BLOCK_BODY      0x0Cu
#define BODY_THING      0x9Cu

/* A copy is picked as a cylinder no thinner and no shorter than this, so a model whose collision
 * the fixup defaulted, 0.25 by 0.8, is still a thing a pointer can land on; and no wider or taller
 * than this, so one outsized cylinder cannot cover the screen. */
#define PICK_RADIUS_MIN 0.3f
#define PICK_RADIUS_MAX 1.5f
#define PICK_HEIGHT_MIN 1.0f
#define PICK_HEIGHT_MAX 4.0f
/* The radius a place is kept off the walls by: the actor file's own, held to what a body could
 * be, and the engine's default where it cannot be read. */
#define WALL_RADIUS_MIN     0.1f
#define WALL_RADIUS_MAX     1.5f
#define WALL_RADIUS_DEFAULT 0.25f
#define SCALE_MIN       0.1f
#define SCALE_MAX       10.0f

void spawn_look_label(const npc_spawner_kind_t *kind, char *out, uint32_t out_size)
{
    const char *name = entity_name_of(kind->file);

    text_format(out, out_size, "%s", name != NULL ? name : kind->name);
}

static bool probe_hit(void *user, const float from[3], const float to[3], float *distance)
{
    (void)user;
    return world_probe_line_hit(npc_census()->level, from, to, distance);
}

static spawn_spot_floor_t probe_floor(void *user, const float at[3], float *offset, bool *mover)
{
    (void)user;
    switch (floor_probe_offset(at, offset, mover)) {
    case FLOOR_PROBE_FOUND: return SPAWN_SPOT_FLOOR_FOUND;
    case FLOOR_PROBE_NONE:  return SPAWN_SPOT_FLOOR_NONE;
    default:                return SPAWN_SPOT_FLOOR_UNAVAILABLE;
    }
}

static bool probe_headroom(void *user, const float at[3])
{
    (void)user;
    return world_probe_headroom(at);
}

/* Read from each copy's actor: a copy whose position cannot be read is taken as standing there.
 * And the player, `user`, whom a copy set on his own feet would stand inside. */
static bool probe_crowded(void *user, const float at[3])
{
    const uint8_t *player = (const uint8_t *)user;
    float          pos[3];
    uint32_t       i;

    if (player != NULL && memory_try_read((uintptr_t)player + PLAYER_POSITION_OFFSET, pos,
                                          sizeof pos) &&
        spawn_place_crowds(at, pos)) {
        return true;
    }
    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        const uint8_t *actor = npc_spawner_ring_actor(i);

        if (actor == NULL) {
            continue;
        }
        if (!memory_try_read((uintptr_t)actor + ACTOR_POSITION, pos, sizeof pos) ||
            spawn_place_crowds(at, pos)) {
            return true;
        }
    }
    return false;
}

/* In a single player game the cap is the panel's; on the host of a session the host's own; a
 * client does not know its host's, which answers a wish past it with a refusal. */
static bool cap_reached(void)
{
    return spawn_place_cap_reached(npc_spawn_link_active(), npc_spawn_link_cap(),
                                   npc_spawner_alive(), NPC_SPAWNER_ALIVE_MAX);
}

static float clamp(float value, float low, float high)
{
    return (value < low) ? low : (value > high) ? high : value;
}

/* A copy's cylinder: its position and its body's collision, held to what a pointer can use. */
static bool copy_shape(const uint8_t *actor, float base[3], float *radius, float *height)
{
    uint32_t body = 0;

    *radius = PICK_RADIUS_MIN;
    *height = PICK_HEIGHT_MIN;
    if (!memory_try_read((uintptr_t)actor + ACTOR_POSITION, base, 3u * sizeof(float))) {
        return false;
    }
    if (memory_try_read((uintptr_t)actor + ACTOR_BODY, &body, sizeof body) && body != 0u) {
        (void)memory_try_read((uintptr_t)body + BODY_RADIUS, radius, sizeof *radius);
        (void)memory_try_read((uintptr_t)body + BODY_HEIGHT, height, sizeof *height);
    }
    *radius = clamp(isfinite(*radius) ? *radius : 0.0f, PICK_RADIUS_MIN, PICK_RADIUS_MAX);
    *height = clamp(isfinite(*height) ? *height : 0.0f, PICK_HEIGHT_MIN, PICK_HEIGHT_MAX);
    return true;
}

static void copy_name(const uint8_t *actor, char *out, uint32_t out_size)
{
    npc_spawn_desc_t desc;
    const char      *name;
    char             stem[NPC_SPAWNER_NAME_MAX];

    if (!npc_spawner_description((uintptr_t)actor, &desc)) {
        text_format(out, out_size, "a copy");
        return;
    }
    name = entity_name_of(desc.file);
    if (name == NULL) {
        npc_foreign_stem(desc.file, stem, sizeof stem);
        name = stem;
    }
    text_format(out, out_size, "%s", name);
}

/* The copy the ray meets first, nearer than whatever the ray struck: a copy behind a wall is not
 * under the pointer (spawn_place_hover_limit). Only the ring's copies can be met, so the level's
 * own actors and its props are never a thing a right click could remove. */
static void find_the_hover(spawn_look_t *out, const float origin[3],
                           const float direction[3], float limit)
{
    world_pick_body_t bodies[NPC_SPAWNER_RING];
    uintptr_t         actors[NPC_SPAWNER_RING];
    uint32_t          count = 0;
    int32_t           nearest;
    uint32_t          i;

    memset(&out->hover, 0, sizeof out->hover);
    for (i = 0; i < NPC_SPAWNER_RING; ++i) {
        const uint8_t *actor = npc_spawner_ring_actor(i);

        if (actor != NULL && copy_shape(actor, bodies[count].base, &bodies[count].radius,
                                        &bodies[count].height)) {
            actors[count++] = (uintptr_t)actor;
        }
    }
    nearest = world_pick_nearest(origin, direction, bodies, count, limit, NULL);
    if (nearest < 0) {
        return;
    }
    out->hover.actor = actors[nearest];
    (void)memory_try_read(out->hover.actor + ACTOR_KEY, &out->hover.key, sizeof out->hover.key);
    out->hover.mine   = npc_spawn_link_owns(out->hover.key);
    out->hover.ridden = npc_spawn_save_rides(out->hover.key);
    copy_name((const uint8_t *)out->hover.actor, out->hover.name, sizeof out->hover.name);
}

/* Corners around every copy the camera sees: white for this player's, grey for another's, and
 * bright for the one under the pointer, whose name goes over it. */
static void mark_the_copies(spawn_look_t *out, const world_pick_camera_t *camera)
{
    uint32_t i;

    for (i = 0; i < NPC_SPAWNER_RING && out->scene.brackets + 1u < SPAWN_MARKS_BRACKETS; ++i) {
        const uint8_t        *actor = npc_spawner_ring_actor(i);
        spawn_mark_bracket_t *mark;
        uint32_t              key = 0;
        float                 base[3];
        float                 head[3];
        float                 radius;
        float                 height;
        float                 fx;
        float                 fy;
        float                 hx;
        float                 hy;
        bool                  mine;

        if (actor == NULL || !copy_shape(actor, base, &radius, &height)) {
            continue;
        }
        head[0] = base[0];
        head[1] = base[1];
        head[2] = base[2] + height;
        if (!world_pick_project(camera, base, &fx, &fy, NULL) ||
            !world_pick_project(camera, head, &hx, &hy, NULL)) {
            continue;   /* behind the eye */
        }
        mine = !memory_try_read((uintptr_t)actor + ACTOR_KEY, &key, sizeof key) ||
               npc_spawn_link_owns(key);
        mark = &out->scene.bracket[out->scene.brackets++];
        spawn_marks_box(fx, fy, hx, hy, mark);
        if ((uintptr_t)actor == out->hover.actor) {
            mark->kind         = mine ? SPAWN_MARK_HOVER_OWN : SPAWN_MARK_HOVER_FOREIGN;
            out->scene.hover_x = mark->left;
            out->scene.hover_y = mark->top;
        } else {
            mark->kind = mine ? SPAWN_MARK_OWN : SPAWN_MARK_FOREIGN;
        }
    }
}

/* The loaded actor file of the chosen kind: the archive's through the engine's loader, which loads
 * it once and keeps it until the world changes, or the level's own out of its model table. */
static uintptr_t kind_actor(const npc_spawner_kind_t *kind, int32_t chosen)
{
    uint32_t models = 0;
    uint32_t actor = 0;
    int32_t  index;

    if (kind->foreign) {
        return (uintptr_t)actor_loader_get(kind->file);
    }
    index = npc_census()->model[chosen];
    if (npc_census()->level == NULL || index < 0 ||
        !memory_try_read((uintptr_t)npc_census()->level + WORLD_MODELS, &models, sizeof models) ||
        !memory_try_read((uintptr_t)models + 4u * (uint32_t)index, &actor, sizeof actor)) {
        return 0;
    }
    return (uintptr_t)actor;
}

/* The collision radius of the chosen kind, as every body raised from it will have it. */
static float kind_radius(const npc_spawner_kind_t *kind, int32_t chosen)
{
    uintptr_t actor  = kind_actor(kind, chosen);
    float     radius = WALL_RADIUS_DEFAULT;

    if (actor == 0 || !memory_try_read(actor + ACTOR_RADIUS, &radius, sizeof radius) ||
        !isfinite(radius) || radius <= 0.0f) {
        return WALL_RADIUS_DEFAULT;
    }
    return clamp(radius, WALL_RADIUS_MIN, WALL_RADIUS_MAX);
}

/* Whether the chosen kind flies: the archive's by the move mode the retail levels fly it with, the
 * level's own by its source placement's. A flyer's height is the builder's, over the place. */
static bool kind_flies(const npc_spawner_kind_t *kind, int32_t chosen)
{
    uint8_t record[PLACE_SIZE];
    char    stem[NPC_SPAWNER_NAME_MAX];
    int32_t fly_mode;

    if (kind->foreign) {
        fly_mode = spawn_script_flyer_mode(kind->file);
        return fly_mode == 2 || (fly_mode >= 4 && fly_mode <= 6);
    }
    return npc_census()->level != NULL &&
           npc_census_read_placement(npc_census()->level, npc_census()->source[chosen], record,
                                     stem, sizeof stem, NULL, 0u) &&
           npc_spawn_record_flies(record);
}

static void want_the_ghost(spawn_look_t *out, const npc_spawner_kind_t *kind,
                           int32_t chosen)
{
    uintptr_t actor = kind_actor(kind, chosen);
    uint32_t  model = 0;
    float     scale = 1.0f;
    float     at[3];
    float     root[12];

    out->ghost_height = PICK_HEIGHT_MIN;
    if (actor == 0 || !memory_try_read(actor + ACTOR_MODEL, &model, sizeof model) || model == 0u) {
        spawn_ghost_want_none(spawn_ghost_state());
        return;
    }
    if (!memory_try_read(actor + ACTOR_SCALE, &scale, sizeof scale) ||
        !(scale >= SCALE_MIN && scale <= SCALE_MAX)) {
        scale = 1.0f;
    }
    if (memory_try_read(actor + ACTOR_HEIGHT, &out->ghost_height, sizeof out->ghost_height)) {
        out->ghost_height = clamp(isfinite(out->ghost_height) ? out->ghost_height : 0.0f,
                                  PICK_HEIGHT_MIN, PICK_HEIGHT_MAX);
    }
    memcpy(at, out->spot, sizeof at);
    if (kind_flies(kind, chosen)) {
        at[2] += NPC_SPAWN_FLYER_HEIGHT;
    }
    spawn_ghost_matrix(root, at, out->facing, scale);
    spawn_ghost_want(spawn_ghost_state(), (uintptr_t)model, npc_spawn_node_epoch(), root);
}

/* The two lines under the pointer. */
static void write_the_lines(spawn_look_t *out, const npc_spawner_kind_t *kind)
{
    char label[ENTITY_NAME_MAX + 1u];

    spawn_look_label(kind, label, sizeof label);
    if (npc_spawn_link_active() && npc_spawn_link_cap() == 0u) {
        text_format(out->scene.first, sizeof out->scene.first, "%s  facing %.0f  %u alive",
                    label, (double)out->facing, npc_spawner_alive());
    } else {
        text_format(out->scene.first, sizeof out->scene.first, "%s  facing %.0f  %u of %u",
                    label, (double)out->facing, npc_spawner_alive(),
                    npc_spawn_link_active() ? npc_spawn_link_cap() : NPC_SPAWNER_ALIVE_MAX);
    }
    out->scene.good = out->camera && out->verdict == SPAWN_SPOT_OK;
    if (out->hover.actor != 0) {
        const char *state;

        switch (spawn_place_remove_verdict(true, npc_spawn_link_active() &&
                                                     npc_spawn_link_is_client(),
                                           out->hover.ridden)) {
        case SPAWN_REMOVE_CLIENT:
            state = out->hover.mine ? "removing one needs the host" : "another player's";
            break;
        case SPAWN_REMOVE_RIDDEN:
            state = "the player rides it";
            break;
        default:
            state = "right click removes it";
            break;
        }

        text_format(out->scene.second, sizeof out->scene.second, "%s: %s", out->hover.name,
                    state);
        text_format(out->scene.hover, sizeof out->scene.hover, "%s", out->hover.name);
    } else {
        text_format(out->scene.second, sizeof out->scene.second, "%s",
                    out->camera ? spawn_place_spot_word(out->verdict)
                                : "the camera cannot be read");
    }
}

void spawn_look_at(spawn_look_t *out, const npc_spawner_kind_t *kind, int32_t chosen)
{
    static const spawn_spot_probes_t PROBES = { &probe_hit, &probe_floor, &probe_headroom,
                                                &probe_crowded, &probe_hit };
    spawn_spot_probes_t  probes = PROBES;
    world_pick_camera_t  camera;
    const uint8_t       *player = (const uint8_t *)player_slot_current();
    uint32_t             body = 0;
    float                origin[3];
    float                direction[3];
    float                toward;
    float                head[3];
    spawn_spot_found_t   found;
    float                fx;
    float                fy;
    float                hx;
    float                hy;

    memset(&out->scene, 0, sizeof out->scene);
    out->strike = SPAWN_SPOT_REACH;
    out->moved  = 0.0f;
    overlay_input_update_pointer();
    overlay_input_pointer(&out->scene.pointer_x, &out->scene.pointer_y);
    out->camera = world_camera_read(&camera) &&
                  world_pick_ray(&camera, out->scene.pointer_x, out->scene.pointer_y, origin,
                                 direction);
    if (!out->camera || player == NULL) {
        spawn_ghost_want_none(spawn_ghost_state());
        memset(&out->hover, 0, sizeof out->hover);
        write_the_lines(out, kind);
        return;
    }
    if (!world_probe_has_headroom()) {
        probes.headroom = NULL;   /* a question this build cannot put is not guessed at */
    }
    out->verdict = spawn_place_spot(&probes, (void *)player, origin, direction,
                                    kind_radius(kind, chosen), cap_reached(), &found);
    memcpy(out->spot, found.at, sizeof out->spot);
    out->strike = found.strike;
    out->moved  = found.moved;
    {
        float here[2] = { out->spot[0], out->spot[1] };
        float there[2];

        memcpy(there, player + PLAYER_POSITION_OFFSET, sizeof there);
        toward = spawn_place_yaw_toward(here, there);
    }
    /* The tripod faces away, its back to the player, who mounts it from behind. */
    if (_stricmp(kind->file, NPC_SPAWN_TRIPOD_FILE) == 0) {
        toward = spawn_place_wrap(toward + 180.0f);
    }
    out->facing = spawn_place_facing(spawn_place_state(), toward);
    find_the_hover(out, origin, direction, spawn_place_hover_limit(found.strike));
    mark_the_copies(out, &camera);
    want_the_ghost(out, kind, chosen);
    head[0] = out->spot[0];
    head[1] = out->spot[1];
    head[2] = out->spot[2] + out->ghost_height;
    if (out->scene.brackets < SPAWN_MARKS_BRACKETS &&
        world_pick_project(&camera, out->spot, &fx, &fy, NULL) &&
        world_pick_project(&camera, head, &hx, &hy, NULL)) {
        spawn_mark_bracket_t *mark = &out->scene.bracket[out->scene.brackets++];

        spawn_marks_box(fx, fy, hx, hy, mark);
        mark->kind = (out->verdict == SPAWN_SPOT_OK) ? SPAWN_MARK_GHOST_OK
                                                     : SPAWN_MARK_GHOST_REFUSED;
    }
    write_the_lines(out, kind);
    if (memory_try_read((uintptr_t)player + BLOCK_BODY, &body, sizeof body) && body != 0u) {
        uint32_t thing = 0;

        if (memory_try_read((uintptr_t)body + BODY_THING, &thing, sizeof thing)) {
            out->player_thing = (uintptr_t)thing;
        }
    }
}
