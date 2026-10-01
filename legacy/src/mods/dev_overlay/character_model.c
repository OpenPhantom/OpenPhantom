/* character_model.c: the swap that puts another body under the same player.
 *
 * The five calls it is carried out with are found by character_model_sites.c, and the caller's side
 * of that file's contract is here: none of the five may be detoured, because all five are resolved
 * as call targets on their own untouched prologues.
 *
 * SIZE NOTE: over the 600 line review limit. Three seams have been taken, each the one this note
 * named at the time: the five patterns and their resolution went to character_model_sites.c, the
 * roster and the asset name matching to character_model_roster.c, and the rebind with the node
 * retargeting to character_rebind.c, which the far bodies' path shares. What is left is the swap
 * of the player's own body, and it is deliberately not a seam: the borrow and the way home have
 * to undo each other field for field, and separating them is how a restore comes to miss one.
 */
#include "character_model.h"

#include "local_look.h"

#include "common/appearance_note.h"

#include "character_bodies.h"
#include "character_model_roster.h"
#include "character_model_sites.h"
#include "character_rebind.h"

#include "model_blade_guard.h"
#include "character_cards.h"
#include "character_facing.h"
#include "character_prop.h"
#include "character_nodemap.h"
#include "common/character_profile.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PLAYER_ACTOR         0x0Cu   /* the player's own bapObj                              */
#define PLAYER_SABRE_NODE    0x4Cu   /* the node index of sabreblad01, 0 for a rig with none */
#define PLAYER_HERO_INDEX    0x6Cu
#define PLAYER_BLADE_SIZE    0x210u  /* 0..1, the length the blade mesh is rebuilt at        */
#define BAPOBJ_THING         0x9Cu   /* the rdroid handle                                    */
#define BAPOBJ_SCALE         0x30u   /* three floats                                          */
#define RDTHING_MODEL3       0x04u
#define RDTHING_PUPPET       0x18u
#define RDTHING_POSE_STAMP   0x1Cu
#define BAPACTOR_SCALE       0xACu   /* one float, written into all three axes                */
#define BAPACTOR_MODEL       0xE0u

#define ACTOR_TAG            0x42414653u   /* 'BAFS' */
#define HERO_SLOTS           4u
#define ASSET_NAME_MAX       40u

/* 1.0f as bits, so the blade park below writes a float without an aliasing cast. */
#define ONE_AS_BITS          0x3F800000u


typedef enum model_verdict {
    MODEL_VERDICT_UNTRIED = 0,
    MODEL_VERDICT_CARRIES,
    MODEL_VERDICT_REFUSED
} model_verdict_t;

typedef struct model_state {
    bool                    tried;
    bool                    resolved;
    bool                    blade_lock_seen;
    bool                    blade_lock_reported;
    bool                    foreign_body_reported;   /* a resize about another body, said */

    character_model_sites_t sites;

    void                   *asset[MODEL_ROWS_MAX];   /* loaded once per row, then kept resident */
    uint8_t                 verdict[MODEL_ROWS_MAX]; /* model_verdict_t, one per row            */

    bool                    borrowed;
    uint32_t                borrowed_id;
    uintptr_t               borrowed_obj;        /* the body the swap was done on   */
    bool                    publish_owed;        /* the note refused the model last said */
    uintptr_t               borrowed_model;      /* the model that was put on it    */
    uintptr_t               home_model;          /* what the body wore before       */
    float                   home_scale[3];
    uint32_t                home_sabre_node;     /* pr+0x4c as the engine left it   */
} model_state_t;

static model_state_t state;

/* ============================================================================================ */

/* Which hero may wear somebody else's body.
 *
 * All four slots the engine has, and the hero index is no longer the test. It used to be the two
 * Jedi, on the grounds that the blade mesh is edited in place and the guard on that edit was
 * written for a Jedi. Nothing below this line was ever measured against a hero index: the fit, the
 * translation, the weapon and the backface drop are all measured against whatever rig the player
 * is in. What is measured instead is the blade, and the measurement is `own_rig_has_a_blade`
 * below.
 *
 * The bound is still real, because the four names in the hero asset table are the four values the
 * engine itself indexes that table with, and a fifth would read past it. */
