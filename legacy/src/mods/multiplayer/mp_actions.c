/* mp_actions.c: the local player's shots, pushes, sabre actions and weapon changes, caught where
 * the engine makes them.
 *
 * The shot comes from the body module's hull, which sees every spawn and knows which bank fired;
 * the push and the weapon change come from two hulls of this module's own on the engine's
 * starters; the sabre actions come from the four hulls of the sabre module, which hands each one
 * here as an action and an operand. Every hull runs inside the engine's own call, so the queue is
 * filled on the engine thread and drained on it too.
 *
 * The appearance is the one moment with no hull behind it. It is sampled from the same substep
 * the others are queued in, out of the mounted actor's own name and out of the record the feature
 * that owns a model change publishes, because those are the only two places the answer exists.
 */
#include "mp_actions.h"

#include "mp_actions_sabre.h"
#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_world.h"
#include "mp_cells.h"
#include "mp_signatures.h"

#include "common/appearance_note.h"
#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The meter the push spends, read out of the live hero block before the starter zeroes it, and
 * the aux slot whose occupant makes the starter refuse. Both offsets were read out of the push
 * starter itself: its first test after the prologue is of the dword at +0x64 and it returns when
 * that is not zero, and it then spends the float at +0x2C0 into the charge and stores zero over
 * it. The weapon selector writes its own continuation into the same +0x64 once it has decided.
 * So a push attempted while a weapon change or another push runs does nothing on this machine,
 * and the hull notes nothing for it, which keeps the far machine from performing a push this one
 * refused. */
#define HERO_BLOCK_AUX_ACTION  0x64u
#define HERO_BLOCK_FORCE_METER 0x2C0u

/* The slot a weapon change is heading for. The equipped slot beside it at +0x84 is the committed
 * one and is what the body's state carries; this is the one the selector writes at the moment of
 * the press, frames before the commit. The offset comes from the selector's own store: once it
 * has decided, it writes the slot to +0x88 and its continuation to +0x64, one after the other. */
#define HERO_BLOCK_REQUESTED_SLOT 0x88u

/* Which of the four heroes the local player is. The far side's spawn indexes a four entry name
 * table with it, so a value outside that range cannot be carried and is not sent. */
#define HERO_BLOCK_HERO_INDEX 0x6Cu

/* The object the hero block names, and the pose fields of it the engine built the muzzle sphere
 * from: the position, then pitch, yaw and roll in degrees. It is the object's pose and not the
 * record's, because the record is copied into the object only at the end of the substep and the
 * spawner ran in the middle of it, so the two differ by up to one substep of movement, eleven
 * hundredths of a unit at a run. The offsets come from the engine's node sphere function, which
 * builds its matrix from the three angles at +0x3C and adds the position at +0x18; the hero
 * block's pointer to the object is the dword at +0x0C that the same call chain loads first. */
#define HERO_BLOCK_OBJECT 0x0Cu
#define BAP_OBJ_POSITION  0x18u
#define BAP_OBJ_ROTATION  0x3Cu

/* The force bolt's projectile kind. The push's own aux spawns it as a class 1 shot, and the push
 * already travels as its own event whose aux spawns the bolt on the far side; noting the bolt as
 * a shot as well would send a second, unscaled wave. The first build listened to every spawn and
 * did exactly that. The spawner is entered by the player's fire aux (class 1, the weapon's shot
 * kind), the player's force bolt (class 1, kind 11), the player on a tripod (class 1, kind 0x16),
 * the enemy code with its own class, and by itself, re-entrantly, for the muzzle flare, the sub
 * shots, the chunks and the blast sphere, all class 0. That is why the listener hears class 1
 * only and skips this one kind of it. */
#define FORCE_BOLT_KIND 11

typedef void(__cdecl *start_push_fn_t)(void);
typedef void(__cdecl *set_weapon_fn_t)(int32_t mode, int32_t arg);

