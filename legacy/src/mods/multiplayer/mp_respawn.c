/* mp_respawn.c: the health first, then the engine's own re-entry, and a seat that was probed
 * rather than guessed.
 *
 * The seat search, its rings, the reading of the anchor and the stages a search goes through when
 * it finds nothing are mp_seat's. They left this file at the seam its size note had named, the day
 * a second caller needed the same search with a slot of its own and a fallback. What stays is the
 * door: the gates, the health, the engine's re-entry and the wait for the body to stand.
 *
 * SIZE NOTE: over 600 lines. The door of the re-entry and, through the same door, the move
 * of a living player a scene asks for: the gates, the landing and the report of both. The move
 * shares the engine's re-entry and the landing listener with the rest and nothing else. The
 * next seam is the pure decisions at the top, mp_respawn_step to mp_respawn_pose_is_usable,
 * which a test reaches without the door.
 *
 * The reasoning is in the header. Two things about the code are worth having in front of a
 * maintainer, because neither is visible in a type:
 *
 * The order of the two calls is the feature. The health write has to happen in the same frame as
 * the re-entry and before it. Writing it at request time instead would put a hundred health on a
 * corpse that is still lying in the level, and the death check phase is still running on it.
 *
 * The search runs on the frame it is asked, not at the request. Where a team mate stands moves,
 * and so does what is around him; a seat computed when the player died would be several seconds
 * stale by the time the gates open.
 */
#include "mp_respawn.h"

#include "mp_cells.h"
#include "mp_seat.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The re-entry and the health writer, read out of the disassembly rather than assumed: plain values
 * and a pointer, the caller cleans up, so cdecl. */
typedef void(__cdecl *respawn_at_fn_t)(int32_t hero, const float position[3], float heading);
typedef void(__cdecl *set_health_fn_t)(int32_t health);

/* The player record's mode slot, the descriptor its phases are walked with. The death entry hangs
 * the death descriptor there after it has set the dead flag, and only a new spawn takes it off. */
#define RECORD_MODE_DESCRIPTOR 0x60u

/* The log's name for this caller of the seat search. */
static const char RESPAWN_WHO[] = "the re-entry";

typedef struct respawn_state {
    bool installed;

    respawn_at_fn_t respawn_at;
    set_health_fn_t set_health;
    uintptr_t       hero_block;

    /* Which gate was shut at each look while a wish waited, so a wish that is dropped can say
     * what held it rather than naming every condition it might have been. */
    uint32_t          shut_frames[4];
    mp_respawn_shut_t last_shut;

    mp_respawn_landed_fn_t landed;

    /* The wish, and its seat. */
    bool             pending;
    mp_seat_wish_t   wish;
    uint32_t         delay_frames;
    uint32_t         frames;
    mp_seat_counts_t seat;

    /* The wait for the engine to finish its own fade. */
    bool     landing;
    bool     left_running;    /* the module state has been seen away from 1 since the call */
    uint32_t landing_frames;

    /* A living player moved by the same re-entry, watched on its own: none of the landing above
     * belongs to it, because no life ended and none began. */
    bool     moving;
    bool     moving_left;
    uint32_t moving_frames;

    /* The wall clock beside each of the three frame counts above, because a deadline needs both
     * to have passed. Each is stamped by the first look of its wait and cleared where the wait
     * begins. */
    bool     wish_stamped;
    uint32_t wish_since_ms;
    bool     landing_stamped;
    uint32_t landing_since_ms;
    bool     moving_stamped;
    uint32_t moving_since_ms;

    /* Counters. Every path out of this module increments exactly one of them. */
    uint32_t asked;
    uint32_t refused;
    uint32_t done;
    uint32_t dropped;
    uint32_t withdrawn;       /* taken back by the caller's rule set before a seat was found */
    uint32_t landed_count;
    uint32_t landings_lost;
    uint32_t landed_dead;     /* the body the engine stood up was a corpse again */
    uint32_t health_writes;
    uint32_t moves_asked;     /* living players moved by the re-entry for a scene */
    uint32_t moves_refused;
    uint32_t moves_landed;
    uint32_t moves_declined;  /* the module never left its running state: the engine said no */
} respawn_state_t;

