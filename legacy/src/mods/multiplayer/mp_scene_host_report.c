/* mp_scene_host_report.c: what the host's half of a scene says in the run report, and the line
 * that measures a scene while it stands. See the header.
 *
 * Split from mp_scene_host.c along the seam that file's size note named. Everything here reads:
 * the counters it is handed, the scene gates' own counts, and for the measuring line two actors,
 * the level's registers and the channel of a spoken line. Nothing is written into the engine or
 * into the scene.
 */
#include "mp_scene_host_report.h"

#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_enemy_bind.h"
#include "mp_scene_hero_watch.h"
#include "mp_scratch.h"
#include "mp_scratch_bind.h"
#include "mp_seat.h"
#include "mp_voice.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_scene_host_report(const mp_scene_host_counts_t *n, const mp_seat_counts_t *seats)
{
    mp_cutscene_counts_t cut;

    memset(&cut, 0, sizeof cut);
    mp_cutscene_counts(&cut);
    log_info("  the scenes of the host: %u begun (%u by a lock, %u by the hero as an actor, %u by "
             "a warp), %u of them a far player's and held until the host stood at its place; %u "
             "hold(s) fell with the host at his place (%u of them with the host there already "
             "and not moved), %u because the host had no place, %u because every far player had "
             "left; %u second scene(s) counted and not begun, %u not begun because nobody else "
             "was in the session, %u taken over that ran with no door (%u a lock, %u the hero as "
             "an actor), %u made a hero's by their own actor right behind its lock, %u ended by "
             "the player's own release; the longest hold %u substep(s); the host's re-entry was "
             "pointed at the place of a scene %u time(s); %u still running as their level ended; "
             "%u line(s) of scenes left out past 16 a scene or 512 in all",
             (unsigned)(n->began[MP_SCENE_KIND_LOCK] + n->began[MP_SCENE_KIND_HERO] +
                        n->began[MP_SCENE_KIND_WARP]),
             (unsigned)n->began[MP_SCENE_KIND_LOCK], (unsigned)n->began[MP_SCENE_KIND_HERO],
             (unsigned)n->began[MP_SCENE_KIND_WARP], (unsigned)n->held, (unsigned)n->at_place,
             (unsigned)n->already_there, (unsigned)n->no_place, (unsigned)n->left_alone,
             (unsigned)n->second, (unsigned)n->alone_doors,
             (unsigned)(n->adopted[MP_SCENE_KIND_LOCK] + n->adopted[MP_SCENE_KIND_HERO]),
             (unsigned)n->adopted[MP_SCENE_KIND_LOCK], (unsigned)n->adopted[MP_SCENE_KIND_HERO],
             (unsigned)n->hero_behind_lock, (unsigned)n->repairs, (unsigned)n->longest_hold,
             (unsigned)n->named, (unsigned)n->still_running, (unsigned)n->lines_left_out);
    log_info("  the locks that were no scene of the host's: %u of a far player's dropped because "
             "the host had no place where that player stood, %u because he could not be brought "
             "there before the wait ran out; the host was let go of each at once, and a lock "
             "stayed owed under a menu of the engine for %u substep(s); %u release(s) a far "
             "player's run was refused made up for the host after their actor went away",
             (unsigned)n->dropped[0], (unsigned)n->dropped[1], (unsigned)n->let_go_waits,
             (unsigned)n->owed_made_up);
    log_info("  the scenes given up (the host): %u after the wait for the host ran out, %u of "
             "them with the host dead, %u where it could not be moved, %u with no body to be "
             "taken, %u whose mode did not read, %u standing again only at the end; each was "
             "played here as it would be with nobody else",
             (unsigned)(n->gave_up[MP_SCENE_MOVE_DEAD] + n->gave_up[MP_SCENE_MOVE_MODE] +
                        n->gave_up[MP_SCENE_MOVE_NO_BODY] + n->gave_up[MP_SCENE_MOVE_UNREAD] +
                        n->gave_up[MP_SCENE_MOVE_YES]),
             (unsigned)n->gave_up[MP_SCENE_MOVE_DEAD], (unsigned)n->gave_up[MP_SCENE_MOVE_MODE],
             (unsigned)n->gave_up[MP_SCENE_MOVE_NO_BODY],
             (unsigned)n->gave_up[MP_SCENE_MOVE_UNREAD],
             (unsigned)n->gave_up[MP_SCENE_MOVE_YES]);
    log_info("  the grabs (the host): %u grab(s) of the hero, %u asked while the host was "
             "brought to its place and held, the longest hold %u substep(s); %u put-back(s) "
             "refused while a hold stood", (unsigned)cut.grabs, (unsigned)cut.grabs_held,
             (unsigned)n->longest_hold, (unsigned)cut.putbacks_held);
    log_info("  the moves (the host): %u seated, %u refused because the body was dead, %u for "
             "its mode (anything the engine would not park for a scene: a jump, a fall, the "
             "water, a ledge, a push block or a gun) and %u because its mode did not read, %u "
             "given up after their tries, %u not moved because the scene ran first; %u fade(s) "
             "ended on their own clock rather than the engine's", (unsigned)n->seated,
             (unsigned)n->refused_dead, (unsigned)n->refused_mode,
             (unsigned)n->refused_unread, (unsigned)n->given_up, (unsigned)n->ran_first,
             (unsigned)n->fades_on_clock);
    log_info("  the host's way to the place (the host): %u by the teleport, %u by the respawn, "
             "%u after a re-entry, %u retries, %u at a gun, %u unread waits; the longest wait "
             "%u substep(s); the input held %u time(s); the hero taken %u time(s) at the place "
             "the host was brought to, %u of them a unit or more from it (must be 0), %u where "
             "the host was left (no place, every far player gone, given up, or a scene of the "
             "host's own)",
             (unsigned)n->own_by_teleport, (unsigned)n->own_by_respawn,
             (unsigned)n->own_after_reentry, (unsigned)n->own_retries,
             (unsigned)n->own_at_gun, (unsigned)n->own_unread, (unsigned)n->own_longest,
             (unsigned)n->input_held, (unsigned)n->grabs_at_place, (unsigned)n->grabs_off,
             (unsigned)n->grabs_left);
    log_info("  the warps (the host): %u taken by the engine of this host (%u quest bit(s) "
             "changed), %u dropped by the engine because the player module was not running, %u "
             "waiting re-entry(ies) ended; the respawns asked of this host: %u by the warp of a "
             "script, %u by the hero swap, %u by another caller in the executable, %u by a DLL",
             (unsigned)n->warps, (unsigned)n->warp_bits, (unsigned)n->warps_dropped,
             (unsigned)n->reentries_ended, (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_WARP],
             (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_SWAP],
             (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_IMAGE],
             (unsigned)n->respawns[MP_SCENE_RESPAWN_BY_DLL]);
    mp_seat_report_searches("the place of a scene:", seats);
    log_info("  the place of a scene, where the far player's own could not be taken: %u "
             "fallback(s) beside the scene's actor, %u place(s) read again for a player in the "
             "air or the water", (unsigned)n->beside_actor, (unsigned)n->place_waits);
    log_info("  the actor holds (the host): %u script run(s) of a scene's actor held back while "
             "the host was brought to its place, %u of them for a lock, %u for the hero as an "
             "actor",
             (unsigned)(n->actor_held[MP_SCENE_KIND_LOCK] + n->actor_held[MP_SCENE_KIND_HERO]),
             (unsigned)n->actor_held[MP_SCENE_KIND_LOCK],
             (unsigned)n->actor_held[MP_SCENE_KIND_HERO]);
    log_info("  the measure of a standing scene (the host): %u line(s) written, %u left out past "
             "%u a scene or %u in all", (unsigned)n->measures, (unsigned)n->measures_left_out,
             (unsigned)MP_SCENE_MEASURE_LINES, (unsigned)MP_SCENE_MEASURE_LINES_ALL);
}