typedef struct mp_actions_state {
    bool             installed;
    bool             complete;      /* every hull stands; a partial install answers false */
    detour_t         push_hull;
    detour_t         weapon_hull;
    mp_event_queue_t queue;
    uint32_t         tick;
    uint32_t         caught;
    uint32_t         unplaced;
    bool             unplaced_logged;
    bool             weapon_noted;       /* a weapon change was noted at least once */
    uint32_t         weapon_noted_tick;  /* and the tick and slot noted with it, which make the */
    uint8_t          weapon_noted_slot;  /* selector's own second entry a repeat, not a change */

    /* The appearance. The model channel answers a state rather than an edge, so the last answer
     * is kept here and a change is this file's own comparison; the actor's change is an edge the
     * world half holds until it is taken. */
    bool             appearance_seen;
    uint16_t         appearance_scale;   /* hundredths, as the event carries it; 0 is its own */
    char             appearance_model[APPEARANCE_MODEL_MAX];
    bool             appearance_due;     /* say it again even with nothing changed */
    uint32_t         appearances;
    uint32_t         appearances_refused;
    bool             appearance_refused_logged;
} mp_actions_state_t;

static mp_actions_state_t actions;

/* The body's pose at the moment of the catch, out of the object the hero block names. */
static bool read_body_pose(float position[3], float rotation[3])
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  object = 0;

    return block != 0 && memory_try_read_u32(block + HERO_BLOCK_OBJECT, &object) && object != 0u &&
           memory_try_read(object + BAP_OBJ_POSITION, position, 3u * sizeof(float)) &&
           memory_try_read(object + BAP_OBJ_ROTATION, rotation, 3u * sizeof(float));
}

/* The muzzle the spawner was handed is a world point. What travels is that point relative to the
 * body: the difference to the object's position, turned into the object's own axes, and the shot's
 * yaw relative to the body's yaw. The far side puts the same offset onto its puppet wherever that
 * puppet stands, so the bolt leaves the drawn weapon rather than the place the sender was.
 *
 * The full three by three rotation is used and not an upright shortcut, because a body on a
 * slope or in a jump carries a pitch and a roll, and the shortcut would place the muzzle of such
 * a body sideways by the sine of that angle times the weapon's offset. The frame's forward is
 * (-sin yaw, cos yaw, 0) with no sign to configure: that was read product by product out of the
 * engine's Euler to matrix function, and the player's own contact handler agrees independently,
 * its knockback direction being sincos(heading + 180) taken as (-s, c) from code that never
 * touches the matrix builder.
 *
 * An object that does not read drops the shot rather than sending the world point, because a
 * world point puts the bolt where the sender was rather than where the puppet is, which is the
 * very symptom the offset exists to remove. */
static void note_shot(int32_t kind, const float muzzle[3], float pitch, float yaw)
{
    mp_event_t event;
    float      position[3];
    float      rotation[3];
    float      matrix[9];
    float      delta[3];
    int        axis;

    if (kind < 0 || kind > 255 || kind == FORCE_BOLT_KIND || muzzle == NULL) {
        return;
    }
    if (!read_body_pose(position, rotation)) {
        ++actions.unplaced;
        if (!actions.unplaced_logged) {
            actions.unplaced_logged = true;
            log_warning("a shot could not be placed in the body's frame because the object "
                        "behind the hero block did not read; it is not sent, and later ones "
                        "are counted");
        }
        return;
    }
    for (axis = 0; axis < 3; ++axis) {
        delta[axis] = muzzle[axis] - position[axis];
    }
    mp_event_body_matrix(rotation[0], rotation[1], rotation[2], matrix);

    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SHOT;
    event.tick      = actions.tick;
    event.shot_kind = (uint8_t)kind;
    mp_event_to_local(matrix, delta, event.origin);
    event.pitch = pitch;
    event.yaw   = mp_event_wrap360(yaw - rotation[1]);
    mp_event_queue_push(&actions.queue, &event);
    ++actions.caught;
}