static respawn_state_t rs;

/* ==============================================================================================
 * The pure decisions.
 * ============================================================================================ */

mp_respawn_step_t mp_respawn_step(bool pending, bool gates_open, uint32_t frames_waited,
                                  uint32_t waited_ms, uint32_t delay_frames)
{
    if (!pending) {
        return MP_RESPAWN_STEP_IDLE;
    }
    if (frames_waited < delay_frames) {
        return MP_RESPAWN_STEP_HOLD;
    }
    if (gates_open) {
        return MP_RESPAWN_STEP_RUN;
    }
    if (mp_respawn_deadline_passed(frames_waited, waited_ms)) {
        return MP_RESPAWN_STEP_DROP;
    }
    return MP_RESPAWN_STEP_WAIT;
}

bool mp_respawn_deadline_passed(uint32_t frames, uint32_t ms)
{
    return frames >= MP_RESPAWN_DEADLINE_FRAMES && ms >= MP_RESPAWN_DEADLINE_MS;
}

uint32_t mp_respawn_waited_ms(bool *stamped, uint32_t *since_ms, uint32_t now_ms)
{
    if (stamped == NULL || since_ms == NULL) {
        return 0u;
    }
    if (!*stamped) {
        *stamped  = true;
        *since_ms = now_ms;
    }
    return (uint32_t)(now_ms - *since_ms);
}

bool mp_respawn_fade_is_lost(uint32_t module_state, bool fade_done, bool fade_runs)
{
    return module_state == MP_RESPAWN_MODULE_FADING && !fade_done && !fade_runs;
}

bool mp_respawn_is_a_corpse(uint32_t dead_flag, uint32_t mode, uint32_t death_descriptor)
{
    /* The engine writes a 1 and only a spawn clears it. The descriptor is asked as well, because
     * the death entry sets the flag before it hangs the descriptor, and a flag without the
     * descriptor is a death still being entered rather than a corpse. With the descriptor's cell
     * unresolved the flag answers alone. */
    if (dead_flag != 1u) {
        return false;
    }
    return death_descriptor == 0u || mode == death_descriptor;
}

bool mp_respawn_record_is_a_corpse(uintptr_t record)
{
    uint32_t dead = 0u;
    uint32_t mode = 0u;

    return record != 0u && memory_try_read_u32(record + MP_HERO_BLOCK_DEAD, &dead) &&
           memory_try_read_u32(record + RECORD_MODE_DESCRIPTOR, &mode) &&
           mp_respawn_is_a_corpse(dead, mode,
                                  (uint32_t)mp_cells_address(MP_CELL_MODE_DEATH_DESC));
}

bool mp_respawn_player_is_a_corpse(void)
{
    return mp_respawn_record_is_a_corpse(mp_cells_address(MP_CELL_HERO_BLOCK));
}

bool mp_respawn_pose_is_usable(const float position[3], float heading)
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

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

bool mp_respawn_install(void)
{
    uintptr_t respawn_site;
    uintptr_t health_site;
    uintptr_t mode_cell;

    if (rs.installed) {
        return true;
    }

    /* The seat's probes first: a re-entry without them still runs, and their own line says so. */
    mp_seat_install();

    respawn_site  = mp_signatures_address(MP_SITE_PLAYER_RESPAWN_AT);
    health_site   = mp_signatures_address(MP_SITE_STATUS_SET_HEALTH);
    rs.respawn_at = (respawn_at_fn_t)respawn_site;
    rs.set_health = (set_health_fn_t)health_site;
    rs.hero_block = mp_cells_address(MP_CELL_HERO_BLOCK);
    mode_cell     = mp_cells_address(MP_CELL_GAME_MODE);

    if (respawn_site == 0u || health_site == 0u || rs.hero_block == 0u || mode_cell == 0u) {
        log_warning("no player can come back: the re-entry is %s, the health writer is %s, the "
                    "hero block is %s and the game mode cell is %s",
                    respawn_site != 0u ? "resolved" : "NOT resolved",
                    health_site != 0u ? "resolved" : "NOT resolved",
                    rs.hero_block != 0u ? "resolved" : "NOT resolved",
                    mode_cell != 0u ? "resolved" : "NOT resolved");
        return false;
    }

    rs.installed = true;
    return true;
}