bool character_model_hero_is_supported(int32_t hero_index)
{
    return hero_index >= 0 && (uint32_t)hero_index < HERO_SLOTS;
}

/* ============================================================================================ */

static void resolve_once(void)
{
    if (state.tried) {
        return;
    }
    state.tried = true;

    if (!character_model_sites_resolve(&state.sites)) {
        return;
    }

    /* The translation layer is a precondition rather than a refinement: the clips keep coming out
     * of the player's own asset and they address joints by ordinal, so a rig that is not the
     * player's own is driven by tracks meant for other joints without it. */
    if (!character_nodemap_install()) {
        return;
    }

    /* And the one guard a borrowed rig needs. A model swap keeps the player's own hero, clips and
     * weapons, so only the blade resize is in its way, and that one is not optional, because the
     * assert behind it ends the process. A refusal here leaves every row reading n/a, which is the
     * honest answer: the swap cannot be offered on a build where the site did not resolve. */
    if (!model_blade_guard_install()) {
        return;
    }

    state.resolved = true;
    log_info("the model swap has its five entry points: bind %08X, release %08X, scale %08X, "
             "weapon remount %08X, loader %08X; the sabre glow cards are %s",
             (unsigned)(uintptr_t)state.sites.set_model,
             (unsigned)(uintptr_t)state.sites.free_arrays,
             (unsigned)(uintptr_t)state.sites.set_scale,
             (unsigned)(uintptr_t)state.sites.rebind_weapon,
             (unsigned)(uintptr_t)state.sites.res_alloc,
             character_cards_is_armed() ? "held to four vertices" : "NOT bounded");
}

bool character_model_resolve(void)
{
    resolve_once();
    return state.resolved;
}

const character_model_sites_t *character_model_sites(void)
{
    return state.resolved ? &state.sites : NULL;
}

bool character_model_blade_lock_seen(void)
{
    return state.blade_lock_seen;
}

/* ============================================================================================ */

static bool player_block(uintptr_t *out)
{
    uint32_t block = 0;

    if (!state.resolved) {
        return false;
    }
    if (!memory_try_read(state.sites.player_record, &block, sizeof block) || block == 0u) {
        return false;              /* no level running */
    }
    *out = (uintptr_t)block;
    return true;
}

/* The live body and its render handle, or false when there is no body to rebind. */
static bool player_body(uintptr_t *out_obj, uintptr_t *out_thing)
{
    uintptr_t block = 0;
    uint32_t  obj = 0;
    uint32_t  thing = 0;

    if (!player_block(&block)) {
        return false;
    }
    if (!memory_try_read(block + PLAYER_ACTOR, &obj, sizeof obj) || obj == 0u) {
        return false;
    }
    if (!memory_try_read((uintptr_t)obj + BAPOBJ_THING, &thing, sizeof thing) || thing == 0u) {
        return false;
    }
    *out_obj = (uintptr_t)obj;
    *out_thing = (uintptr_t)thing;
    return true;
}

static bool hero_index(int32_t *out)
{
    uintptr_t block = 0;
    int32_t   hero = 0;

    if (!player_block(&block) ||
        !memory_try_read(block + PLAYER_HERO_INDEX, &hero, sizeof hero)) {
        return false;
    }
    *out = hero;
    return true;
}

/* Whether the swap is still on the body, asked of the engine rather than remembered.
 *
 * A level change builds a fresh body from the hero asset table, which this module never touches,
 * so the player comes back in his own model with nothing to undo. Nothing tells this module that
 * happened, and nothing needs to: both the body and the model it wears have to still be the ones
 * the swap was done on. That is also what keeps the blade lock from outliving the swap and
 * silently disabling the sabre for the rest of the session. */