static void __cdecl hook_start_push(void)
{
    start_push_fn_t original = (start_push_fn_t)actions.push_hull.original;

    /* Bank 0 is the local player. A swapped bank is the puppet being made to push by the far
     * player's event, and noting that would bounce the event straight back to its sender. */
    if (mp_armed_transport() && mp_bank_active_class() == 1) {
        uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
        uint32_t  aux = 0;
        uint32_t  meter_bits = 0;
        mp_event_t event;

        /* The starter refuses while an aux runs; a refused push is not an event. */
        if (block != 0 && memory_try_read_u32(block + HERO_BLOCK_AUX_ACTION, &aux) && aux == 0 &&
            memory_read_u32(block + HERO_BLOCK_FORCE_METER, &meter_bits)) {
            memset(&event, 0, sizeof event);
            event.kind = MP_EVENT_PUSH;
            event.tick = actions.tick;
            memcpy(&event.charge, &meter_bits, sizeof event.charge);
            mp_event_queue_push(&actions.queue, &event);
            ++actions.caught;
        }
    }
    original();
}

/* The slot the change that just started is heading for, out of the record the selector wrote it
 * into. The selector works four of its five modes out for itself, and folds a re-selection of the
 * equipped weapon into a holster, so the argument the caller passed is not the answer; the record
 * is. A change is a call that filled the aux slot: the selector refuses outright while one runs,
 * and the continuation it installs is what commits the slot later.
 *
 * Its first mode calls the entry point a second time for key one, so the hull runs twice for one
 * change. The tick and slot the inner call noted make the outer one a repeat rather than a second
 * event, and no two changes of the local player's can share a tick because the first one's
 * continuation makes the selector refuse the second. */
static void note_weapon(uintptr_t block)
{
    mp_event_t event;
    uint32_t   aux = 0;
    uint32_t   slot = 0;

    if (!memory_try_read_u32(block + HERO_BLOCK_AUX_ACTION, &aux) || aux == 0u ||
        !memory_read_u32(block + HERO_BLOCK_REQUESTED_SLOT, &slot) ||
        slot >= MP_EVENT_WEAPON_SLOTS) {
        return;
    }
    if (actions.weapon_noted && actions.weapon_noted_tick == actions.tick &&
        actions.weapon_noted_slot == (uint8_t)slot) {
        return;
    }
    actions.weapon_noted      = true;
    actions.weapon_noted_tick = actions.tick;
    actions.weapon_noted_slot = (uint8_t)slot;

    memset(&event, 0, sizeof event);
    event.kind        = MP_EVENT_WEAPON;
    event.tick        = actions.tick;
    event.weapon_slot = (uint8_t)slot;
    mp_event_queue_push(&actions.queue, &event);
    ++actions.caught;
}

/* The aux slot is read before the call and the slot after it, because the call is what fills the
 * one and writes the other. A swapped bank is the puppet being dressed by the far player's own
 * event, and noting that would bounce the event straight back to its sender. */
static void __cdecl hook_set_weapon(int32_t mode, int32_t arg)
{
    set_weapon_fn_t original = (set_weapon_fn_t)actions.weapon_hull.original;
    uintptr_t       block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t        aux = 0;
    bool            local = mp_armed_transport() && mp_bank_active_class() == 1 &&
                            block != 0 &&
                            memory_try_read_u32(block + HERO_BLOCK_AUX_ACTION, &aux) && aux == 0u;

    original(mode, arg);
    if (local) {
        note_weapon(block);
    }
}

/* A sabre action the sabre module caught, stamped and queued like the rest. The bank test is the
 * sabre module's own, made before it calls here. */