bool mp_respawn_installed(void)
{
    return rs.installed;
}

void mp_respawn_set_landed_listener(mp_respawn_landed_fn_t listener)
{
    rs.landed = listener;
}

/* ==============================================================================================
 * The two engine gates, read rather than assumed.
 * ============================================================================================ */

/* The player module's own state field has to read 1, which the re-entry compares against before
 * it does anything, and there has to be a body. Neither is true while a level loads, in the
 * frontend, or while a cutscene has the player parked. What makes the death survivable here is
 * that the death path itself does not touch the state field, so a corpse passes both: neither the
 * death entry at 0x004500B0 nor the death update at 0x00450309 writes +0x04, and a census of the
 * writers of that field over the whole image finds eight, none of them in the death path. What a
 * dead player has instead is the death descriptor on its mode pointer, which the alive test at
 * 0x00447D18 reads, and the dead flag at +0x394; the re-entry looks at neither. */
static bool gates_are_open(void)
{
    uint32_t module_state = 0u;
    uint32_t actor = 0u;

    if (!mp_seat_level_running()) {
        return false;
    }
    return memory_try_read_u32(rs.hero_block + MP_HERO_BLOCK_MODULE_STATE, &module_state) &&
           module_state == 1u &&
           memory_try_read_u32(rs.hero_block + MP_HERO_BLOCK_HACTOR, &actor) && actor != 0u;
}

mp_respawn_shut_t mp_respawn_gate_shut(bool level_running, bool module_running, bool have_body)
{
    if (!level_running) {
        return MP_RESPAWN_SHUT_LEVEL;
    }
    if (!module_running) {
        return MP_RESPAWN_SHUT_MODULE;
    }
    return have_body ? MP_RESPAWN_SHUT_NOTHING : MP_RESPAWN_SHUT_BODY;
}

const char *mp_respawn_shut_word(mp_respawn_shut_t shut)
{
    switch (shut) {
    case MP_RESPAWN_SHUT_LEVEL:  return "no level was running";
    case MP_RESPAWN_SHUT_MODULE: return "the player module was not in its running state";
    case MP_RESPAWN_SHUT_BODY:   return "the module was running and there was no body to move";
    case MP_RESPAWN_SHUT_NOTHING:
    default:                     return "the engine was ready and no seat could be probed";
    }
}

/* The same reading as gates_are_open, kept apart because the tick wants to know WHICH gate. */
static mp_respawn_shut_t which_gate_is_shut(void)
{
    uint32_t module_state = 0u;
    uint32_t actor = 0u;
    bool     module_running =
        memory_try_read_u32(rs.hero_block + MP_HERO_BLOCK_MODULE_STATE, &module_state) &&
        module_state == 1u;
    bool     have_body =
        memory_try_read_u32(rs.hero_block + MP_HERO_BLOCK_HACTOR, &actor) && actor != 0u;

    return mp_respawn_gate_shut(mp_seat_level_running(), module_running, have_body);
}

static bool module_state_is(uint32_t wanted)
{
    uint32_t module_state = 0u;

    return memory_try_read_u32(rs.hero_block + MP_HERO_BLOCK_MODULE_STATE, &module_state) &&
           module_state == wanted;
}

bool mp_respawn_player_lives(void)
{
    return rs.installed && module_state_is(1u) && !mp_respawn_player_is_a_corpse();
}