static bool swap_is_live(void)
{
    uintptr_t obj = 0;
    uintptr_t thing = 0;
    uint32_t  model = 0;

    if (!state.borrowed) {
        return false;
    }
    if (!player_body(&obj, &thing) || obj != state.borrowed_obj) {
        return false;
    }
    if (!memory_try_read(thing + RDTHING_MODEL3, &model, sizeof model)) {
        return false;
    }
    return (uintptr_t)model == state.borrowed_model;
}

/* A swap that ended without anybody being told, taken down. The commonest way to get here is a
 * level change, which builds a fresh body this module never touched.
 *
 * Only the player's own row is taken down: a far body's row belongs to the far bodies' path and is
 * let go there. Each of the two disarms tests the live handle for itself, so arriving here with no
 * level running takes nothing down that is still in use.
 *
 * The note is owed "no model" afterwards and nothing is published from here: this also runs from
 * inside the blade resize, and the frame tick says it again at the end of the scene. */
static void take_down_dead_swap(void)
{
    state.borrowed = false;
    character_nodemap_disarm_local();
    character_prop_disarm();
    state.publish_owed = true;
}

/* It answers whether the swap is live, because it has to ask anyway and both callers want it. The
 * question costs nothing when nothing is borrowed. */
static bool release_a_dead_swap(void)
{
    bool live = swap_is_live();

    if (!live && state.borrowed) {
        take_down_dead_swap();
    }
    return live;
}

bool character_model_borrows_shared_mesh(void)
{
    uintptr_t    obj = 0;
    uintptr_t    thing = 0;
    bool         read;
    body_guard_t answer;

    /* The flag is set before anything else, so it records that the guard asked even on the calls
     * where the answer is no. That is the point of it: what is being proven is that the question
     * reaches this module at all. */
    state.blade_lock_seen = true;

    /* A resize asked about another player's body. The multiplayer runs a far body's tick with
     * that body's record COPIED into the player's block, and spawns and removes far bodies the
     * same way, so the cell and the block address stay what they were and only the body in the
     * block tells the two apart. Comparing blocks missed every one of those: a far Jedi drawing
     * or putting away his sabre, or merely arriving or leaving, took this swap down under a body
     * still wearing the borrowed rig (seen in a field run). That resize is the
     * engine's business, and nothing is taken down for it; a swap that really ended is noticed
     * by the frame tick, where no window is open. */
    read = state.borrowed && player_body(&obj, &thing);
    answer = character_bodies_guard(state.borrowed, read, obj, state.borrowed_obj,
                                    swap_is_live());
    if (answer == BODY_GUARD_ENGINE && read && obj != state.borrowed_obj &&
        !state.foreign_body_reported) {
        state.foreign_body_reported = true;
        log_info("a blade resize was asked about body %08X, not the swapped %08X; the engine "
                 "answers it and the swap stays on. Later ones are not logged",
                 (unsigned)obj, (unsigned)state.borrowed_obj);
    }
    /* The blade guard asks this on every resize, which is every substep the blade is moving, so
     * for a player who owns a blade this is where a swap on his own body that ended is noticed.
     * For a player who owns none it never runs, which is why the tick and the panel ask too. */
    if (answer == BODY_GUARD_TAKE_DOWN) {
        take_down_dead_swap();
    }
    return answer == BODY_GUARD_DECLINE;
}

/* Whether the player's own rig carries a lightsaber blade, and this is what replaced the hero
 * index.
 *
 * pr+0x4c is the node index the engine resolved for `sabreblad01` against the rig the player
 * spawned in, and it is 0 for a rig that carries no such node.
 *
 * Plr_SetBladeSize asserts on that field being non zero, and the assert stands in front of the mesh
 * write rather than beside it. So a player whose blade node is zero cannot reach the write at all,
 * whatever calls the function: the damage the blade lock exists to prevent is out of reach by
 * construction, and the observation that the lock is live is neither obtainable for him nor needed.
 * What the swap must not do is give him a blade node, which is why character_rebind_local_nodes
 * keeps the slot at zero for him.
 *
 * The value the engine left is used while the swap is live, because the swap overwrites the field.
 * A read that fails answers yes, so the proof is asked for rather than assumed away. */
