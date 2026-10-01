/* mp_start.h: how a lobby puts everybody into the same level, through the game's own front door.
 *
 * ===================================== The lever, and why =====================================
 *
 * The title screen is a loop of the engine's own: it reads the focused widget, then reads the
 * navigation code, then acts on the pair. This feature already sits on the navigation code (the
 * hull in mp_menu.c), so it can answer that question, and the focus is sampled BEFORE the hull
 * runs, which is the whole trick:
 *
 *   frame A   the hull puts the focus on a widget and answers "nothing happened"
 *   frame B   the loop samples that focus, the hull answers ACCEPT, and the title menu acts
 *
 * Widget 1 is NEW GAME and widget 2 is LOAD GAME, so two frames of that is the game being started
 * exactly as a player starts it: no patched jump, no forged click, no reimplementation of a load.
 * What decides WHICH level then loads is set in the engine's own cells before frame B. Widget
 * ids above 4 must never be focused this way: the title loop indexes its four background videos
 * with the focus id less one and tests only for -1. Ids 1 and 2 are safe.
 *
 * ==================================== The three ways in ======================================
 *
 *   A SHIPPED LEVEL      BOTH level cells take the table index, the start level at [0x6CCFDC]
 *                        and the current level at [0x88136C], and the campaign round loads it
 *                        from the table. The level index stays sane, so finishing it advances
 *                        to the next one exactly as the campaign does. For a long time only the
 *                        first cell was written and every deathmatch host loaded the first row of
 *                        the table whatever he chose, because the round runs
 *
 *                            restart:
 *                                g_restorePending = 0;
 *                                g_campaignLevelIndex = g_startLevelIndex;   ; the copy
 *                                module_broadcast(0, SAVMSG_NEW_GAME);
 *                                want = title_main_menu();              ; the lobby lives here
 *
 *                        so the copy has happened before the lobby runs, and a write to the start
 *                        cell alone is one round late. The current level cell is what the loader
 *                        indexes with and what the completion arm increments. The start cell is
 *                        still written because the arm for "quit the level to the front end"
 *                        jumps back to the restart label from above the round's own clearing of
 *                        it, and copies it again; what should stand there is the lobby's level.
 *
 *   ANY .b3d BY PATH     the load-by-name flag at [0x6CD008] is set and the path is written into
 *                        the engine's own 128 byte buffer at [0x881374], which the loader then
 *                        takes VERBATIM: `if (flag == 1) { levelIndex = -1; load(buffer); }`,
 *                        with no test of the answer. This is the game's own command-line switch
 *                        (the parser at 0x0043F0C8 sets the same two cells), not a mechanism of
 *                        ours.
 *
 *   A SAVEGAME           the title menu is steered onto LOAD GAME, and a detour on the shipped
 *                        load screen restores the chosen file and answers "loaded" without ever
 *                        showing itself. The campaign round then skips its own level load,
 *                        because the save restored one.
 *
 * The load-by-name flag is transient. The campaign round never clears it, so a set flag would
 * make every later round reload the same buffer; the engine's own save-restore sets it and clears
 * it around one call. This module does the same, from the frame pump, as soon as the game mode
 * says a level is running.
 *
 * The by-name branch does not check the loader's answer. A path that names nothing leaves a
 * null world pointer behind. Nothing is started here that mp_levels_present has not confirmed.
 *
 * ========================= Placing a player, and the two silent gates =========================
 *
 * Once a level is up, two more things this module does are placing the player somewhere other
 * than where the level put him, and putting him on the hero he chose in the lobby. Both go
 * through engine entry points that refuse quietly rather than fail:
 *
 *   the player module's own state field has to read 1. It reads 0 for the whole of a level load,
 *   in the frontend, and while a cutscene has the player parked, and the respawn the hero swap
 *   goes through opens by comparing it against 1 and returning without a word otherwise:
 *
 *       00447C90  55 8B EC                 push ebp; mov ebp, esp
 *       00447C93  83 3D 44 F6 6C 00 01     cmp dword [0x6CF644], 1     ; the hero block's +0x04
 *       00447C9A  74 02 EB 55              je +2 / jmp over the whole body
 *
 *   The despawn writes 0 there, the hero spawn writes the 1 in its LAST line, and the cutscene
 *   park at 0x00450F25 holds it away from 1 for as long as the player is parked;
 *
 *   there has to be a living body. The swap asks for one itself and does nothing when there is
 *   none, and a placement written before the body exists is overwritten by the spawn. The
 *   engine's own test at 0x00447D18 answers no body when the mode descriptor is the death
 *   descriptor at [0x004B5448]; what is checked here is the cheaper actor handle at the block's
 *   +0x0C, which is necessary and not sufficient, a dead player still has an actor, and that is
 *   why the hero swap is confirmed by reading the hero index back rather than by trusting the
 *   call.
 *
 * A restore into a process that has already run a level has one more hazard. The restore's last
 * phase seats the player through the teleport, which zeroes the ground contact block at the
 * hero block's +0x2CC; a field crash on the first substep read the floor polygon pointer at that
 * block's +0x14 out of a world that had just been freed, in the carry step at 0x00449047 through
 * the rider sub node acquisition at 0x0040BC71. Whether the teleport's clear was undone or never
 * reached is not known, so the block is read after the restore returns, and a pointer standing
 * in it is said with its value and cleared the way the teleport clears.
 *
 * So neither call is made at the moment it is asked for. A caller records a WISH, the frame pump
 * retries it every frame, and after MP_START_WISH_DEADLINE_FRAMES it is dropped with a line in the
 * log rather than carried out at some later moment nobody chose it. Each wish is carried out
 * exactly once: a second placement, after the player has begun to move, is a rubber band.
 */