/* ==============================================================================================
 * The measuring line.
 * ============================================================================================ */

/* An actor's fields from its start up to the clip its script asked for, read in one piece. */
#define ACTOR_BYTES (MP_CHARACTER_CLIP_ASKED + 4u)

/* One actor as the line says it: its placement, the state the actor list ticks it in, its
 * script's own state, the five counters, its health, and the clip it plays beside the one its
 * script asked for. An address the pool has taken back keeps its bytes, so the actor is asked of
 * its own record first and one that is no longer its placement's is said to be gone. */
static void say_an_actor(char *out, size_t size, uintptr_t actor, uint32_t key, bool keyed)
{
    uint8_t bytes[ACTOR_BYTES];
    int32_t state  = 0;
    int32_t script = 0;
    int32_t health = 0;
    int32_t clip   = 0;
    int32_t asked  = 0;
    int32_t counter[MP_CHARACTER_COUNTER_COUNT];

    if (actor == 0u) {
        (void)text_format(out, size, "none");
        return;
    }
    if (!keyed) {
        (void)text_format(out, size, "at %08X with no placement read", (unsigned)actor);
        return;
    }
    if (!mp_enemy_slot_is_actor(mp_enemy_bind_slot(actor, key, NULL)) ||
        !memory_try_read(actor, bytes, sizeof bytes)) {
        (void)text_format(out, size, "placement %u, gone", (unsigned)key);
        return;
    }
    memcpy(&state, bytes + MP_CHARACTER_STATE, sizeof state);
    memcpy(&script, bytes + MP_CHARACTER_SCRIPT_STATE, sizeof script);
    memcpy(&health, bytes + MP_CHARACTER_HEALTH, sizeof health);
    memcpy(&clip, bytes + MP_CHARACTER_CLIP, sizeof clip);
    memcpy(&asked, bytes + MP_CHARACTER_CLIP_ASKED, sizeof asked);
    memcpy(counter, bytes + MP_CHARACTER_COUNTERS, sizeof counter);
    (void)text_format(out, size, "placement %u in state %d, its script in state %d, counters %d "
                      "%d %d %d %d, health %d, clip %d (asked %d)", (unsigned)key, (int)state,
                      (int)script, (int)counter[0], (int)counter[1], (int)counter[2],
                      (int)counter[3], (int)counter[4], (int)health, (int)clip, (int)asked);
}

