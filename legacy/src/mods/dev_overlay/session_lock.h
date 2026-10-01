/* session_lock.h: the rows a running multiplayer session takes away, and the sentence that says
 * why.
 *
 * The panel has no idea what the multiplayer is doing and must not ask it: a feature DLL does not
 * depend on another one. What it can do is listen. The multiplayer publishes `common/session_note`
 * whenever a transport goes up or comes down, and this file turns that one fact into an answer per
 * row.
 *
 * What gets taken away, and why it is a list rather than a rule. Four different things put a row
 * on it, and only the first is obvious:
 *
 *   a row that moves this machine out of the world the others are in. The level skip is the whole
 *   of that one: what takes everybody into a level together is the host's own start, and a skip
 *   takes one machine and leaves the rest;
 *
 *   a row whose value the session decides for every machine. The host's draw distance, fog band
 *   and dismemberment mode are the ones in force on each client (common/host_settings_note); the
 *   host's difficulty and its two shot cheats are put back onto a client's cells whenever they
 *   differ; the 60fps cheat is held off on every machine, because a session counts in 1/32 s; and
 *   the graphics detail level decides which placements the host's activation scan wakes. On a
 *   client such a row writes a value nothing reads until the session ends, or flips a cell the
 *   multiplayer puts back, and the draw distance and the fog band show the host's value in their
 *   chip instead. On the host these are the values every machine plays at, and the lock takes
 *   them there as well, so the panel does not change them under everybody in the middle of a
 *   level. That is most of the list, and it is why the fog band is on it while "No fog" beside it
 *   is not: the band writes a key of `[view_distance_fix]` the host hands out, the switch writes
 *   `[dev_overlay]`, which is this machine's own;
 *
 *   a row whose effect the session cannot carry. The NPC spawner is on it while the session's
 *   multiplayer does not run the copies (a loopback, or a multiplayer older than the copies):
 *   a copy nobody hands a key out for is one the other machines cannot be shown. Once the
 *   multiplayer runs them, the group is open and its rows become wishes (npc_spawn_link.c);
 *
 *   and rows that are this machine's own. The two switches under the draw distance decide how far
 *   this machine's frame governor may lower it, which every machine decides for itself, and
 *   nothing compares `[framerate_fix]` or hands it out: the frame rate reaches the shared clock
 *   only through a frame longer than the tenth of a second the engine clamps one to (0x00475706),
 *   which a limit under ten frames a second makes. The list holds them all the same.
 *
 * What is not on it is as deliberate. The four play-as codes travel, because what a player wears
 * is sampled every substep, and so do the two player scales, which ride the appearance
 * (local_look.c): a giant is a giant on every machine. No clip, super run and jump boost travel as
 * what they do, because positions travel. Unlimited health and ammunition are this machine's own
 * business and cost the session nothing. The field of view, the subtitle size, the window, the
 * pad and this panel's own size are settings nobody compares.
 *
 * The cost of being wrong is not symmetric, which is why the list is written out rather than
 * guessed at: a row locked that did not need to be is a row somebody cannot use while they play
 * together, and a row left open that needed locking moves one machine out of the shared world,
 * or changes that world under everybody else.
 */
#ifndef SESSION_LOCK_H
#define SESSION_LOCK_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* What the panel writes under the rows it has taken away. The entity spawner has none of its own
 * here, on purpose: a session is not what takes that group, a session that does not run the copies
 * is, and that is one of the four states the group already answers for itself, in a sentence its
 * rows, the placement mode and the log all read out of spawn_reason.c. A second sentence here read
 * "The multiplayer runs no spawned entities" three rows under "Why: the session runs no copies",
 * which is one state with two spellings. */
#define SESSION_LOCK_WORD "Not during a multiplayer session"

/* Reads the note once, for the rebuild that follows. Every answer below comes out of that one
 * reading, so one picture cannot hold two answers. */
void session_lock_refresh(void);

/* Whether that reading saw a session running, for a click that has to refuse what the lock
 * refused when its picture was drawn. */
bool session_lock_running(void);

/* Whether this machine is the one the others follow. Meaningless while nothing runs, and false
 * there. It is here rather than in the drawing because this file is the only one in the panel that
 * reads the multiplayer's note at all, and a second reader of it would be a second answer.
 *
 * The note says this much and no more: whether a session runs and who hosts it. It carries no
 * count of the players, so nothing in the panel can say one. */
bool session_lock_is_host(void);

/* Whether the panel may hold the simulation still while it is open. Out of a session it may; in
 * one it may not, and this reads the note itself rather than the last refresh, because the answer
 * is wanted the moment the panel opens rather than once a picture. */
bool session_lock_panel_may_pause(void);

/* Takes the row away when a session cannot survive it: `available` goes false, which the panel
 * already draws as `n/a` and refuses to click. Answers whether it did. */
bool session_lock_take(uint32_t group, uint32_t slot, overlay_row_t *row);

/* Whether anything in this group is taken away, so a group can say so once under its rows. */
bool session_lock_holds_group(uint32_t group);

/* The sentence that group writes under them, or NULL when the group says it itself and the lock
 * would only be saying it again. The entity spawner is the one, see the word above. */
const char *session_lock_word(uint32_t group);

/* The retail toggles a running session takes, by the code's own text, because the index of a
 * code is the executable's and not this tree's.
 *
 * Readable from outside so that the three can be held against the eleven the game actually
 * has (cheats_original_known_code). They were three string literals here and three more in
 * unittests/session_lock.c, held against each other; a fourth code typed into either list
 * was a lock that matched nothing, and every program in the build stayed green. NULL past
 * the end. */
uint32_t    session_lock_taken_toggle_count(void);
const char *session_lock_taken_toggle(uint32_t index);

/* Whether a session has taken the entity spawner: one runs, and the multiplayer does not run the
 * NPC copies in it. The placement mode asks this as well as the rows, out of this one function,
 * because a mode that refuses on one reading of that state while the rows beside it are open on
 * another is one state with two answers. It takes its own reading, the way
 * session_lock_panel_may_pause does, since the mode is asked between pictures. */
bool session_lock_holds_the_spawner(void);

#endif /* SESSION_LOCK_H */