#ifndef MULTIPLAYER_MP_START_H
#define MULTIPLAYER_MP_START_H

#include <stdbool.h>
#include <stdint.h>

/* How long a wish waits for the two gates. The frame pump does not run while a level loads, so
 * these are drawn frames after the load rather than wall clock time. */
#define MP_START_WISH_DEADLINE_FRAMES 600u

/* What one tick does with one wish. */
typedef enum mp_start_wish_step {
    MP_START_WISH_IDLE,   /* nothing is wanted */
    MP_START_WISH_WAIT,   /* wanted, and the engine is not ready for it */
    MP_START_WISH_RUN,    /* carry it out now */
    MP_START_WISH_DROP    /* the deadline passed with the gates never open */
} mp_start_wish_step_t;

typedef enum mp_start_kind {
    MP_START_NONE = 0,
    MP_START_SHIPPED,   /* a level of the game's own, by table index */
    MP_START_BY_PATH,   /* any .b3d under the data root, by relative path */
    MP_START_SAVE       /* a savegame, restored by the shipped screen's own code */
} mp_start_kind_t;

/* Resolves the sites and installs the detour on the load screen. False when a site did not
 * resolve, in which case nothing can be started and every request below answers false. */
bool mp_start_install(void);

/* Ask for a start. Each validates what it can before it says yes: an index inside the table, a
 * file that is really on disk. Answering true only means the request stands; the level loads once
 * the screens are closed and the hull has driven the title menu. */
bool mp_start_shipped(uint8_t level_index);
bool mp_start_by_path(const char *relative);
bool mp_start_save(const char *file);

/* The one choice between the three, for the lobby and for a client following its host into a
 * new world: a savegame when one is named and here, a shipped level by its table index so the
 * campaign can advance afterwards, anything else by its path. */
bool mp_start_level(const char *level, uint8_t level_index, bool from_save, const char *save);

bool            mp_start_pending(void);
mp_start_kind_t mp_start_kind(void);
void            mp_start_cancel(void);

/* Told once when a drive gives up: the lobby that asked for the start has already promised it
 * to others, and nothing in this module can reach them. */
void mp_start_set_given_up_listener(void (*listener)(void));

/* From the navigation hull, once per frame while the title screen is on show. Answers the code the
 * hull should return: the caller's own code while nothing is pending, and otherwise the two step
 * sequence that makes the title menu act. */
int32_t mp_start_drive(int32_t code);

/* From the frame pump, and it does two things. It clears the by-name flag once a level is running,
 * which is what the campaign round would otherwise carry into every later round, and it is where a
 * held pose or hero is retried, carried out or dropped. */
void mp_start_tick(void);

/* Whether a level is running right now, by the engine's own game mode cell: it reads 2 while a
 * level runs, 3 at the end of one, 1 for quit to menu and 4 or more for a death. Writing 1 into
 * it is how the game itself leaves a level. */
bool mp_start_level_running(void);