static bool own_rig_has_a_blade(void)
{
    uintptr_t block = 0;
    uint32_t  node = 0;

    if (swap_is_live()) {
        return state.home_sabre_node != 0u;
    }
    if (!player_block(&block) ||
        !memory_try_read(block + PLAYER_SABRE_NODE, &node, sizeof node)) {
        return true;
    }
    return node != 0u;
}

/* Which roster row names the asset the hero slot is pointing at right now. That is the model the
 * body was built from, so before any swap it is the row that reads Current. It answers -1 when the
 * hero wears something the roster does not carry. */
static int32_t home_candidate(void)
{
    int32_t  hero = 0;
    uint32_t name = 0;
    char     asset[ASSET_NAME_MAX];

    if (!state.resolved || !hero_index(&hero) || hero < 0 || (uint32_t)hero >= HERO_SLOTS) {
        return -1;
    }
    if (!memory_try_read(state.sites.name_table + 4u * (uintptr_t)hero, &name, sizeof name) ||
        name == 0u) {
        return -1;
    }
    if (!memory_try_read((uintptr_t)name, asset, sizeof asset)) {
        return -1;
    }
    asset[sizeof asset - 1u] = '\0';
    return character_model_candidate_of(asset);
}

/* ============================================================================================ */

/* The asset, loaded at most once per row and then kept. The reference is deliberately not
 * released: it holds the model resident for as long as a body might be wearing it, and releasing
 * it would mean guessing at a lifetime the resource layer already tracks. A refusal is remembered
 * too, so a level that does not carry an asset is asked once rather than once per click. */
static void *asset_of(uint32_t id)
{
    const char *name;
    void       *asset;

    if (id >= MODEL_ROWS_MAX) {
        return NULL;
    }
    if (state.asset[id] != NULL) {
        return state.asset[id];
    }
    if (state.verdict[id] == (uint8_t)MODEL_VERDICT_REFUSED) {
        return NULL;
    }
    name = character_model_roster_asset(id);
    if (name == NULL) {
        return NULL;
    }
    asset = state.sites.res_alloc(ACTOR_TAG, name);
    if (asset == NULL) {
        state.verdict[id] = (uint8_t)MODEL_VERDICT_REFUSED;
        log_warning("the resource layer does not find %s", name);
        return NULL;
    }
    state.asset[id] = asset;
    return asset;
}

/* The model an asset carries, or 0. */
static uintptr_t model_of(uint32_t id)
{
    void    *asset = asset_of(id);
    uint32_t model = 0;

    if (asset == NULL ||
        !memory_try_read((uintptr_t)asset + BAPACTOR_MODEL, &model, sizeof model)) {
        return 0u;
    }
    return (uintptr_t)model;
}

void *character_model_load(uint32_t id, uintptr_t *out_model)
{
    void *asset;

    if (out_model != NULL) {
        *out_model = 0u;
    }
    if (!state.resolved || out_model == NULL) {
        return NULL;
    }
    asset = asset_of(id);
    if (asset != NULL) {
        *out_model = model_of(id);
    }
    return asset;
}

/* Put the player's own blade back before borrowing somebody else's body.
 *
 * While the swap is live the blade lock declines every resize, and one of the calls it declines is
 * the one player_despawn makes to restore the shared mesh before the asset is released. A player
 * who swaps with the sabre holstered would therefore hand the next level a hero asset whose blade
 * mesh is collapsed into the hilt, and no amount of switching back would undo it.
 *
 * So the last thing that happens on the player's own model is a resize to full length. It is done
 * by lying to the remount about the size for the length of one call rather than by resolving
 * Plr_SetBladeSize separately: the remount already asks for that resize, and the value it asks
 * with is this one field.
 *
 * A player whose own rig has no blade node has no blade mesh to park and no field worth writing,
 * and the resize he would be asking for is the one the engine's own assert says is never made for
 * him. */
