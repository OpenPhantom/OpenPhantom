/* mp_respawn.h: putting a dead player back into a level that is still running.
 *
 * ================================ Why this is buildable at all ================================
 *
 * The engine has a re-entry of its own and the campaign uses it for the checkpoint: the player
 * module's state is dropped to 4, which fades the screen out, despawns the body, spawns it again
 * at three globals and fades back in. Two readings of the death path settled that the module
 * state field is NOT touched while a player is dead, so that entry point accepts a corpse; that
 * is the fact this module stands on.
 *
 * What the engine's own re-entry does NOT do is give the body its health back. The spawn writes
 * the start loadout and the ammunition and never touches the health word, so a body respawned
 * after a death comes back with the zero it died with and dies again on the next substep. The
 * campaign never noticed because its two callers only ever call it on a LIVING player. So the
 * order here is not a preference:
 *
 *     the health first, then the re-entry.
 *
 * ==================================== One machinery, two rules ================================
 *
 * Cooperative play wants the dead player back beside whoever is still standing, at once.
 * A deathmatch wants him at a spawn point after a delay the host configured. Those are two
 * different callers, not two mechanisms, so this interface knows neither mode: a caller says
 * WHERE and WHEN, and the difference between the modes is which of the two entry points it uses
 * and what it passes.
 *
 * ================================== It is a wish, not a call ==================================
 *
 * Two engine gates decide whether the re-entry does anything, and both refuse in silence. The
 * player module's own state field has to read 1, which it does not while a level loads, in the
 * frontend, or while a cutscene has the player parked; and there has to be a body. On top of
 * those this module has a third condition of its own, the seat: it will not put a body inside a
 * wall or inside the team mate.
 *
 * So a request is remembered rather than performed. Every frame it is retried, and once
 * MP_RESPAWN_DEADLINE_FRAMES and MP_RESPAWN_DEADLINE_MS have both passed with the gates shut it
 * is dropped with a line in the log rather than carried out at some later moment nobody chose.
 * That is the same shape the lobby's placement and hero wishes already have, and for the same
 * reasons.
 *
 * =================================== The seat is searched at the time =========================
 *
 * A wish beside the living stores the rule, not a place: where this player died and whose ring he
 * is seated on. The search runs when the gates open, around the far players as they stand on that
 * frame, the one nearest the death first, and falls back to an authored point when nothing is
 * free for three seconds on a good anchor. The search, its rings, the four cases that wait and the
 * fallback are mp_seat's; the re-entry keeps the door.
 *
 * ================================= What it hands back afterwards =============================
 *
 * The engine's re-entry takes the contact callback off the player's task node so that a player on
 * his way back cannot be hit, and the spawn at the far end puts the ENGINE's own callback back
 * there. Any module that had a dispatcher in that slot has lost it. This module does not know who
 * that is, so it calls a listener once the body is standing again, and whoever owns the slot
 * re-arms it there.
 */
#ifndef MULTIPLAYER_MP_RESPAWN_H
#define MULTIPLAYER_MP_RESPAWN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long a wish waits for the engine's gates, counted from the request and including whatever
 * delay the caller asked for, and how long a landing is waited for. Two numbers, and both have to
 * be reached. The frames are there because the frame pump does not run while a level loads, so a
 * wait cannot run out behind a loading screen. The milliseconds are there because frames alone
 * measure the frame rate: nine hundred of them are fifteen seconds at sixty a second, which is
 * what was meant, and under four at two hundred and forty, where the engine's own fade of one
 * second and a seat search still have to fit. A wish whose gates are open and whose seat is not
 * found ends through the seat's own stages instead. */
#define MP_RESPAWN_DEADLINE_FRAMES 900u
#define MP_RESPAWN_DEADLINE_MS     15000u

/* The player module's state while the engine's re-entry waits for its fade to end: state 4 starts
 * the fade and becomes this one, and this one spawns the body once the fade has run its time. */
#define MP_RESPAWN_MODULE_FADING 3u

