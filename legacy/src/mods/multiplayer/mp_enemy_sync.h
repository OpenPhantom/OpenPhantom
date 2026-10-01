/* mp_enemy_sync.h: the enemies on the wire, host to client.
 *
 * Layer 3. It joins the binding, which can read and write an actor, to the record codec, which
 * knows what an enemy is worth sending. Neither of those knows about a packet and neither should.
 *
 * ==================================== One direction only ======================================
 *
 * The level belongs to the host, and an enemy belongs to the level. So the host describes and the
 * client applies, and a block arriving at a host is refused rather than applied.
 *
 * ================================= Identity, and why it is two bytes ===========================
 *
 * An actor is identified by its PLACEMENT INDEX, which the engine writes once when the actor is
 * spawned and never again, and by a GENERATION, which the engine does not have at all.
 *
 * The generation exists because a placement respawns. Index alone would have the receiver apply
 * the new life's position to the old life's mirror and vice versa, and the two would fight over
 * one body without anything saying so. The host counts a new generation whenever an index appears
 * that was not live in the previous substep, which needs no engine support: the walk already knows
 * the whole live set every substep.
 *
 * Both are a byte. That is measured rather than assumed: the largest of the shipped levels holds
 * 255 placements, so the largest index anywhere is 254 and a u8 has exactly one value of headroom.
 * An index past that is refused loudly rather than truncated, because a truncated index is another
 * enemy's index and the two would trade bodies.
 *
 * ================================= Why a presence bitmap ======================================
 *
 * The receiver has to know not only what changed but what is GONE. A block carries the whole live
 * index set as 256 bits, which is 32 bytes, and anything not in it is no longer alive on the host.
 * That is a thirtieth of the budget for an answer that is otherwise a second message type with its
 * own loss and ordering problems.
 *
 * The bitmap is absolute, not a delta, so a lost packet costs nothing: the next one is the truth
 * again. That is the same reason the block rides the UNRELIABLE payload rather than the reliable
 * channel. An enemy's position is superseded by the next one, and waiting for a lost packet to be
 * resent would hold every later one behind it. There is one unreliable payload per packet, so the
 * block shares it with the bodies and goes FIRST, behind a two byte length: the snapshot decoder
 * then still receives a buffer that begins with its own header and ends where its bytes end, and
 * did not have to change.
 *
 * ================================= Which level a block is about ===============================
 *
 * A placement index means something inside one level only, and the session survives a level load
 * on purpose. So the block says which level it describes, in the same two bytes the map's digest
 * uses, and a receiver in another level refuses it whole. That mattered little while a block could
 * only move bodies that already existed; it matters now that a block can CREATE them, because a
 * block from the level the host just left would otherwise create the wrong ones here.
 *
 * The bridge tells this module which level this machine is in, once per substep, from inside the
 * substep. With no level told, nothing is applied and nothing is built.
 *
 * ================================ The mirror follows the sender ================================
 *
 * A record is a delta against what the sender believes this side holds, and the sender advances
 * that belief whenever a payload goes out. The receiver's mirror therefore has to advance on every
 * record it decodes, whether or not there is a body here to put it on. The first version of this
 * file advanced it only on a successful write, so a placement with no local actor decoded every
 * later delta against nothing and read zero for every field that had not changed; a body that
 * turned up later inherited that.
 *
 * ================================== Spawning, and when ==========================================
 *
 * A placement the host lists and this machine has no actor for is remembered as WANTED, and the
 * bodies are created in one call from the substep, not where the block was decoded: the same
 * decode runs from the idle pump, which runs from the message loop and can find the engine in the
 * middle of a level load. Creating a body needs the engine's own spawner and a whole world.
 *
 * A placement the spawner will never create here, the one hosting the player or an index past the
 * table, is remembered as such until its generation changes, so that a refusal is counted once
 * and not once per block for the rest of the level.
 *
 * ===================================== The copies' part =======================================
 *
 * A copy an editor spawned is not a placement and has no index in the level: it travels under
 * the key 256 + k (mp_wire.h), which the placement bitmap cannot name. So a block carries a
 * second part behind the placements, and only while the host has a copy alive: the length of a
 * bitmap over k, the bitmap, a count, and per record k in two bytes, the generation and the
 * delta, whose index field holds k. With no copy alive the block is the one it was before, byte
 * for byte, and nothing follows the part when it is there.
 *
 * The part's head is taken out of the room BEFORE the placements are walked. A block that ran
 * out of room without it would read on the far side as "no copies at all".
 *
 * A receiver follows a copy's mirror and does nothing else with it: no wish, no write, no
 * letting go. The body is the editor's to build once the host has granted the copy, and a body
 * under the same key that this machine built on its own is not the host's copy.
 *
 * ================================== The world events' part ====================================
 *
 * Between the bitmap and the first placement record every block carries the world events for its
 * peer (mp_world_event_rule.h): a head of five bytes, which the block header counts, and the events
 * the head names. It is at a fixed place and always there, so the copies' part behind the records
 * stays what it was, the rest of the block. The events are taken out of the room before any record
 * is chosen, because a record that measured its room without them would not fit when written.
 */