static void park_own_blade(void)
{
    uintptr_t block = 0;
    uint32_t  saved = 0;

    if (state.home_sabre_node == 0u) {
        return;
    }
    if (!player_block(&block) ||
        !memory_try_read(block + PLAYER_BLADE_SIZE, &saved, sizeof saved)) {
        return;
    }
    *(volatile uint32_t *)(block + PLAYER_BLADE_SIZE) = ONE_AS_BITS;
    state.sites.rebind_weapon();
    *(volatile uint32_t *)(block + PLAYER_BLADE_SIZE) = saved;
}

/* Where the player's force push leaves from on the rig he has just put on, said once per swap.
 *
 * The engine caches one node index for it at spawn and never looks again, so the swap has to hand
 * it one that belongs to the new skeleton. The node has to carry a mesh, because the bolt starts
 * at the centre of that node's sphere and the sphere call writes nothing at all for a node without
 * one; five of the rows the panel offers used to be given a node that carries none. This line is
 * the only place a reader can see which node a rig was given. */
static void say_where_the_push_leaves_from(uintptr_t model, int32_t push, bool on_the_hand)
{
    char name[ASSET_NAME_MAX];

    if (push < 0) {
        log_warning("this rig states no node the force push could leave from, so the push starts "
                    "at the root of the body");
        return;
    }
    if (on_the_hand) {
        log_warning("the only node this rig's push can leave from is the hand its weapon hangs "
                    "on, so the push starts at the weapon rather than at the fist");
        return;
    }
    if (character_rebind_node_name(model, push, name, sizeof name)) {
        log_info("the player's force push leaves from %s on the rig he is wearing", name);
    }
}

/* The swap itself, in the order a control pass settled on. Every step is here because leaving it
 * out has a symptom, and the comment at each step names it; character_rebind.c does the same for
 * the bind. */
static bool wear_model(uintptr_t obj, uintptr_t thing, uintptr_t model, const float scale[3],
                       bool translate)
{
    uint32_t  stamp = 0;
    uint32_t  puppet = 0;
    uintptr_t block = 0;

    if (!memory_try_read(thing + RDTHING_POSE_STAMP, &stamp, sizeof stamp) ||
        !memory_try_read(thing + RDTHING_PUPPET, &puppet, sizeof puppet)) {
        log_warning("the render handle at %08X could not be read", (unsigned)thing);
        return false;
    }

    /* The weapon half lets go of the outgoing rig first, and this is the only place it can be done
     * correctly. It holds node indices of the model the body is wearing, and the table those index
     * is about to be freed and reallocated at the new rig's node count. Giving the words back after
     * the bind writes the old rig's slots into the new rig's table, which is a write past the end
     * of an engine allocation and killed the process one swap later. */
    character_prop_body_model_changing();

    /* Armed before the bind, and the order is not cosmetic. The moment a foreign model is on the
     * handle, the next pose rebuild reads the player's clips through that model's node table; if
     * the translation were not already standing, a rig with fewer nodes than the clip has would
     * leave keyframe cursors nobody clears. Nothing between here and the bind poses anything. */
    if (translate) {
        nodemap_body_t body;

        memset(&body, 0, sizeof body);
        body.thing = thing;
        body.obj = obj;
        body.reference = state.home_model;
        body.target = model;
        body.local = true;
        if (!character_nodemap_arm(&body)) {
            log_warning("the skeleton could not be translated, so the model is not put on");
            return false;
        }
        /* The two sided bit is authored per FACE, and the hero rigs were marked by someone who
         * knew a third person camera would expose the limb shells. The actor rigs were not: 85 of
         * the 97 offered are a more open shell than Obi-Wan, up to 95 per cent of their faces. */
        (void)character_facing_arm(thing);
    } else {
        character_nodemap_disarm_local();
    }

    /* The lightning arcs on the body hold node indices of the rig it wears until this bind. */
    character_model_sites_let_go_of_arcs(obj);
    if (!character_rebind_bind(&state.sites, thing, model)) {
        character_nodemap_disarm_local();
        log_warning("the model bind at %08X refused: the per node arrays could not be allocated",
                    (unsigned)model);
        return false;
    }

    /* Asked once here the way the pose hook asks it every frame, so that a body the translation
     * does not recognise is a line in the log and not only a rig driven by the wrong joints. */
    if (translate && !character_nodemap_holds(thing, NULL)) {
        log_warning("the translation does not recognise the body at %08X it was armed for, so the "
                    "player's clips reach the borrowed rig by ordinal", (unsigned)thing);
    }

    state.sites.set_scale((void *)obj, scale[0], scale[1], scale[2]);

    /* The weapon hangs off a node INDEX, and the index the player record remembers was resolved
     * against the skeleton that has just been replaced. Re-resolving the six by name is what makes
     * them mean the hand, the chest and the blade again; the remount is what puts the weapon model
     * back on the one that names the mount.
     *
     * The way home puts the player's own rig back, so whatever blade node that rig carries is his
     * to have. A borrow gives him one only if he came with one. */
    if (player_block(&block)) {
        bool    on_the_hand = false;
        int32_t push = character_rebind_local_nodes(
            block, model, !translate || state.home_sabre_node != 0u, &on_the_hand);

        say_where_the_push_leaves_from(model, push, on_the_hand);
    }
    state.sites.rebind_weapon();
    return true;
}

