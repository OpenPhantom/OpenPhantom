/* mp_start_wish.c: the pose and the hero a lobby asks for, held until a level can take them.
 *
 * The lever into a level fires in the title menu, and the engine is not ready for either wish at
 * that moment: there is no body to seat and no player module to swap. So each wish is held, the
 * two gates it waits for are read every frame, and it is carried out once, retried never, and
 * dropped at a deadline rather than applied at a moment nobody chose. The hero goes first when
 * both are held, because the swap zeroes the block a pose would have been written into.
 */
#include "mp_start.h"
#include "mp_start_wish.h"

#include "mp_cells.h"
#include "mp_lobby.h"
#include "mp_respawn.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void(__cdecl *hero_swap_fn)(int32_t hero);

/* Both arguments read out of the disassembly rather than assumed: the position arrives as a
 * pointer to three floats, which the body only reads, and the heading as a float by value that is
 * stored straight into the record. Arguments are cleaned up by the caller, so cdecl. */
typedef void(__cdecl *player_teleport_fn)(const float position[3], float heading);

typedef struct wish_state {
    bool installed;

    hero_swap_fn       swap_hero;
    player_teleport_fn teleport;

    bool     place_pending;
    bool     place_for_scene;   /* the pose held is a scene's seat, which goes first */
    float    place_position[3];
    float    place_heading;
    uint32_t place_frames;

    bool     hero_pending;
    uint8_t  hero_wanted;
    uint32_t hero_frames;
    /* The swap is not done when the call returns. It asks the engine's own re-entry, which drops
     * the player module to state 4 and comes back to 1 only after the fade, the despawn and the
     * spawn have run, so the block reads the OLD hero for a good many frames after the call. A
     * read-back on the frame of the call said "declined" about every swap that took,
     * and the pose behind it was seated by luck rather than by order. */
    bool     hero_in_flight;
    uint8_t  hero_before;
    uint32_t hero_flight_frames;

    uint32_t places_asked;
    uint32_t places_done;
    uint32_t places_dropped;
    uint32_t places_refused;
    uint32_t places_cancelled;

    uint32_t scene_asked;          /* a scene's seats, apart from the arrivals above */
    uint32_t scene_done;
    uint32_t scene_dropped;
    uint32_t arrivals_replaced;    /* an arrival still held when a scene's seat came */
    uint32_t arrivals_set_aside;   /* an arrival asked for while a scene's seat was held */

    uint32_t heroes_asked;
    uint32_t heroes_done;
    uint32_t heroes_dropped;
    uint32_t heroes_refused;
    uint32_t heroes_clamped;
} wish_state_t;

static wish_state_t wish;

void mp_start_wish_install(void)
{
    uintptr_t swap_site     = mp_signatures_address(MP_SITE_HERO_SWAP);
    uintptr_t teleport_site = mp_signatures_address(MP_SITE_PLAYER_TELEPORT);

    if (wish.installed) {
        return;
    }
    wish.swap_hero = swap_site != 0u ? (hero_swap_fn)swap_site : NULL;
    wish.teleport  = teleport_site != 0u ? (player_teleport_fn)teleport_site : NULL;

    /* Both are optional and both are named, because a start that works while the player lands in
     * the wrong place as the wrong hero is exactly the kind of half-installed feature that reads
     * as working. */
    if (wish.teleport == NULL) {
        log_warning("no player can be placed: the teleport did not resolve, so a client will "
                    "begin a level wherever the level itself puts it");
    }
    if (wish.swap_hero == NULL) {
        log_warning("no hero can be applied: the hero swap did not resolve, so everyone plays "
                    "whichever hero the level prescribes");
    }
    wish.installed = true;
}

/* ==============================================================================================
 * Placing the player, and putting him on the hero he chose.
 * ============================================================================================ */

mp_start_wish_step_t mp_start_wish_step(bool pending, bool gates_open, uint32_t frames_waited)
{
    if (!pending) {
        return MP_START_WISH_IDLE;
    }
    /* An open gate is asked about before the deadline on purpose: a wish that becomes possible on
     * the very frame it runs out is better carried out than thrown away. */
    if (gates_open) {
        return MP_START_WISH_RUN;
    }
    if (frames_waited >= MP_START_WISH_DEADLINE_FRAMES) {
        return MP_START_WISH_DROP;
    }
    return MP_START_WISH_WAIT;
}

/* The engine stores this index raw and indexes its hero table with it raw. One entry past the last
 * hero is a scalar that would be handed on as a name pointer, and the assert behind that shows a
 * message box and ends the process, so the clamp is not tidiness. */
