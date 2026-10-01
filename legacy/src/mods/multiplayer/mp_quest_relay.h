/* mp_quest_relay.h: the shared story bits between the two machines.
 *
 * Layer 3. The codec and the bit arithmetic are mp_quest, which has no engine in it; the bank is
 * mp_scratch_bind, which owns the cell. What is here is the AUTHORITY and the cadence.
 *
 * ==================================== The rule, in three lines ================================
 *
 *   the host repeats all thirty-four bits, absolute, and that is the story;
 *   a client writes them over its own and keeps no opinion of its own;
 *   a client that sees one of its bits move CLAIMS the change, and the host decides.
 *
 * =============================== Why a client may claim at all ================================
 *
 * It looks redundant, because the level belongs to the host and the scripts that set story bits
 * run there. Two things still move a bit on a client and neither is reachable from the host:
 *
 *   * an actor the host has not activated runs its own script HERE. The presence bitmap only parks
 *     what the host holds; a placement the host's scan has not woken is this machine's own, and
 *     its script sets its own flags;
 *   * the locked button CONSUMES a key (`0x0044C9A0` forms `bit = key + 0x4a`, tests it and clears
 *     it). That runs on the machine whose player is standing at the door, and in co-op that is as
 *     often the client as the host.
 *
 * Without the claim the first case quietly diverges and the second spends a key on one machine
 * only, so the door is open for one player and locked for the other.
 *
 * ================================== Why the claim is optimistic ===============================
 *
 * A client that claimed a bit and then went on writing the host's older truth over it would see
 * the bit flicker for a round trip and would re-send the same claim every substep until the echo
 * came back. So a claim that the channel ACCEPTED also moves this side's copy of the truth at
 * once. If the host disagrees, its next repeat says so and the bit goes back, which is the host
 * deciding rather than this side guessing.
 *
 * Only when the send was accepted. The claim rides the reliable channel, so an accepted one
 * arrives; a REFUSED one (a full channel) must not move the local copy, or the change would be
 * believed here and never told to anybody.
 *
 * SIZE NOTE: under 250 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_QUEST_RELAY_H
#define MULTIPLAYER_MP_QUEST_RELAY_H

#include "mp_quest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How often the host repeats its truth when nothing has changed. The same cadence as the roster,
 * the setup and the pickup list: a second, because this is not latency sensitive and a joiner has
 * to be told the whole story at a moment nobody chose. A CHANGE is sent at once. */
#define MP_QUEST_REPEAT_SUBSTEPS 32u

/* How many claims one substep may send. A client that has just had its bank rewritten wholesale,
 * by a level load or a savegame restore, can differ in every one of the thirty-four bits at
 * once, and pushing thirty-four reliable messages into the channel in one substep is exactly
 * the starvation that cost the setup note its start bit when the savegame transfer flooded the
 * control channel. Four a substep clears the worst case in nine substeps, which is under a
 * third of a second. */
#define MP_QUEST_CLAIMS_PER_SUBSTEP 4u

typedef bool (*mp_quest_relay_send_fn_t)(const uint8_t *bytes, size_t count);

/* Needs the scratchpad binding, because the bank cell lives there. False when that is missing, and
 * then this module does nothing at all rather than half of it. */
bool mp_quest_relay_install(void);

/* Which side this is. A host describes and decides; a client applies and proposes. */
void mp_quest_relay_set_host(bool host);

/* Once per substep, from inside the substep, on both roles. */
void mp_quest_relay_tick(uint32_t substep, mp_quest_relay_send_fn_t send);

/* A note off the reliable channel. True when it was addressed to this module, whatever became of
 * it, which is the recogniser contract every other relay here follows. */
bool mp_quest_relay_take_message(const uint8_t *note, size_t bytes);

void mp_quest_relay_report(void);

#endif /* MULTIPLAYER_MP_QUEST_RELAY_H */