static bool go_home(uintptr_t obj, uintptr_t thing)
{
    float scale[3];

    scale[0] = state.home_scale[0];
    scale[1] = state.home_scale[1];
    scale[2] = state.home_scale[2];

    /* Cleared BEFORE the remount, so the resize inside it is let through and the player's own
     * blade comes back at the length the sabre state says it should have. */
    state.borrowed = false;
    character_prop_disarm();
    if (!wear_model(obj, thing, state.home_model, scale, false)) {
        return false;
    }
    log_info("the player is back in his own model; the sabre card bound answered %u times",
             (unsigned)character_cards_bound_count());
    return true;
}

/* Why a worn model can leave the player empty handed, and it is said once per swap rather than
 * not at all.
 *
 * The player's weapon is drawn as a second render instance hung on the borrowed rig's `rhand`, so a
 * rig without that node carries neither the hilt nor the blade. Four of the 99 rows that clear the
 * fit floor are such rigs: `destroyr` and `tatcrit` end their arm at `rforarm`, `jawa` and
 * `jawagun` at `rarm`. Nothing along that path says so, and a lightsaber that is drawn on most
 * models and on those four is not is exactly what a player reports as one that is sometimes not
 * rendered.
 *
 * It is a measurement of the model's own live node table and changes nothing. */
static void report_weapon_hand(uintptr_t model, const char *name)
{
    if (character_nodemap_find(model, "rhand") >= 0) {
        return;
    }
    log_warning("%s carries no rhand node, so the player's own weapon has nothing to hang on and "
                "neither the hilt nor the blade is drawn while this model is worn", name);
}