uint8_t mp_start_clamp_hero(uint8_t hero)
{
    return hero > (uint8_t)MP_LOBBY_HERO_MAX ? (uint8_t)MP_LOBBY_HERO_MAX : hero;
}

/* The two gates, read rather than assumed. The player module's own state field has to read 1,
 * which the respawn compares against before it does anything, and there has to be a body. This is
 * what turns a call that would return in silence into a wish that waits. */
static bool gates_are_open(void)
{
    uintptr_t block        = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  module_state = 0;
    uint32_t  actor        = 0;

    if (block == 0u || !mp_start_level_running()) {
        return false;
    }
    return memory_try_read_u32(block + MP_HERO_BLOCK_MODULE_STATE, &module_state) &&
           module_state == 1u &&
           memory_try_read_u32(block + MP_HERO_BLOCK_HACTOR, &actor) && actor != 0u;
}

/* A pose arrives over the wire, so it is not this machine's arithmetic and may be anything. The
 * teleport writes what it is handed without looking at it. */
bool mp_start_pose_is_usable(const float position[3], float heading)
{
    int axis;

    if (position == NULL || !isfinite(heading)) {
        return false;
    }
    for (axis = 0; axis < 3; ++axis) {
        if (!isfinite(position[axis])) {
            return false;
        }
    }
    return true;
}

mp_start_pose_take_t mp_start_pose_take(bool held, bool held_for_scene, bool for_scene)
{
    if (!held || held_for_scene == for_scene) {
        return MP_START_POSE_TAKE;
    }
    return for_scene ? MP_START_POSE_REPLACE_ARRIVAL : MP_START_POSE_SET_ASIDE;
}

/* The one door into the one pose, for an arrival and for a scene's seat. A scene's seats are
 * counted apart, so the placement line the field runs carry keeps counting arrivals only. */
static bool hold_the_pose(const float position[3], float heading, bool for_scene)
{
    if (!wish.installed || wish.teleport == NULL) {
        log_warning("no player can be placed: this module is %s and the teleport is %s",
                    wish.installed ? "installed" : "NOT installed",
                    wish.teleport != NULL ? "resolved" : "NOT resolved");
        ++wish.places_refused;
        return false;
    }
    if (!mp_start_pose_is_usable(position, heading)) {
        log_warning("no player can be placed: the pose is not a finite point and heading");
        ++wish.places_refused;
        return false;
    }
    switch (mp_start_pose_take(wish.place_pending, wish.place_for_scene, for_scene)) {
    case MP_START_POSE_SET_ASIDE:
        ++wish.arrivals_set_aside;
        log_info("an arrival's point at %.2f %.2f %.2f is set aside: a scene's seat is held for "
                 "the player, and the scene is where everybody is being brought",
                 (double)position[0], (double)position[1], (double)position[2]);
        return false;
    case MP_START_POSE_REPLACE_ARRIVAL:
        ++wish.arrivals_replaced;
        break;
    case MP_START_POSE_TAKE:
    default:
        break;
    }
    memcpy(wish.place_position, position, sizeof wish.place_position);
    wish.place_heading   = heading;
    wish.place_pending   = true;
    wish.place_for_scene = for_scene;
    wish.place_frames    = 0u;
    if (for_scene) {
        ++wish.scene_asked;
    } else {
        ++wish.places_asked;
    }
    log_info("a pose is held for the player%s, %.2f %.2f %.2f facing %.2f, to be seated as soon "
             "as a level is running and a living body exists", for_scene ? " for a scene" : "",
             (double)position[0], (double)position[1], (double)position[2], (double)heading);
    return true;
}

bool mp_start_place_for_scene(const float position[3], float heading)
{
    return hold_the_pose(position, heading, true);
}

bool mp_start_place_at(const float position[3], float heading)
{
    return hold_the_pose(position, heading, false);
}

void mp_start_cancel_pose(void)
{
    if (!wish.place_pending) {
        return;
    }
    wish.place_pending = false;
    ++wish.places_cancelled;
    log_info("a held pose is forgotten because a new world is starting: the three floats belong "
             "to the level they were computed in and mean nothing in the next one");
}