static void note_sabre(uint8_t action, uint8_t operand)
{
    mp_event_t event;

    memset(&event, 0, sizeof event);
    event.kind    = MP_EVENT_SABRE;
    event.tick    = actions.tick;
    event.action  = action;
    event.operand = operand;
    mp_event_queue_push(&actions.queue, &event);
    ++actions.caught;
}

/* ==============================================================================================
 * The appearance.
 * ============================================================================================ */

/* Which hero the local player is, refused outright when it is not one the far side's spawn can
 * index. The engine does not clamp this field and a body built from an index past the table's end
 * ends the far machine's process, so an unusable value is a change that does not travel. */
static bool local_hero(uint8_t *out)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  hero = 0;

    if (block == 0 || !memory_read_u32(block + HERO_BLOCK_HERO_INDEX, &hero) ||
        hero >= MP_EVENT_HERO_SLOTS) {
        return false;
    }
    *out = (uint8_t)hero;
    return true;
}

/* Which of the two names goes out when both of them say something.
 *
 * The actor name is what the local player IS. The hero block points at the mounted actor and the
 * actor's own header carries its file name, which covers a change of hero and a change of
 * character, because both of those replace the actor.
 *
 * The model name is what the local player LOOKS LIKE. A model change replaces the mesh on the body
 * and leaves the actor alone, so after one of those the actor name still answers the character
 * underneath and says nothing at all about what anybody can see. It cannot be recovered from the
 * engine either, which is why it arrives as a record from the feature that set it.
 *
 * What travels here is an appearance, so the model wins whenever one is being worn. An empty model
 * means the player is back in his own, which is not the same as no record at all, and in both of
 * those cases the actor is what is seen and what goes out.
 *
 * Recovering the model from the body was tried on paper and refused: the geometry block at the
 * body names the source mesh rather than the actor file, and a census of the shipped actors
 * resolves 265 of them to only 228 such names, the largest colliding group being six. A receiver
 * choosing from that would build the wrong character in those six cases.
 *
 * The name goes out with its KIND, and the receiver builds by the kind, not by the name: a
 * character is an actor to build, a model is worn over the hero's own body, whose clips are the
 * ones the wire's ordinals index. Building both out of the name was once taken for a price worth
 * paying. It gave a model's far body the model file's clip table, and the far player's next weapon
 * change ended the host in the engine's assert. */
static const char *appearance_to_send(const char *model, uint8_t *kind)
{
    if (model[0] != '\0') {
        *kind = (uint8_t)MP_SKIN_MODEL;
        return model;
    }
    *kind = (uint8_t)MP_SKIN_CHARACTER;
    return mp_bridge_world_local_asset();
}

uint16_t mp_actions_own_scale(void)
{
    return actions.appearance_scale;
}

uint16_t mp_actions_scale_hundredths(float scale)
{
    long rounded;

    /* An ordinary body says nothing rather than saying one hundred: a sender that has never seen
     * a scale and one that is at its own size are the same thing to a reader, and the shortest
     * thing to say is the one a build without the two cheats says. Written as a test for being
     * inside the bounds, because a NaN is outside every one of them. */
    if (!(scale > 0.995f) || !(scale < 1.005f)) {
        rounded = (long)(scale * 100.0f + 0.5f);
        if (rounded >= (long)MP_WIRE_SCALE_MIN && rounded <= (long)MP_WIRE_SCALE_MAX) {
            return (uint16_t)rounded;
        }
    }
    return (uint16_t)MP_WIRE_SCALE_NONE;
}

