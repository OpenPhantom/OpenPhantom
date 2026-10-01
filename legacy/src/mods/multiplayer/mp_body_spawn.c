/* mp_body_spawn.c: a far body is built, taken down and built again, out of a named actor.
 *
 * SIZE NOTE: over 600 lines, one responsibility, and the length is the ordering rather than the
 * code. The build, the teardown and the recorded appearance are one subject because each is the
 * other two's precondition: an appearance may only be carried out through a teardown, and a
 * teardown is only ever followed by a build. Splitting them would put the order that is the whole
 * design on two sides of a translation unit. The seam, if it grows again, is the appearance half,
 * the wish and its deadline, which touches the engine at no point and would test without a game.
 *
 * Left mp_body.c when a far body learned to wear the far player's own actor. That file's own size
 * note had named this seam and measured it: the build reads the player's block once and never
 * again, while the dispatcher, the shot hull and the tick run every substep and never read it.
 * The record they share is in mp_body_internal.h, the proof that a name is safe to spawn is in
 * mp_body_asset.c, and the blade a Jedi body keeps after its spawn is chosen in mp_blade.c; what
 * is here is the order the steps have to happen in.
 *
 * ================================ Why the order is the design =================================
 *
 * DOWN BEFORE UP, always. The spawn takes an object slot out of a pool of 255 that also serves
 * every shot, every corpse and every muzzle flare, and the release gives one back. Building the
 * new body before taking the old one down would hold two at once against a wall the engine has
 * been measured hitting with 252 shots in a single substep.
 *
 * Nothing inside an open window. Both halves swap the player pointer, and the bank refuses a swap
 * while another is active. So the whole of this runs from the substep task slot, with nothing of
 * ours swapped in, and it says so and refuses rather than half doing it.
 *
 * A wish rather than an act. The engine will not spawn or place anybody unless a level is running
 * and the player module is started, and the caller that learns of an appearance is not standing
 * in the task slot when it learns. So a change is recorded and carried out from the tick, retried
 * while it cannot be, and dropped with a line when it has waited too long. A wish that quietly
 * disappeared would show up as "the far player changed and nothing happened".
 *
 * The down path goes through the resolved site. player_despawn clears the module state word at
 * the hero block's +0x04 ABSOLUTELY, outside its own null test, so a call that did not go through
 * the lifecycle hull's block loan would put bank 0 into moduleState 0 and the local player would
 * stop existing. And the window for it is the PERSISTENT one: the refreshing swap makes the bank
 * a fresh copy of bank 0 first, which would hand the local player's own object and resource
 * reference to the release.
 */
#include "mp_body.h"

#include "mp_body_asset.h"
#include "mp_body_gate.h"
#include "mp_body_internal.h"
#include "mp_body_wear.h"

#include "mp_bank.h"
#include "mp_blade.h"
#include "mp_blade_draw.h"
#include "mp_capacity.h"
#include "mp_cells.h"
#include "mp_phases.h"
#include "mp_pool.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Who is told that a body has been taken down and is about to be built again. One listener; the
 * installer wires it, and until it does the teardown says so. */
static mp_body_rebuilt_fn_t rebuilt_listener;
static bool                 rebuilt_listener_warned;

void mp_body_set_rebuilt_listener(mp_body_rebuilt_fn_t listener)
{
    rebuilt_listener = listener;
}

typedef void(__cdecl *spawn_hero_fn_t)(int32_t hero_index, const void *at, float heading);
typedef void(__cdecl *set_active_fn_t)(int32_t hero_index);
typedef void(__cdecl *despawn_fn_t)(void);

/* How long an appearance is worth carrying, in substeps of the ladder that runs this tick.
 *
 * Three things stop a rebuild and only one of them lasts. A bank window is open for the length of
 * one call. A level is between loads for a few seconds. The third is that there is no level at
 * all, and that one does not end: a change recorded as a level ends would otherwise be carried out
 * minutes later, on a body the player is looking at, for a reason nobody can see. Ten seconds at
 * the shipped 32 substeps a second is more than an order of magnitude above the two waits that
 * are real and finite, for the sake of the one that is not. An expired wish says so, because the
 * symptom otherwise reported is "he changed and my screen did not". The ladder is the engine's:
 * its substep runner at 0x004756FC selects 1/32 unless the cell at 0x00882294 holds 1. */