#ifndef MULTIPLAYER_MP_ENEMY_SYNC_H
#define MULTIPLAYER_MP_ENEMY_SYNC_H

#include "mp_enemy_bind.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_wire.h"
#include "mp_npc_copies.h"
#include "mp_npc_copies_client.h"
#include "mp_wire.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many placements a level may hold before this refuses to describe it. The census says the
 * largest shipped level has 255, so this is exact rather than generous. */
#define MP_ENEMY_SYNC_MAX_PLACEMENTS 256u

/* Every enemy key this module keeps a row for: the placements and the copies (mp_wire.h). The
 * block's own bitmap still covers the placements alone. */
#define MP_ENEMY_SYNC_KEYS MP_WIRE_KEY_COUNT

/* The presence bitmap: one bit per placement. */
#define MP_ENEMY_SYNC_BITMAP_BYTES (MP_ENEMY_SYNC_MAX_PLACEMENTS / 8u)

/* Two bytes of identity in front of every record's delta. */
#define MP_ENEMY_SYNC_IDENTITY_BYTES 2u

/* Two bytes saying which level the block is about. */
#define MP_ENEMY_SYNC_LEVEL_BYTES 2u

/* Block header: the count, the level, the bitmap, then the world events' head. */
#define MP_ENEMY_SYNC_HEADER_BYTES                                                                \
    (1u + MP_ENEMY_SYNC_LEVEL_BYTES + MP_ENEMY_SYNC_BITMAP_BYTES + MP_WORLD_EVENT_HEAD_BYTES)

/* The copies' part: a bitmap of at most one bit per copy the wire names, and in front of each
 * record k in two bytes, low byte first, then the generation. */
#define MP_ENEMY_SYNC_COPY_BITMAP_BYTES   (MP_WIRE_COPY_MAX / 8u)
#define MP_ENEMY_SYNC_COPY_IDENTITY_BYTES 3u

/* How many peers a host keeps a view for: what each is believed to hold of every placement. A
 * record is a delta against what its receiver holds, and two receivers lose different packets,
 * so one belief for all of them is wrong for every receiver but the one it was built for. The
 * number is the peers a listen host seats, one per far bank; the bridge asserts that. */
#define MP_ENEMY_SYNC_VIEWS 3u

/* Forgets every mirror, every generation, every wish and the level. Called when a session begins
 * or ends, and when a level changes, because a placement index means a different enemy in a
 * different level. */
void mp_enemy_sync_reset(void);

/* How many resets there have been. A module that keeps actors by address and cannot be reset from
 * here compares it with the count its memory belongs to. */
uint32_t mp_enemy_sync_resets(void);

