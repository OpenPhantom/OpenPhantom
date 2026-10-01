/* mp_bridge_world.h: the world's two ends on the bridge.
 *
 * Everything on the bridge that touches a snapshot lives here: the engine's bodies read out of the
 * banks and their objects into wire bodies, the host's world built, stored and delta encoded
 * against the baseline the client acknowledged, the received world decoded against the baseline
 * this side holds and stored, and the loopback's check of the decoded world against the built one
 * at the same tick. The bridge keeps the sessions, the modes, the pump and the puppet; it hands a
 * session in and takes a decoded snapshot out.
 *
 * The acknowledgement rides the client's unreliable payload, eight bytes in front of whatever the
 * client sends, snapshot or command stream: every packet carries the newest host tick the client
 * holds and a bit for each of the ticks before it that it holds too (mp_payload_prefix), so a lost
 * packet costs nothing and the reliable channel carries events alone.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_WORLD_H
#define MULTIPLAYER_MP_BRIDGE_WORLD_H

#include "mp_payload_prefix.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One body slot out of a hero block shaped range of bytes: the reader hands in a function that
 * can copy from its source, so the same builder serves the live block and the bank, and a unit
 * test can feed it a block of its own. False when a field the body cannot do without did not
 * read. What the block does not hold, the playheads and the live bits, comes from the object. */
typedef bool (*mp_bridge_world_reader_t)(size_t offset, void *out, size_t size);
bool mp_bridge_world_build_body(mp_wire_body_t *body, mp_bridge_world_reader_t read_bytes);

/* The animation state only the live object holds: the clip on each channel as the object plays
 * it, the playhead of each track, whether each channel is live, and whether the base clip's last
 * change was a crossfade. The fade note is consumed by the read, so each fade is reported once.
 * An object handle of zero, or one that does not read, leaves the playheads at zero and the
 * mask clear and answers false; the body still crosses without them. */
bool mp_bridge_world_read_object(mp_wire_body_t *body, uint32_t object);

/* Everything that indexes the far side's ticks: both histories and the acknowledged baseline. Run
 * on every arrival of a peer, because a restarted peer counts from tick one and holds no
 * history. */
void mp_bridge_world_reset(void);

/* ============================ The local player's appearance ==================================
 *
 * Sampled where the hero block is read anyway, once per substep, in both roles: the host and the
 * client build their own body through the same path. What is sampled is the NAME of the mounted
 * actor, out of the asset's own header rather than out of the block, because the block holds only
 * a pointer and the hero index it does hold has four values while every added character rides one
 * of them. A name names a file and means the same on two machines.
 *
 * This end only NOTICES a change. Putting one on the wire belongs to whoever owns the event, and
 * a change is consumed by the first caller that takes it, so exactly one sender sees each. The
 * first sample of a session counts as a change: until the far side has been told once, two
 * players who never switch never learn what the other is wearing. */
bool mp_bridge_world_take_local_asset_change(char *out, size_t bytes);

/* What the local player wears right now, lower case and cut at the first zero byte, or "" before
 * anything has been sampled. For a report and for a handshake; it consumes nothing. */
const char *mp_bridge_world_local_asset(void);

/* Substeps in which this side's own body was read off its model rather than off the block,
 * because a script had the player module stopped for a scene. */
uint32_t mp_bridge_world_bodies_off_the_model(void);

/* In which module state, 0 to 4, the stopped module was seen on each substep. Dying and
 * respawning are sent off the block and not the model, so those two are the samples taken off
 * the block; the split is what tells a parked module from an ordinary death and respawn. */
uint32_t mp_bridge_world_off_the_model_in(uint32_t state);

/* The host's half, after the tick: this tick's snapshot (slot 0 the player live, with its health
 * out of the status record; every far player in the slot its bank shows, as that player last
 * sent it), stored, and set as the unreliable payload of every connected peer, each delta
 * encoded against the baseline that peer acknowledged, each without that peer's own body, and
 * each with the enemies as that peer is believed to hold them. */
void mp_bridge_world_send(mp_session_t *session, uint32_t tick);

/* The host learns which baseline a client really holds, out of that client's payload prefix, and
 * forgets it when a new connection takes the peer's index: a newcomer holds nothing. The newest
 * tick is the delta's baseline, as in Quake 3; the bits go to the enemies and their world events,
 * which open only what really did not arrive. */
void mp_bridge_world_acknowledged(size_t peer, const mp_payload_ack_t *ack);
void mp_bridge_world_forget_peer(size_t peer);

/* The client's payload prefix, built here out of the history this side keeps of the host's world
 * and nowhere else, so the loopback, the client and every test say what the history holds rather
 * than what a caller thinks it holds. MP_PAYLOAD_ACK_BYTES long. */
bool mp_bridge_world_put_ack(uint8_t *buffer, size_t capacity);

/* The client's half: its OWN body, slot `slot` of a full one-body snapshot stamped with `tick`,
 * behind the acknowledgement prefix, set as the unreliable payload. The tick is the bridge's
 * substep, the same one the substep's events carry. */
void mp_bridge_world_send_own(mp_session_t *session, size_t slot, uint32_t tick);

/* What one read of a session's payload ring answers. Consumed and decoded are told apart on
 * purpose: a caller drains the ring by reading until nothing is there, and a payload that was
 * consumed and refused ends nothing, because the payloads behind it are newer state that would
 * otherwise wait a substep in the ring and be pushed out by the next arrivals. */
typedef enum mp_bridge_world_read {
    MP_BRIDGE_WORLD_NOTHING,   /* the ring was empty */
    MP_BRIDGE_WORLD_REFUSED,   /* a payload was consumed and did not decode; counted */
    MP_BRIDGE_WORLD_DECODED    /* a payload was consumed and `out` holds it */
} mp_bridge_world_read_t;

/* The host's read of one client's own state, out of the ring of peer `peer`: the prefix into
 * `ack`, then a FULL snapshot only, decoded against nothing and stored nowhere, because the
 * client's stream carries no delta. Decoded with `out` filled when a world carrying a body in
 * `slot` was decoded this call; anything else that was read is a refusal. One payload per call. */
mp_bridge_world_read_t mp_bridge_world_receive_full(mp_session_t *session, size_t peer,
                                                    size_t slot, mp_snapshot_t *out,
                                                    mp_payload_ack_t *ack);

/* The client's read of the host's world: decoded against the baseline this side holds and
 * stored. Decoded with `out` filled when a world was decoded this call; a refusal is the codec
 * keeping itself honest over loss and is counted. One payload per call. */
mp_bridge_world_read_t mp_bridge_world_receive(mp_session_t *session, mp_snapshot_t *out);

/* The world the host end built and stored at `tick`, while its history still holds it; NULL
 * after. What the loopback's check below holds a decoded world against. */
const mp_snapshot_t *mp_bridge_world_built(uint32_t tick);

/* The loopback's check, in mp_bridge_world_check.c: the decoded world against the host's stored
 * world at the SAME tick, to the wire's own quantisation; the first fault is logged, later ones
 * counted. The counts are the world line's in the report. */
void mp_bridge_world_verify(const mp_snapshot_t *decoded);
void mp_bridge_world_check_counts(uint32_t *checks, uint32_t *faults, float *worst_error);

/* The report lines of this end: snapshots sent and decoded, refusals, checks and their worst
 * error, and the newest world the client holds against the host's tick. */
void mp_bridge_world_report(uint32_t host_tick);

#endif /* MULTIPLAYER_MP_BRIDGE_WORLD_H */