#define WISH_DEADLINE_SUBSTEPS 320u

/* Three attempts, then rest.
 *
 * The refusal flag used to be a latch that the first refusal set and nothing cleared, so one
 * transient reason, a bank that happened to be active or a level in the middle of a load, cost the
 * far player his body for the rest of the session. It cannot simply be dropped either: a pool that
 * is genuinely full would then be reported once per substep, and every reason below writes a line
 * where it is found. A small cap is both. A new appearance clears the count, because a different
 * asset is a different question. */
#define SPAWN_ATTEMPTS_BEFORE_REST 3u

static void note_spawn_failure(mp_body_far_t *far)
{
    ++far->rebuild_refusals;
    if (++far->spawn_attempts >= SPAWN_ATTEMPTS_BEFORE_REST) {
        far->failed = true;
    }
}

/* ==============================================================================================
 * Reading the block.
 * ============================================================================================ */

/* Read player 0's position and heading out of the real hero block, before anything is swapped.
 * The body is spawned at exactly that point so the two cylinders overlap and the push is
 * immediate and visible; player 0 stands somewhere valid, so no wall is risked.
 *
 * A rebuild uses the same point rather than the body's own last one, and it costs nothing: a far
 * body in a session is a puppet, and the puppet's placement writes its position into the block
 * every substep, so the spawn point is what the body shows for less than one substep. A far body
 * that is NOT a puppet is the provocation configuration, where it is a body of ours standing next
 * to the player anyway.
 *
 * The three offsets are how the spawn at 0x00447E58 consumes its own arguments: after the clear
 * of the whole 0x3AC byte block it stores the position it was handed as three floats at +0x118,
 * the heading at +0x2A0, and the hero index it was given at +0x6C, with the weapon at +0x84
 * beside them. What it leaves behind, in order: the body handle at +0x0C out of the pool
 * allocator at 0x0041223E (one of 255 slots), the actor reference at +0x00 out of the resource
 * loader, the bind at 0x00412458 with no null test in front of it, the body's previous position,
 * position, previous rotation and rotation at +0x54, +0x18, +0x60 and +0x3C, the present flag,
 * both class words set to 1 (the stores at 0x00447FD8 and 0x00447FE7) and impact code 0x3D, the
 * handler task at +0xA4, a play of clip 0, the contact slot store, the active status setter at
 * 0x00459AB7 with the index it was given, six node lookups into +0x40 to +0x54, the jedi arm for
 * index 0 or 1 (0x00449835 and 0x00449977), the animation set, the stand entry at 0x0044CD1C,
 * the module state at +0x04 set to 1 and the dead flag at +0x394 cleared. */
static bool read_player0_spawn(float pos[3], float *heading, int32_t *hero_index)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  raw;
    int       axis;

    if (block == 0) {
        return false;
    }
    for (axis = 0; axis < 3; ++axis) {
        if (!memory_read_u32(block + HERO_BLOCK_POS + (unsigned)axis * 4u, &raw)) {
            return false;
        }
        memcpy(&pos[axis], &raw, sizeof(float));
    }
    if (!memory_read_u32(block + HERO_BLOCK_HEADING, &raw)) {
        return false;
    }
    memcpy(heading, &raw, sizeof(float));
    if (!memory_read_u32(block + HERO_BLOCK_HERO_INDEX, &raw)) {
        return false;
    }
    *hero_index = (int32_t)raw;
    return true;
}

/* Stamp the bank's co-op class onto the freshly spawned body and seed its world position, both
 * directly on the object. While the bank is swapped in, the player pointer cell holds the bank
 * block address, and the body handle sits at +0x0C of it as a raw bapObj pointer.
 *
 * The position seed is what makes the body visible. spawnHero seeds obj->pos from a hero-block
 * field its own rep stosd has just zeroed, so without a tick the body sits at the world origin and
 * is frustum-culled. The draw path is otherwise fully tick-independent: it collects from the
 * object list on (pActor, PRESENT), and builds the pose, the skeleton and the animation itself
 * every frame. So one write of pos, with prevPos equal to it, is enough for a standing body.
 *
 * That was a control pass over the draw path, not an assumption. The object draw at 0x00411028
 * collects candidates on two conditions only, the actor pointer at obj+0x14 being non zero and
 * bit 0 of the flags, with no class filter and no ticked this frame bit; it builds the world
 * matrix from the object's own position at +0x18 interpolated against the previous position at
 * +0x54, dispatches the render handle at +0x9C, and advances the animation tracks itself. There
 * is no read of the player pointer anywhere in it. prevPos has to equal pos, or that per substep
 * interpolation shivers the body between the origin and its place. */
