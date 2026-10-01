/* The campaign bank for a player who enters the host's world mid game.
 *
 * The host sends only the bytes that differ from its mirror, and its mirror is one for the whole
 * session. A player who arrives late holds whatever its own level begin left in its bank, and a
 * byte it holds at 0x55 where the host has held 0 since the level began is never sent: nothing
 * about that byte ever changes on the host. In a field run the late joiner applied no blackboard
 * at all, against eight on the players who were there from the start.
 *
 * The real mp_scratch_wire runs both ends one after the other; the engine's bank is played here,
 * one bank for the host and one for the newcomer. The host sweeps, a player arrives, the host
 * sweeps again, and the newcomer takes both sweeps in order:
 *
 *   - the first sweep alone leaves the newcomer's byte where its own level put it;
 *   - the arrival's sweep carries it, zeros included, with the blackboard;
 *   - a message from before the arrival is refused once the arrival's generation has been seen.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_scratch.h"
#include "mp_scratch_bind.h"
#include "mp_scratch_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Outside the per-hero window, which is bytes 6 to 11. */
#define HOST_ZERO_AT  20u
#define HOST_SEVEN_AT 30u
#define MESSAGES_KEPT 16u

/* ---- the engine's bank, as this test plays it ----------------------------------------------- */

static uint8_t  s_host_bank[MP_SCRATCH_BANK_BYTES];
static uint8_t  s_client_bank[MP_SCRATCH_BANK_BYTES];
static uint8_t *s_bank = s_host_bank;
static uint32_t s_ai_writes;

bool mp_scratch_bind_installed(void)
{
    return true;
}

bool mp_scratch_bind_world_now(float *out)
{
    *out = 12.0f;
    return true;
}

bool mp_scratch_bind_read_bank(uint8_t *out)
{
    memcpy(out, s_bank, MP_SCRATCH_BANK_BYTES);
    return true;
}

bool mp_scratch_bind_write_bank(const uint8_t *want, size_t *bytes_written)
{
    uint32_t i;
    size_t   written = 0u;

    for (i = 0; i < MP_SCRATCH_BANK_BYTES; ++i) {
        if (!mp_scratch_is_per_hero(i) && s_bank[i] != want[i]) {
            s_bank[i] = want[i];
            ++written;
        }
    }
    *bytes_written = written;
    return true;
}

bool mp_scratch_bind_read_ai(int32_t *flag, int32_t *previous, float *expiry)
{
    memset(flag, 0, sizeof(int32_t) * MP_SCRATCH_AI_SLOTS);
    memset(previous, 0, sizeof(int32_t) * MP_SCRATCH_AI_SLOTS);
    memset(expiry, 0, sizeof(float) * MP_SCRATCH_AI_SLOTS);
    flag[0] = 3;
    return true;
}

bool mp_scratch_bind_write_ai(const mp_scratch_ai_t *ai)
{
    (void)ai;
    ++s_ai_writes;
    return true;
}

bool mp_scratch_bind_take_resync(void)
{
    return false;
}

void mp_scratch_bind_counters(uint32_t *transitions, uint32_t *bank_bytes, uint32_t *ai_writes,
                              uint32_t *quest_writes)
{
    if (transitions != NULL) {
        *transitions = 0u;
    }
    if (bank_bytes != NULL) {
        *bank_bytes = 0u;
    }
    if (ai_writes != NULL) {
        *ai_writes = s_ai_writes;
    }
    if (quest_writes != NULL) {
        *quest_writes = 0u;
    }
}

/* ---- the channel, kept ---------------------------------------------------------------------- */

typedef struct kept_messages {
    uint8_t bytes[MESSAGES_KEPT][MP_CHANNEL_MESSAGE_BYTES];
    size_t  length[MESSAGES_KEPT];
    size_t  count;
} kept_messages_t;

static kept_messages_t *s_into;

static bool keep_send(const uint8_t *bytes, size_t count)
{
    if (s_into == NULL || s_into->count >= MESSAGES_KEPT || count > MP_CHANNEL_MESSAGE_BYTES) {
        return false;
    }
    memcpy(s_into->bytes[s_into->count], bytes, count);
    s_into->length[s_into->count] = count;
    ++s_into->count;
    return true;
}

/* Four periods of the host's substeps, which a sweep of 1250 bytes finishes in. */
static void host_ticks(kept_messages_t *into, uint32_t *substep)
{
    uint32_t end = *substep + 4u * MP_SCRATCH_WIRE_PERIOD;

    s_into = into;
    for (; *substep < end; ++*substep) {
        mp_scratch_wire_tick(*substep, &keep_send);
    }
    s_into = NULL;
}