static bool borrow(uint32_t id, uintptr_t obj, uintptr_t thing)
{
    const char   *name = character_model_roster_asset(id);
    void         *asset = asset_of(id);
    uintptr_t     model = model_of(id);
    uint32_t      authored = 0;
    float         scale[3];
    nodemap_fit_t fit;

    if (name == NULL || asset == NULL) {
        return false;
    }
    if (model == 0u) {
        state.verdict[id] = (uint8_t)MODEL_VERDICT_REFUSED;
        log_warning("%s carries no model", name);
        return false;
    }
    if (!memory_try_read((uintptr_t)asset + BAPACTOR_SCALE, &authored, sizeof authored)) {
        return false;
    }

    if (!swap_is_live()) {
        uintptr_t block = 0;
        uint32_t  home = 0;

        /* The blade node is captured with the model, because the retarget below overwrites it and
         * because it is the one field that decides whether this player's blade half needs a proof
         * at all. */
        if (!memory_try_read(thing + RDTHING_MODEL3, &home, sizeof home) || home == 0u ||
            !memory_try_read(obj + BAPOBJ_SCALE, state.home_scale, sizeof state.home_scale) ||
            !player_block(&block) ||
            !memory_try_read(block + PLAYER_SABRE_NODE, &state.home_sabre_node,
                             sizeof state.home_sabre_node)) {
            log_warning("%s: what the body wears now could not be read, so it could not be put "
                        "back afterwards and the swap is refused", name);
            return false;
        }
        state.home_model = (uintptr_t)home;
        park_own_blade();
    }

    /* Measured before anything is touched, so a rig this module cannot drive is refused rather
     * than half worn. The measurement is against the model the player is in right now, which is
     * where his clips come from, and it is remembered so that a refused row goes quiet instead of
     * being tried again on every click. */
    if (!character_nodemap_measure(state.home_model, model, &fit)) {
        state.verdict[id] = (uint8_t)MODEL_VERDICT_REFUSED;
        log_warning("%s: the two skeletons could not be measured against each other", name);
        return false;
    }
    if (!character_nodemap_fit_is_offered(&fit)) {
        state.verdict[id] = (uint8_t)MODEL_VERDICT_REFUSED;
        log_warning("%s is refused: %u of its %u nodes carry a name the player's %u node rig knows,"
                    " and the waist, the chest and the head are not all among them, so the body "
                    "would ride along without being animated", name,
                    fit.matched, fit.target_nodes, fit.reference_nodes);
        return false;
    }

    /* The lock goes on before the remount inside wear_model asks for a resize, because from here
     * on that resize would land in an asset this player does not own. */
    state.borrowed = true;
    state.borrowed_id = id;
    state.borrowed_obj = obj;
    state.borrowed_model = model;

    memcpy(&scale[0], &authored, sizeof scale[0]);
    scale[1] = scale[0];
    scale[2] = scale[0];

    if (!wear_model(obj, thing, model, scale, true)) {
        state.borrowed = false;
        return false;
    }
    /* The weapon is the last thing, because it needs the worn model's hand to hang off. It is not a
     * precondition: a rig with no right hand wears the model and carries nothing, which is what it
     * did before there was a borrowed weapon at all. */
    /* The contact node is a NAME, and a borrowed model answers it differently: while one is worn a
     * weapon name lands on the hand the weapon is drawn on, and when it is put away the same name
     * answers zero, which switches the contact layer off. Whatever the swing rows hold now was
     * answered against a body that is no longer the one being worn. */

    /* Arm first, then hook: the arm matches prologues the hook would have overwritten. */
    if (character_prop_arm(state.sites.player_record, obj, state.home_model)) {
        (void)character_prop_draw_install();
    }
    report_weapon_hand(model, name);

    state.verdict[id] = (uint8_t)MODEL_VERDICT_CARRIES;
    log_info("%s: the body now wears it at scale %d/1000, and every clip is still the player's own",
             name, (int)(scale[0] * 1000.0f));
    return true;
}

/* ============================================================================================ */

uint32_t character_model_count(void)
{
    return character_model_roster_count();
}

const char *character_model_name(uint32_t id)
{
    return (id < character_model_roster_count()) ? character_model_roster_label(id) : NULL;
}

bool character_model_is_available(uint32_t id)
{
    int32_t hero = 0;

    resolve_once();
    if (id >= character_model_roster_count() || !state.resolved) {
        return false;
    }
    /* The panel is the other place a dead swap is noticed, and for a player whose rig carries no
     * blade it is the only one: nothing else in this module runs while a level is not swapping. */
    (void)release_a_dead_swap();
    /* A row that was tried and refused stays on screen and reads n/a. Nothing is measured here:
     * deciding a row would mean loading its asset, and the panel asks this question about every
     * row on every repaint. */
    if (state.verdict[id] == (uint8_t)MODEL_VERDICT_REFUSED) {
        return false;
    }
    if (!model_blade_guard_is_armed()) {
        return false;
    }
    if (!hero_index(&hero) || !character_model_hero_is_supported(hero)) {
        return false;
    }
    /* The handshake is asked for from a player whose blade can actually be resized, and from him
     * only. For a player whose own rig carries none it is not obtainable, because the engine never
     * makes the call that would satisfy it, and that same silence is what makes it unnecessary. */
    if (own_rig_has_a_blade() && !state.blade_lock_seen) {
        if (!state.blade_lock_reported) {
            state.blade_lock_reported = true;
            log_warning("no model may be worn yet: the blade resize guard has not asked this "
                        "module whether the mesh under it is borrowed, so a swap would edit the "
                        "shared geometry of every actor built from the same asset");
        }
        return false;
    }
    return true;
}