/* ==============================================================================================
 * Asking.
 * ============================================================================================ */

/* What every request has to pass before it is held. */
static bool may_remember(uint32_t delay_frames)
{
    if (!rs.installed) {
        log_warning("no player can come back: this module never installed, so nothing resolved "
                    "the engine's own re-entry");
        ++rs.refused;
        return false;
    }
    if (delay_frames >= MP_RESPAWN_DEADLINE_FRAMES) {
        log_warning("a re-entry delay of %u frames is not shorter than the %u frame deadline the "
                    "wish is dropped at, so it would never be carried out",
                    (unsigned)delay_frames, (unsigned)MP_RESPAWN_DEADLINE_FRAMES);
        ++rs.refused;
        return false;
    }
    return true;
}

static void hold(uint32_t delay_frames)
{
    rs.delay_frames = delay_frames;
    rs.frames       = 0u;
    rs.wish_stamped = false;
    rs.pending      = true;
    ++rs.asked;
}

bool mp_respawn_at(const float position[3], float heading, uint8_t slot, uint32_t delay_frames)
{
    if (!mp_respawn_pose_is_usable(position, heading)) {
        log_warning("no player can come back: the pose asked for is not a finite point and "
                    "heading");
        ++rs.refused;
        return false;
    }
    if (!may_remember(delay_frames)) {
        return false;
    }
    mp_seat_wish_at_point(&rs.wish, RESPAWN_WHO, position, heading, slot);
    hold(delay_frames);
    log_info("a re-entry is held at %.2f %.2f %.2f facing %.2f, after %u frame(s), to be carried "
             "out as soon as a level is running and a seat has been probed",
             (double)position[0], (double)position[1], (double)position[2], (double)heading,
             (unsigned)delay_frames);
    return true;
}

bool mp_respawn_beside(const float *died_at, uint8_t slot, uint32_t delay_frames)
{
    if (died_at != NULL && !mp_respawn_pose_is_usable(died_at, 0.0f)) {
        log_warning("no player can come back: the place of the death is not a finite point");
        ++rs.refused;
        return false;
    }
    if (!may_remember(delay_frames)) {
        return false;
    }
    mp_seat_wish_beside_players(&rs.wish, RESPAWN_WHO, died_at, slot);
    hold(delay_frames);
    if (died_at != NULL) {
        log_info("a re-entry is held beside whichever far player stands nearest to %.2f %.2f "
                 "%.2f, where this player died, after %u frame(s), its ring starting in "
                 "direction %u; the seat is searched around the players as they stand once a "
                 "level is running", (double)died_at[0], (double)died_at[1], (double)died_at[2],
                 (unsigned)delay_frames, (unsigned)mp_seat_rule_ring_start(slot));
    } else {
        log_info("a re-entry is held beside the first far player standing in bank order, the "
                 "place of the death not being known, after %u frame(s), its ring starting in "
                 "direction %u; the seat is searched around the players as they stand once a "
                 "level is running", (unsigned)delay_frames,
                 (unsigned)mp_seat_rule_ring_start(slot));
    }
    return true;
}

bool mp_respawn_pending(void)
{
    return rs.pending || rs.landing || rs.moving;
}

void mp_respawn_cancel(void)
{
    rs.pending = false;
    rs.landing = false;
}

bool mp_respawn_withdraw(void)
{
    /* A body whose re-entry has been called is already in the engine's fade and stands up of its
     * own; only a wish still waiting for its seat can be taken back. */
    if (!rs.pending || rs.landing) {
        return false;
    }
    rs.pending = false;
    ++rs.withdrawn;
    return true;
}

/* ==============================================================================================
 * Carrying it out.
 * ============================================================================================ */

/* The health write on its own, for the one other caller of the engine's re-entry: the lobby's
 * hero swap goes through the same fade and the same spawn, and a client whose chosen hero's
 * status record came out of the host's savegame at whatever that hero last had would come back
 * with that, or with nothing. Counted with the writes the re-entry itself makes. */