/* What the body comes back with. The engine's own "give the player his health" sites write this
 * value, so it is the game's idea of full rather than this module's. */
#define MP_RESPAWN_HEALTH 100

/* Which of the engine's three gates is shut. */
typedef enum mp_respawn_shut {
    MP_RESPAWN_SHUT_NOTHING = 0,  /* all three open: a seat is all that is wanted */
    MP_RESPAWN_SHUT_LEVEL,        /* no level is running: a load, the front end, an ended level */
    MP_RESPAWN_SHUT_MODULE,       /* the player module is not in its running state */
    MP_RESPAWN_SHUT_BODY          /* the module is running and there is no body to move */
} mp_respawn_shut_t;

/* The rule, in the order the gates are read: without a running level the other two mean nothing,
 * and a module that is not running is a different situation from one that is running with no
 * body. The drop warning used to name all three in one sentence, which left the field run of
 * 2026-09-17 with a deathmatch wish held for 3186 frames and no way to tell which held it. */
mp_respawn_shut_t mp_respawn_gate_shut(bool level_running, bool module_running, bool have_body);

/* That state as the sentence the log prints. Never NULL. */
const char *mp_respawn_shut_word(mp_respawn_shut_t shut);

/* What one tick does with one wish. */
typedef enum mp_respawn_step {
    MP_RESPAWN_STEP_IDLE,   /* nothing is wanted */
    MP_RESPAWN_STEP_HOLD,   /* wanted, and the delay the caller asked for has not run out */
    MP_RESPAWN_STEP_WAIT,   /* the delay is over and the engine is not ready */
    MP_RESPAWN_STEP_RUN,    /* look for the seat now, and carry it out if there is one */
    MP_RESPAWN_STEP_DROP    /* the deadline passed with the gates never open */
} mp_respawn_step_t;

/* Called once, after the body is standing again, so that whoever owns the contact dispatch slot
 * on the player's task node can put its procedure back. */
typedef void (*mp_respawn_landed_fn_t)(void);

/* Resolves the sites, the cells and the seat's three world probes. False with a log line when
 * something required did not resolve, in which case every request below answers false rather than
 * calling into an address that is not there. */
bool mp_respawn_install(void);

bool mp_respawn_installed(void);

/* ==============================================================================================
 * Asking for a re-entry. Both say WHERE and WHEN and neither knows the mode. `slot` is this
 * player's world slot, which is the direction his ring starts in.
 * ============================================================================================ */

/* At a point of the caller's choosing: a spawn point in a deathmatch. The point itself is tried
 * first and the rings around it after, so an authored standing position that a mover has since
 * covered still answers. `delay_frames` is counted from this call. */
bool mp_respawn_at(const float position[3], float heading, uint8_t slot, uint32_t delay_frames);

/* Beside whichever far player stands when the seat is searched: the team mate in cooperative play.
 * `died_at` is where this player died, and the far player nearest to it is tried first; NULL tries
 * them in bank order. No far player is ever a candidate himself, and the line from him to every
 * candidate has to be walkable, so the body comes back somewhere its mate could have walked to. */
bool mp_respawn_beside(const float *died_at, uint8_t slot, uint32_t delay_frames);

/* True while a wish waits, while a re-entry lands, and while a living player moved by
 * mp_respawn_move_living lands. */
bool mp_respawn_pending(void);
void mp_respawn_cancel(void);

/* The engine's re-entry for a LIVING player, with his own hero, onto `position`: the one way the
 * engine has to bring a player out of any mode, a jump, a fall, the water or a ledge, which the
 * teleport would leave him in. Nothing else of a re-entry: no health is written, because the
 * player lives and the spawn keeps the health it finds; no seat is searched, because the caller
 * has one; and no seat is noted as one a life began on, because no life ended. Carried out at
 * once or refused: refused unless the module runs, no wish or landing of this module is under way
 * and the pose is finite. Its landing is watched apart from a re-entry's, and the contact slot's
 * listener is called once the body stands again, because the spawn has overwritten that slot
 * just as it does after a death. */
