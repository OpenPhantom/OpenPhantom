/* mp_scratch_wire.h: the world scratchpad on the reliable channel.
 *
 * Layer 3. It joins the pure delta logic to the engine binding and decides WHEN something goes
 * out; neither of the other two knows about a channel and neither should.
 *
 * ===================================== Who sends what =========================================
 *
 * The level belongs to the host, so only the host runs the scripts and only the host sets a
 * campaign bit. This is therefore one directional: the host describes, the client applies. A
 * client never sends here, and a message arriving at a host is refused rather than applied, which
 * is what keeps a mistaken mode from letting a client rewrite everybody's campaign.
 *
 * ================================ The generation, and why it exists ============================
 *
 * Six module messages move a whole bank at once: a level opening, beginning, ending or
 * restarting, a savegame being restored, and the two new game entries. A delta encoded before one
 * of those and delivered after it describes a bank that no longer exists.
 *
 * The reliable channel guarantees delivery, not that delivery happens before the next thing the
 * engine does. So every message carries the sender's transition count and a receiver applies only
 * what matches its own. Both machines load the same levels and therefore see the same messages;
 * when the counts disagree the two are looking at different worlds, and refusing is the only
 * honest answer. It is also a loud one, which is the point: silently applying to the wrong bank
 * shows up an hour later as a door that will not open.
 *
 * ==================================== What the rate is ========================================
 *
 * The bank is checked every eighth substep, four times a second. Campaign progress is not a thing
 * a player can see arriving late, and an unchanged bank costs a memcmp and no packet at all. A
 * transition puts the cursor back to the start, so the sweep that follows resends everything.
 */
#ifndef MULTIPLAYER_MP_SCRATCH_WIRE_H
#define MULTIPLAYER_MP_SCRATCH_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The tags. The reliable channel tells its message kinds apart by tag and length together, so
 * these have to stay clear of the event kinds and of the map's digest. */
#define MP_SCRATCH_TAG_BANK 0x8Bu
#define MP_SCRATCH_TAG_AI   0x8Cu

/* Tag, generation, then the delta the logic layer produced. */
#define MP_SCRATCH_WIRE_HEADER 5u

/* Tag, generation, then the twelve blackboard dwords as the logic layer writes them. */
#define MP_SCRATCH_WIRE_AI_BYTES (MP_SCRATCH_WIRE_HEADER + 48u)

/* How often the bank is looked at, in substeps. */
#define MP_SCRATCH_WIRE_PERIOD 8u

typedef bool (*mp_scratch_wire_send_fn_t)(const uint8_t *bytes, size_t count);

/* Forgets everything believed about the far side. Called when a session begins or ends, so a
 * second session never inherits the first one's mirror. */
void mp_scratch_wire_reset(void);

/* Which side this machine is. A host describes; anything else applies. */
void mp_scratch_wire_set_host(bool host);

/* HOST: a player has entered this host's world. The next sweep carries every byte of the bank, the
 * zeros included, and the whole blackboard, under a raised generation, to every player; a player
 * who arrives in a world mid game holds its own level begin's bank, not the host's. Nothing on a
 * client or with no bank to read. */
void mp_scratch_wire_note_arrival(void);

/* One substep's worth of work for the sending side. Does nothing at all on a client, and nothing
 * on a host outside the period unless a transition has just raised a resync. */
void mp_scratch_wire_tick(uint32_t substep, mp_scratch_wire_send_fn_t send);

/* Takes a reliable message if it is one of ours, and says so. A message that is ours but cannot
 * be applied is still taken: it belongs to nobody else, and passing it on would have the event
 * decoder try to read a campaign delta as a body action. */
bool mp_scratch_wire_take_message(const uint8_t *note, size_t bytes);

/* One line for the report: what has actually crossed, and what was refused and why. */
void mp_scratch_wire_report(void);

#endif /* MULTIPLAYER_MP_SCRATCH_WIRE_H */