bool mp_respawn_grant_health(void)
{
    if (!rs.installed || rs.set_health == NULL) {
        return false;
    }
    rs.set_health(MP_RESPAWN_HEALTH);
    ++rs.health_writes;
    return true;
}

/* The health first and the re-entry second, in the same frame. The spawn at the far end of the
 * engine's own fade writes the loadout and the ammunition and never touches the health word, so a
 * body that comes back without this dies again on the next substep it is ticked. In the spawn at
 * 0x00447E58 the two writes through the status pointer are the ammunition into the per weapon
 * slot and then the equipped weapon id, each behind its own null test, and there is no write to
 * word 0 of the record, which is the health, anywhere in the body; the campaign never met this
 * because its own two callers of the re-entry, the checkpoint arm of the death fade and the
 * script warp, only ever call it on a living player. The writer used is the one at 0x00459EA9,
 * which sets the value and the displayed value together so the bar does not crawl; the one at
 * 0x00459E85 sets the value alone and lets the meter creep up to it. It writes through the
 * ACTIVE status pointer, and the spawn repoints that pointer to the hero it is given; since the
 * hero index passed back is the one read out of the block, the record written here is the record
 * the spawn will point at afterwards, and that is why a re-entry never changes hero.
 *
 * The re-entry itself, at 0x00447C90, is eighteen instructions and respawns nothing: it returns
 * in silence unless the module state at +0x04 reads 1, holds the actor's clip, writes the hero
 * index, the heading and the position into three globals (0x006CF638, 0x006CF634 and
 * 0x006CF628), nulls the contact slot at +0x18 of the player's task node, and sets the state to 4.
 * The player's task tick at 0x00447D38 then fades out in state 4 and moves to 3, and in state 3
 * waits for the fade flag at 0x006C4E7C to read 1 before it despawns (0x00448201), spawns from
 * the three globals and fades back in, and the state is 1 again. */
static void run_now(const float seat[3], float heading)
{
    uint32_t hero = 0u;

    if (!memory_read_u32(rs.hero_block + MP_HERO_BLOCK_HERO_INDEX, &hero)) {
        log_warning("the hero index could not be read, so the re-entry is not attempted: the "
                    "engine indexes its hero table with this value raw");
        return;
    }

    rs.set_health(MP_RESPAWN_HEALTH);
    ++rs.health_writes;

    rs.respawn_at((int32_t)hero, seat, heading);
    mp_seat_note_seated(seat);

    rs.pending         = false;
    rs.landing         = true;
    rs.left_running    = false;
    rs.landing_frames  = 0u;
    rs.landing_stamped = false;
    ++rs.done;
    log_info("hero %u was given %d health and asked back at %.2f %.2f %.2f facing %.2f; the "
             "engine's own fade now takes it from here", (unsigned)hero, MP_RESPAWN_HEALTH,
             (double)seat[0], (double)seat[1], (double)seat[2], (double)heading);
}

/* The engine drops the module state to 4, fades out, despawns, spawns and comes back to 1. The
 * landing is that round trip, and it is watched rather than assumed because the listener behind
 * it re-arms a slot the spawn has just overwritten: firing it early would write over the engine's
 * handler before the engine had put it there. Two writes to the task node's contact slot bracket
 * the whole re-entry: the re-entry writes 0 into it, so nothing is delivered to a player on his
 * way back, and the spawn writes the engine's own contact handler (0x00448369) into it, so any
 * module that held a dispatcher there has lost it by the time the body stands. */
