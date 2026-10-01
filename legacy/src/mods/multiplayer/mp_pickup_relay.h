/* mp_pickup_relay.h: a client claims a pickup, the host grants it, and the effect lands on the
 * claimant alone.
 *
 * Layer 2. One detour, on the player's own pickup routine.
 *
 * ================================ What a pickup is, and how it goes ============================
 *
 * A pickup is not a special object. It is an ENMY placement whose class falls in a band, and when
 * the player's cylinder overlaps its body the player's contact handler calls `Plr_PickUp` with the
 * class and the body. That routine tests bit 3 of the body's flags (already taken), applies the
 * effect to THE LOCAL PLAYER'S status record, sets bit 3, and sends the pickup's own task message
 * 1, whose handler marks the actor for removal. The removal next substep is reason 1: the
 * placement is buried for good.
 *
 * ================================== Why a client cannot just take one ==========================
 *
 * Under the ownership model the pickup is the host's actor, and on a client it is parked. A client
 * running the routine unchanged would give itself the health, set bit 3 on ITS body, and send the
 * message to a parked actor, whose handler turns back at its first gate; the body would stand
 * there taken and untouchable while the host still had it untaken, and the host's player could
 * take it a second time. Two players, one medipack, twice.
 *
 * ==================================== The claim and the grant ====================================
 *
 * On a client the detour takes NOTHING for a pickup the host owns. It sends a claim naming the
 * level, the placement, its generation and the class, and returns. The host checks that the actor
 * is alive, of that generation, and untaken, sets bit 3 on its own body, has the engine's own
 * contact handler mark the actor for removal exactly as its own player's pickup would, and sends
 * the same message back as the grant. The removal that follows travels through the enemy relay
 * like every other, so the body disappears on both machines.
 *
 * The client applies the effect on receiving the grant, by running the very routine it refused to
 * run before, on the same body. The effect therefore lands in the claimant's own status record on
 * the claimant's own machine, which is where its health lives under the ownership model; the host
 * player gets nothing, and a second claimant finds bit 3 set and gets nothing either.
 *
 * Two classes inside the band are refused by the codec: the retail routine reaches an
 * uninitialised stack slot for them, no shipped level places one, and a claim off the wire is the
 * one way that defect could be reached. The host uses ITS body's class for the grant, never the
 * claim's.
 *
 * A player standing on a pickup touches it every substep, so a claim is repeated no more than once
 * a second per placement until the grant or the removal arrives.
 *
 * The grant names the claimant's world slot, and a client applies only a grant naming its own.
 * The host sends it to everybody, and with two clients both used to apply it: two players, one
 * medipack, twice, which is the defect this module exists to stop. The slot is the host's word,
 * the slot of the peer the claim came from, whatever the claim itself says.
 *
 * In a deathmatch the grant asks one thing more: that the claimant's body stands within reach of
 * the pickup on the host. A claim from across the level is one nobody made by walking over it.
 */
#ifndef MULTIPLAYER_MP_PICKUP_RELAY_H
#define MULTIPLAYER_MP_PICKUP_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*mp_pickup_relay_send_fn_t)(const uint8_t *bytes, size_t count);

/* The host's contact replay: the far body of bank `bank`, the claimant's, as the sender, the actor
 * a placement names as the receiver, and a contact code. The hit relay has exactly this, and it
 * is handed in rather than linked so that this file carries no bank of its own. */
typedef bool (*mp_pickup_relay_perform_fn_t)(size_t bank, uint32_t key, uint8_t code);

/* Where the far body of bank `bank` stands on this machine. The bridge has it; handed in for the
 * same reason as the replay above. */
typedef bool (*mp_pickup_relay_far_body_fn_t)(size_t bank, float out[3]);

/* How near a claimant's body on the host has to stand to a pickup for a deathmatch to grant it,
 * in world units. The player runs at three and a half a second, and its body on the host stands
 * where it was a moment before the claim left: five is over a second of running and short of a
 * room. A claim refused for it is repeated while the player stands on the pickup, and the next
 * one finds the body arrived. */
#define MP_PICKUP_RELAY_REACH 5.0f

/* How many substeps a claim for one placement waits before it may be repeated. The player stands
 * on the pickup, the cylinders overlap every substep and the routine is entered every substep
 * until the body is gone, so a claim per touch would be thirty two a second per placement; one a
 * second per placement and generation is enough for a round trip and a removal. */
#define MP_PICKUP_RELAY_CLAIM_INTERVAL 32u

bool mp_pickup_relay_install(void);
bool mp_pickup_relay_installed(void);

void mp_pickup_relay_set_host(bool host);
void mp_pickup_relay_set_send(mp_pickup_relay_send_fn_t send);
void mp_pickup_relay_set_perform(mp_pickup_relay_perform_fn_t perform);
void mp_pickup_relay_set_far_body(mp_pickup_relay_far_body_fn_t far_body);

/* Which world slot this machine holds: what a claim from here names, and the one grant a client
 * applies. Set from the bridge, which is told it by the host. */
void mp_pickup_relay_set_slot(uint8_t slot);

/* Once per substep: the tick the claims carry and the clock the repeat interval counts in. */
void mp_pickup_relay_tick(uint32_t tick);

/* Takes a pickup message if it is one. On a host it is a claim to decide, from the peer whose far
 * bank is `bank` and whose world slot is `slot`, and the grant goes to that slot; on a client it
 * is a grant to apply when it names this machine's own slot, and the two are not read. */
bool mp_pickup_relay_take_message(size_t bank, uint8_t slot, const uint8_t *note, size_t bytes);

/* The pure decision behind the repeat interval: whether a claim for a placement last sent at
 * `last` (0 for never) may go out again at `now`. */
bool mp_pickup_relay_may_claim(uint32_t last, uint32_t now);

/* The pure decision behind the reach: whether a claimant standing at `claimant` is near enough
 * to a pickup standing at `pickup`. */
bool mp_pickup_relay_in_reach(const float claimant[3], const float pickup[3]);

void mp_pickup_relay_report(void);

#endif /* MULTIPLAYER_MP_PICKUP_RELAY_H */