bool character_model_is_current(uint32_t id)
{
    if (!character_model_is_available(id)) {
        return false;
    }
    if (swap_is_live()) {
        return id == state.borrowed_id;
    }
    return (int32_t)id == home_candidate();
}

/* Saying it out loud, so the other machine can wear it too.
 *
 * A model swap replaces the mesh on the body and leaves the actor alone, so nothing in the engine
 * afterwards names the model that is being worn: the geometry block carries the name of the source
 * mesh, and 265 actors share only 228 of those. The multiplayer feature therefore cannot recover
 * this from the engine at all, and it may not call into this DLL to ask. So this tells it, through
 * a record both link the same code to reach.
 *
 * An empty name is published for a player back in his own model, because "he swapped back" and "he
 * never swapped" have to look the same to a reader.
 *
 * Nothing here depends on anybody listening. If the multiplayer feature is not loaded the record
 * is written and never read, which costs one page and no decision. */
static void publish_appearance(void)
{
    static char last[APPEARANCE_MODEL_MAX];
    const char *worn = NULL;
    char        name[APPEARANCE_MODEL_MAX];

    if (swap_is_live()) {
        worn = character_model_roster_asset(state.borrowed_id);
    }
    name[0] = '\0';
    if (worn != NULL) {
        size_t length = strlen(worn);

        if (length + 1u > sizeof name) {
            return;   /* a name this long is not one the resource layer would have loaded */
        }
        memcpy(name, worn, length + 1u);
    }
    if (strcmp(name, last) == 0) {
        state.publish_owed = false;
        return;   /* the serial only moves for a real change, so a reader can trust it */
    }
    /* Through local_look and not through the note itself: the note carries the model AND the
     * size the player is drawn at, and publishing one field would put the other back. The size is
     * not this swap's business: it scales the object to what the target asset asks for, which is
     * that asset's own size and not a factor over it. So it says the model and leaves the rest
     * alone.
     *
     * The multiplayer reads the note and builds the far body from it, so a model worn here is
     * worn on every machine in the session, over the LAN and through the relay alike.
     *
     * `last` moves only when the note took it; a refused write is owed, and
     * character_model_tick says it again the next frame rather than leaving the far machine on
     * the model before. */
    state.publish_owed = !local_look_set_model(name);
    if (!state.publish_owed) {
        memcpy(last, name, strlen(name) + 1u);
    }
}

void character_model_tick(void)
{
    /* A swap that ended without anybody being told, a level change above all, is taken down here:
     * once a frame, at the end of the scene, where none of the multiplayer's windows is open and
     * the block holds this player's own record. The blade guard no longer does it for a body that
     * is not the swapped one. */
    (void)release_a_dead_swap();
    if (state.publish_owed) {
        publish_appearance();
    }
}

bool character_model_select(uint32_t id)
{
    uintptr_t obj = 0;
    uintptr_t thing = 0;
    bool      done;

    if (!character_model_is_available(id) || !player_body(&obj, &thing)) {
        return false;
    }
    if (!swap_is_live()) {
        if ((int32_t)id == home_candidate()) {
            return true;                  /* his own model, and he is already in it */
        }
        done = borrow(id, obj, thing);
    } else if (id == state.borrowed_id) {
        return true;                      /* already wearing it */
    } else if (model_of(id) == state.home_model && state.home_model != 0u) {
        /* Going back is decided on the MODEL the body was built from rather than on its name. The
         * name is what the panel reads to mark a row Current; the pointer is what the swap
         * replaced. */
        done = go_home(obj, thing);
    } else {
        done = borrow(id, obj, thing);
    }
    if (done) {
        publish_appearance();
    }
    return done;
}