/* Hold a pose for the player, and seat him there once a level is running and both gates are open.
 * The three floats are copied, so nothing of the caller's is kept. False means the request could
 * not be taken at all, which is counted and said in the log rather than passed on in silence.
 *
 * The seat is the engine's own teleport at 0x00451266, the entry every level warp and every
 * cutscene placement goes through, `void __cdecl (const float *pos, float heading)`:
 *
 *     00451266  55 / 8B EC / 57          push ebp; mov ebp, esp; push edi
 *     0045126A  B9 22 00 00 00           mov  ecx, 0x22
 *     0045126F  33 C0                    xor  eax, eax
 *     00451271  8B 3D 20 52 4B 00        mov  edi, [the player record]      (operand masked)
 *     00451277  81 C7 CC 02 00 00        add  edi, 0x2CC                    ; the ground block
 *     0045127D  F3 AB                    rep  stosd
 *     0045127F  A1 20 52 4B 00           mov  eax, [the player record]      (operand masked)
 *     00451284  8B 4D 0C                 mov  ecx, [ebp+0xC]
 *     00451287  89 88 A0 02 00 00        mov  [eax+0x2A0], ecx              ; the heading
 *
 * and the position goes into +0x118 and the rider position into +0x300. The 0x22 dwords cleared
 * are the ground contact block, and clearing them is the reason to call this rather than write
 * three floats: without it the rider carry drags the player back toward whatever he was standing
 * on. Thirty nine bytes, both operands masked, one match on each of the five retail links and one
 * at 0x00451206 on the recompile, whose player record moves from 0x004B5220 to 0x004B51D0; both
 * operands name the player record, so the cell table reads it out of this site as a cross check
 * against its other rows. */
bool mp_start_place_at(const float position[3], float heading);

/* The same placement for a seat a scene hands out, and the scene's seat goes first. The placement
 * holds one pose, and an arrival after the host's savegame and a gathering for a scene can both
 * want it at once; the seat the scene hands out is the place every other player is being brought
 * to, so it wins, and an arrival asked for while it is held is set aside and counted. A scene's
 * seat replaces an arrival that was still held, counted as well. */
bool mp_start_place_for_scene(const float position[3], float heading);

/* What a new pose does to the one the placement holds. */
typedef enum mp_start_pose_take {
    MP_START_POSE_TAKE = 0,            /* nothing held, or the same kind: the newer wins */
    MP_START_POSE_REPLACE_ARRIVAL,     /* a scene's seat over a held arrival */
    MP_START_POSE_SET_ASIDE            /* an arrival while a scene's seat is held: refused */
} mp_start_pose_take_t;

mp_start_pose_take_t mp_start_pose_take(bool held, bool held_for_scene, bool for_scene);

/* Forget a pose that is still held. A pose is a point in ONE level's world, and a wish that
 * outlived the level it was made for would seat the player at coordinates from a map that is no
 * longer loaded. The deadline catches the ordinary case; this is for the caller that knows a new
 * world is starting and can say so before the deadline would. */
void mp_start_cancel_pose(void);

/* The same for the hero, through the engine's own swap at 0x004302AA, the worker behind the two
 * shipped hero cheats: it takes the living body's position and heading and hands them with the
 * index to the respawn at 0x00447C90. The index is clamped into range first: the spawn stores it
 * raw at 0x00447E99 and indexes the hero table at [0x004B5180] with it raw at 0x00447ED7, and the
 * dword after the table, [0x004B5190], is the scalar 0x21, which would be handed on as a name
 * pointer; the assert behind that shows a message box and ends the process.
 *
 * The swap zeroes the whole player block, 0xEB dwords from 0x6CF640, and rewrites the checkpoint
 * a later death returns to: the respawn writes the hero into [0x6CF638], the position into
 * [0x6CF628] and the heading into [0x6CF634], and there is no variant of the call that does not.
 * When a pose is wanted as well, the hero is applied first and the pose waits for the frame after,
 * because the swap would otherwise wipe it. */
bool mp_start_apply_hero(uint8_t hero);

/* The three pure decisions behind the wishes, so that a test can pin them with no game in the
 * process. The order inside the first is itself the decision: an open gate beats an expired
 * deadline, because a wish that becomes possible on the very frame it runs out is better carried
 * out than thrown away. */
mp_start_wish_step_t mp_start_wish_step(bool pending, bool gates_open, uint32_t frames_waited);
uint8_t              mp_start_clamp_hero(uint8_t hero);
bool                 mp_start_pose_is_usable(const float position[3], float heading);

void mp_start_report(void);

#endif /* MULTIPLAYER_MP_START_H */