/* Switches the whole module off.
 *
 * The answer is the transport, never the game. Both games want one world: a deathmatch because
 * the arena emptied the map and the two sides must agree about what is left, a campaign because a
 * co-operative level is the level its authors wrote and two machines walking two copies of it is
 * not co-operation. Keying this on the game mode on 2026-09-07 left a co-operative session with no
 * enemy replication at all, and nothing flagged it: the last good field run, on 2026-09-06, sent
 * 92451 records and applied 6279 blocks, and every run after it read `sent 0 | applied 0`.
 *
 * It is off for the loopback, and that is not a limitation to be lifted later. The loopback is
 * one process being both sides, so the describing half and the applying half would run over THE
 * SAME actors: the census would run twice a substep, so nothing would ever look newly alive, no
 * respawn would be seen, and the applying half would park the actors the describing half is
 * reading. A UDP listen host is not affected, because there the two roles are two processes with
 * two pools. */
void mp_enemy_sync_set_enabled(bool enabled);

/* Which level this machine is in, or that none is open. Told by the bridge from inside a substep,
 * because that is the one moment the world is certainly whole. A change forgets every wish, since
 * a wish was about the level before. */
void mp_enemy_sync_set_level(bool known, uint16_t identity);

/* A host's substep of sending, in two parts. The census runs once: who is alive, which life each
 * placement is on, and what each is doing, read once out of the engine. False when the module is
 * off or no level is open here, and then no block is built this substep. */
bool mp_enemy_sync_begin_send(void);

/* Then one block per peer, against that peer's own view, every record whole while no
 * acknowledgement has confirmed that view. False when the census did not run this substep or the
 * arguments are unusable; a level with no live actor is NOT a failure and produces
 * a header with a count of zero and an empty bitmap, which is what tells a receiver to let go of
 * every replica it holds.
 *
 * The bitmap always names every live key. Which RECORDS go is the interest rule's answer for that
 * peer: the ones that may not wait first, then the ones that may not wait behind the rest, then by
 * accumulated priority, placements and copies in one ranking and each written into its own part.
 * A key left out keeps what it had accumulated, so nothing waits for as long as the ones ahead of
 * it keep moving.
 *
 * The view is NOT advanced here. A block that the packet layer refuses must not leave the
 * receiver believed to hold bytes it never got, so the caller commits separately once the payload
 * has been accepted. */
bool mp_enemy_sync_encode_for(size_t view, uint8_t *out, size_t capacity, size_t *bytes);

/* The payload carrying the last encode went out, on the substep `tick`. The view's baseline moves
 * over what that encode described, and the KEYS it carried are remembered under that substep so
 * that a payload the far side never names can be made good later.
 *
 * Moving the baseline here and stopping there is what this module did until 2026-09-20, and it is
 * why a client that missed one payload kept every enemy of that payload at the bottom of the
 * position range for the rest of the level: a field left out of a mask because the sender believed
 * it delivered was never offered again. Sending is not receiving. What closes that is not moving
 * the baseline later, it is NOTICING, which is what the acknowledgement below is for.
 *
 * A payload the ring would forget while no acknowledgement has named it or stepped over it gives
 * the view up whole instead of being stamped. What that measures is the LAG of the newest
 * acknowledgement behind the payload going out now, and two different things reach it: a far side
 * that has stopped acknowledging, which is what it does when it refuses every payload, and a line
 * whose acknowledgements arrive steadily but more than a ring late. Either way the record of what
 * that payload carried is about to be overwritten with nothing having answered it, and afterwards
 * nothing could tell whether it arrived.
 *
 * The acknowledgement side measures something else, the GAP one acknowledgement jumps over since
 * the last. The two are not the same condition and neither implies the other. */
void mp_enemy_sync_sent_for(size_t view, uint32_t tick);