static void watch_for_landing(uint32_t now_ms)
{
    uint32_t waited_ms = mp_respawn_waited_ms(&rs.landing_stamped, &rs.landing_since_ms, now_ms);

    ++rs.landing_frames;

    if (!rs.left_running) {
        if (!module_state_is(1u)) {
            rs.left_running = true;
        } else if (mp_respawn_deadline_passed(rs.landing_frames, waited_ms)) {
            rs.landing = false;
            ++rs.landings_lost;
            log_warning("the re-entry was asked for %u frames ago and the player module never "
                        "left its running state, so the engine declined it in silence (%u ms)",
                        (unsigned)rs.landing_frames, (unsigned)waited_ms);
        }
        return;
    }

    if (gates_are_open()) {
        rs.landing = false;
        /* Standing means a living body, not only a running module and an object: a body can be
         * spawned and killed again before this frame looks. Such a life is over before it
         * was handed on, and the death of it is already a wish of its own. */
        if (mp_respawn_player_is_a_corpse()) {
            ++rs.landed_dead;
            log_warning("the player was asked back %u frame(s) ago and the body the engine stood "
                        "up is a corpse again, so nothing is handed on for it (%u ms)",
                        (unsigned)rs.landing_frames, (unsigned)waited_ms);
            return;
        }
        ++rs.landed_count;
        mp_seat_note_landed();
        if (rs.landed != NULL) {
            rs.landed();
        }
        log_info("the player is standing again after %u frame(s), %u ms",
                 (unsigned)rs.landing_frames, (unsigned)waited_ms);
        return;
    }
    if (mp_respawn_deadline_passed(rs.landing_frames, waited_ms)) {
        rs.landing = false;
        ++rs.landings_lost;
        log_warning("the player was asked back %u frames ago and no living body has come up "
                    "since, so whatever owns the contact dispatch slot is NOT re-armed (%u ms)",
                    (unsigned)rs.landing_frames, (unsigned)waited_ms);
    }
}

bool mp_respawn_move_living(const float position[3], float heading)
{
    uint32_t hero = 0u;

    if (!rs.installed || rs.pending || rs.landing || rs.moving ||
        !mp_respawn_pose_is_usable(position, heading) || !module_state_is(1u) ||
        !memory_read_u32(rs.hero_block + MP_HERO_BLOCK_HERO_INDEX, &hero)) {
        ++rs.moves_refused;
        return false;
    }
    rs.respawn_at((int32_t)hero, position, heading);
    rs.moving         = true;
    rs.moving_left    = false;
    rs.moving_frames  = 0u;
    rs.moving_stamped = false;
    ++rs.moves_asked;
    return true;
}

/* The round trip of a living player's move: the module leaves its running state and comes back to
 * it. A module that never left within the deadline was refused by the engine in silence. */
static void watch_the_move(uint32_t now_ms)
{
    uint32_t waited_ms = mp_respawn_waited_ms(&rs.moving_stamped, &rs.moving_since_ms, now_ms);

    ++rs.moving_frames;
    if (!rs.moving_left) {
        rs.moving_left = !module_state_is(1u);
        if (!rs.moving_left && mp_respawn_deadline_passed(rs.moving_frames, waited_ms)) {
            rs.moving = false;
            ++rs.moves_declined;
        }
        return;
    }
    if (gates_are_open()) {
        rs.moving = false;
        ++rs.moves_landed;
        if (rs.landed != NULL) {
            rs.landed();
        }
        return;
    }
    if (mp_respawn_deadline_passed(rs.moving_frames, waited_ms)) {
        rs.moving = false;
        ++rs.moves_declined;
    }
}

/* The gates are open: one look for the seat. No seat this frame keeps the wish; the seat's own
 * stages are what end a search that never finds one. */
static void look_for_the_seat(uint32_t substeps)
{
    float seat[3];
    float heading = 0.0f;

    if (mp_seat_wish_step(&rs.wish, substeps, &rs.seat, seat, &heading)) {
        run_now(seat, heading);
        return;
    }
    ++rs.frames;
}

