/* mp_bridge_savefile.h: the host's savegame carried to its clients, and restored there.
 *
 * Layer 3. The codec is mp_savefile; what is here is the two ends of the transfer on the bridge:
 * the host holds the file the lobby chose and answers requests for it, the client asks for the
 * file the host's setup note names, puts it together and writes it under MP_SAVES_JOIN_PATH, and
 * the lobby screen then starts from that file through the same path the host started from.
 *
 * ================================== Why the client has to ask ================================
 *
 * The host never pushes. A client that arrived after the host chose, a client that missed the
 * choice, a client that lost its assembly to a host choosing something else, and a client that
 * assembled a file which did not hash to its name are one case each for a pusher and one case
 * altogether for an asker: the client says what it holds and the host sends the rest.
 *
 * What it says is a bitmask of every slice it has, ten times a second, and the whole mask every
 * time. That one sentence is the request, the acknowledgement and the repair together, so the
 * four cases above need no state on the host that tells them apart, and a mask that is lost costs
 * a tenth of a second rather than a slice. The setup note is repeated once a second, so the
 * comparison that starts all of it can be made on any repeat.
 *
 * A transfer is finished when the mask is full, and only then. The older shape called it
 * finished when the host's own cursor ran off the end, which is how a field run reported one
 * completed transfer while the client sat at 89 per cent with eight slices it never got. The
 * receiver is the only side that can answer that question.
 *
 * ================================= What the start does with it ===============================
 *
 * A start bit that arrives with MP_LOBBY_F_FROM_SAVE and a named file is HELD by the lobby screen
 * until the file is on disk, and the level is then entered through mp_start_save with the received
 * file, which is the path the host's own START takes: the title menu steered onto LOAD GAME and
 * the shipped load screen's detour restoring the file. Nothing about the restore is this module's;
 * it only makes the file exist on the second machine. A start that waits past
 * MP_BRIDGE_SAVEFILE_WAIT_MS does NOT fall back to the fresh level. It says so on the lobby
 * band and goes on waiting, because a client that begins the level fresh is a second
 * campaign wearing the first one's face: its doors are shut, its pickups are back, its story
 * flags are whatever a new level starts with, and nothing on screen says any of it. Waiting
 * is also productive rather than merely hopeful, the client re-asks for the chunks it is
 * missing, so a transfer that stalled resumes on its own.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_SAVEFILE_H
#define MULTIPLAYER_MP_BRIDGE_SAVEFILE_H

#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many slices go out per tick, to one peer and to all of them together. Named here rather than
 * in the .c because the test that proves the second number holds has to name it, and because the
 * two are the whole pacing of the transfer: there is no rate estimator behind them.
 *
 * The per peer number is deliberately small and fixed, and it is NOT a bandwidth: the tick is the
 * bridge's idle pump, timer and frame hook together, whose rate nothing in the tree states and
 * which the report therefore measures ("the transfer ticked N times over M ms"). The counter of
 * ticks at this budget says whether it was ever the binding limit; the rate itself is held by
 * the second's budget below.
 *
 * The total over all peers exists because a mask is an amplifier: thirty six bytes in, up to four
 * slices out, and fifteen peer slots multiply that. So the lane is capped per tick, whatever the
 * peer count, and the cap is sized against what a tick may put on the wire anyway. One slice
 * datagram is the envelope plus a chunk, 1052 bytes; the game's own connected packets are at most
 * 1200 bytes to each of fifteen peers, 18000 bytes a tick. Eight slices are 8416 bytes, under
 * half of that, so the lane's share of a tick never exceeds the game's own however many ask; and
 * eight is twice the per peer budget, so two transfers run at full pace side by side and a third
 * shares, which the round robin over peers is for. The .c asserts the byte arithmetic. */
#define MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK  4u
#define MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK (2u * MP_BRIDGE_SAVEFILE_PEER_SLICES_PER_TICK)

/* How many slices the lane lays in a second over all peers, whatever the rate of the pump that
 * ticks it. The per tick total above stays the most one tick may lay, so a tick never lays more
 * than eight and a second never more than this. It is the total at thirty two ticks a second,
 * the rate the per tick numbers were once explained by and which the idle pump does not keep: at
 * a hundred and forty four frames a second the per tick budget alone let the lane lay over a
 * thousand slices a second. At 1052 bytes a slice this is about 270 KB a second, a savegame of
 * eighty slices in a third of a second to one client. */
#define MP_BRIDGE_SAVEFILE_SLICES_PER_SECOND (32u * MP_BRIDGE_SAVEFILE_TOTAL_SLICES_PER_TICK)

/* How long a client holds a start quietly before it says on the band that the file is slow. A save
 * is under eighty slices and the lane carries four of them a tick, so a transfer still running
 * after this is meeting something, and the report then says which slices, by index.
 *
 * It is a threshold for saying so and no longer a deadline: nothing happens when it passes but a
 * changed sentence and one line in the log. The wait itself has no end, because every end anybody
 * has proposed for it is worse than waiting, see this file's head. */
#define MP_BRIDGE_SAVEFILE_WAIT_MS 20000u

/* MP_BRIDGE_SAVEFILE_SILENCE_MS IS GONE with the shape it belonged to. It said how long a
 * client waited after the last chunk before asking again, and it had to exist because a repeated
 * request REWOUND the host's cursor: everything behind the first missing chunk was sent a second
 * time, so asking often was expensive and asking rarely was slow. A mask costs nothing to repeat
 * and names exactly what is missing, so there is no silence to wait out and no number to tune;
 * see BULK_ACK_MS in the .c, which is a rate rather than a threshold. */

/* The bridge tells this module which sessions it stands on and which side it is. */
void mp_bridge_savefile_bind(mp_session_t *host, mp_session_t *client, bool is_client);

/* HOST: the lobby chose this savegame. Reads it, names it, and answers the two values the setup
 * note carries. False, with both zero, when the file cannot be read or is not a size the transfer
 * carries; the session then begins from the level alone, as before. */
bool mp_bridge_savefile_offer(const char *file, uint32_t *save_id, uint32_t *save_bytes);
/* HOST: the lobby chose a fresh level instead. Nothing is answered any more. */
void mp_bridge_savefile_withdraw(void);

/* Both: the reliable channel's arm, kept only as a recogniser. Nothing of the transfer rides
 * that channel any more; a request, a chunk or a mask arriving there is a note from a build of
 * the old shape, and it is claimed and counted so no other module reads it. */
bool mp_bridge_savefile_take_note(size_t peer_index, const uint8_t *note, size_t bytes);

/* Both: from the lobby tick, which runs in a lobby and inside a level alike. It empties the bulk
 * lane first. A host then sends every peer that has asked the slices its mask does not name, a
 * few per tick; a client compares the setup's file against what it holds, says what it has,
 * and writes the file once it is whole. */
void mp_bridge_savefile_tick(uint32_t now_ms);

/* CLIENT: whether the file the setup names is on disk under mp_bridge_savefile_path, complete
 * and verified. What the lobby screen asks before it acts on a start. */
bool mp_bridge_savefile_ready(uint32_t save_id, uint32_t save_bytes);
const char *mp_bridge_savefile_path(void);

/* CLIENT: how much of the file the setup names is in, 0..100, for the screen's line. */
uint32_t mp_bridge_savefile_percent(void);

void mp_bridge_savefile_report(void);

#endif /* MULTIPLAYER_MP_BRIDGE_SAVEFILE_H */