bool mp_start_apply_hero(uint8_t hero)
{
    uint8_t wanted = mp_start_clamp_hero(hero);

    if (!wish.installed || wish.swap_hero == NULL) {
        log_warning("no hero can be applied: this module is %s and the hero swap is %s",
                    wish.installed ? "installed" : "NOT installed",
                    wish.swap_hero != NULL ? "resolved" : "NOT resolved");
        ++wish.heroes_refused;
        return false;
    }
    if (wanted != hero) {
        ++wish.heroes_clamped;
        log_warning("hero %u is past the last one the game ships and was clamped to %u; the "
                    "engine indexes its table with this value raw and a larger one ends the "
                    "process", (unsigned)hero, (unsigned)wanted);
    }
    wish.hero_wanted  = wanted;
    wish.hero_pending = true;
    wish.hero_frames  = 0u;
    ++wish.heroes_asked;
    log_info("hero %u is held for the player, to be applied as soon as a level is running and a "
             "living body exists", (unsigned)wanted);
    return true;
}

/* Called once and disarmed whatever it answers. The swap goes through the respawn, which fades out
 * and in, so retrying it every frame would be a strobe rather than a correction.
 *
 * The respawn rewrites the checkpoint a later death returns to, hero, position and heading alike.
 * That is the engine's own behaviour and there is no variant of the call that does not.
 *
 * The health is written after, not before, and the control pass over the swap is why. The engine's
 * health setter writes through the status POINTER, and the spawn at the far end of the fade
 * repoints it (status_setActivePlayer) at the incoming hero's record, which is a
 * different record per hero. A write before the swap lands on the OUTGOING hero. So the write
 * is made by confirm_the_swap, once the block names the new hero and the pointer with it. The
 * re-entry after a death writes before, and is right to: it keeps the hero.
 *
 * Whether the swap took is NOT read back here. The call returns in silence both when it declined
 * (no living body) and when it accepted, because acceptance is a module state of 4 and a fade;
 * the block changes hero many frames later, and confirm_the_swap reads it then. */
static void apply_hero_now(void)
{
    uintptr_t block  = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  before = 0xFFFFFFFFu;

    wish.hero_pending = false;
    (void)memory_read_u32(block + MP_HERO_BLOCK_HERO_INDEX, &before);
    if (before == (uint32_t)wish.hero_wanted) {
        ++wish.heroes_done;
        log_info("the player is already hero %u, so no respawn is asked for",
                 (unsigned)wish.hero_wanted);
        return;
    }
    wish.swap_hero((int32_t)wish.hero_wanted);
    wish.hero_in_flight     = true;
    wish.hero_before        = (uint8_t)before;
    wish.hero_flight_frames = 0u;
    log_info("hero %u is asked of the engine's own re-entry; the block reads %u until the fade, "
             "the despawn and the spawn have run, and the pose waits behind it",
             (unsigned)wish.hero_wanted, (unsigned)before);
}

/* The swap, watched to its end. The engine's re-entry answers nothing, so the only proof that it
 * took is the block naming the new hero with the player module back in state 1, and the only
 * proof that it declined is that this never happens. The deadline is the wish's own: after it the
 * swap is counted refused, which is what it was when the body it asked for was not alive. */
static void confirm_the_swap(bool gates)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  now   = 0xFFFFFFFFu;

    if (gates && memory_try_read_u32(block + MP_HERO_BLOCK_HERO_INDEX, &now) &&
        now == (uint32_t)wish.hero_wanted) {
        bool healed;

        wish.hero_in_flight = false;
        ++wish.heroes_done;
        /* Now the status pointer names the new hero's record, which the spawn left at whatever
         * the record held: a hundred from the level's start, or what the host's savegame said
         * this hero last had. The player who chose a hero begins with its health whole. */
        healed = mp_respawn_grant_health();
        log_info("the player was hero %u and is now hero %u%s; the checkpoint a death returns "
                 "to carries the new one", (unsigned)wish.hero_before,
                 (unsigned)wish.hero_wanted,
                 healed ? " with full health" : ", and its health could not be written");
        return;
    }
    if (++wish.hero_flight_frames >= MP_START_WISH_DEADLINE_FRAMES) {
        wish.hero_in_flight = false;
        ++wish.heroes_refused;
        log_warning("the swap to hero %u was asked %u frame(s) ago and the block still reads %u, "
                    "so the engine's re-entry never came back with it: it does nothing at all "
                    "when the body it asks for is not alive", (unsigned)wish.hero_wanted,
                    (unsigned)wish.hero_flight_frames, (unsigned)now);
    }
}

/* One shot as well, and for a different reason: a second placement, after the player has begun to
 * move, is the rubber band this exists to avoid rather than to cause.
 *
 * The seat is read back so that the counter means something. The teleport stores the three floats
 * it was handed, so the comparison is exact rather than approximate. */