void mp_respawn_tick(uint32_t substeps, uint32_t now_ms)
{
    mp_respawn_shut_t shut;
    uint32_t          waited_ms;

    if (!rs.installed) {
        return;
    }
    if (rs.moving) {
        watch_the_move(now_ms);
        return;
    }
    /* A body on its way back outranks a fresh wish, and the wish waits rather than being lost:
     * mp_respawn_pending answers true throughout, so a caller that asks twice is told so. */
    if (rs.landing) {
        watch_for_landing(now_ms);
        return;
    }
    if (!rs.pending) {
        return;
    }

    /* Read once and used twice: the step wants to know whether the door is open, and a wish that
     * ends up dropped wants to say which gate was shut the last time it looked. */
    shut          = which_gate_is_shut();
    rs.last_shut  = shut;
    rs.shut_frames[(size_t)shut] += 1u;
    waited_ms     = mp_respawn_waited_ms(&rs.wish_stamped, &rs.wish_since_ms, now_ms);

    switch (mp_respawn_step(true, shut == MP_RESPAWN_SHUT_NOTHING, rs.frames, waited_ms,
                            rs.delay_frames)) {
    case MP_RESPAWN_STEP_RUN:
        look_for_the_seat(substeps);
        break;
    case MP_RESPAWN_STEP_DROP:
        rs.pending = false;
        ++rs.dropped;
        log_warning("a re-entry was asked for %u frames ago and is dropped rather than carried "
                    "out at some later moment nobody chose it (%u ms). The last look said: %s. Of "
                    "those frames %u had no level, %u no running player module, %u no body and %u "
                    "an open door with no seat", (unsigned)rs.frames, (unsigned)waited_ms,
                    mp_respawn_shut_word(rs.last_shut),
                    (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_LEVEL],
                    (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_MODULE],
                    (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_BODY],
                    (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_NOTHING]);
        break;
    case MP_RESPAWN_STEP_IDLE:
    case MP_RESPAWN_STEP_HOLD:
    case MP_RESPAWN_STEP_WAIT:
    default:
        /* The seat's clock counts only the looks it made, not the time the gates held it. */
        mp_seat_wish_pass(&rs.wish, substeps);
        ++rs.frames;
        break;
    }
}

void mp_respawn_report(void)
{
    if (!rs.installed) {
        /* Said rather than skipped. A silent report is how a module ships built, tested and never
         * called, and this line is the one that would have been missing. */
        log_warning("  the re-entry: this module never installed, so no dead player was ever put "
                    "back into a running level");
        return;
    }
    log_info("  the re-entry: %u asked for, %u carried out, %u dropped at the deadline, %u "
             "refused at the door, %u health write(s); the world probes are %s; %u taken back "
             "by the rule set before a seat was found",
             (unsigned)rs.asked, (unsigned)rs.done, (unsigned)rs.dropped, (unsigned)rs.refused,
             (unsigned)rs.health_writes,
             mp_seat_probes_resolved() ? "all resolved" : "NOT all resolved",
             (unsigned)rs.withdrawn);
    mp_seat_report("the seat:", "the re-entry's fallback:", &rs.seat);
    log_info("  the landing: %u body(s) came back and were handed on, %u never did, %u landing(s) "
             "that found the body dead again (must be 0)",
             (unsigned)rs.landed_count, (unsigned)rs.landings_lost, (unsigned)rs.landed_dead);
    log_info("  the move of a living player: %u asked by a scene, %u refused at the door, %u "
             "landed, %u declined by the engine", (unsigned)rs.moves_asked,
             (unsigned)rs.moves_refused, (unsigned)rs.moves_landed, (unsigned)rs.moves_declined);
    log_info("  the looks while a wish waited: %u with no level, %u with the player module not "
             "running, %u with no body, %u with the door open and no seat",
             (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_LEVEL],
             (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_MODULE],
             (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_BODY],
             (unsigned)rs.shut_frames[MP_RESPAWN_SHUT_NOTHING]);
    if (rs.landed == NULL) {
        log_warning("  the landing listener is NOT set, so nothing puts the contact dispatch "
                    "slot back after a re-entry and contacts stop reaching this feature from the "
                    "first one onwards");
    }
}
