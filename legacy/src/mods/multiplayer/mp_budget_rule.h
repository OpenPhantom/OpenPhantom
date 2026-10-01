/* mp_budget_rule.h: how one packet from a host to a client is shared, steering before mass.
 *
 * Layer 1, arithmetic only. A packet carries the channel's reliable messages and one payload, the
 * payload being the enemy block behind its length and then the bodies' snapshot. The channel
 * reserves the payload first, and the enemy block used to take everything the bodies left, so the
 * messages got what remained: 45 bytes in the four player run of 2026-09-25, where a roster is 230
 * and the map digest 183, and a message that does not fit waits with everything queued behind it.
 *
 * Quake 3 writes the reliable commands into a packet before the snapshot, and Tribes has its
 * managers write in a fixed order, moves, events, then the ghosts that describe the world, which
 * get what is left. The split here is that order in bytes, per packet:
 *
 *   1. the bodies' reserve, the snapshot's header and the largest record per body in it;
 *   2. a floor for the enemy block, which it keeps whatever is due: MP_BUDGET_ENEMY_FLOOR_BYTES, or
 *      more in a substep whose records that may not wait need more (mp_enemy_sync_floor_bytes);
 *   3. what is due on the channel, asked for by the same walk the build seats by, up to the room
 *      the first two leave;
 *   4. the enemy block gets the rest, and gives way first when messages are due.
 *
 * Nothing here knows a byte of the wire; the caller passes the reserve, the floor and what is due.
 */
#ifndef MULTIPLAYER_MP_BUDGET_RULE_H
#define MULTIPLAYER_MP_BUDGET_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two bytes in front of the enemy block that say how long it is. */
#define MP_BUDGET_ENEMY_LENGTH_BYTES 2u

/* The least the enemy block keeps whatever is due: its head with the presence bitmap, the copies'
 * part head at its largest, and room for a handful of deltas, the ones a quiet level sends. A
 * substep whose records that may not wait need more raises the floor to what they need, and a quiet
 * one keeps exactly this, which is why two players see no difference. */
#define MP_BUDGET_ENEMY_FLOOR_BYTES 128u

/* The most bytes of messages, headers included, a packet with a payload may seat once the bodies'
 * `reserve` and the enemy block's `enemy_floor` are set aside. 0 when nothing is left. */
size_t mp_budget_message_limit(size_t reserve, size_t enemy_floor);

/* The room the enemy block gets in a payload of at most `payload_max` bytes, once the bodies'
 * `reserve` is set aside and `due` bytes of messages have to fit beside the payload. 0 when there
 * is none. */
size_t mp_budget_enemy_room(size_t payload_max, size_t reserve, size_t due);

/* ================================ More packets, not larger ones ================================
 *
 * A packet stays at 1200 bytes, the size every path carries, and what does not fit goes in a
 * second packet of the same substep, with messages only, within a rate in bytes a second for each
 * peer. GameNetworkingSockets, Quake 3 and Tribes all limit a rate, not a size: a larger datagram
 * would be fragmented or lost on some paths and would not help, because the enemy block would fill
 * it too.
 *
 * The token bucket is that rate. Every packet takes its bytes from it; the packet with the payload
 * is never refused and may leave the bucket in debt, and only an extra packet waits for tokens. */

/* 48 KiB a second a peer, a quarter more than one full packet every substep, and a depth of eight
 * full packets, a quarter of a second of burst. A fixed value until the field has measured what a
 * payload really weighs. */
#define MP_BUDGET_RATE_BYTES_A_SECOND (48u * 1024u)
#define MP_BUDGET_DEPTH_BYTES         (8u * 1200u)

typedef struct mp_budget_bucket {
    int32_t  tokens;      /* bytes; below nought after a payload packet it could not cover */
    uint32_t at_ms;       /* when it was last filled */
    bool     started;
} mp_budget_bucket_t;

/* Full, as a peer that just arrived has sent nothing. */
void mp_budget_bucket_start(mp_budget_bucket_t *bucket, uint32_t now_ms);

/* Refills by the rate for the time since the last fill, up to the depth. */
void mp_budget_bucket_fill(mp_budget_bucket_t *bucket, uint32_t now_ms);

/* Whether an extra packet of this many bytes may go now. */
bool mp_budget_bucket_allows(const mp_budget_bucket_t *bucket, size_t bytes);

/* A packet went, extra or not; the debt is bounded by one depth. */
void mp_budget_bucket_spend(mp_budget_bucket_t *bucket, size_t bytes);

#endif /* MULTIPLAYER_MP_BUDGET_RULE_H */