static void place_now(void)
{
    float seated[3];

    wish.place_pending = false;
    wish.teleport(wish.place_position, wish.place_heading);

    if (mp_cells_hero_position(seated) &&
        memcmp(seated, wish.place_position, sizeof seated) == 0) {
        if (wish.place_for_scene) {
            ++wish.scene_done;
        } else {
            ++wish.places_done;
        }
        log_info("the player was placed at %.2f %.2f %.2f facing %.2f",
                 (double)wish.place_position[0], (double)wish.place_position[1],
                 (double)wish.place_position[2], (double)wish.place_heading);
        return;
    }
    ++wish.places_refused;
    log_warning("the player was asked to %.2f %.2f %.2f and the record does not hold it "
                "afterwards, so the placement did not take", (double)wish.place_position[0],
                (double)wish.place_position[1], (double)wish.place_position[2]);
}

/* The hero goes first when both are wanted, and the reason is what the swap does: it zeroes the
 * whole player block, the position and the ground contact among it, so a placement carried out
 * before it would simply be gone. The respawn also shuts both gates for the length of its fade,
 * which is why the pose is not seated in the same frame but on the first frame after they open
 * again. */
void mp_start_wish_tick(void)
{
    bool gates;

    if (!wish.hero_pending && !wish.hero_in_flight && !wish.place_pending) {
        return;
    }
    gates = gates_are_open();

    if (wish.hero_pending) {
        switch (mp_start_wish_step(true, gates, wish.hero_frames)) {
        case MP_START_WISH_RUN:
            apply_hero_now();
            break;
        case MP_START_WISH_DROP:
            wish.hero_pending = false;
            ++wish.heroes_dropped;
            log_warning("hero %u was asked for %u frames ago and no level with a living body has "
                        "come up since, so the choice is dropped rather than applied at some "
                        "later moment nobody chose it",
                        (unsigned)wish.hero_wanted, (unsigned)wish.hero_frames);
            break;
        case MP_START_WISH_IDLE:
        case MP_START_WISH_WAIT:
        default:
            ++wish.hero_frames;
            break;
        }
        return;   /* the pose waits for the frame after the swap, never the same one */
    }
    if (wish.hero_in_flight) {
        /* And behind the whole of the fade, because the spawn at its end zeroes the block: a
         * pose seated during the flight would be gone by the time the new body stands. */
        confirm_the_swap(gates);
        return;
    }

    switch (mp_start_wish_step(true, gates, wish.place_frames)) {
    case MP_START_WISH_RUN:
        place_now();
        break;
    case MP_START_WISH_DROP:
        wish.place_pending = false;
        if (wish.place_for_scene) {
            ++wish.scene_dropped;
        } else {
            ++wish.places_dropped;
        }
        log_warning("a pose was held for %u frames and no level with a living body has come up "
                    "since, so it is dropped rather than applied at some later moment nobody "
                    "chose it", (unsigned)wish.place_frames);
        break;
    case MP_START_WISH_IDLE:
    case MP_START_WISH_WAIT:
    default:
        ++wish.place_frames;
        break;
    }
}

void mp_start_wish_report(void)
{
    log_info("  the placement: %u asked for, %u seated, %u dropped at the deadline, %u forgotten "
             "at a new world, %u refused; the teleport is %s",
             (unsigned)wish.places_asked, (unsigned)wish.places_done,
             (unsigned)wish.places_dropped, (unsigned)wish.places_cancelled,
             (unsigned)wish.places_refused,
             wish.teleport != NULL ? "resolved" : "NOT resolved");
    log_info("  the placement's scene seats: %u asked for, %u seated, %u dropped at the "
             "deadline, %u arrival point(s) replaced by a scene seat, %u arrival point(s) set "
             "aside while a scene seat was held",
             (unsigned)wish.scene_asked, (unsigned)wish.scene_done, (unsigned)wish.scene_dropped,
             (unsigned)wish.arrivals_replaced, (unsigned)wish.arrivals_set_aside);
    log_info("  the hero: %u asked for, %u applied, %u dropped at the deadline, %u refused, %u "
             "clamped into range; the swap is %s", (unsigned)wish.heroes_asked,
             (unsigned)wish.heroes_done, (unsigned)wish.heroes_dropped,
             (unsigned)wish.heroes_refused, (unsigned)wish.heroes_clamped,
             wish.swap_hero != NULL ? "resolved" : "NOT resolved");
}