bool mp_respawn_move_living(const float position[3], float heading);

/* Takes back a wish that is still waiting for its seat, for the caller whose rule set no longer
 * wants it carried out. A body whose re-entry the engine has already been asked for is left to
 * land. True when a wish was taken back. */
bool mp_respawn_withdraw(void);

/* From the frame pump, once per frame, with the substep count the seat's clock is measured in and
 * the wall clock the deadlines are measured in. This is where a wish is retried, carried out or
 * dropped, and where the landing is noticed. */
void mp_respawn_tick(uint32_t substeps, uint32_t now_ms);

void mp_respawn_set_landed_listener(mp_respawn_landed_fn_t listener);

/* ==============================================================================================
 * The pure decisions, so that a test can pin them with no game in the process.
 * ============================================================================================ */

/* One tick's answer for one wish. The delay is asked about BEFORE the gates, because a delay a
 * mode configured is a rule of the round and an open gate is not a reason to break it; the gates
 * are asked about before the deadline, because a wish that becomes possible on the very frame it
 * runs out is better carried out than thrown away. */
mp_respawn_step_t mp_respawn_step(bool pending, bool gates_open, uint32_t frames_waited,
                                  uint32_t waited_ms, uint32_t delay_frames);

/* Whether a wait has run out: both the frames and the milliseconds have been reached. */
bool mp_respawn_deadline_passed(uint32_t frames, uint32_t ms);

/* The milliseconds a wait has lasted at this look. The first look of a wait stamps its time, and
 * the stamp has a bit of its own: the wall clock starts at nought, so a time of nought is a time
 * and not "not stamped", and a wait that was never stamped would read as fifteen seconds old the
 * moment the clock passed that. Whoever begins a wait clears the bit. */
uint32_t mp_respawn_waited_ms(bool *stamped, uint32_t *since_ms, uint32_t now_ms);

/* Whether the engine's re-entry has lost its fade: the player module waits in the state that
 * ends when the fade does, and the tint is neither running nor run out. The engine's own sequence
 * never produces that, so it means the fade was stopped under the wait. */
bool mp_respawn_fade_is_lost(uint32_t module_state, bool fade_done, bool fade_runs);

/* Whether a pose is a finite point and heading at all. A pose that came off the wire is not this
 * machine's arithmetic and the engine's re-entry stores what it is handed without looking. */
bool mp_respawn_pose_is_usable(const float position[3], float heading);

/* ============================== The one answer to "is he a corpse" ============================
 *
 * The dead flag at 1 and the death descriptor on the mode slot: what the death entry leaves and
 * only a spawn takes away. The death hull asks it before it lets a death be entered twice, the
 * landing asks it before it calls a body standing, and the corpse watch asks it every frame, so
 * the three cannot disagree about the same body. `death_descriptor` nought, an unresolved cell,
 * leaves the flag to answer alone. */
bool mp_respawn_is_a_corpse(uint32_t dead_flag, uint32_t mode, uint32_t death_descriptor);

/* The same, read off a player record; false for one that does not read. */
bool mp_respawn_record_is_a_corpse(uintptr_t record);

/* The same, read off this machine's own player, the hero block. */
bool mp_respawn_player_is_a_corpse(void);

/* This machine's player lives: the module in its running state and no corpse. A body in the fade
 * of its re-entry, parked by a scene or dead is not living. False before the install. */
bool mp_respawn_player_lives(void);

void mp_respawn_report(void);

/* Writes MP_RESPAWN_HEALTH into the active status record through the engine's own setter and
 * nothing else, for a caller about to send a LIVING player through the engine's re-entry (the
 * lobby's hero swap): the spawn at the far end restores no health, and the record the swap lands
 * on may be one a savegame left at anything. False before the install. */
bool mp_respawn_grant_health(void);

#endif /* MULTIPLAYER_MP_RESPAWN_H */