/* The far side has decoded the payload of `tick`, which is the newest it holds. A substep the
 * acknowledgements step OVER that its bits do not name carried a payload that never arrived, and
 * the keys it described are opened again, so their next description is a full record.
 *
 * Quake 3 answers the same question by keeping every unacknowledged snapshot and delta-ing against
 * the one the client names. That is exact and costs a megabyte a peer here. Remembering only WHICH
 * keys each payload carried costs 48 bytes a substep and buys the same end state one round later,
 * which for a body that is redescribed thirty times a second is the right trade.
 *
 * All of that needs a first acknowledgement to count from. Until one names a payload this view
 * sent, the view is unconfirmed and every record goes out whole, because nothing says the far side
 * holds anything to read a delta against: a client that is still loading refuses every payload, and
 * the first one it takes would otherwise be a delta against payloads it threw away. The confirming
 * acknowledgement opens again every key that no payload from that one on carried.
 *
 * `bits` are the far side's word about the ticks before `tick`: bit k set says it holds the payload
 * of `tick - 1 - k` as well. A substep stepped over whose bit is set was decoded there and kept,
 * and its keys stay known; only a missing one opens its keys. The world events of every payload a
 * bit names are acknowledged with it. Nought is the word of a side that says nothing about them,
 * and then every substep stepped over is taken for lost, as before the bits. */
void mp_enemy_sync_acked_for(size_t view, uint32_t tick, uint32_t bits);

/* The host's line about those bits, beside the enemies' own. */
void mp_enemy_sync_report_acknowledgement_bits(void);

/* The other half of that pair: the payload did NOT go out, so forget what the encode described.
 * One of the two MUST follow every encode, or the next commit adopts a description that was never
 * sent and the view is wrong for that placement until the level ends. */
void mp_enemy_sync_abandon_for(size_t view);

/* A new connection holds nothing: its view starts from nothing, and its blocks describe every
 * placement whole until an acknowledgement names one of them. */
void mp_enemy_sync_forget_view(size_t view);

/* Lets go of every replica this side holds. Called when a session ends: a parked actor is stepped
 * over before its tick, so one left parked with no wire behind it stands still for good. A copy's
 * replica is not let go but given up in the client's table, whose overlay removes it. */
void mp_enemy_sync_release_all(void);

/* Whether this side describes its enemies: the census ran in the last substep with a level open.
 * A host in a session, and nobody else. */
bool mp_enemy_sync_describing(void);

/* Whether a block of the level this side is in has been taken here since the last reset. */
bool mp_enemy_sync_block_applied(void);

/* Whether this side let `actor` go lately, out of the last few it handed back to its own
 * simulation. For a line only: a slot is reused, so an address is no proof. */
bool mp_enemy_sync_let_go_before(uintptr_t actor);

/* The NPC copies' tables, handed over by the copies' module for a session and taken back (NULL)
 * after it. The host's says which life each copy's key is and whether a block may describe it; a
 * client's which replica stands for it. Without them a copy's row counts its lives by the census
 * like a placement's and nothing is applied to it. */
void mp_enemy_sync_set_copies(const mp_npc_copies_t *host, mp_npc_copies_client_t *client);

/* Where the interest rule (mp_enemy_interest_rule.h) reads the world. `viewer` says where one
 * view's player stands, `subject` what one live key is doing, and `report` adds the lines of the
 * module that answers both. A callback left NULL, or one that answers false, leaves the key or the
 * view unmeasured, and an unmeasured key is ranked in the middle class: with equal priorities that
 * is the round robin this block had before, which is also what a unit test with no engine gets.
 * `view_of_slot` names the view whose player holds a world slot, by the same slot rule the session
 * addresses a peer by; left NULL, a hit on a player is not fed to the rule. */
typedef struct mp_enemy_sync_interest {
    bool (*viewer)(size_t view, mp_enemy_viewer_t *out);
    bool (*subject)(size_t key, uintptr_t actor, mp_enemy_subject_t *out);
    void (*report)(void);
    bool (*view_of_slot)(uint8_t slot, size_t *view);
} mp_enemy_sync_interest_t;