static const char *when_text(mp_scene_measure_when_t when)
{
    switch (when) {
    case MP_SCENE_MEASURE_BEGUN:    return "as it began";
    case MP_SCENE_MEASURE_RELEASED: return "as its hold fell";
    case MP_SCENE_MEASURE_STANDING:
    default:                        return "while it stands";
    }
}

void mp_scene_host_measure(const mp_scene_measure_t *what)
{
    char      door[224];
    char      driver_text[224];
    char      registers[96];
    char      voice[64];
    uintptr_t driver;
    uint32_t  driver_key = 0u;
    bool      driver_keyed;
    int32_t   flag[MP_SCRATCH_AI_SLOTS];
    int32_t   previous[MP_SCRATCH_AI_SLOTS];
    float     expiry[MP_SCRATCH_AI_SLOTS];
    int32_t   channel = -1;

    if (what == NULL) {
        return;
    }
    say_an_actor(door, sizeof door, what->door, what->door_key, what->door_keyed);
    driver       = mp_scene_hero_watch_driver(what->substep);
    driver_keyed = driver != 0u && mp_enemy_bind_index(driver, &driver_key);
    if (driver != 0u && driver == what->door) {
        (void)text_format(driver_text, sizeof driver_text, "the same actor");
    } else {
        say_an_actor(driver_text, sizeof driver_text, driver, driver_key, driver_keyed);
    }
    if (mp_scratch_bind_read_ai(flag, previous, expiry)) {
        (void)text_format(registers, sizeof registers, "%d %d %d %d", (int)flag[0], (int)flag[1],
                          (int)flag[2], (int)flag[3]);
    } else {
        (void)text_format(registers, sizeof registers, "not read");
    }
    if (!mp_voice_channel_now(&channel)) {
        (void)text_format(voice, sizeof voice, "whether a line is voiced did not read");
    } else if (channel >= 0) {
        (void)text_format(voice, sizeof voice, "a line is voiced on channel %d", (int)channel);
    } else {
        (void)text_format(voice, sizeof voice, "no line is voiced");
    }
    log_info("scene %u measured %s, %u substep(s) in: the actor whose script opened it: %s; the "
             "actor driving the host's body: %s; the level's registers: %s; %s",
             (unsigned)what->serial, when_text(what->when), (unsigned)what->since, door,
             driver_text, registers, voice);
}
