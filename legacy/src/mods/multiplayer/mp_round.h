/* mp_round.h: what a deathmatch is worth so far, on the cadence the host's setup note goes out on.
 *
 * The arithmetic itself is mp_score, which is pure and knows nothing of a wire or a clock. This
 * file is what feeds it: it reads the host's repeated setup note once per drawn frame, starts a
 * round when the generation in that note changes, hands every death it is told about to the table,
 * and, on the machine that is the authority, publishes the table on the same channel.
 *
 * A round is a generation. The host raises the setup's generation on every world change, so both
 * sides start a round from the same repeated note and neither has to be told separately.
 *
 * It lived in multiplayer.c until that file was full. The seam is exactly this subject: the round
 * shares with the installer only the two lines that register it and the answer to whether this
 * machine is the authority, and that answer is now set rather than read out of the installer's
 * state.
 *
 * What it does not do is end the level. A decided round is g_levelOutcome = 3 with
 * g_restorePending = 1, which belongs with the step that takes everybody into the next world
 * rather than with the arithmetic that decided this one. The named place for it is in the source.
 */
#ifndef MULTIPLAYER_MP_ROUND_H
#define MULTIPLAYER_MP_ROUND_H

#include "mp_hit_relay.h"
#include "mp_rules.h"
#include "mp_score.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Which side keeps the truth. A host works the table out against its own substep counter and
 * publishes it; a client works nothing out and takes what arrives. It is set rather than read
 * because the two ways into a session, the menu and the ini, decide the role differently and a
 * loopback bridge is neither. Off until somebody says otherwise, which is the safe direction: a
 * machine that never learns it is the host publishes nothing instead of publishing a second
 * truth. */
void mp_round_set_authority(bool is_host);

/* Every death this machine has to know about arrives here exactly once: its own from the contact
 * dispatcher through the relay, a far player's off the wire. Wire it to the relay's death
 * listener. */
void mp_round_take_death(const mp_death_note_t *note);

/* The arm on the bridge's reliable channel for the host's repeated table. Answers whether the
 * note was one of the round's, which is what the drain's note taker contract asks. */
bool mp_round_take_note(const uint8_t *note, size_t bytes);

/* Once per drawn frame, after the session has been pumped, so a setup that arrived in this frame
 * starts its round in this frame. */
void mp_round_pump(void);

/* The numbers, handed to the file that prints the rest of the bridge's report. */
void mp_round_report(void);

/* The session this round belonged to is over.
 *
 * Until this existed there was NO assignment of false to the running flag anywhere in the module:
 * a round, its table and its score survived the session that made them and the level they were
 * played in, and mp_round_board kept answering true, so the next level opened with the last
 * one's scoreboard still on the screen and its deaths still being counted.
 *
 * It is deliberately not called at the end of a ROUND. A decided round stays running on purpose,
 * because that is what keeps its final table on the screen for the players to read. What ends it
 * is the session, and that is this. */
void mp_round_end_session(void);

/* What a display may read, and both hand out a COPY. The table changes on a death and the rule
 * set changes when a host's note arrives, so anything that held a pointer into this module would
 * be reading a structure that moves under it while it draws. Both answer false while no round is
 * running, which is the case a caller has to have anyway: there is nothing to show. */
bool mp_round_board(mp_score_board_t *out);
bool mp_round_rules(mp_rules_t *out);

#endif /* MULTIPLAYER_MP_ROUND_H */