/* Handed over when a session is armed, NULL to take it back. Kept across resets. */
void mp_enemy_sync_set_interest(const mp_enemy_sync_interest_t *interest);

/* The bytes the records view `view` must carry this substep take in its block, their identity
 * included: a change of what a watcher sees against what that view was sent, a new life of a key
 * it had described before. The block writes exactly these first, so
 * a room of mp_enemy_sync_frame_bytes and this answer holds every one of them. The block asks it
 * too, and the report counts a block whose records that may not wait took other bytes.
 *
 * A pure question: it moves no accumulator, no age and no mirror, and asked twice it answers
 * twice the same. 0 when the census has not run this substep. It is meant to be asked before the
 * block is built, by whoever divides the payload between the enemies and the messages. */
size_t mp_enemy_sync_must_bytes(size_t view);

/* The bytes view `view`'s block takes besides its records: the header with the bitmap and the
 * world events' head, the events for that view, and while a copy is alive the copies' head. With
 * mp_enemy_sync_must_bytes it is the least room a block needs this substep to carry every event
 * and every record that may not wait, which is what a floor kept under the enemies has to hold.
 * Valid after the census, like that one, and the block sizes its own room with it. */
size_t mp_enemy_sync_frame_bytes(size_t view);

/* What a world event asks of one view on a host after the census. Whether the view holds the life
 * `life` of `key`, or would be told of it: every key but a far one the peer has never been told of,
 * and for a key the census no longer reads, only the life the peer holds. And where the view's
 * player stands, false when there is nothing to read. */
bool mp_enemy_sync_view_concerns(size_t view, uint32_t key, uint8_t life);
bool mp_enemy_sync_view_position(size_t view, float out[3]);

/* Whether a player standing at `position` would have woken the enemy under `key`, as the last
 * census read it: the interest rule's near class, measured the way the blocks are ranked. False
 * with no census or no reading of the key. Pure like the two above. */
bool mp_enemy_sync_wakes_for(uint32_t key, const float position[3]);

/* The floor the enemy block of view `view` keeps under the messages this substep: what its head and
 * its records that may not wait take, mp_enemy_sync_frame_bytes and mp_enemy_sync_must_bytes, and
 * never less than the budget's fixed MP_BUDGET_ENEMY_FLOOR_BYTES. Asked by whoever divides the
 * payload, after the census and before the block, and pure like the two it adds up; the block works
 * out the same number for its report. Before a census it is the fixed floor. */
size_t mp_enemy_sync_floor_bytes(size_t view);

/* The enemy under `key` hit the far player of world slot `slot`, and the host has addressed that
 * hit to the slot's peer. For the next second the interest rule ranks the enemy for that peer's
 * view as going for the player, whatever its target says, and no other view is touched. Called by
 * the hit relay right after the addressed send. A key with no row, a hit with no attacker among
 * them, and a slot no view holds are ignored. */
void mp_enemy_sync_note_struck(uint32_t key, uint8_t slot);

/* Applies a block on a client, from the payload of substep `tick`. False when the block is
 * malformed, about another level, or arrives with no level open here, or when it holds a record
 * that names no position and has nothing here to be read against, in which case nothing at all
 * is applied: a half applied set of enemies is worse than none, because the half that landed
 * cannot be told from the half that did not.
 *
 * A caller that gets false must refuse the WHOLE PAYLOAD, acknowledgement included. This is the
 * other half of the acknowledged baseline and it is not a nicety: a client that threw a block away
 * and still acknowledged the payload carrying it left the host believing it held those bytes, and
 * every field the host then left out of a mask was gone for the rest of the level. A stationary
 * enemy's position never changes, so it was never offered again, and the client drew every one of
 * them at the bottom of the position range, outside the level, where nobody saw them.
 *
 * `tick` also orders the stream: a block older than one already taken is refused, because the
 * channel accepts a reordered packet and an old block would write stale fields over fresh ones.
 *
 * May run outside a substep. It parks and writes bodies that exist and remembers the ones that do
 * not; it creates nothing. */