static size_t take_all(const kept_messages_t *from)
{
    size_t i;
    size_t taken = 0u;

    for (i = 0; i < from->count; ++i) {
        taken += mp_scratch_wire_take_message(from->bytes[i], from->length[i]) ? 1u : 0u;
    }
    return taken;
}

static bool is_blackboard(const kept_messages_t *from, size_t i)
{
    return from->length[i] > 0u && from->bytes[i][0] == (uint8_t)MP_SCRATCH_TAG_AI;
}

static size_t blackboards_in(const kept_messages_t *from)
{
    size_t i;
    size_t count = 0u;

    for (i = 0; i < from->count; ++i) {
        count += is_blackboard(from, i) ? 1u : 0u;
    }
    return count;
}

int main(void)
{
    static kept_messages_t before;
    static kept_messages_t arrival;
    uint32_t               substep = 0u;
    size_t                 i;

    memset(s_host_bank, 0, sizeof s_host_bank);
    s_host_bank[HOST_SEVEN_AT] = 7u;
    memset(s_client_bank, 0, sizeof s_client_bank);
    s_client_bank[HOST_ZERO_AT] = 0x55u;   /* what the newcomer's own level begin left */

    ut_section("the host sweeps its bank against a mirror that began empty");
    s_bank = s_host_bank;
    mp_scratch_wire_set_host(true);
    mp_scratch_wire_reset();
    host_ticks(&before, &substep);
    ut_checkf(before.count >= 2u && blackboards_in(&before) == 1u,
              "the sweep went out with one blackboard (%u message(s))", (unsigned)before.count);
    memset(&arrival, 0, sizeof arrival);
    host_ticks(&arrival, &substep);
    ut_checkf(arrival.count == 0u,
              "and with nothing changed, the host sends nothing more (%u)",
              (unsigned)arrival.count);

    ut_section("a player enters the host's world");
    mp_scratch_wire_note_arrival();
    host_ticks(&arrival, &substep);
    ut_checkf(arrival.count >= 2u && blackboards_in(&arrival) == 1u,
              "the arrival's sweep goes out, with the blackboard again (%u message(s))",
              (unsigned)arrival.count);

    ut_section("the newcomer takes what the host sent");
    s_bank = s_client_bank;
    mp_scratch_wire_set_host(false);
    mp_scratch_wire_reset();
    ut_check(take_all(&before) == before.count, "every message of the first sweep is taken");
    ut_checkf(s_client_bank[HOST_SEVEN_AT] == 7u && s_client_bank[HOST_ZERO_AT] == 0x55u,
              "the host's 7 arrived, and the byte the host holds at 0 is still 0x%02X here: no "
              "sweep against an empty mirror ever carries a zero",
              (unsigned)s_client_bank[HOST_ZERO_AT]);
    ut_check(take_all(&arrival) == arrival.count, "every message of the arrival's sweep is taken");
    ut_checkf(s_client_bank[HOST_ZERO_AT] == 0u && s_client_bank[HOST_SEVEN_AT] == 7u,
              "after the arrival's sweep the newcomer holds the host's 0 as well (0x%02X)",
              (unsigned)s_client_bank[HOST_ZERO_AT]);
    ut_checkf(s_ai_writes == 2u, "and it applied the blackboard of both sweeps (%u)",
              (unsigned)s_ai_writes);

    ut_section("a message from before the arrival comes too late");
    s_client_bank[HOST_SEVEN_AT] = 9u;
    for (i = 0; i < before.count; ++i) {
        if (!is_blackboard(&before, i)) {
            (void)mp_scratch_wire_take_message(before.bytes[i], before.length[i]);
        }
    }
    ut_checkf(s_client_bank[HOST_SEVEN_AT] == 9u,
              "a bank message of the generation before the arrival is refused, not applied over "
              "the sweep (0x%02X)", (unsigned)s_client_bank[HOST_SEVEN_AT]);

    ut_section("an arrival on a client sends nothing");
    memset(&arrival, 0, sizeof arrival);
    mp_scratch_wire_note_arrival();
    host_ticks(&arrival, &substep);
    ut_check(arrival.count == 0u, "a client describes nothing, arrival or not");
    mp_scratch_wire_report();

    return ut_summary("mp_scratch_wire_arrival");
}