static void stamp_body(size_t index, const float pos[3])
{
    mp_body_far_t *far = mp_body_far_at(index);
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t  block_addr = 0;
    uint32_t  object = 0;
    uint32_t  pos_bits[3];
    int32_t   class_word = mp_bank_class_of(index);
    int       axis;

    if (far == NULL || pr_cell == 0 || !memory_read_u32(pr_cell, &block_addr) || block_addr == 0) {
        log_error("bank %u's body could not be finished: the bank block is unreadable",
                  (unsigned)index);
        return;
    }
    if (!memory_read_u32(block_addr + HERO_BLOCK_HACTOR, &object) || object == 0) {
        log_error("bank %u's body has no object handle, so it cannot be finished", (unsigned)index);
        return;
    }

    /* The cylinder as the spawn bound it, read before anything else touches the object: it is
     * what a death clip zeroes and what the restore puts back. It belongs to the model that was
     * just bound, which is why a rebuild has to forget the old one rather than carry it over. */
    far->cylinder_saved = memory_read_u32(object + BAPOBJ_CYLINDER_RADIUS,
                                         &far->cylinder_radius_bits) &&
                          memory_read_u32(object + BAPOBJ_CYLINDER_HEIGHT,
                                          &far->cylinder_height_bits);
    if (!far->cylinder_saved) {
        log_warning("bank %u's body's cylinder did not read at the spawn, so its collision cannot "
                    "be restored after a death clip", (unsigned)index);
    }

    patch_write_u32(object + BAPOBJ_OBJ_CLASS, (uint32_t)class_word);
    patch_write_u32(object + BAPOBJ_SHOOTER_CLASS, (uint32_t)class_word);
    /* The spawn gave it the player's node, whose slot is empty while the local player is dead;
     * in a session it takes a node of its own, so a contact on it reaches the dispatcher then. */
    mp_body_gate_give_node(index, object);

    for (axis = 0; axis < 3; ++axis) {
        memcpy(&pos_bits[axis], &pos[axis], sizeof(uint32_t));
        patch_write_u32(object + BAPOBJ_POS + (unsigned)axis * 4u, pos_bits[axis]);
        patch_write_u32(object + BAPOBJ_PREV_POS + (unsigned)axis * 4u, pos_bits[axis]);
    }

    log_info("bank %u's body stands: object %08X, class %d, at player 0's position",
             (unsigned)index, (unsigned)object, (int)class_word);
}

/* ==============================================================================================
 * The appearance a body is asked to wear.
 * ============================================================================================ */

/* Lower case, cut at the first zero byte, bounded. An empty answer means "whatever the slot's own
 * entry names", which is the ordinary case and not a failure.
 *
 * The identity an appearance is compared by is this name and not the hero index. Measured over
 * the 265 unique shipped actors, the name at +0x08 of the actor is the file name in 265 of 265,
 * and two of them carry two stray bytes after the terminator, which a raw 32 byte comparison
 * reports as a difference for ever; hence the cut. The hero index cannot serve: it has four
 * values, and the character feature puts all 164 of its profiles on one of them. */