bool mp_enemy_sync_apply(const uint8_t *block, size_t bytes, uint32_t tick);

/* Whether a block is worth handing to this module at all. A caller uses it to tell "this side
 * takes no enemies" from "this side refused this block": only the second may refuse a payload, and
 * a module that is off would otherwise refuse every payload for ever and the far side would never
 * hear an acknowledgement again. */
bool mp_enemy_sync_is_enabled(void);

/* Write what the wire said into the bodies, once, at the start of a substep. The decode may
 * run from the idle pump and mostly does; the WRITE may not, because the engine interpolates
 * a body between the pose it had and the pose it has and a pair replaced mid-frame is a
 * visible jump backwards. Returns how many bodies were written. Inside a substep only. */
uint32_t mp_enemy_sync_flush(void);

/* Creates the bodies the last blocks named and this machine has not got, through the engine's
 * spawner, and applies to each what the host last said about it. Inside a substep only. Returns how
 * many were created. */
uint32_t mp_enemy_sync_spawn_pending(void);

/* How many placements wait for a body right now. */
uint32_t mp_enemy_sync_pending(void);

/* The live actor a placement index names on THIS machine, or zero. A wire index is another
 * machine's name for an enemy; this is how it becomes a body here. */
uintptr_t mp_enemy_sync_actor_for(uint32_t key);

/* On a receiver, the local actor that stands for what the host names under `key`, or zero. A
 * placement's is the census's actor, because both machines built it from the same record. A
 * copy's is only the replica this machine's overlay built for the host's grant of the life the
 * last block named: an actor under the same key that this machine raised on its own is not the
 * host's copy, and a removal or a hit the host names must not reach it. */
uintptr_t mp_enemy_sync_replica_for(uint32_t key);

/* What the host last said about a placement, whether or not a body exists here. False when nothing
 * has been decoded for it since the last reset or generation change. */
bool mp_enemy_sync_mirror(uint32_t key, mp_enemy_record_t *out);

/* Which life of a placement this side is on: the host's count on a host, the last decoded one on
 * a client. False before anything has been said about it. A removal message carries it so that a
 * removal of the old life cannot reach the new one. */
bool mp_enemy_sync_generation(uint32_t key, uint8_t *out);

/* The actor for a placement is gone, by this side's own hand: the pointer is forgotten at once
 * rather than at the next census, because between the two the slot may already hold somebody
 * else. The mirror goes with it only while the table still holds `generation`, the life the
 * removal was for. */
void mp_enemy_sync_forget(uint32_t key, uint8_t generation);

/* On a receiver, what the slot of `actor` holds for placement `key` in the life `generation`:
 * the binding's answer, with a kept corpse counted as that life's actor only when this side
 * kept it for that very life. The one question behind every write, every let go and every
 * removal performed here. */
mp_enemy_slot_t mp_enemy_sync_slot(uint32_t key, uintptr_t actor, uint8_t generation);

/* What this side remembers after it performed a removal the host sent, on `actor` for the life
 * `generation`, placement or copy alike: for a removal that keeps the body, that the corpse is
 * that life's, so it goes on being written; for any other, nothing of the body. */
void mp_enemy_sync_performed(uint32_t key, uintptr_t actor, uint8_t generation, uint8_t reason);

/* One line for the report. */
void mp_enemy_sync_report(void);

/* And one for the replicas handed back, split into a reset or a session end and the host no
 * longer listing them, the second with how many the host's last word called dead. */
void mp_enemy_sync_report_let_go(void);

/* Two measurements of a host's substep, after its census and after every peer's block: how many
 * actors the census found alive, and for the traced placement the class each peer's view gave it
 * (mp_cadence.h). Both only read the table. */
uint32_t mp_enemy_sync_census_actors(void);
void     mp_enemy_sync_trace_host(void);

#endif /* MULTIPLAYER_MP_ENEMY_SYNC_H */
