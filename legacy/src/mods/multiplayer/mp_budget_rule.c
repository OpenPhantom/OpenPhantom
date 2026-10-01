/* mp_budget_rule.c: the shares of one packet. See the header. */
#include "mp_budget_rule.h"

#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Two packets a peer and substep at the most, 32 substeps a second. The relay's own ceiling on
 * packets from one member a second is 96 (mp-relay, internal/config/config.go, MemberInPPS). A
 * client sends its two a substep and besides them its savegame masks, ten a second, and a pace or
 * keepalive now and then; together they must stay under the ceiling with room over. */
#define PACKETS_A_SUBSTEP          2u
#define SUBSTEPS_A_SECOND          32u
#define SAVEGAME_MASKS_A_SECOND    10u
#define RELAY_MEMBER_IN_PPS        96u
#define PACE_AND_KEEPALIVE_MARGIN  16u

_Static_assert(PACKETS_A_SUBSTEP * SUBSTEPS_A_SECOND + SAVEGAME_MASKS_A_SECOND +
                   PACE_AND_KEEPALIVE_MARGIN <= RELAY_MEMBER_IN_PPS,
               "two packets a substep and the savegame masks leave the relay no margin");

size_t mp_budget_message_limit(size_t reserve, size_t enemy_floor)
{
    size_t taken = MP_BUDGET_ENEMY_LENGTH_BYTES + reserve + enemy_floor;

    return taken < MP_CHANNEL_PAYLOAD_BYTES ? MP_CHANNEL_PAYLOAD_BYTES - taken : 0u;
}

size_t mp_budget_enemy_room(size_t payload_max, size_t reserve, size_t due)
{
    /* The payload may use what the messages leave of the packet, and never more than its own
     * buffer; below a handful of due bytes the buffer is the smaller of the two, as it always
     * was. */
    size_t payload = (due < MP_CHANNEL_PAYLOAD_BYTES) ? MP_CHANNEL_PAYLOAD_BYTES - due : 0u;
    size_t taken   = MP_BUDGET_ENEMY_LENGTH_BYTES + reserve;

    if (payload > payload_max) {
        payload = payload_max;
    }
    return payload > taken ? payload - taken : 0u;
}

void mp_budget_bucket_start(mp_budget_bucket_t *bucket, uint32_t now_ms)
{
    bucket->tokens  = (int32_t)MP_BUDGET_DEPTH_BYTES;
    bucket->at_ms   = now_ms;
    bucket->started = true;
}

void mp_budget_bucket_fill(mp_budget_bucket_t *bucket, uint32_t now_ms)
{
    uint32_t elapsed;
    int32_t  gained;

    if (!bucket->started) {
        mp_budget_bucket_start(bucket, now_ms);
        return;
    }
    elapsed = now_ms - bucket->at_ms;
    if (elapsed >= 0x80000000u) {
        return;   /* a clock that ran backwards gives nothing */
    }
    /* A second fills any debt and more, so a longer gap is a second, and the product stays inside
     * 32 bits. */
    if (elapsed > 1000u) {
        elapsed = 1000u;
    }
    gained = (int32_t)(MP_BUDGET_RATE_BYTES_A_SECOND * elapsed / 1000u);
    if (gained == 0) {
        return;   /* keep the time, so a string of short gaps still adds up */
    }
    bucket->at_ms  = now_ms;
    bucket->tokens = bucket->tokens + gained > (int32_t)MP_BUDGET_DEPTH_BYTES
                         ? (int32_t)MP_BUDGET_DEPTH_BYTES
                         : bucket->tokens + gained;
}

bool mp_budget_bucket_allows(const mp_budget_bucket_t *bucket, size_t bytes)
{
    return bucket->started && bucket->tokens >= 0 && (size_t)bucket->tokens >= bytes;
}

void mp_budget_bucket_spend(mp_budget_bucket_t *bucket, size_t bytes)
{
    int32_t cost = bytes > MP_BUDGET_DEPTH_BYTES ? (int32_t)MP_BUDGET_DEPTH_BYTES : (int32_t)bytes;

    bucket->tokens -= cost;
    if (bucket->tokens < -(int32_t)MP_BUDGET_DEPTH_BYTES) {
        bucket->tokens = -(int32_t)MP_BUDGET_DEPTH_BYTES;
    }
}