bool mp_actions_note_appearance(uint8_t world_slot, char *worn, size_t bytes, uint8_t *worn_kind)
{
    char        model[APPEARANCE_MODEL_MAX];
    const char *name;
    mp_event_t  event;
    uint8_t     buffer[MP_EVENT_MAX_BYTES];
    uint8_t     kind = (uint8_t)MP_SKIN_CHARACTER;
    uint8_t     hero = 0;
    float       drawn_at = 1.0f;
    size_t      length;
    bool        changed;

    if (worn != NULL && bytes != 0u) {
        worn[0] = '\0';
    }
    /* Inside a bank window the hero block and the mounted actor belong to a far body, so a sample
     * taken there would report the local player as having turned into the puppet and back again
     * once per substep. */
    if (mp_bank_active() != 0u) {
        return false;
    }

    /* The actor's change is an edge, and it is taken whether or not it is the name that goes out:
     * leaving it standing would make the next call see a change that has already been answered. */
    changed = mp_bridge_world_take_local_asset_change(NULL, 0u);

    model[0] = '\0';
    if (!appearance_note_read(model, sizeof model, &drawn_at)) {
        model[0] = '\0';   /* nobody has published one, which is an installation without the
                            * overlay and means the body wears the model its actor came with */
    }
    if (!actions.appearance_seen || strcmp(model, actions.appearance_model) != 0 ||
        mp_actions_scale_hundredths(drawn_at) != actions.appearance_scale) {
        changed = true;
    }
    actions.appearance_seen  = true;
    actions.appearance_scale = mp_actions_scale_hundredths(drawn_at);
    memcpy(actions.appearance_model, model, strlen(model) + 1u);
    if (!changed && !actions.appearance_due) {
        return false;
    }

    name = appearance_to_send(model, &kind);
    if (name[0] == '\0' || !local_hero(&hero)) {
        /* Nothing has been sampled yet, or the block did not read. The wish stands rather than
         * being lost with the edge that was just consumed. */
        actions.appearance_due = true;
        return false;
    }

    length = strlen(name);
    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SKIN;
    event.tick      = actions.tick;
    event.skin_kind = kind;
    event.skin_hero = hero;
    event.skin_slot  = world_slot;
    event.skin_scale = actions.appearance_scale;
    if (length + 1u <= sizeof event.skin_asset) {
        memcpy(event.skin_asset, name, length);
    }

    /* The codec is the authority on what may cross, so a name it will not carry is refused HERE.
     * The send loop drops an event it cannot encode without a word, and a dropped appearance reads
     * in the field as a change that never happened. */
    actions.appearance_due = false;
    if (length + 1u > sizeof event.skin_asset ||
        mp_event_encode(&event, buffer, sizeof buffer) == 0u) {
        ++actions.appearances_refused;
        if (!actions.appearance_refused_logged) {
            actions.appearance_refused_logged = true;
            log_warning("the local player's appearance is not a name this wire can carry, so the "
                        "far side keeps what it has; later refusals are counted, not logged");
        }
        return false;
    }

    mp_event_queue_push(&actions.queue, &event);
    ++actions.caught;
    ++actions.appearances;
    if (worn != NULL && bytes > length) {
        memcpy(worn, name, length + 1u);
    }
    if (worn_kind != NULL) {
        *worn_kind = kind;
    }
    log_info("this player's appearance goes out: world slot %u, hero %u, %s %s",
             (unsigned)world_slot, (unsigned)hero,
             kind == (uint8_t)MP_SKIN_MODEL ? "model" : "actor", name);
    return true;
}

/* The starter sits at 0x0044BCDE in the retail image. Its prologue is the eight byte
 * push ebp / mov ebp, esp / mov eax, [pr], three whole instructions, and the trampoline copies
 * all eight, so the cut falls on an instruction boundary. The puppet module calls the same
 * function to perform the far player's push, which is why the hook looks at the bank first. */
static bool install_push_hull(void)
{
    uintptr_t site = mp_signatures_address(MP_SITE_PLR_START_FORCE_PUSH);

    if (site == 0 ||
        !detour_install(&actions.push_hull, site, (const void *)&hook_start_push,
                        mp_signatures_prologue(MP_SITE_PLR_START_FORCE_PUSH))) {
        log_warning("the push starter is not hulled, so the local player's force pushes do not "
                    "reach the far side as events");
        return false;
    }
    return true;
}

