/* mp_scratch_wire.c: when a campaign message is ours, and the two cases where taking one would be
 * worse than dropping it.
 *
 * Nothing here has an engine behind it, so the binding refuses every read and the wire layer has
 * to behave anyway. That is not a limitation of the test, it is the case that actually ships
 * first: a machine sitting in the main menu with a session already open.
 *
 * Three properties, each of which would be silent if it were wrong:
 *
 *   a message that is NOT ours must be handed back, or a campaign delta swallows somebody's shot
 *   and the player it belonged to never fires;
 *
 *   a message that IS ours must be kept even when it cannot be applied, or the event decoder gets
 *   handed a run delta and reads it as a body action;
 *
 *   and a host must refuse what a client sends here, because the level belongs to the host and
 *   the alternative is one player rewriting everyone's campaign.
 */
#include "unittest.h"

#include "mp_events.h"
#include "mp_scratch.h"
#include "mp_scratch_wire.h"
#include "mp_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t s_sent[64][256];
static size_t  s_sent_bytes[64];
static size_t  s_sends;
static bool    s_send_answer;

static bool recording_send(const uint8_t *bytes, size_t count)
{
    if (!s_send_answer) {
        return false;
    }
    if (s_sends < 64 && count <= sizeof s_sent[0]) {
        memcpy(s_sent[s_sends], bytes, count);
        s_sent_bytes[s_sends] = count;
    }
    ++s_sends;
    return true;
}

static void start(bool host)
{
    s_sends       = 0;
    s_send_answer = true;
    memset(s_sent_bytes, 0, sizeof s_sent_bytes);
    mp_scratch_wire_set_host(host);
    mp_scratch_wire_reset();
}

/* A message of ours, built by hand, because the sender cannot run without an engine. */
static size_t build(uint8_t tag, uint32_t generation, size_t payload, uint8_t *out)
{
    size_t i;

    out[0] = tag;
    out[1] = (uint8_t)(generation & 0xFFu);
    out[2] = (uint8_t)((generation >> 8) & 0xFFu);
    out[3] = (uint8_t)((generation >> 16) & 0xFFu);
    out[4] = (uint8_t)((generation >> 24) & 0xFFu);
    for (i = 0; i < payload; ++i) {
        out[MP_SCRATCH_WIRE_HEADER + i] = (uint8_t)i;
    }
    return MP_SCRATCH_WIRE_HEADER + payload;
}

int main(void)
{
    uint8_t message[256];
    size_t  bytes;

    ut_section("what belongs to this module and what does not");
    start(false);
    bytes = build(MP_SCRATCH_TAG_BANK, 0u, 8u, message);
    ut_check(mp_scratch_wire_take_message(message, bytes),
             "a bank message is taken by the module that owns the tag");
    bytes = build(MP_SCRATCH_TAG_AI, 0u, 48u, message);
    ut_check(mp_scratch_wire_take_message(message, bytes), "and so is a blackboard message");

    /* Every event kind in the shipped set, over this recogniser. One of them being taken here is
     * a player action that silently never happens. */
    {
        const uint8_t kinds[] = {
            MP_EVENT_SHOT, MP_EVENT_PUSH, MP_EVENT_SABRE, MP_EVENT_WEAPON, MP_EVENT_MOVER,
            MP_WORLD_DIGEST_TAG, MP_EVENT_SPAWN, MP_EVENT_DESPAWN, MP_EVENT_SKIN, MP_EVENT_PICKUP
        };
        size_t i;
        size_t length;

        for (i = 0; i < sizeof kinds / sizeof kinds[0]; ++i) {
            for (length = 1u; length <= 64u; ++length) {
                memset(message, 0, sizeof message);
                message[0] = kinds[i];
                ut_checkf(!mp_scratch_wire_take_message(message, length),
                          "event kind 0x%02X at %u bytes is handed back rather than swallowed",
                          (unsigned)kinds[i], (unsigned)length);
            }
        }
    }

    ut_section("a message of ours is kept even when it cannot be used");
    start(false);
    bytes = build(MP_SCRATCH_TAG_BANK, 0u, 8u, message);
    ut_check(mp_scratch_wire_take_message(message, bytes),
             "with no binding the message is still taken, because handing it back would have the "
             "event decoder read a run delta as a body action");

    ut_section("too short to carry a header");
    for (bytes = 0; bytes <= MP_SCRATCH_WIRE_HEADER; ++bytes) {
        memset(message, 0, sizeof message);
        message[0] = (uint8_t)MP_SCRATCH_TAG_BANK;
        ut_checkf(!mp_scratch_wire_take_message(message, bytes),
                  "%u bytes cannot hold a tag and a generation, so it is not claimed",
                  (unsigned)bytes);
    }
    ut_check(!mp_scratch_wire_take_message(NULL, 32u), "a null message is not claimed");

    ut_section("the host refuses what only a client may be told");
    start(true);
    bytes = build(MP_SCRATCH_TAG_BANK, 0u, 8u, message);
    ut_check(mp_scratch_wire_take_message(message, bytes),
             "a host still TAKES the message, so it cannot be read as something else");
    bytes = build(MP_SCRATCH_TAG_AI, 0u, 48u, message);
    ut_check(mp_scratch_wire_take_message(message, bytes),
             "and takes a blackboard message the same way");

    ut_section("a client sends nothing here, whatever the substep");
    start(false);
    {
        uint32_t substep;

        for (substep = 0; substep < 4u * MP_SCRATCH_WIRE_PERIOD; ++substep) {
            mp_scratch_wire_tick(substep, &recording_send);
        }
    }
    ut_check(s_sends == 0u, "the level belongs to the host, so a client describes nothing");

    ut_section("a host with no binding sends nothing either");
    start(true);
    {
        uint32_t substep;

        for (substep = 0; substep < 4u * MP_SCRATCH_WIRE_PERIOD; ++substep) {
            mp_scratch_wire_tick(substep, &recording_send);
        }
    }
    ut_check(s_sends == 0u,
             "an unbound host sends nothing rather than sending an empty bank, which would look "
             "on the far side exactly like a campaign that never changes");

    ut_section("the report runs without a session");
    mp_scratch_wire_report();
    ut_check(true, "reporting on a wire that never carried anything is not a fault");

    return ut_summary("mp_scratch_wire");
}
