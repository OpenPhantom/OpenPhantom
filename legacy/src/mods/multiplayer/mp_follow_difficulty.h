/* mp_follow_difficulty.h: every player of a session plays the host's difficulty.
 *
 * The difficulty is the column of the damage table a hit on the player is read from, and it is
 * campaign progress rather than a setting: the engine raises it by one at every level end and puts
 * it back to four when a new game begins. A host that goes on to its next level through its own
 * movie takes the first path; its clients follow it through the title screen and take the second,
 * so after the first level change the two sides took damage from different columns and nothing on
 * either screen said so.
 *
 * THE HOST says its difficulty in the setup note it repeats once a second, read from its own cell
 * on every send and put on a copy of the note (mp_bridge_lobby), so a later note carries any
 * later change. Doom's start packet and Quake 3's system info carry the skill the same way: the
 * one node that owns the session says it, and everybody else takes it.
 *
 * THE CLIENT writes it into its own cell as the first thing a level begins with, so everything the
 * level begin reads, the world's line in the log included, reads the host's value. It writes it
 * again after a savegame's restore, which the engine runs after the level begin and which restores
 * the difficulty the file was written with; and while a level of the session runs it holds the
 * cell to the note, which takes a later note's value and puts back a change the engine made here.
 * It writes nothing while an exit waits, because the level that begins then is single player, and
 * nothing while the note says none, which a dedicated server says: the cell keeps this side's own.
 *
 * Single player never comes here: every entry refuses without an armed transport.
 */
#ifndef MULTIPLAYER_MP_FOLLOW_DIFFICULTY_H
#define MULTIPLAYER_MP_FOLLOW_DIFFICULTY_H

#include <stdbool.h>
#include <stdint.h>

/* Whether an exit of the session waits, asked of the module that owns the exit: a level that
 * begins then is single player. Unset, no exit ever waits. */
void mp_follow_difficulty_set_exit_question(bool (*exit_waits)(void));

/* HOST: the byte the setup note says, the cell plus one, or MP_LOBBY_DIFFICULTY_NONE when the cell
 * does not read or holds no difficulty. The setup note's source, asked on every send. */
uint8_t mp_follow_difficulty_to_say(void);

/* CLIENT: module message 5, before anything else of the level begin reads the cell. */
void mp_follow_difficulty_note_level_begin(void);

/* CLIENT: a restore by name has run, and with it the engine's own restore of the difficulty. */
void mp_follow_difficulty_note_restore(void);

/* CLIENT: once per drawn frame. `leaving` is that this side is on its way out of its level to
 * follow the host, whose level end raises the cell here as well. */
void mp_follow_difficulty_hold(bool leaving);

void mp_follow_difficulty_report(void);

#endif /* MULTIPLAYER_MP_FOLLOW_DIFFICULTY_H */