/* The selector sits at 0x0044B268 in the retail image and has five modes behind one entry point,
 * only one of which is handed the slot: mode 0 takes the hero's start loadout, modes 3 and 4 walk
 * to the next or previous owned slot, mode 1 goes through the per hero key map, and every mode
 * then folds a re-selection of the equipped weapon into a holster. That is why the hook reads
 * the record after the call instead of trusting the argument.
 *
 * The puppet reaches the same function to dress its own weapon, so this hull sees that call too;
 * the bank test inside it is what keeps the far player's change from being sent back. */
static bool install_weapon_hull(void)
{
    uintptr_t site = mp_signatures_address(MP_SITE_PLR_SET_WEAPON);

    if (site == 0 ||
        !detour_install(&actions.weapon_hull, site, (const void *)&hook_set_weapon,
                        mp_signatures_prologue(MP_SITE_PLR_SET_WEAPON))) {
        log_warning("the weapon selector is not hulled, so the far side starts the local "
                    "player's weapon changes from the committed slot instead, a draw clip's "
                    "marker later");
        return false;
    }
    return true;
}

bool mp_actions_install(void)
{
    bool   pushes;
    bool   weapons;
    size_t sabre;

    if (actions.installed) {
        return actions.complete;
    }
    actions.installed = true;
    mp_event_queue_init(&actions.queue);
    mp_body_set_shot_listener(&note_shot);

    pushes  = install_push_hull();
    weapons = install_weapon_hull();
    sabre   = mp_actions_sabre_install(&note_sabre);

    actions.complete = pushes && weapons && sabre == (size_t)MP_ACTIONS_SABRE_HULLS;
    if (actions.complete) {
        log_info("the local player's shots, force pushes, weapon changes and sabre actions are "
                 "caught as events for the wire");
    } else {
        log_warning("the local player's shots are caught as events for the wire; pushes %s, "
                    "weapon changes %s, and %u of %u sabre hulls stand, the missing ones named "
                    "above",
                    pushes ? "are too" : "are not", weapons ? "are too" : "are not",
                    (unsigned)sabre, (unsigned)MP_ACTIONS_SABRE_HULLS);
    }
    return actions.complete;
}

void mp_actions_set_tick(uint32_t tick)
{
    actions.tick = tick;
}

void mp_actions_note_moment(const mp_event_t *moment)
{
    mp_event_t stamped;

    if (moment == NULL) {
        return;
    }
    stamped      = *moment;
    stamped.tick = actions.tick;
    mp_event_queue_push(&actions.queue, &stamped);
    ++actions.caught;
}

bool mp_actions_peek(mp_event_t *out)
{
    return mp_event_queue_peek(&actions.queue, out);
}

bool mp_actions_pop(mp_event_t *out)
{
    return mp_event_queue_pop(&actions.queue, out);
}

void mp_actions_clear(void)
{
    uint32_t dropped = actions.queue.dropped;

    mp_event_queue_init(&actions.queue);
    actions.queue.dropped = dropped;
    actions.weapon_noted  = false;   /* the repeat test is about one call, not across a peer */

    /* A peer that has just arrived has not been told what this player looks like, and the actor's
     * change that would have said so was consumed by whoever was there before. So the appearance
     * is owed again, and the next sample sends it whether or not anything moved. */
    actions.appearance_due = true;
}

void mp_actions_owe_appearance(void)
{
    actions.appearance_due = true;
}

uint32_t mp_actions_appearances(void)
{
    return actions.appearances;
}

uint32_t mp_actions_appearances_refused(void)
{
    return actions.appearances_refused;
}

uint32_t mp_actions_caught(void)
{
    return actions.caught;
}

uint32_t mp_actions_dropped(void)
{
    return actions.queue.dropped;
}

uint32_t mp_actions_unplaced(void)
{
    return actions.unplaced;
}