static void normalise_asset(const char *asset, char *out, size_t bytes)
{
    size_t index;

    out[0] = '\0';
    if (asset == NULL) {
        return;
    }
    for (index = 0; index + 1u < bytes && asset[index] != '\0'; ++index) {
        char c = asset[index];

        out[index] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[index] = '\0';
}

bool mp_body_set_scale_at(size_t index, float scale)
{
    mp_body_far_t *far = mp_body_far_at(index);

    /* The bounds are the wire's own, in the units a caller thinks in. Written as a test for being
     * INSIDE, because a NaN compares false against every bound and a body drawn at NaN is a body
     * that is not drawn at all. */
    if (far == NULL || !(scale >= MP_BODY_SCALE_MIN) || !(scale <= MP_BODY_SCALE_MAX)) {
        return false;
    }
    far->scale_wanted = scale;
    return true;
}

bool mp_body_set_asset_at(size_t index, int32_t hero, const char *asset)
{
    mp_body_far_t *far = mp_body_far_at(index);
    char           want[MP_BODY_ASSET_MAX];

    if (far == NULL || !mp_body_module_ready()) {
        return false;
    }
    if (hero < 0 || hero >= MP_BODY_ASSET_SLOTS) {
        log_warning("bank %u was asked for hero %d, which is not one of the %u the spawn knows; "
                    "the appearance is refused", (unsigned)index, (int)hero,
                    (unsigned)MP_BODY_ASSET_SLOTS);
        return false;
    }
    normalise_asset(asset, want, sizeof want);

    /* IDEMPOTENT, and it has to be twice over. The far side's appearance is state and is resent,
     * so without this every repetition would take a standing body down and build it again, and
     * the body would be missing for part of every substep it was described in. The fields compared
     * are what the body wears or last tried to wear, which is the second half: after an attempt
     * the spawn refused, a repetition must not clear the refusal latch and try again every
     * substep, or a pool that is simply full becomes a log storm. A genuinely different appearance
     * does clear it and is tried. */
    if (!far->wish && far->hero == hero && strcmp(far->asset, want) == 0) {
        return true;
    }
    if (far->wish && far->wish_hero == hero && strcmp(far->wish_asset, want) == 0) {
        return true;   /* the same wish is already standing and still within its deadline */
    }

    far->wish      = true;
    far->wish_hero = hero;
    far->wish_age  = 0u;
    (void)text_format(far->wish_asset, sizeof far->wish_asset, "%s", want);
    log_info("bank %u is asked to wear hero %d as %s; it is carried out from the substep tick",
             (unsigned)index, (int)hero,
             want[0] != 0 ? want : "that hero's own shipped asset");
    return true;
}

bool mp_body_set_second_asset(int32_t hero, const char *asset)
{
    return mp_body_set_asset_at(1u, hero, asset);
}

/* Which hero table entry a body wearing `asset` has to ride, and whether the slot's name has to be
 * borrowed for it. An empty asset, or one the wanted hero's own entry already names, rides that
 * hero with no borrow at all; anything else rides the foreign slot. The slot's name is read live
 * rather than from a table taken at installation, because another fix owns the same entries while
 * the local player rides a character of its own. */
static int32_t choose_slot(int32_t hero, const char *asset, bool *borrow)
{
    char slot_name[MP_BODY_ASSET_MAX];

    *borrow = false;
    if (asset == NULL || asset[0] == '\0') {
        return hero;
    }
    if (mp_body_asset_slot_name(hero, slot_name, sizeof slot_name) &&
        strcmp(slot_name, asset) == 0) {
        return hero;
    }
    *borrow = true;
    return MP_BODY_ASSET_FOREIGN_SLOT;
}

/* ==============================================================================================
 * Taking a body down.
 * ============================================================================================ */

/* The despawn at 0x00448201, whole: when the handle at +0x0C is non zero it resets the blade size
 * and releases the light slot for hero index 0 or 1, frees the object through the pool at
 * 0x0044825A, hands the actor reference back through the release at 0x0044826B, and zeroes both
 * fields; then, OUTSIDE that test and through an absolute address, it stores 0 to 0x006CF644 at
 * 0x0044828B. The player pointer holds 0x006CF640, so that cell is the hero block's +0x04, the
 * module state, and a call that did not go through the lifecycle hull's block loan would leave
 * bank 0 at module state 0: the respawn and everything else that tests the word would then
 * simply return, and the local player would stop existing without a line. The release at
 * 0x0044826B is also the only place in the image that gives a hero actor reference back, which
 * is the second half of why a rebuild is a despawn plus a spawn rather than a rebind. */
bool mp_body_teardown_at(size_t index)
{
    mp_body_far_t *far = mp_body_far_at(index);
    uintptr_t      despawn_site;
    bool           was_spawned;
    bool           had_cylinder;

    if (far == NULL || !mp_body_module_ready()) {
        return false;
    }
    if (!far->spawned) {
        return true;   /* nothing stands, which is the state the caller wanted */
    }
    despawn_site = mp_signatures_address(MP_SITE_PLAYER_DESPAWN);
    if (despawn_site == 0) {
        log_warning("bank %u's body cannot be taken down: the despawn site did not resolve, and "
                    "the address must not be called any other way", (unsigned)index);
        return false;
    }
    if (mp_bank_active() != 0u) {
        return false;   /* a window is open; the caller retries from the task slot */
    }

    /* Off the wire's paths before anything is freed. The tick and the snapshot builder both key
     * on `spawned`, and both would otherwise walk an object that is about to go back to the pool.
     * The cylinder goes with it: it belongs to the model that is leaving, and a death clip's
     * restore would put that model's collision onto whatever stands next. */
    was_spawned         = far->spawned;
    had_cylinder        = far->cylinder_saved;
    far->spawned        = false;
    far->cylinder_saved = false;

    if (!mp_bank_swap_in_persistent_at(index)) {
        far->spawned        = was_spawned;
        far->cylinder_saved = had_cylinder;
        ++far->rebuild_refusals;
        log_warning("bank %u's body cannot be taken down: the persistent window did not open, and "
                    "a half taken down body is worse than a standing one", (unsigned)index);
        return false;
    }
    /* The take down parks a Jedi's blade at full length on the mesh the body draws, which is the
     * asset's; it parks a copy instead. */
    mp_blade_draw_guard_despawn(index);
    ((despawn_fn_t)despawn_site)();
    mp_blade_draw_guard_close();
    mp_bank_swap_out();

    mp_body_forget_body_state(far);
    mp_phases_note_revived_at(index);   /* the dead latch belonged to the body that just left */
    /* Both once-only latches fall, or a rebuild would be refused before it began. The asset is
     * deliberately kept: it says what this body is MEANT to wear, and a teardown that is not part
     * of an appearance change, a peer leaving and coming back, should bring the same body back
     * rather than fall silently to a shipped hero. An appearance change overwrites it anyway. */
    far->failed         = false;
    far->spawn_attempts = 0u;
    far->slot           = -1;
    far->wearing_asset  = false;
    ++far->serial;      /* before the listener, which tells the overlay about this body */

    /* The puppet's clip, weapon and sabre state describe a skeleton that no longer exists. Carried
     * over, a clip ordinal would be resolved against the new actor's table and mean something
     * else, which is the quiet kind of wrong: a running body, the correct number, the wrong
     * animation. It is a listener rather than a direct call because nothing else here knows about
     * a puppet, and an unwired listener says so loudly instead of leaving the fault to be found in
     * the field. */
    if (rebuilt_listener != NULL) {
        rebuilt_listener(index);
    } else if (!rebuilt_listener_warned) {
        rebuilt_listener_warned = true;
        log_warning("bank %u's body was taken down with nothing listening for it: the puppet's "
                    "clip, weapon and sabre state still describe the skeleton that just left, so "
                    "every ordinal from the wire will mean something else until mp_puppet_reset "
                    "is reached. Wire mp_body_set_rebuilt_listener", (unsigned)index);
    }
    return true;
}

/* Carry out a recorded appearance, or decide it cannot be carried out yet. Runs from the spawn
 * tick, which is the one place with no window open. */
static void carry_out_wish(size_t index, mp_body_far_t *far)
{
    if (!far->wish) {
        return;
    }
    if (far->spawned && far->hero == far->wish_hero &&
        strcmp(far->asset, far->wish_asset) == 0) {
        far->wish = false;      /* already so; nothing is torn down for a repetition */
        return;
    }
    if (mp_bank_active() != 0u) {
        return;                 /* not here, and not a failure */
    }
    if (++far->wish_age > WISH_DEADLINE_SUBSTEPS) {
        far->wish = false;
        ++far->rebuild_refusals;
        log_warning("bank %u was asked to wear hero %d as %s %u substep(s) ago and the body could "
                    "not be rebuilt since, so the appearance is dropped rather than applied at "
                    "some later moment nobody chose it", (unsigned)index, (int)far->wish_hero,
                    far->wish_asset[0] != 0 ? far->wish_asset : "that hero's own shipped asset",
                    (unsigned)far->wish_age);
        return;
    }
    if (!mp_body_teardown_at(index)) {
        return;                 /* counted and logged there; the wish stands and is retried */
    }

    /* The refusal latch falls with the appearance that set it. A teardown clears it when it really
     * took a body down; this covers the other case, where the last attempt never produced a body
     * to take down and the latch is all that is left of it. */
    far->failed         = false;
    far->spawn_attempts = 0u;
    far->hero           = far->wish_hero;
    (void)text_format(far->asset, sizeof far->asset, "%s", far->wish_asset);
    far->wish = false;
    ++far->rebuilds;
}

/* ==============================================================================================
 * Building one.
 * ============================================================================================ */

/* What the bank's block ended up naming as its actor, against what the proof handed back. Read
 * out of the bank's own persistent block after the window has closed, which is the same bytes the
 * window wrote and is reachable without opening a second one. */
static void measure_the_bind(size_t index, void *probed)
{
    uint32_t bound = 0;

    if (probed == NULL) {
        return;
    }
    if (!mp_bank_read_at(index, MP_HERO_BLOCK_HERO_ACTOR, &bound, sizeof bound)) {
        log_warning("bank %u's block did not read back after the spawn, so the borrowed name "
                    "cannot be held against what the body was built from", (unsigned)index);
        return;
    }
    mp_body_asset_note_bound(index, bound, probed);
}

/* What one build of a far body carries from its checks into the spawn window: the two engine
 * sites, where player 0 stands and which hero it is, and which slot the body is built in out of
 * which name. */
typedef struct build_plan {
    uintptr_t spawn_site;
    uintptr_t active_site;
    float     pos[3];
    float     heading;
    int32_t   local_hero;
    int32_t   slot;
    bool      borrow;
    bool      wearing;
    void     *probed;
} build_plan_t;

/* Whether a build may start now: both sites resolve, the pool has room past the reserve, and
 * player 0 reads as one of the four heroes. Every refusal is counted and said. */
static bool plan_may_start(size_t index, mp_body_far_t *far, build_plan_t *plan)
{
    const mp_capacity_reading_t *reading;

    plan->spawn_site  = mp_signatures_address(MP_SITE_PLAYER_SPAWN_HERO);
    plan->active_site = mp_signatures_address(MP_SITE_STATUS_SET_ACTIVE);
    if (plan->spawn_site == 0 || plan->active_site == 0) {
        note_spawn_failure(far);
        log_warning("bank %u's body cannot spawn: a required site did not resolve",
                    (unsigned)index);
        return false;
    }

    /* The pool must have room past the reserve for the body's object and its actor. The spawn
     * calls thing_alloc directly, so the check is here rather than at the pool latch, and a
     * refusal is a clean skip rather than a crash inside the engine's unchecked allocation. */
    mp_capacity_sample();
    reading = mp_capacity_last();
    if (!reading->objects_readable ||
        !mp_pool_may_take(reading->objects_live + 1u, reading->objects_capacity,
                          mp_pool_reserve())) {
        note_spawn_failure(far);
        log_warning("bank %u's body is not spawned: the pool has no room past the reserve",
                    (unsigned)index);
        return false;
    }

    if (!read_player0_spawn(plan->pos, &plan->heading, &plan->local_hero)) {
        note_spawn_failure(far);
        log_warning("bank %u's body cannot spawn: player 0's position is unreadable",
                    (unsigned)index);
        return false;
    }
    if (plan->local_hero < 0 || plan->local_hero >= MP_BODY_ASSET_SLOTS) {
        /* Not one of the four heroes, so the status index the spawn has to be paired with is not
         * one this build understands. Refuse rather than hand it on. */
        note_spawn_failure(far);
        log_warning("bank %u's body is not spawned: the local player is hero %d, which is outside "
                    "the %u the spawn knows", (unsigned)index, (int)plan->local_hero,
                    (unsigned)MP_BODY_ASSET_SLOTS);
        return false;
    }
    return true;
}

/* Which hero slot the body is built in, and out of which name: the far player's choice once the
 * name is proven mounted, the hero this machine stands in when it is not. */
static void plan_the_slot(size_t index, mp_body_far_t *far, build_plan_t *plan)
{
    /* Before this module existed a far body was spawned as the LOCAL player's hero, and the
     * reason written here was that this is the one template proven loaded. The proof is what that
     * reason was really about, and it is now a call rather than an assumption. Without an asset
     * layer, or with nothing named, the body falls back to exactly the old behaviour. */
    if (far->hero < 0 || far->hero >= MP_BODY_ASSET_SLOTS) {
        far->hero     = plan->local_hero;
        far->asset[0] = '\0';
    }
    plan->slot    = choose_slot(far->hero, far->asset, &plan->borrow);
    plan->wearing = far->asset[0] != 0;

    /* PROVE THE NAME, and prove it whichever name it is.
     *
     * A borrowed asset is the obvious case. A hero the FAR player chose is exactly as dangerous
     * and was the hole this nearly shipped with: hero 2 asked for on a level that mounted only
     * hero 0 reaches the same unchecked bind through the slot's OWN shipped name, and the far
     * side chooses that number. The one name that needs no proof is the local player's own hero,
     * because he is standing in it. */
    if (plan->slot != plan->local_hero) {
        char                    wear[MP_BODY_ASSET_MAX];
        mp_body_asset_verdict_t verdict;

        wear[0] = 0;
        if (plan->borrow) {
            (void)text_format(wear, sizeof wear, "%s", far->asset);
        } else if (!mp_body_asset_slot_name(plan->slot, wear, sizeof wear)) {
            wear[0] = 0;
        }

        verdict = mp_body_asset_probe(wear, &plan->probed);
        if (verdict != MP_BODY_ASSET_OK) {
            /* Refused here rather than handed to a bind that would show a message box and end the
             * process for everyone in the room. The build falls back to the hero this machine is
             * standing in, which is proven mounted; what the far side ASKED for is kept, so a
             * repetition of the same request does not tear the body down and try again. */
            log_warning("bank %u is not built out of %s: it is %s. The body falls back to hero %d, "
                        "the one this machine is wearing", (unsigned)index,
                        wear[0] != 0 ? wear : "an unnamed asset",
                        mp_body_asset_verdict_text(verdict), (int)plan->local_hero);
            mp_body_asset_release_probe();
            plan->probed  = NULL;
            plan->borrow  = false;
            plan->wearing = false;
            plan->slot    = plan->local_hero;
        }
    }
}

/* The spawn inside the bank's window, and everything it moved put back before anything else
 * runs. False when the name could not be lent or the window did not open. */
static bool build_in_window(size_t index, mp_body_far_t *far, build_plan_t *plan)
{
    bool     slot_kept = false;
    uint32_t kept_slot = 0u;

    if (plan->borrow && !mp_body_asset_lend_name(plan->slot, far->asset)) {
        mp_body_asset_release_probe();
        note_spawn_failure(far);
        log_warning("bank %u's body is not spawned: hero slot %d could not be lent the name %s",
                    (unsigned)index, (int)plan->slot, far->asset);
        return false;
    }

    if (!mp_bank_swap_in_for_spawn(index, plan->local_hero)) {
        if (plan->borrow) {
            mp_body_asset_return_name(plan->slot);
        }
        mp_body_asset_release_probe();
        note_spawn_failure(far);
        log_warning("bank %u's body cannot spawn: the spawn's window did not open; the bank's "
                    "own line says why the first time", (unsigned)index);
        return false;
    }

    /* Through the resolved site so the lifecycle hull's block loan carries the absolute clear into
     * the bank instead of wiping bank 0. The sabre arm inside it reads the blade mesh the body
     * binds and folds it, and that mesh is every body's of the asset, the local player's too; for
     * this one call it is pointed at a copy of itself, and it gets its own vertices back before
     * anything else runs. */
    mp_blade_draw_guard_spawn(index, plan->slot, plan->local_hero, plan->probed);
    /* The spawn writes the engine's own handler into the LOCAL player's contact slot, whatever
     * it held. Held empty, that slot is the engine's refusal of a corpse and of a body on its way
     * back; left as the spawn wrote it, a far body built while this player lay dead would let the
     * next contact kill the corpse again. So it goes back to exactly what it was. */
    slot_kept = mp_body_gate_keep_local_slot(&kept_slot);
    ((spawn_hero_fn_t)plan->spawn_site)(plan->slot, plan->pos, plan->heading);
    if (slot_kept) {
        mp_body_gate_put_back_local_slot(kept_slot);
    }
    mp_blade_draw_guard_close();

    /* The name goes back at once, while nothing but this call has run. Left in place it would
     * decide which asset the LOCAL player's next respawn into that slot is built out of. */
    if (plan->borrow) {
        mp_body_asset_return_name(plan->slot);
    }

    stamp_body(index, plan->pos);

    /* Put the active status index, and the shared inventory and key bits it moves, back on the
     * player's own hero. This is the LOCAL hero and never the far player's: the status record
     * carries health, force, thirteen ammunition counts and the key bits, and those belong to the
     * player sitting here. The spawn itself calls the setter with the index it was given and
     * would otherwise leave the six story flag bytes on that hero's record, which is the one
     * pairing the control pass required. An early field run showed a blind restore to 0 turned
     * the player into Obi-Wan on any level whose hero is not 0.
     *
     * This puts back the index and the pointer, not the contents: it loads the six bytes out of
     * the local record as its last status_setActivePlayer left them. The window's swap out below
     * gives back the bytes and all four records, the one a far body's starting kit was written
     * into included, and it has to come after this, the spawn's last engine writer. */
    ((set_active_fn_t)plan->active_site)(plan->local_hero);

    mp_bank_swap_out();

    measure_the_bind(index, plan->probed);
    mp_body_asset_release_probe();   /* the spawn has taken its own reference; ours can go */

    /* The blade vectors, settled before the body counts as standing. The sabre arm inside the
     * spawn read them off a mesh this body shares with every body of the same asset, which one
     * of those may have folded or half grown; the longest reading anybody has of that asset
     * takes their place. The arm branches on the SLOT, so a body riding the foreign slot has no
     * blade to settle and is left alone there. */
    mp_blade_after_far_spawn(index);
    return true;
}

static void spawn_at_inner(size_t index)
{
    mp_body_far_t *far  = mp_body_far_at(index);
    build_plan_t   plan = { 0u, 0u, { 0.0f, 0.0f, 0.0f }, 0.0f, 0, 0, false, false, NULL };

    if (far == NULL || !mp_body_module_ready()) {
        return;
    }
    /* A bank nobody occupies has no body, and a body whose bank has emptied goes now. The wish is
     * deliberately left standing: an appearance that arrived just as the player left describes
     * the body that will be built when they come back. */
    if (!far->occupied) {
        if (far->spawned && mp_body_teardown_at(index)) {
            log_info("bank %u's body is taken down: no far player occupies it any more",
                     (unsigned)index);
        }
        return;
    }
    carry_out_wish(index, far);
    if (far->spawned || far->failed) {
        return;
    }

    if (!plan_may_start(index, far, &plan)) {
        return;
    }
    plan_the_slot(index, far, &plan);
    if (!build_in_window(index, far, &plan)) {
        return;
    }

    far->slot           = plan.slot;
    far->wearing_asset  = plan.wearing;
    far->spawned        = true;
    ++far->serial;
    far->spawn_attempts = 0u;

    /* The slot holds what it held before the spawn. The dispatcher is still asked, because the
     * engine's own handler may have been standing there before the first far body of a session
     * was built; it takes a slot only from a real handler and leaves an empty one empty. */
    mp_body_arm_dispatcher();

    log_info("bank %u's body has been spawned at player 0's position: the far player's hero %d "
             "riding slot %d as %s, class %d", (unsigned)index, (int)far->hero, (int)plan.slot,
             plan.wearing ? far->asset : "that slot's own asset",
             (int)mp_bank_class_of(index));
}

/* The build, then the wear tick, on every way out of the build. The build returns early for a
 * body that stands, which is every substep but the first, and the tick has to see exactly that
 * body: whether it wears something, what the overlay answered for it and what size it is. */
void mp_body_spawn_at(size_t index)
{
    spawn_at_inner(index);
    mp_body_wear_tick(index);
}
