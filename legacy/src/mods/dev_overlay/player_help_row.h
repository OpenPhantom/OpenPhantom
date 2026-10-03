/* player_help_row.h: the two buttons under Multiplayer, "Repair lock" and "Teleport to host", and
 * the line under them that says what the last press came to.
 *
 * Neither button is carried out here. Giving a player's controls and camera back, and moving a
 * living player beside the host, are the multiplayer's to do: it knows what a scene holds and
 * where the host stands, and a feature DLL may not call another one. So a press files an ask in
 * one record of common/player_help_note and the multiplayer files its answer in a second one,
 * each with exactly one writer. This file is the panel's end of that. It files the ask, reads
 * the answer, and turns the answer into whether a button can be pressed and into the sentence
 * under the buttons.
 *
 * A press closes the panel FIRST. An open panel holds the player's module on its idle state
 * (input_freeze.c), and from the multiplayer's side that looks exactly like a scene that parked
 * the player: a repair would find a held player it may not release, and a teleport would wait
 * for a body it may move. The one time the panel stays up is under the free camera in flight,
 * which keeps the panel for a reason of its own (input_owner.c). The ask then says that the
 * overlay still holds the player, and the multiplayer leaves that player's module alone.
 *
 * Because the panel is gone when the answer arrives, the answer is kept: the line under the
 * buttons reads it at the next opening, and the log says it the moment it comes.
 *
 * A button is OFFERED only while a session runs and the multiplayer says in its answer record
 * that it would carry that button out. The second half is not caution for its own sake. The
 * session note reads as running for a multiplayer of another build as well (session_lock.c),
 * and that one would never read the ask: the row would be pressable and do nothing.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_PLAYER_HELP_ROW_H
#define DEV_OVERLAY_PLAYER_HELP_ROW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The room a sentence of the line gets, terminator included: a row's label less the ten
 * characters of "    Last: " in front of it. Every sentence this file writes fits, and the unit
 * test holds each of them to it, because a label that is too long is cut without a word. */
#define PLAYER_HELP_ROW_WORDS_MAX 38u

/* Files the empty ask, once, when the overlay loads. The multiplayer's reader looks for the ask
 * every frame of a session, and a name nobody filed is a failed lookup in the operating system
 * each time; with the empty ask on file its first look finds the note. */
void player_help_row_install(void);

/* Once a frame, from the end of the scene and before the panel is rebuilt: one reading of the
 * answer record for this picture, and a line in the log when it answers the last press. The
 * record is read only while a session runs, and until the first read that found it at most once
 * per PLAYER_HELP_NOTE_RETRY_MS of `now_ms`, which is GetTickCount's. */
void player_help_row_tick(uint32_t now_ms);

/* Whether the button of `kind`, PLAYER_HELP_KIND_REPAIR or PLAYER_HELP_KIND_TELEPORT, can be
 * pressed, out of the last tick's reading. `reason`, when not NULL, receives the overlay_reason_t
 * the row shows when it cannot: no session, the host asking to be taken to the host, a press
 * still being worked on, and no reason at all for a session whose multiplayer does not answer
 * for that button. */
bool player_help_row_offered(uint8_t kind, uint32_t *reason);

/* A press. Refuses a button that is not offered, so a click that got past an older picture asks
 * nothing. Otherwise it closes the panel, files the ask under the next serial and says so in the
 * log. False when nothing was asked. */
bool player_help_row_press(uint8_t kind);

/* The sentence for the line under the buttons, without the "Last: " in front of it. False while
 * no button has been pressed in this process, and then there is no line. `out` may be NULL for a
 * caller that only counts rows. `refused`, when not NULL, receives whether the session turned
 * the press down, which the line is drawn in the warning colour for. */
bool player_help_row_last(char *out, size_t size, bool *refused);

/* The words for one answer: which button, a PLAYER_HELP_OUTCOME_* and a PLAYER_HELP_REASON_*.
 * Pure, and total: an outcome of NONE reads as a press nobody answered yet, and a reason this
 * build does not know reads as a refusal with no reason. */
void player_help_row_sentence(uint8_t kind, uint8_t outcome, uint8_t reason, char *out,
                              size_t size);

#endif /* DEV_OVERLAY_PLAYER_HELP_ROW_H */
